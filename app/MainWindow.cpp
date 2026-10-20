#include "MainWindow.h"

#include "AlgorithmRegistry.h"
#include "AnalysisWorker.h"
#include "DatasetConfig.h"
#include "FrameSource.h"
#include "ParameterEditor.h"
#include "RoiPreviewWidget.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

using namespace turb;

namespace {

const int kAlgIdRole = Qt::UserRole + 1;

QString fmt(double v)
{
    if (std::isnan(v))
        return QStringLiteral("NaN");
    return QString::number(v, 'g', 8);
}

QSpinBox *makeSpin(int min, int max, int value)
{
    QSpinBox *s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    return s;
}

// Raw frame -> 8-bit QImage for display (min/max stretch for >8-bit data).
QImage toQImage(const cv::Mat &m)
{
    if (m.empty())
        return QImage();
    cv::Mat u8;
    if (m.depth() == CV_8U)
        u8 = m;
    else
        cv::normalize(m, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
    if (u8.channels() == 1)
        return QImage(u8.data, u8.cols, u8.rows, int(u8.step), QImage::Format_Grayscale8).copy();
    cv::Mat rgb;
    if (u8.channels() == 4)
        cv::cvtColor(u8, rgb, cv::COLOR_BGRA2RGB);
    else
        cv::cvtColor(u8, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
}

QTableWidgetItem *roItem(const QString &text)
{
    QTableWidgetItem *it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    qRegisterMetaType<turb::AnalysisOutput>("turb::AnalysisOutput");
    buildUi();
    setWindowTitle(tr("Atmospheric Turbulence Image Analysis Platform"));
    resize(1400, 900);
    log(tr("Ready. OpenCV %1. Open an image folder or a video to start.").arg(CV_VERSION));
}

MainWindow::~MainWindow()
{
    if (m_worker)
        m_worker->cancel();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_thread && m_thread->isRunning()) {
        if (QMessageBox::question(this, tr("Analysis running"), tr("An analysis is running. Cancel it and quit?")) !=
            QMessageBox::Yes) {
            e->ignore();
            return;
        }
        m_worker->cancel();
        m_thread->quit();
        m_thread->wait();
    }
    e->accept();
}

// =====================================================================================
// UI construction
// =====================================================================================

void MainWindow::buildUi()
{
    QSplitter *split = new QSplitter(Qt::Horizontal);
    QWidget *left = new QWidget;
    QVBoxLayout *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    m_configPanel = buildConfigPanel();
    scroll->setWidget(m_configPanel);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumWidth(m_configPanel->sizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 4);
    lv->addWidget(scroll, 1);
    lv->addWidget(m_runPanel);
    split->addWidget(left);

    QWidget *right = new QWidget;
    QVBoxLayout *rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    QSplitter *vsplit = new QSplitter(Qt::Vertical);
    vsplit->addWidget(buildResultPanel());
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    vsplit->addWidget(m_log);
    vsplit->setStretchFactor(0, 4);
    vsplit->setStretchFactor(1, 1);
    rv->addWidget(vsplit);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);
}

QWidget *MainWindow::buildConfigPanel()
{
    QWidget *panel = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(panel);

    // --- dataset -------------------------------------------------------------------------
    QGroupBox *ds = new QGroupBox(tr("1. Dataset"));
    QFormLayout *dsf = new QFormLayout(ds);
    m_datasetEdit = new QLineEdit;
    m_datasetEdit->setPlaceholderText(tr("Image folder or video file"));
    connect(m_datasetEdit, &QLineEdit::returnPressed, this, &MainWindow::openDatasetFromEdit);
    QPushButton *bFolder = new QPushButton(tr("Folder..."));
    QPushButton *bVideo = new QPushButton(tr("Video..."));
    connect(bFolder, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(bVideo, &QPushButton::clicked, this, &MainWindow::browseVideo);
    QHBoxLayout *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_datasetEdit, 1);
    pathRow->addWidget(bFolder);
    pathRow->addWidget(bVideo);
    dsf->addRow(pathRow);
    m_datasetInfo = new QLabel(tr("No dataset"));
    m_datasetInfo->setWordWrap(true);
    dsf->addRow(m_datasetInfo);
    m_fpsSpin = new QDoubleSpinBox;
    m_fpsSpin->setRange(0.0, 1e6);
    m_fpsSpin->setDecimals(3);
    m_fpsSpin->setSpecialValueText(tr("auto (video rate / frame index)"));
    m_fpsSpin->setToolTip(tr("Frame rate used for the timestamp column of frame-level CSV files."));
    dsf->addRow(tr("Frame rate (Hz)"), m_fpsSpin);
    m_startSpin = makeSpin(0, 100000000, 0);
    m_endSpin = makeSpin(-1, 100000000, -1);
    m_endSpin->setSpecialValueText(tr("last"));
    m_stepSpin = makeSpin(1, 100000, 1);
    QHBoxLayout *rangeRow = new QHBoxLayout;
    rangeRow->addWidget(new QLabel(tr("start")));
    rangeRow->addWidget(m_startSpin);
    rangeRow->addWidget(new QLabel(tr("end")));
    rangeRow->addWidget(m_endSpin);
    rangeRow->addWidget(new QLabel(tr("step")));
    rangeRow->addWidget(m_stepSpin);
    dsf->addRow(tr("Frames"), rangeRow);
    QPushButton *bSave = new QPushButton(tr("Save dataset config"));
    bSave->setToolTip(tr("Saves ROI, preprocessing, registration, range and algorithm parameters next to the "
                         "dataset (turbulence_dataset.json / <video>.turbulence.json)."));
    QPushButton *bLoad = new QPushButton(tr("Load config..."));
    connect(bSave, &QPushButton::clicked, this, &MainWindow::saveDatasetConfig);
    connect(bLoad, &QPushButton::clicked, this, &MainWindow::loadDatasetConfig);
    QHBoxLayout *cfgRow = new QHBoxLayout;
    cfgRow->addWidget(bSave);
    cfgRow->addWidget(bLoad);
    dsf->addRow(cfgRow);
    v->addWidget(ds);

    // --- ROI -----------------------------------------------------------------------------
    QGroupBox *roi = new QGroupBox(tr("2. ROI (drag on the preview to select)"));
    QFormLayout *rf = new QFormLayout(roi);
    m_roiX = makeSpin(0, 100000, 0);
    m_roiY = makeSpin(0, 100000, 0);
    m_roiW = makeSpin(0, 100000, 0);
    m_roiH = makeSpin(0, 100000, 0);
    m_roiW->setSpecialValueText(tr("full"));
    m_roiH->setSpecialValueText(tr("full"));
    QHBoxLayout *roiRow = new QHBoxLayout;
    roiRow->addWidget(new QLabel("x"));
    roiRow->addWidget(m_roiX);
    roiRow->addWidget(new QLabel("y"));
    roiRow->addWidget(m_roiY);
    roiRow->addWidget(new QLabel("w"));
    roiRow->addWidget(m_roiW);
    roiRow->addWidget(new QLabel("h"));
    roiRow->addWidget(m_roiH);
    rf->addRow(roiRow);
    for (QSpinBox *s : {m_roiX, m_roiY, m_roiW, m_roiH})
        connect(s, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MainWindow::onRoiSpinChanged);
    QPushButton *bFull = new QPushButton(tr("Full frame"));
    connect(bFull, &QPushButton::clicked, this, &MainWindow::setFullFrameRoi);
    m_previewFrameSpin = makeSpin(0, 100000000, 0);
    connect(m_previewFrameSpin, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            &MainWindow::updatePreview);
    m_previewProcessed = new QCheckBox(tr("show preprocessed ROI"));
    connect(m_previewProcessed, &QCheckBox::toggled, this, &MainWindow::updatePreview);
    QHBoxLayout *prevRow = new QHBoxLayout;
    prevRow->addWidget(bFull);
    prevRow->addWidget(new QLabel(tr("preview frame")));
    prevRow->addWidget(m_previewFrameSpin);
    prevRow->addWidget(m_previewProcessed);
    rf->addRow(prevRow);
    v->addWidget(roi);

    // --- preprocessing -----------------------------------------------------------------
    QGroupBox *pre = new QGroupBox(tr("3. Preprocessing (grayscale + ROI crop always applied)"));
    QFormLayout *pf = new QFormLayout(pre);
    m_denoiseCombo = new QComboBox;
    for (int i = 0; i <= PreprocessConfig::DenoiseBilateral; ++i)
        m_denoiseCombo->addItem(denoiseName(PreprocessConfig::Denoise(i)));
    m_denoiseKernel = makeSpin(1, 31, 3);
    m_denoiseKernel->setSingleStep(2);
    m_denoiseKernel->setToolTip(tr("Kernel size (odd). Median on float data supports 3 and 5."));
    m_denoiseSigma = new QDoubleSpinBox;
    m_denoiseSigma->setRange(0.0, 100.0);
    m_denoiseSigma->setValue(1.0);
    m_denoiseSigma->setToolTip(tr("Gaussian sigma / bilateral colour sigma (relative, x10% of full scale)."));
    QHBoxLayout *dnRow = new QHBoxLayout;
    dnRow->addWidget(m_denoiseCombo, 1);
    dnRow->addWidget(new QLabel("k"));
    dnRow->addWidget(m_denoiseKernel);
    dnRow->addWidget(new QLabel(QString::fromUtf8("\xcf\x83")));
    dnRow->addWidget(m_denoiseSigma);
    pf->addRow(tr("Denoise"), dnRow);
    m_contrastCombo = new QComboBox;
    for (int i = 0; i <= PreprocessConfig::ContrastClahe; ++i)
        m_contrastCombo->addItem(contrastName(PreprocessConfig::Contrast(i)));
    m_claheClip = new QDoubleSpinBox;
    m_claheClip->setRange(0.1, 100.0);
    m_claheClip->setValue(2.0);
    m_claheTiles = makeSpin(1, 64, 8);
    QHBoxLayout *ctRow = new QHBoxLayout;
    ctRow->addWidget(m_contrastCombo, 1);
    ctRow->addWidget(new QLabel(tr("clip")));
    ctRow->addWidget(m_claheClip);
    ctRow->addWidget(new QLabel(tr("tiles")));
    ctRow->addWidget(m_claheTiles);
    pf->addRow(tr("Contrast"), ctRow);
    m_normCombo = new QComboBox;
    for (int i = 0; i <= PreprocessConfig::NormZScoreFrame; ++i)
        m_normCombo->addItem(normalizationName(PreprocessConfig::Normalization(i)));
    m_normCombo->setToolTip(tr("Per-frame normalizations remove absolute intensity fluctuations "
                               "(do not use them with intensity / scintillation features)."));
    pf->addRow(tr("Normalization"), m_normCombo);
    for (QComboBox *c : {m_denoiseCombo, m_contrastCombo, m_normCombo})
        connect(c, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
                &MainWindow::updatePreview);
    v->addWidget(pre);

    // --- registration ----------------------------------------------------------------------
    QGroupBox *reg = new QGroupBox(tr("4. Registration (global motion removal)"));
    QFormLayout *gf = new QFormLayout(reg);
    m_regModeCombo = new QComboBox;
    for (int i = 0; i <= RegistrationConfig::Affine; ++i)
        m_regModeCombo->addItem(registrationModeName(RegistrationConfig::Mode(i)));
    m_regModeCombo->setToolTip(tr("Registration is estimated inside the ROI: it also removes the turbulence-induced "
                                  "whole-ROI tilt that displacement features measure."));
    m_regRefCombo = new QComboBox;
    for (int i = 0; i <= RegistrationConfig::FixedFrame; ++i)
        m_regRefCombo->addItem(registrationReferenceName(RegistrationConfig::Reference(i)));
    m_regIndexSpin = makeSpin(0, 100000000, 0);
    QHBoxLayout *regRow = new QHBoxLayout;
    regRow->addWidget(m_regModeCombo, 1);
    regRow->addWidget(m_regRefCombo, 1);
    regRow->addWidget(new QLabel("#"));
    regRow->addWidget(m_regIndexSpin);
    gf->addRow(regRow);
    v->addWidget(reg);

    // --- algorithm ---------------------------------------------------------------------------
    QGroupBox *alg = new QGroupBox(tr("5. Algorithm (single algorithm mode)"));
    QVBoxLayout *av = new QVBoxLayout(alg);
    m_algTree = new QTreeWidget;
    m_algTree->setHeaderHidden(true);
    m_algTree->setMinimumHeight(220);
    populateAlgorithmTree();
    connect(m_algTree, &QTreeWidget::itemChanged, this, &MainWindow::onAlgorithmItemChanged);
    connect(m_algTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
        if (cur && cur->data(0, kAlgIdRole).isValid())
            showParametersFor(cur->data(0, kAlgIdRole).toString().toStdString());
    });
    av->addWidget(m_algTree);
    m_algDescription = new QLabel;
    m_algDescription->setWordWrap(true);
    m_algDescription->setStyleSheet("color: palette(dark);");
    av->addWidget(m_algDescription);
    QGroupBox *params = new QGroupBox(tr("Parameters"));
    QVBoxLayout *pv = new QVBoxLayout(params);
    m_paramEditor = new ParameterEditor;
    pv->addWidget(m_paramEditor);
    QPushButton *bReset = new QPushButton(tr("Reset to defaults"));
    connect(bReset, &QPushButton::clicked, this, &MainWindow::resetParameters);
    pv->addWidget(bReset, 0, Qt::AlignRight);
    av->addWidget(params);
    v->addWidget(alg);

    // --- run ---------------------------------------------------------------------------------
    QGroupBox *run = new QGroupBox(tr("6. Run"));
    QVBoxLayout *runv = new QVBoxLayout(run);
    QHBoxLayout *btnRow = new QHBoxLayout;
    m_runButton = new QPushButton(tr("RUN"));
    m_runButton->setMinimumHeight(32);
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setEnabled(false);
    connect(m_runButton, &QPushButton::clicked, this, &MainWindow::runAnalysis);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancelAnalysis);
    btnRow->addWidget(m_runButton, 1);
    btnRow->addWidget(m_cancelButton);
    runv->addLayout(btnRow);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    runv->addWidget(m_progress);
    m_statusLabel = new QLabel(tr("Idle"));
    m_frameLabel = new QLabel;
    QHBoxLayout *stRow = new QHBoxLayout;
    stRow->addWidget(m_statusLabel, 1);
    stRow->addWidget(m_frameLabel);
    runv->addLayout(stRow);
    m_runPanel = run; // placed below the scroll area so it stays enabled while running
    v->addStretch(1);

    // select the first algorithm
    QTreeWidgetItem *first = m_algTree->topLevelItem(0) ? m_algTree->topLevelItem(0)->child(0) : nullptr;
    if (first) {
        first->setCheckState(0, Qt::Checked);
        m_algTree->setCurrentItem(first);
    }
    return panel;
}

void MainWindow::populateAlgorithmTree()
{
    m_updatingUi = true;
    const AlgorithmGroup groups[] = {AlgorithmGroup::Intensity, AlgorithmGroup::QualityStructure,
                                     AlgorithmGroup::MotionGeometric};
    for (AlgorithmGroup g : groups) {
        QTreeWidgetItem *gi = new QTreeWidgetItem(m_algTree, QStringList(algorithmGroupName(g)));
        gi->setFlags(Qt::ItemIsEnabled);
        QFont f = gi->font(0);
        f.setBold(true);
        gi->setFont(0, f);
        for (const AlgorithmRegistry::Entry &e : AlgorithmRegistry::instance().entries()) {
            if (e.group != g)
                continue;
            QTreeWidgetItem *it = new QTreeWidgetItem(gi, QStringList(QString::fromStdString(e.name)));
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            it->setCheckState(0, Qt::Unchecked);
            it->setData(0, kAlgIdRole, QString::fromStdString(e.id));
            it->setToolTip(0, QString::fromStdString(e.id));
        }
        gi->setExpanded(true);
    }
    m_updatingUi = false;
}

QWidget *MainWindow::buildResultPanel()
{
    m_tabs = new QTabWidget;

    // preview
    m_preview = new RoiPreviewWidget;
    connect(m_preview, &RoiPreviewWidget::roiSelected, this, &MainWindow::onRoiSelected);
    m_tabs->addTab(m_preview, tr("Dataset / ROI"));

    // summary
    QWidget *sum = new QWidget;
    QVBoxLayout *sv = new QVBoxLayout(sum);
    m_resultTitle = new QLabel(tr("No result yet"));
    QFont tf = m_resultTitle->font();
    tf.setBold(true);
    m_resultTitle->setFont(tf);
    sv->addWidget(m_resultTitle);
    m_summaryTable = new QTableWidget(0, 4);
    m_summaryTable->setHorizontalHeaderLabels({tr("Feature"), tr("Value"), tr("Unit"), tr("Key")});
    m_summaryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_summaryTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_summaryTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_summaryTable->verticalHeader()->setVisible(false);
    m_summaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    sv->addWidget(m_summaryTable, 1);
    QHBoxLayout *fvRow = new QHBoxLayout;
    fvRow->addWidget(new QLabel(tr("Feature vector")));
    m_featureVectorEdit = new QLineEdit;
    m_featureVectorEdit->setReadOnly(true);
    fvRow->addWidget(m_featureVectorEdit, 1);
    QPushButton *bCopy = new QPushButton(tr("Copy"));
    connect(bCopy, &QPushButton::clicked, this, &MainWindow::copyFeatureVector);
    fvRow->addWidget(bCopy);
    sv->addLayout(fvRow);
    QHBoxLayout *exRow = new QHBoxLayout;
    QPushButton *bFrames = new QPushButton(tr("Export frame-level CSV..."));
    QPushButton *bSummary = new QPushButton(tr("Export summary CSV..."));
    QPushButton *bJson = new QPushButton(tr("Export JSON..."));
    connect(bFrames, &QPushButton::clicked, this, &MainWindow::exportFrameCsv);
    connect(bSummary, &QPushButton::clicked, this, &MainWindow::exportSummaryCsv);
    connect(bJson, &QPushButton::clicked, this, &MainWindow::exportJson);
    exRow->addWidget(bFrames);
    exRow->addWidget(bSummary);
    exRow->addWidget(bJson);
    exRow->addStretch(1);
    sv->addLayout(exRow);
    m_tabs->addTab(sum, tr("Summary"));

    // run info
    QWidget *info = new QWidget;
    QVBoxLayout *iv = new QVBoxLayout(info);
    m_metaTable = new QTableWidget(0, 2);
    m_metaTable->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_metaTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_metaTable->verticalHeader()->setVisible(false);
    iv->addWidget(m_metaTable, 3);
    iv->addWidget(new QLabel(tr("Notes / warnings")));
    m_notesEdit = new QPlainTextEdit;
    m_notesEdit->setReadOnly(true);
    iv->addWidget(m_notesEdit, 1);
    m_tabs->addTab(info, tr("Run info"));

    // comparison
    QWidget *cmp = new QWidget;
    QVBoxLayout *cv = new QVBoxLayout(cmp);
    QLabel *cmpHelp = new QLabel(tr("Every run is a column (double-click a header to rename it, e.g. with the "
                                    "atmospheric condition). Rows are the summary features."));
    cmpHelp->setWordWrap(true);
    cv->addWidget(cmpHelp);
    m_compTable = new QTableWidget;
    m_compTable->verticalHeader()->setVisible(false);
    m_compTable->horizontalHeader()->setSectionsClickable(true);
    connect(m_compTable->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this,
            &MainWindow::renameComparisonColumn);
    cv->addWidget(m_compTable, 1);
    QHBoxLayout *cr = new QHBoxLayout;
    m_autoCompare = new QCheckBox(tr("Add every run"));
    m_autoCompare->setChecked(true);
    QPushButton *bRemove = new QPushButton(tr("Remove selected run"));
    QPushButton *bClear = new QPushButton(tr("Clear"));
    QPushButton *bCmpCsv = new QPushButton(tr("Export comparison CSV..."));
    QPushButton *bMatrix = new QPushButton(tr("Export feature matrix CSV..."));
    bMatrix->setToolTip(tr("One row per run, one column per feature-vector entry (input for later feature "
                           "selection / classification)."));
    connect(bRemove, &QPushButton::clicked, this, &MainWindow::removeComparisonColumn);
    connect(bClear, &QPushButton::clicked, this, &MainWindow::clearComparison);
    connect(bCmpCsv, &QPushButton::clicked, this, &MainWindow::exportComparisonCsv);
    connect(bMatrix, &QPushButton::clicked, this, &MainWindow::exportFeatureMatrix);
    cr->addWidget(m_autoCompare);
    cr->addStretch(1);
    cr->addWidget(bRemove);
    cr->addWidget(bClear);
    cr->addWidget(bCmpCsv);
    cr->addWidget(bMatrix);
    cv->addLayout(cr);
    m_tabs->addTab(cmp, tr("Dataset comparison"));

    return m_tabs;
}

// =====================================================================================
// Dataset / ROI
// =====================================================================================

void MainWindow::browseFolder()
{
    QSettings s;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Image sequence folder"),
                                                          s.value("lastDir", QDir::homePath()).toString());
    if (dir.isEmpty())
        return;
    s.setValue("lastDir", dir);
    openDataset(dir, true);
}

void MainWindow::browseVideo()
{
    QSettings s;
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Video file"), s.value("lastDir", QDir::homePath()).toString(),
        tr("Video (*.avi *.mp4 *.mov *.mkv *.mpg *.mpeg *.wmv *.m4v);;All files (*)"));
    if (file.isEmpty())
        return;
    s.setValue("lastDir", QFileInfo(file).absolutePath());
    openDataset(file, true);
}

void MainWindow::openDatasetFromEdit()
{
    openDataset(m_datasetEdit->text().trimmed(), true);
}

bool MainWindow::openDataset(const QString &path, bool loadSidecar)
{
    if (path.isEmpty())
        return false;
    std::string err;
    std::unique_ptr<FrameSource> src = FrameSource::create(path.toStdString(), &err);
    if (!src) {
        QMessageBox::warning(this, tr("Dataset"), QString::fromStdString(err));
        log(tr("Cannot open dataset: %1").arg(QString::fromStdString(err)));
        return false;
    }
    m_openedDataset = path;
    m_datasetEdit->setText(path);
    m_frameCount = src->frameCount();
    m_frameSize = QSize(src->frameSize().width, src->frameSize().height);
    m_datasetInfo->setText(QString::fromStdString(src->description()));
    m_previewFrameSpin->setMaximum(std::max(0, m_frameCount - 1));
    m_startSpin->setMaximum(std::max(0, m_frameCount - 1));
    m_endSpin->setMaximum(std::max(0, m_frameCount - 1));
    m_regIndexSpin->setMaximum(std::max(0, m_frameCount - 1));
    log(tr("Opened %1: %2").arg(path, QString::fromStdString(src->description())));

    const QString sidecar = DatasetConfig::sidecarPath(path);
    if (loadSidecar && QFileInfo::exists(sidecar)) {
        AnalysisConfig c = currentConfig();
        QString e;
        if (DatasetConfig::load(sidecar, c, true, &e)) {
            c.dataset.path = path.toStdString();
            applyConfig(c);
            log(tr("Loaded dataset config %1").arg(sidecar));
        } else {
            log(tr("Cannot read %1: %2").arg(sidecar, e));
        }
    }
    m_tabs->setCurrentIndex(0);
    updatePreview();
    return true;
}

void MainWindow::saveDatasetConfig()
{
    if (m_openedDataset.isEmpty()) {
        QMessageBox::information(this, tr("Save config"), tr("Open a dataset first."));
        return;
    }
    storeCurrentParameters();
    const QString file = DatasetConfig::sidecarPath(m_openedDataset);
    QString err;
    if (DatasetConfig::save(file, currentConfig(), &err))
        log(tr("Dataset config saved to %1").arg(file));
    else
        QMessageBox::warning(this, tr("Save config"), err);
}

void MainWindow::loadDatasetConfig()
{
    QSettings s;
    const QString file = QFileDialog::getOpenFileName(this, tr("Load config"), s.value("lastDir").toString(),
                                                      tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    AnalysisConfig c = currentConfig();
    QString err;
    if (!DatasetConfig::load(file, c, !m_openedDataset.isEmpty(), &err)) {
        QMessageBox::warning(this, tr("Load config"), err);
        return;
    }
    if (m_openedDataset.isEmpty() && !c.dataset.path.empty())
        openDataset(QString::fromStdString(c.dataset.path), false);
    applyConfig(c);
    log(tr("Config loaded from %1").arg(file));
    updatePreview();
}

void MainWindow::setFullFrameRoi()
{
    m_updatingUi = true;
    m_roiX->setValue(0);
    m_roiY->setValue(0);
    m_roiW->setValue(0);
    m_roiH->setValue(0);
    m_updatingUi = false;
    onRoiSpinChanged();
}

void MainWindow::onRoiSelected(const QRect &r)
{
    m_updatingUi = true;
    m_roiX->setValue(r.x());
    m_roiY->setValue(r.y());
    m_roiW->setValue(r.width());
    m_roiH->setValue(r.height());
    m_updatingUi = false;
    m_preview->setRoi(r);
    log(tr("ROI set to x=%1 y=%2 w=%3 h=%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
    if (m_previewProcessed->isChecked())
        updatePreview();
}

void MainWindow::onRoiSpinChanged()
{
    if (m_updatingUi)
        return;
    if (m_previewProcessed->isChecked())
        updatePreview();
    else
        m_preview->setRoi(QRect(m_roiX->value(), m_roiY->value(), m_roiW->value(), m_roiH->value()));
}

void MainWindow::updatePreview()
{
    if (m_updatingUi || m_openedDataset.isEmpty())
        return;
    const int idx = m_previewFrameSpin->value();
    if (m_previewProcessed->isChecked()) {
        AnalysisConfig c = currentConfig();
        cv::Mat f;
        std::string err;
        if (!AnalysisEngine::loadPreprocessedFrame(c, idx, f, &err)) {
            log(tr("Preview: %1").arg(QString::fromStdString(err)));
            return;
        }
        m_preview->setImage(toQImage(f));
        m_preview->setRoi(QRect());
    } else {
        std::string err;
        std::unique_ptr<FrameSource> src = FrameSource::create(m_openedDataset.toStdString(), &err);
        cv::Mat raw;
        if (!src || !src->readAt(idx, raw)) {
            log(tr("Preview: cannot read frame %1").arg(idx));
            return;
        }
        m_preview->setImage(toQImage(raw));
        m_preview->setRoi(QRect(m_roiX->value(), m_roiY->value(), m_roiW->value(), m_roiH->value()));
    }
}

// =====================================================================================
// Configuration <-> UI
// =====================================================================================

std::vector<std::string> MainWindow::checkedAlgorithmIds() const
{
    std::vector<std::string> ids;
    for (int g = 0; g < m_algTree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *gi = m_algTree->topLevelItem(g);
        for (int i = 0; i < gi->childCount(); ++i)
            if (gi->child(i)->checkState(0) == Qt::Checked)
                ids.push_back(gi->child(i)->data(0, kAlgIdRole).toString().toStdString());
    }
    return ids;
}

AnalysisConfig MainWindow::currentConfig() const
{
    AnalysisConfig c;
    c.dataset.path = m_openedDataset.toStdString();
    c.dataset.frameRate = m_fpsSpin->value();
    c.dataset.startFrame = m_startSpin->value();
    c.dataset.endFrame = m_endSpin->value();
    c.dataset.frameStep = m_stepSpin->value();

    c.preprocess.roi.x = m_roiX->value();
    c.preprocess.roi.y = m_roiY->value();
    c.preprocess.roi.width = m_roiW->value();
    c.preprocess.roi.height = m_roiH->value();
    c.preprocess.denoise = PreprocessConfig::Denoise(m_denoiseCombo->currentIndex());
    c.preprocess.denoiseKernel = m_denoiseKernel->value();
    c.preprocess.denoiseSigma = m_denoiseSigma->value();
    c.preprocess.contrast = PreprocessConfig::Contrast(m_contrastCombo->currentIndex());
    c.preprocess.claheClipLimit = m_claheClip->value();
    c.preprocess.claheTileGrid = m_claheTiles->value();
    c.preprocess.normalization = PreprocessConfig::Normalization(m_normCombo->currentIndex());

    c.registration.mode = RegistrationConfig::Mode(m_regModeCombo->currentIndex());
    c.registration.reference = RegistrationConfig::Reference(m_regRefCombo->currentIndex());
    c.registration.fixedFrameIndex = m_regIndexSpin->value();

    for (const std::string &id : checkedAlgorithmIds()) {
        AlgorithmRun run;
        run.id = id;
        if (id == m_currentParamAlg) {
            run.params = m_paramEditor->values();
        } else {
            std::map<std::string, ParamSet>::const_iterator it = m_paramsById.find(id);
            if (it != m_paramsById.end())
                run.params = it->second;
        }
        c.algorithms.push_back(run);
    }
    return c;
}

void MainWindow::applyConfig(const AnalysisConfig &c)
{
    m_updatingUi = true;
    m_fpsSpin->setValue(c.dataset.frameRate);
    m_startSpin->setValue(c.dataset.startFrame);
    m_endSpin->setValue(c.dataset.endFrame);
    m_stepSpin->setValue(c.dataset.frameStep);
    m_roiX->setValue(c.preprocess.roi.x);
    m_roiY->setValue(c.preprocess.roi.y);
    m_roiW->setValue(c.preprocess.roi.width);
    m_roiH->setValue(c.preprocess.roi.height);
    m_denoiseCombo->setCurrentIndex(int(c.preprocess.denoise));
    m_denoiseKernel->setValue(c.preprocess.denoiseKernel);
    m_denoiseSigma->setValue(c.preprocess.denoiseSigma);
    m_contrastCombo->setCurrentIndex(int(c.preprocess.contrast));
    m_claheClip->setValue(c.preprocess.claheClipLimit);
    m_claheTiles->setValue(c.preprocess.claheTileGrid);
    m_normCombo->setCurrentIndex(int(c.preprocess.normalization));
    m_regModeCombo->setCurrentIndex(int(c.registration.mode));
    m_regRefCombo->setCurrentIndex(int(c.registration.reference));
    m_regIndexSpin->setValue(c.registration.fixedFrameIndex);

    if (!c.algorithms.empty()) {
        for (const AlgorithmRun &run : c.algorithms)
            m_paramsById[run.id] = run.params;
        QTreeWidgetItem *select = nullptr;
        for (int g = 0; g < m_algTree->topLevelItemCount(); ++g) {
            QTreeWidgetItem *gi = m_algTree->topLevelItem(g);
            for (int i = 0; i < gi->childCount(); ++i) {
                QTreeWidgetItem *it = gi->child(i);
                const std::string id = it->data(0, kAlgIdRole).toString().toStdString();
                bool checked = false;
                for (const AlgorithmRun &run : c.algorithms)
                    checked = checked || run.id == id;
                if (!kAllowMultipleAlgorithms && select)
                    checked = false;
                it->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
                if (checked && !select)
                    select = it;
            }
        }
        m_currentParamAlg.clear(); // parameters of the loaded config win over the editor
        m_updatingUi = false;
        if (select) {
            m_algTree->setCurrentItem(select);
            showParametersFor(select->data(0, kAlgIdRole).toString().toStdString());
        }
    }
    m_updatingUi = false;
    m_preview->setRoi(QRect(m_roiX->value(), m_roiY->value(), m_roiW->value(), m_roiH->value()));
}

void MainWindow::storeCurrentParameters()
{
    if (!m_currentParamAlg.empty())
        m_paramsById[m_currentParamAlg] = m_paramEditor->values();
}

void MainWindow::showParametersFor(const std::string &id)
{
    if (id == m_currentParamAlg)
        return;
    storeCurrentParameters();
    AlgorithmPtr a = AlgorithmRegistry::instance().create(id);
    if (!a)
        return;
    m_currentParamAlg = id;
    m_paramEditor->setSpecs(a->parameterSpecs(), m_paramsById[id]);
    m_algDescription->setText(QString::fromStdString(a->description()));
}

void MainWindow::onAlgorithmItemChanged(QTreeWidgetItem *item, int)
{
    if (m_updatingUi || !item->data(0, kAlgIdRole).isValid())
        return;
    if (item->checkState(0) == Qt::Checked) {
        if (!kAllowMultipleAlgorithms) {
            m_updatingUi = true;
            for (int g = 0; g < m_algTree->topLevelItemCount(); ++g) {
                QTreeWidgetItem *gi = m_algTree->topLevelItem(g);
                for (int i = 0; i < gi->childCount(); ++i)
                    if (gi->child(i) != item)
                        gi->child(i)->setCheckState(0, Qt::Unchecked);
            }
            m_updatingUi = false;
        }
        m_algTree->setCurrentItem(item);
        showParametersFor(item->data(0, kAlgIdRole).toString().toStdString());
    }
}

void MainWindow::resetParameters()
{
    m_paramEditor->resetToDefaults();
}

// =====================================================================================
// Running
// =====================================================================================

void MainWindow::setRunning(bool running)
{
    m_configPanel->setEnabled(!running);
    m_runButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
}

void MainWindow::runAnalysis()
{
    if (m_thread)
        return;
    if (m_openedDataset.isEmpty()) {
        QMessageBox::information(this, tr("Run"), tr("Open a dataset first."));
        return;
    }
    storeCurrentParameters();
    const AnalysisConfig cfg = currentConfig();
    if (cfg.algorithms.empty()) {
        QMessageBox::information(this, tr("Run"), tr("Select an algorithm."));
        return;
    }
    if (cfg.preprocess.isPerFrameNormalization()) {
        for (const AlgorithmRun &r : cfg.algorithms)
            if (r.id == "temporal_intensity_variance" || r.id == "scintillation_index")
                log(tr("Warning: per-frame normalization removes the intensity fluctuations measured by %1.")
                        .arg(QString::fromStdString(r.id)));
    }

    m_thread = new QThread(this);
    m_worker = new AnalysisWorker(cfg);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &AnalysisWorker::run);
    connect(m_worker, &AnalysisWorker::progress, m_progress, &QProgressBar::setValue);
    connect(m_worker, &AnalysisWorker::frameProgress, this, &MainWindow::onFrameProgress);
    connect(m_worker, &AnalysisWorker::status, m_statusLabel, &QLabel::setText);
    connect(m_worker, &AnalysisWorker::resultReady, this, &MainWindow::onResult);
    connect(m_worker, &AnalysisWorker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, this, &MainWindow::onWorkerFinished);

    QStringList names;
    for (const AlgorithmRun &r : cfg.algorithms)
        names << QString::fromStdString(r.id);
    log(tr("Run started: %1 on %2").arg(names.join(", "), m_openedDataset));
    m_progress->setValue(0);
    setRunning(true);
    m_thread->start();
}

void MainWindow::cancelAnalysis()
{
    if (m_worker) {
        m_worker->cancel();
        m_statusLabel->setText(tr("Cancelling..."));
    }
}

void MainWindow::onFrameProgress(int processed, int total, int frameIndex)
{
    m_frameLabel->setText(tr("frame %1  (%2 / %3)").arg(frameIndex).arg(processed).arg(total));
}

void MainWindow::onWorkerFinished()
{
    m_worker->deleteLater();
    m_thread->deleteLater();
    m_worker = nullptr;
    m_thread = nullptr;
    setRunning(false);
}

void MainWindow::onResult(const AnalysisOutput &out)
{
    if (!out.ok) {
        m_statusLabel->setText(out.cancelled ? tr("Cancelled") : tr("Error"));
        log(tr("Run failed: %1").arg(QString::fromStdString(out.error)));
        if (!out.cancelled)
            QMessageBox::warning(this, tr("Analysis error"), QString::fromStdString(out.error));
        return;
    }
    m_progress->setValue(100);
    m_statusLabel->setText(tr("Done in %1 s").arg(out.elapsedSeconds, 0, 'f', 2));
    log(tr("Run finished in %1 s").arg(out.elapsedSeconds, 0, 'f', 2));
    ++m_runCounter;
    m_lastResults = out.results;
    showResults(out.results);
    if (m_autoCompare->isChecked())
        addToComparison(out.results);
    m_tabs->setCurrentIndex(1);
}

// =====================================================================================
// Results
// =====================================================================================

void MainWindow::showResults(const std::vector<AlgorithmResult> &results)
{
    m_summaryTable->setRowCount(0);
    m_metaTable->setRowCount(0);
    m_notesEdit->clear();
    m_featureVectorEdit->clear();
    if (results.empty())
        return;

    QStringList titles, vectors;
    for (const AlgorithmResult &r : results) {
        titles << QString::fromStdString(r.algorithmName);
        // separator row per algorithm when several results are shown (multiple mode)
        if (results.size() > 1) {
            const int row = m_summaryTable->rowCount();
            m_summaryTable->insertRow(row);
            QTableWidgetItem *h = roItem(QString::fromStdString(r.algorithmName));
            QFont f = h->font();
            f.setBold(true);
            h->setFont(f);
            m_summaryTable->setItem(row, 0, h);
        }
        for (const Feature &f : r.features) {
            const int row = m_summaryTable->rowCount();
            m_summaryTable->insertRow(row);
            m_summaryTable->setItem(row, 0, roItem(QString::fromStdString(f.label)));
            QTableWidgetItem *val = roItem(fmt(f.value));
            val->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_summaryTable->setItem(row, 1, val);
            m_summaryTable->setItem(row, 2, roItem(QString::fromStdString(f.unit)));
            m_summaryTable->setItem(row, 3, roItem(QString::fromStdString(f.key)));
        }
        QStringList vals;
        for (double v : r.featureVectorValues())
            vals << fmt(v);
        vectors << "[" + vals.join(", ") + "]";

        for (const auto &kv : r.metadata) {
            const int row = m_metaTable->rowCount();
            m_metaTable->insertRow(row);
            m_metaTable->setItem(row, 0, roItem(QString::fromStdString(kv.first)));
            m_metaTable->setItem(row, 1, roItem(QString::fromStdString(kv.second)));
        }
        {
            const int row = m_metaTable->rowCount();
            m_metaTable->insertRow(row);
            m_metaTable->setItem(row, 0, roItem("frame_rows"));
            m_metaTable->setItem(row, 1, roItem(QString::number(r.frameRows.size())));
        }
        m_metaTable->resizeColumnToContents(0);
        for (const std::string &n : r.notes)
            m_notesEdit->appendPlainText(QString::fromStdString(r.algorithmName + ": " + n));
    }
    m_featureVectorEdit->setText(vectors.join("  "));
    m_resultTitle->setText(tr("%1  -  %2").arg(titles.join(", "), QFileInfo(m_openedDataset).fileName()));
    m_summaryTable->resizeRowsToContents();
}

void MainWindow::copyFeatureVector()
{
    QApplication::clipboard()->setText(m_featureVectorEdit->text());
}

QString MainWindow::exportBaseName(const AlgorithmResult &r) const
{
    const QFileInfo fi(QString::fromStdString(r.dataset));
    return fi.completeBaseName() + "_" + QString::fromStdString(r.algorithmId) + "_" +
           QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
}

void MainWindow::exportFrameCsv()
{
    if (m_lastResults.empty())
        return;
    QSettings s;
    for (const AlgorithmResult &r : m_lastResults) {
        const QString file = QFileDialog::getSaveFileName(
            this, tr("Frame-level CSV (%1)").arg(QString::fromStdString(r.algorithmName)),
            s.value("lastExportDir", QDir::homePath()).toString() + "/" + exportBaseName(r) + "_frames.csv",
            tr("CSV (*.csv)"));
        if (file.isEmpty())
            continue;
        s.setValue("lastExportDir", QFileInfo(file).absolutePath());
        std::string err;
        if (exporter::writeFrameCsv(r, file.toStdString(), &err))
            log(tr("Frame-level CSV written: %1 (%2 rows)").arg(file).arg(r.frameRows.size()));
        else
            QMessageBox::warning(this, tr("Export"), QString::fromStdString(err));
    }
}

void MainWindow::exportSummaryCsv()
{
    if (m_lastResults.empty())
        return;
    QSettings s;
    for (const AlgorithmResult &r : m_lastResults) {
        const QString file = QFileDialog::getSaveFileName(
            this, tr("Summary CSV (%1)").arg(QString::fromStdString(r.algorithmName)),
            s.value("lastExportDir", QDir::homePath()).toString() + "/" + exportBaseName(r) + "_summary.csv",
            tr("CSV (*.csv)"));
        if (file.isEmpty())
            continue;
        s.setValue("lastExportDir", QFileInfo(file).absolutePath());
        std::string err;
        if (exporter::writeSummaryCsv(r, file.toStdString(), &err))
            log(tr("Summary CSV written: %1").arg(file));
        else
            QMessageBox::warning(this, tr("Export"), QString::fromStdString(err));
    }
}

void MainWindow::exportJson()
{
    if (m_lastResults.empty())
        return;
    QSettings s;
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Experiment JSON"),
        s.value("lastExportDir", QDir::homePath()).toString() + "/" + exportBaseName(m_lastResults.front()) + ".json",
        tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    s.setValue("lastExportDir", QFileInfo(file).absolutePath());
    std::string err;
    if (exporter::writeJson(m_lastResults, file.toStdString(), &err))
        log(tr("JSON written: %1").arg(file));
    else
        QMessageBox::warning(this, tr("Export"), QString::fromStdString(err));
}

// =====================================================================================
// Dataset comparison
// =====================================================================================

void MainWindow::addToComparison(const std::vector<AlgorithmResult> &results)
{
    for (const AlgorithmResult &r : results) {
        LabeledResult lr;
        lr.label = QString("#%1 %2 | %3")
                       .arg(m_runCounter)
                       .arg(QFileInfo(QString::fromStdString(r.dataset)).fileName(),
                            QString::fromStdString(r.algorithmId))
                       .toStdString();
        lr.result = r;
        m_comparison.push_back(lr);
    }
    rebuildComparisonTable();
}

void MainWindow::rebuildComparisonTable()
{
    // rows: union of (algorithm, feature) in order of first appearance
    std::vector<std::pair<std::string, const Feature *> > rows;
    for (const LabeledResult &lr : m_comparison)
        for (const Feature &f : lr.result.features) {
            bool found = false;
            for (const auto &r : rows)
                if (r.first == lr.result.algorithmId && r.second->key == f.key) {
                    found = true;
                    break;
                }
            if (!found)
                rows.push_back(std::make_pair(lr.result.algorithmId, &f));
        }

    m_compTable->clear();
    m_compTable->setColumnCount(int(m_comparison.size()) + 2);
    m_compTable->setRowCount(int(rows.size()));
    QStringList headers;
    headers << tr("Algorithm") << tr("Feature");
    for (const LabeledResult &lr : m_comparison)
        headers << QString::fromStdString(lr.label);
    m_compTable->setHorizontalHeaderLabels(headers);
    for (int i = 0; i < int(rows.size()); ++i) {
        m_compTable->setItem(i, 0, roItem(QString::fromStdString(rows[size_t(i)].first)));
        m_compTable->setItem(i, 1, roItem(QString::fromStdString(rows[size_t(i)].second->key)));
        m_compTable->item(i, 1)->setToolTip(QString::fromStdString(rows[size_t(i)].second->label));
        for (int c = 0; c < int(m_comparison.size()); ++c) {
            const AlgorithmResult &r = m_comparison[size_t(c)].result;
            const Feature *f = r.algorithmId == rows[size_t(i)].first ? r.feature(rows[size_t(i)].second->key) : nullptr;
            QTableWidgetItem *it = roItem(f ? fmt(f->value) : QString());
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_compTable->setItem(i, c + 2, it);
        }
    }
    m_compTable->resizeColumnsToContents();
}

void MainWindow::renameComparisonColumn(int column)
{
    const int idx = column - 2;
    if (idx < 0 || idx >= int(m_comparison.size()))
        return;
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Rename run"), tr("Label (e.g. dataset condition):"),
                                                QLineEdit::Normal,
                                                QString::fromStdString(m_comparison[size_t(idx)].label), &ok);
    if (!ok || label.isEmpty())
        return;
    m_comparison[size_t(idx)].label = label.toStdString();
    rebuildComparisonTable();
}

void MainWindow::removeComparisonColumn()
{
    const int idx = m_compTable->currentColumn() - 2;
    if (idx < 0 || idx >= int(m_comparison.size()))
        return;
    m_comparison.erase(m_comparison.begin() + idx);
    rebuildComparisonTable();
}

void MainWindow::clearComparison()
{
    m_comparison.clear();
    rebuildComparisonTable();
}

void MainWindow::exportComparisonCsv()
{
    if (m_comparison.empty())
        return;
    QSettings s;
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Comparison CSV"), s.value("lastExportDir", QDir::homePath()).toString() + "/comparison.csv",
        tr("CSV (*.csv)"));
    if (file.isEmpty())
        return;
    s.setValue("lastExportDir", QFileInfo(file).absolutePath());
    std::string err;
    if (exporter::writeComparisonCsv(m_comparison, file.toStdString(), &err))
        log(tr("Comparison CSV written: %1").arg(file));
    else
        QMessageBox::warning(this, tr("Export"), QString::fromStdString(err));
}

void MainWindow::exportFeatureMatrix()
{
    if (m_comparison.empty())
        return;
    QSettings s;
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Feature matrix CSV"), s.value("lastExportDir", QDir::homePath()).toString() + "/feature_matrix.csv",
        tr("CSV (*.csv)"));
    if (file.isEmpty())
        return;
    s.setValue("lastExportDir", QFileInfo(file).absolutePath());
    const bool full = QMessageBox::question(this, tr("Feature matrix"),
                                            tr("Include all summary features?\n(No = feature-vector entries only)")) ==
                      QMessageBox::Yes;
    std::string err;
    if (exporter::writeFeatureMatrixCsv(m_comparison, full, file.toStdString(), &err))
        log(tr("Feature matrix CSV written: %1").arg(file));
    else
        QMessageBox::warning(this, tr("Export"), QString::fromStdString(err));
}

void MainWindow::log(const QString &msg)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + msg);
}
