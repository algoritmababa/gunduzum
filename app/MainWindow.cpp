#include "MainWindow.h"

#include "Datasets.h"
#include "FramePlayer.h"
#include "FrameSource.h"
#include "ImageUtils.h"
#include "MeasureWorker.h"
#include "Metrics.h"
#include "ResultTable.h"
#include "RoiApplyDialog.h"
#include "RoiView.h"
#include "TrendView.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <QCheckBox>
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
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <cstdlib>

namespace {

QTableWidgetItem *cell(const QString &text)
{
    QTableWidgetItem *it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

QString num(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 4) : QStringLiteral("-");
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Turbulence Measurement"));
    resize(1300, 820);
    QSettings s;

    // ---------------- inputs (left) ----------------
    m_inputs = new QWidget;
    QVBoxLayout *iv = new QVBoxLayout(m_inputs);
    iv->setContentsMargins(0, 0, 0, 0);

    QGroupBox *recBox = new QGroupBox(tr("1. Dataset"));
    QVBoxLayout *rv = new QVBoxLayout(recBox);
    QHBoxLayout *folderRow = new QHBoxLayout;
    QPushButton *bDatasets = new QPushButton(tr("Dataset folder..."));
    bDatasets->setToolTip(tr("Folder that holds all datasets (image subfolders and videos, also one level deeper, "
                             "e.g. folder/LOW/rec1). Labels and ROIs given before are remembered."));
    connect(bDatasets, &QPushButton::clicked, this, &MainWindow::chooseDatasetFolder);
    m_folderLabel = new QLabel(tr("no dataset folder"));
    m_folderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_folderLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); // long paths must not widen the panel
    folderRow->addWidget(bDatasets);
    folderRow->addWidget(m_folderLabel, 1);
    QPushButton *bRescan = new QPushButton(tr("Rescan"));
    bRescan->setToolTip(tr("Read the dataset folder again (new datasets, labels, measurements)."));
    connect(bRescan, &QPushButton::clicked, this, &MainWindow::refreshDatasets);
    folderRow->addWidget(bRescan);
    rv->addLayout(folderRow);
    m_datasets = new QTreeWidget;
    m_datasets->setRootIsDecorated(false);
    m_datasets->setAlternatingRowColors(true);
    m_datasets->setHeaderLabels(QStringList() << tr("Dataset") << tr("Label") << tr("ROI") << tr("Measured"));
    m_datasets->setMinimumHeight(170);
    m_datasets->header()->setStretchLastSection(false);
    m_datasets->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_datasets->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_datasets->setToolTip(tr("Click a dataset to open it. Unlabeled datasets (orange) cannot be measured."));
    connect(m_datasets, &QTreeWidget::itemClicked, this, &MainWindow::onDatasetClicked);
    rv->addWidget(m_datasets);
    m_datasetSummary = new QLabel;
    m_datasetSummary->setWordWrap(true);
    m_datasetSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    rv->addWidget(m_datasetSummary);
    QHBoxLayout *pathRow = new QHBoxLayout;
    m_pathEdit = new QLineEdit;
    m_pathEdit->setPlaceholderText(tr("Image folder or video file"));
    connect(m_pathEdit, &QLineEdit::returnPressed, this, [this]() { openRecording(m_pathEdit->text().trimmed()); });
    QPushButton *bFolder = new QPushButton(tr("Folder..."));
    QPushButton *bVideo = new QPushButton(tr("Video..."));
    connect(bFolder, &QPushButton::clicked, this, &MainWindow::browseFolder);
    connect(bVideo, &QPushButton::clicked, this, &MainWindow::browseVideo);
    pathRow->addWidget(m_pathEdit, 1);
    pathRow->addWidget(bFolder);
    pathRow->addWidget(bVideo);
    rv->addLayout(pathRow);
    m_info = new QLabel(tr("No recording"));
    rv->addWidget(m_info);
    iv->addWidget(recBox);

    QGroupBox *labelBox = new QGroupBox(tr("2. Label (saved with the dataset)"));
    QFormLayout *lf = new QFormLayout(labelBox);
    m_category = new QComboBox;
    m_category->setEditable(true);
    m_category->addItems(QStringList() << "LOW" << "MEDIUM" << "HIGH");
    m_category->setCurrentIndex(-1);
    m_category->setToolTip(tr("Turbulence category of this recording (empty = unlabeled). It is saved with the "
                              "dataset and loaded whenever the dataset is opened again. "
                              "The order of the list is used as LOW -> HIGH in the summary."));
    m_note = new QLineEdit;
    m_note->setPlaceholderText(tr("time, weather, exposure, distance ..."));
    lf->addRow(tr("Category"), m_category);
    lf->addRow(tr("Note"), m_note);
    m_labelState = new QLabel;
    m_labelState->setToolTip(tr("Every dataset is labeled once: the category and note are saved with it and "
                                "loaded whenever it is opened again."));
    lf->addRow(m_labelState);
    connect(m_category, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
            [this](int) { commitLabel(); });
    connect(m_category->lineEdit(), &QLineEdit::editingFinished, this, &MainWindow::commitLabel);
    connect(m_note, &QLineEdit::editingFinished, this, &MainWindow::commitLabel);
    iv->addWidget(labelBox);

    QGroupBox *roiBox = new QGroupBox(tr("3. ROI (drag on the image)"));
    QVBoxLayout *roiV = new QVBoxLayout(roiBox);
    QHBoxLayout *roiRow = new QHBoxLayout;
    m_roiLabel = new QLabel;
    QPushButton *bFull = new QPushButton(tr("Full frame"));
    connect(bFull, &QPushButton::clicked, this, &MainWindow::setFullFrame);
    roiRow->addWidget(m_roiLabel, 1);
    roiRow->addWidget(bFull);
    roiV->addLayout(roiRow);
    m_applyRoi = new QPushButton(tr("Apply this ROI to other datasets..."));
    m_applyRoi->setToolTip(tr("Optional: use the same ROI for other recordings. Their first frames are shown with "
                              "the ROI before anything is changed."));
    m_applyRoi->setEnabled(false);
    connect(m_applyRoi, &QPushButton::clicked, this, &MainWindow::applyRoiToOthers);
    roiV->addWidget(m_applyRoi);
    iv->addWidget(roiBox);

    QGroupBox *preBox = new QGroupBox(tr("4. Preprocessing (optional)"));
    QFormLayout *pf = new QFormLayout(preBox);
    m_clahe = new QCheckBox(tr("CLAHE - local contrast equalization"));
    m_clahe->setToolTip(tr("Contrast limited adaptive histogram equalization of the ROI, frame by frame, before the "
                           "algorithms. Evens out brightness / contrast differences between recordings, but it also "
                           "changes the brightness fluctuations inside a recording (scintillation). The variant is "
                           "stored in the 'preprocess' column, so the trend analysis can compare runs with and "
                           "without it."));
    m_clahe->setChecked(s.value("clahe", false).toBool());
    pf->addRow(m_clahe);
    m_clipLimit = new QDoubleSpinBox;
    m_clipLimit->setRange(0.1, 40.0);
    m_clipLimit->setDecimals(1);
    m_clipLimit->setSingleStep(0.5);
    m_clipLimit->setValue(s.value("claheClip", 2.0).toDouble());
    m_clipLimit->setToolTip(tr("Contrast limit: higher = stronger equalization (and more amplified noise). "
                               "Around 2-4 = mild, 40 (OpenCV default) = close to full histogram equalization."));
    pf->addRow(tr("Clip limit"), m_clipLimit);
    m_tiles = new QSpinBox;
    m_tiles->setRange(1, 64);
    m_tiles->setValue(s.value("claheTiles", 8).toInt());
    m_tiles->setToolTip(tr("The ROI is split into N x N tiles that are equalized separately (8 = OpenCV default). "
                           "1 = one histogram for the whole ROI."));
    pf->addRow(tr("Tiles per side"), m_tiles);
    m_previewPre = new QCheckBox(tr("Show the result on the image"));
    m_previewPre->setToolTip(tr("The ROI on the image (player and measurement view) shows what the algorithms "
                                "see after the preprocessing."));
    m_previewPre->setChecked(s.value("claheShow", true).toBool());
    pf->addRow(m_previewPre);
    connect(m_clahe, &QCheckBox::toggled, this, &MainWindow::preprocessChanged);
    connect(m_previewPre, &QCheckBox::toggled, this, &MainWindow::preprocessChanged);
    connect(m_clipLimit, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged), this,
            &MainWindow::preprocessChanged);
    connect(m_tiles, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this,
            &MainWindow::preprocessChanged);
    iv->addWidget(preBox);

    QGroupBox *outBox = new QGroupBox(tr("5. Measurement"));
    QFormLayout *of = new QFormLayout(outBox);
    m_window = new QSpinBox;
    m_window->setRange(0, 10000000);
    m_window->setValue(s.value("windowLength", 0).toInt());
    m_window->setSpecialValueText(tr("whole recording"));
    m_window->setToolTip(tr("Frames per window. Every window becomes one row of the results table."));
    of->addRow(tr("Window length (frames)"), m_window);
    QHBoxLayout *resRow = new QHBoxLayout;
    m_resultsEdit = new QLineEdit(
        s.value("resultsFile", QDir::homePath() + "/turbulence_results.csv").toString());
    QPushButton *bRes = new QPushButton(tr("..."));
    connect(bRes, &QPushButton::clicked, this, &MainWindow::chooseResultsFile);
    connect(m_resultsEdit, &QLineEdit::editingFinished, this, [this]() {
        m_trendView->setCsvPath(m_resultsEdit->text());
        reloadResults();
    });
    resRow->addWidget(m_resultsEdit, 1);
    resRow->addWidget(bRes);
    of->addRow(tr("Results file"), resRow);
    m_replaceRows = new QCheckBox(tr("Measuring a dataset again replaces its earlier rows"));
    m_replaceRows->setToolTip(tr("Earlier rows of the dataset with the same preprocessing are removed from the "
                                 "results file, so a new ROI or window length does not mix with the old one. "
                                 "Rows of other preprocessing variants are kept."));
    m_replaceRows->setChecked(s.value("replaceRows", true).toBool());
    of->addRow(m_replaceRows);
    m_skipMeasured = new QCheckBox(tr("RUN ALL: skip datasets already measured with this preprocessing"));
    m_skipMeasured->setChecked(s.value("skipMeasured", true).toBool());
    of->addRow(m_skipMeasured);
    m_frameCsv = new QCheckBox(tr("Also save per-frame values"));
    of->addRow(m_frameCsv);
    m_showFrames = new QCheckBox(tr("Show frames while measuring"));
    m_showFrames->setToolTip(tr("Displays the frames being measured in the Image tab (about 15 per second, so "
                                "the measurement is not slowed down)."));
    m_showFrames->setChecked(s.value("showFrames", false).toBool());
    of->addRow(m_showFrames);
    iv->addWidget(outBox);

    QGroupBox *metricsBox = new QGroupBox(tr("Algorithms (all run on every recording)"));
    QVBoxLayout *mv = new QVBoxLayout(metricsBox);
    for (const std::unique_ptr<turb::Metric> &m : turb::createMetrics()) {
        QLabel *l = new QLabel(QString("<b>%1</b> &nbsp;<span style='color:#57606a'>%2</span>")
                                   .arg(QString::fromStdString(m->label()), QString::fromStdString(m->algorithm())));
        l->setToolTip(QString::fromStdString(m->description()));
        mv->addWidget(l);
    }
    iv->addWidget(metricsBox);
    iv->addStretch(1);

    QWidget *left = new QWidget;
    QVBoxLayout *lv = new QVBoxLayout(left);
    QScrollArea *inputScroll = new QScrollArea;
    inputScroll->setWidget(m_inputs);
    inputScroll->setWidgetResizable(true);
    inputScroll->setFrameShape(QFrame::NoFrame);
    inputScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inputScroll->setMinimumWidth(m_inputs->minimumSizeHint().width() + 20);
    lv->addWidget(inputScroll, 1);
    QHBoxLayout *runRow = new QHBoxLayout;
    m_runButton = new QPushButton(tr("RUN"));
    m_runButton->setMinimumHeight(34);
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setEnabled(false);
    connect(m_runButton, &QPushButton::clicked, this, &MainWindow::run);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancel);
    m_runAllButton = new QPushButton(tr("RUN ALL"));
    m_runAllButton->setMinimumHeight(34);
    m_runAllButton->setToolTip(tr("Measure every dataset of the dataset folder with its own label and ROI. "
                                  "Blocked while a dataset is unlabeled."));
    m_runAllButton->setEnabled(false);
    connect(m_runAllButton, &QPushButton::clicked, this, &MainWindow::runAll);
    runRow->addWidget(m_runButton, 1);
    runRow->addWidget(m_runAllButton, 1);
    runRow->addWidget(m_cancelButton);
    lv->addLayout(runRow);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    lv->addWidget(m_progress);
    m_status = new QLabel(tr("Idle"));
    m_status->setWordWrap(true);
    lv->addWidget(m_status);

    // ---------------- outputs (right) ----------------
    m_tabs = new QTabWidget;
    QWidget *imagePage = new QWidget;
    QVBoxLayout *ip = new QVBoxLayout(imagePage);
    ip->setContentsMargins(0, 0, 0, 0);
    ip->setSpacing(2);
    m_view = new RoiView;
    connect(m_view, &RoiView::roiSelected, this, &MainWindow::onRoiSelected);
    ip->addWidget(m_view, 1);
    m_player = new FramePlayer;
    connect(m_player, &FramePlayer::frameReady, this, &MainWindow::showFrame);
    ip->addWidget(m_player);
    m_tabs->addTab(imagePage, tr("Image / ROI"));

    m_results = new QTableWidget;
    m_results->verticalHeader()->setVisible(false);
    m_tabs->addTab(m_results, tr("Results"));

    m_trendView = new TrendView;
    m_trendView->setCsvPath(m_resultsEdit->text());
    m_trendView->setCategoryOrderProvider([this]() { return categoryOrder(); });
    connect(m_trendView, &TrendView::logMessage, this, &MainWindow::log);
    m_tabs->addTab(m_trendView, tr("Trend analysis"));

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_tabs->addTab(m_log, tr("Log"));

    QSplitter *split = new QSplitter;
    split->addWidget(left);
    split->addWidget(m_tabs);
    split->setStretchFactor(1, 1);
    split->setSizes(QList<int>() << 480 << 820);
    setCentralWidget(split);

    updateRoiLabel();
    preprocessChanged();
    reloadResults();
    m_datasetFolder = s.value("datasetFolder").toString();
    if (!m_datasetFolder.isEmpty() && !QFileInfo(m_datasetFolder).isDir())
        m_datasetFolder.clear();
    refreshDatasets();
    log(tr("OpenCV %1").arg(CV_VERSION));
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
    QSettings s;
    s.setValue("windowLength", m_window->value());
    s.setValue("resultsFile", m_resultsEdit->text());
    s.setValue("clahe", m_clahe->isChecked());
    s.setValue("claheClip", m_clipLimit->value());
    s.setValue("claheTiles", m_tiles->value());
    s.setValue("claheShow", m_previewPre->isChecked());
    s.setValue("skipMeasured", m_skipMeasured->isChecked());
    s.setValue("replaceRows", m_replaceRows->isChecked());
    if (m_thread && m_thread->isRunning()) {
        m_worker->cancel();
        m_thread->quit();
        m_thread->wait();
    }
    e->accept();
}

void MainWindow::log(const QString &msg)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + msg);
}

// ---------------------------------------------------------------------------------------

void MainWindow::browseFolder()
{
    QSettings s;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Image folder"),
                                                          s.value("lastDir", QDir::homePath()).toString());
    if (dir.isEmpty())
        return;
    s.setValue("lastDir", QFileInfo(dir).absolutePath());
    openRecording(dir);
}

void MainWindow::browseVideo()
{
    QSettings s;
    const QString f = QFileDialog::getOpenFileName(this, tr("Video"), s.value("lastDir", QDir::homePath()).toString(),
                                                   tr("Video (*.avi *.mp4 *.mov *.mkv *.mpg *.mpeg *.wmv *.m4v);;"
                                                      "All files (*)"));
    if (f.isEmpty())
        return;
    s.setValue("lastDir", QFileInfo(f).absolutePath());
    openRecording(f);
}

bool MainWindow::openRecording(const QString &path, bool quiet)
{
    if (path.isEmpty())
        return false;
    std::string err;
    std::unique_ptr<turb::FrameSource> src = turb::FrameSource::create(path.toStdString(), &err);
    cv::Mat first;
    if (!src || !src->readAt(0, first)) {
        const QString msg = err.empty() ? tr("Cannot read the first frame.") : QString::fromStdString(err);
        if (quiet)
            log(tr("Cannot open %1: %2").arg(path, msg));
        else
            QMessageBox::warning(this, tr("Recording"), msg);
        return false;
    }
    commitLabel(); // an unfinished edit of the previous dataset's label
    m_path = QDir::cleanPath(path);
    m_pathEdit->setText(m_path);
    m_info->setText(QString::fromStdString(src->description()));
    m_view->setImage(toQImage(first));
    m_player->setSource(m_path);
    m_applyRoi->setEnabled(true);
    rememberRecording(m_path);
    loadLabel(); // may take over the label and ROI of the same dataset at its old place
    // the ROI drawn last time for this recording (or applied from another one) is remembered
    m_roi = QSettings().value("roi/" + m_path).toRect();
    m_view->setRoi(m_roi);
    updateRoiLabel();
    // highlight it in the dataset list
    for (int i = 0; i < m_datasets->topLevelItemCount(); ++i)
        if (m_datasets->topLevelItem(i)->data(0, Qt::UserRole).toString() == m_path)
            m_datasets->setCurrentItem(m_datasets->topLevelItem(i));
    m_tabs->setCurrentIndex(0);
    log(tr("Opened %1 (%2)").arg(path, QString::fromStdString(src->description())));
    return true;
}

void MainWindow::onRoiSelected(const QRect &roi)
{
    m_roi = roi;
    if (!m_path.isEmpty())
        QSettings().setValue("roi/" + m_path, roi);
    updateRoiLabel();
    m_player->refresh(); // the preprocessing preview follows the ROI
}

void MainWindow::setFullFrame()
{
    m_roi = QRect();
    if (!m_path.isEmpty())
        QSettings().remove("roi/" + m_path);
    m_view->setRoi(m_roi);
    updateRoiLabel();
    m_player->refresh();
}

turb::Table MainWindow::loadResults() const
{
    turb::Table table;
    const QString results = m_resultsEdit->text().trimmed();
    if (QFileInfo::exists(results))
        turb::loadTable(results.toStdString(), table, nullptr);
    return table;
}

void MainWindow::loadLabel()
{
    m_loadingLabel = true;
    const datasets::Label l = datasets::lookup(m_path, loadResults());
    if (l.category.isEmpty()) {
        m_category->setCurrentIndex(-1);
        m_category->setEditText(QString());
    } else {
        if (m_category->findText(l.category) < 0)
            m_category->addItem(l.category);
        m_category->setCurrentIndex(m_category->findText(l.category));
    }
    m_note->setText(l.note);
    m_loadingLabel = false;
    QString origin;
    switch (l.origin) {
    case datasets::Origin::Results: origin = tr("Label taken from the results file."); break;
    case datasets::Origin::SameName: origin = tr("Label taken over from %1.").arg(l.from); break;
    case datasets::Origin::Conflict:
        origin = tr("Datasets with this name carry different labels - choose one.");
        break;
    default: break;
    }
    if (l.origin == datasets::Origin::SameName)
        log(tr("%1: label '%2' taken over from %3").arg(QFileInfo(m_path).fileName(), l.category, l.from));
    updateLabelState(origin);
}

void MainWindow::commitLabel()
{
    if (m_loadingLabel || m_path.isEmpty())
        return;
    const QString category = m_category->currentText().trimmed();
    const QString note = m_note->text().trimmed();
    QSettings s;
    const bool changed = !s.contains("category/" + m_path) || s.value("category/" + m_path).toString() != category;
    s.setValue("category/" + m_path, category);
    s.setValue("note/" + m_path, note);
    if (!category.isEmpty() && m_category->findText(category) < 0)
        m_category->addItem(category); // a new category name joins the order (after the existing ones)
    updateLabelState();
    if (!changed)
        return;
    refreshDatasets();

    // rows measured with another label would put the dataset into two categories
    const QString results = m_resultsEdit->text().trimmed();
    turb::Table table;
    if (!QFileInfo::exists(results) || !turb::loadTable(results.toStdString(), table, nullptr))
        return;
    const int n = turb::countRowsToRelabel(table, m_path.toStdString(), category.toStdString());
    if (n == 0)
        return;
    log(tr("Label of %1 set to '%2'").arg(QFileInfo(m_path).fileName(), category.isEmpty() ? tr("unlabeled") : category));
    if (QMessageBox::question(
            this, tr("Label"),
            tr("The results file has %1 row(s) of this dataset with another label. Set them to '%2' too?\n\n"
               "Otherwise the trend analysis sees the dataset in two categories.")
                .arg(n)
                .arg(category.isEmpty() ? tr("unlabeled") : category)) != QMessageBox::Yes)
        return;
    std::string err;
    const int done = turb::relabelSource(results.toStdString(), m_path.toStdString(), category.toStdString(), &err);
    if (done < 0) {
        QMessageBox::warning(this, tr("Label"), QString::fromStdString(err));
        return;
    }
    log(tr("%1 row(s) of %2 relabeled in the results file").arg(done).arg(QFileInfo(m_path).fileName()));
    reloadResults();
}

void MainWindow::updateLabelState(const QString &origin)
{
    if (m_path.isEmpty()) {
        m_labelState->clear();
        return;
    }
    m_labelState->setWordWrap(true);
    if (m_category->currentText().trimmed().isEmpty())
        m_labelState->setText(QString::fromUtf8("<span style='color:#9a6700'>\xe2\x9a\xa0 ") +
                              (origin.isEmpty() ? tr("Not labeled yet - choose a category once (RUN is blocked "
                                                     "until then).")
                                                : origin.toHtmlEscaped()) +
                              "</span>");
    else
        m_labelState->setText(QString::fromUtf8("<span style='color:#1a7f37'>\xe2\x9c\x93 ") +
                              (origin.isEmpty() ? tr("Label saved with this dataset.") : origin.toHtmlEscaped()) +
                              "</span>");
}

turb::Preprocess MainWindow::preprocess() const
{
    turb::Preprocess pp;
    pp.clahe = m_clahe->isChecked();
    pp.clipLimit = m_clipLimit->value();
    pp.tileGrid = m_tiles->value();
    return pp;
}

void MainWindow::preprocessChanged()
{
    const bool on = m_clahe->isChecked();
    m_clipLimit->setEnabled(on);
    m_tiles->setEnabled(on);
    m_previewPre->setEnabled(on);
    if (on && m_previewPre->isChecked()) {
        const turb::Preprocess pp = preprocess();
        m_player->setFilter([this, pp](cv::Mat &frame) {
            turb::Roi r;
            r.x = m_roi.x();
            r.y = m_roi.y();
            r.width = m_roi.width();
            r.height = m_roi.height();
            if (m_roi.isEmpty())
                r = turb::Roi();
            turb::previewPreprocess(frame, turb::effectiveRoi(r, frame.size()), pp);
        });
    } else {
        m_player->setFilter(std::function<void(cv::Mat &)>());
    }
    m_player->refresh();
    refreshDatasets(); // "Measured" depends on the preprocessing
}

void MainWindow::updateRoiLabel()
{
    m_roiLabel->setText(m_roi.isEmpty() ? tr("full frame")
                                        : tr("x=%1  y=%2  %3 x %4 px")
                                              .arg(m_roi.x())
                                              .arg(m_roi.y())
                                              .arg(m_roi.width())
                                              .arg(m_roi.height()));
}

void MainWindow::showFrame(const QImage &image, int frameIndex)
{
    m_view->setImage(image); // the ROI stays drawn on top
    if (m_thread)
        m_status->setText(tr("measuring frame %1").arg(frameIndex + 1));
}

void MainWindow::rememberRecording(const QString &path)
{
    QSettings s;
    QStringList recent = s.value("recentRecordings").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 50)
        recent.removeLast();
    s.setValue("recentRecordings", recent);
}

// Candidates for "apply ROI": recordings of the results file and recently opened ones.
QStringList MainWindow::knownRecordings() const
{
    QStringList paths;
    turb::Table table;
    if (QFileInfo::exists(m_resultsEdit->text().trimmed()) &&
        turb::loadTable(m_resultsEdit->text().trimmed().toStdString(), table, nullptr)) {
        const int c = table.column("source");
        if (c >= 0)
            for (const std::vector<std::string> &r : table.rows)
                if (size_t(c) < r.size()) {
                    const QString p = QDir::cleanPath(QString::fromStdString(r[size_t(c)]));
                    if (!paths.contains(p))
                        paths << p;
                }
    }
    for (const QString &p : QSettings().value("recentRecordings").toStringList())
        if (!paths.contains(p) && QFileInfo::exists(p))
            paths << p;
    paths.removeAll(m_path);
    return paths;
}

void MainWindow::applyRoiToOthers()
{
    if (m_path.isEmpty())
        return;
    m_player->pause();
    RoiApplyDialog dlg(m_roi, m_path, knownRecordings(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QStringList applied = dlg.appliedTo();
    log(tr("ROI %1 applied to %2 dataset(s): %3")
            .arg(m_roiLabel->text())
            .arg(applied.size())
            .arg(applied.join(", ")));
    for (const QString &p : applied)
        rememberRecording(p);
    m_status->setText(tr("ROI applied to %1 dataset(s); it is used when they are opened.").arg(applied.size()));
}

void MainWindow::chooseResultsFile()
{
    const QString f = QFileDialog::getSaveFileName(this, tr("Results file (rows are appended)"), m_resultsEdit->text(),
                                                   tr("CSV (*.csv)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (f.isEmpty())
        return;
    m_resultsEdit->setText(f);
    m_trendView->setCsvPath(f);
    QSettings().setValue("resultsFile", f);
    reloadResults();
}

QStringList MainWindow::categoryOrder() const
{
    QStringList order;
    for (int i = 0; i < m_category->count(); ++i)
        order << m_category->itemText(i);
    return order;
}

// ---------------------------------------------------------------------------------------

void MainWindow::setRunning(bool running)
{
    // the player and the measurement would both draw into the image view
    if (running)
        m_player->pause();
    m_player->setEnabled(!running && !m_path.isEmpty());
    m_inputs->setEnabled(!running);
    m_runButton->setEnabled(!running);
    m_runAllButton->setEnabled(!running && m_datasets->topLevelItemCount() > 0);
    m_cancelButton->setEnabled(running);
}

void MainWindow::run()
{
    if (m_thread)
        return;
    if (m_path.isEmpty()) {
        QMessageBox::information(this, tr("Run"), tr("Open a recording first."));
        return;
    }
    const QString results = m_resultsEdit->text().trimmed();
    if (results.isEmpty()) {
        QMessageBox::information(this, tr("Run"), tr("Choose a results file."));
        return;
    }

    commitLabel();
    if (m_category->currentText().trimmed().isEmpty()) {
        // every measured dataset carries its category
        if (!m_batch)
            QMessageBox::warning(this, tr("Run"),
                                 tr("%1 is not labeled.\n\nChoose its category in \"2. Label\" first - it is saved "
                                    "with the dataset, so this is needed only once.")
                                     .arg(QFileInfo(m_path).fileName()));
        return;
    }
    turb::Recording rec;
    rec.path = m_path.toStdString();
    rec.category = m_category->currentText().trimmed().toStdString();
    rec.note = m_note->text().trimmed().toStdString();
    if (!m_roi.isEmpty()) {
        rec.roi.x = m_roi.x();
        rec.roi.y = m_roi.y();
        rec.roi.width = m_roi.width();
        rec.roi.height = m_roi.height();
    }
    turb::MeasureSettings settings;
    settings.windowLength = m_window->value();
    settings.preprocess = preprocess();
    if (m_frameCsv->isChecked()) {
        const QFileInfo rf(results);
        settings.frameCsvPath = (rf.absolutePath() + "/" + rf.completeBaseName() + "_" +
                                 QString::fromStdString(turb::recordingId(rec.path)) + "_" +
                                 QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_frames.csv")
                                    .toStdString();
    }
    QSettings().setValue("resultsFile", results);
    QSettings().setValue("windowLength", m_window->value());
    QSettings().setValue("showFrames", m_showFrames->isChecked());

    m_thread = new QThread(this);
    m_worker = new MeasureWorker(rec, settings, results, m_showFrames->isChecked(), m_previewPre->isChecked());
    if (m_replaceRows->isChecked())
        m_worker->setReplaceSources(datasets::replacedSources(m_path, m_category->currentText().trimmed(), loadResults()));
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &MeasureWorker::run);
    connect(m_worker, &MeasureWorker::progress, this, &MainWindow::onProgress);
    connect(m_worker, &MeasureWorker::frame, this, &MainWindow::showFrame);
    connect(m_worker, &MeasureWorker::done, this, &MainWindow::onDone);
    connect(m_worker, &MeasureWorker::done, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    log(tr("Measuring %1  [category: %2, preprocessing: %3]")
            .arg(m_path, rec.category.empty() ? tr("unlabeled") : QString::fromStdString(rec.category),
                 QString::fromStdString(settings.preprocess.label())));
    if (!settings.frameCsvPath.empty())
        log(tr("Per-frame values -> %1").arg(QString::fromStdString(settings.frameCsvPath)));
    m_progress->setValue(0);
    m_status->setText(tr("Running..."));
    if (m_showFrames->isChecked())
        m_tabs->setCurrentIndex(0);
    setRunning(true);
    m_thread->start();
}

// ---------------------------------------------------------------------------------------
// dataset folder

void MainWindow::chooseDatasetFolder()
{
    QSettings s;
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Dataset folder"), m_datasetFolder.isEmpty() ? s.value("lastDir", QDir::homePath()).toString()
                                                              : m_datasetFolder);
    if (dir.isEmpty())
        return;
    m_datasetFolder = QDir::cleanPath(dir);
    s.setValue("datasetFolder", m_datasetFolder);
    refreshDatasets();
    log(tr("Dataset folder %1: %2 dataset(s)").arg(m_datasetFolder).arg(m_datasets->topLevelItemCount()));
}

void MainWindow::refreshDatasets()
{
    m_datasets->clear();
    if (m_datasetFolder.isEmpty()) {
        m_folderLabel->setText(tr("no dataset folder - or open single datasets below"));
        m_datasetSummary->clear();
        m_datasets->hide();
        m_runAllButton->setEnabled(false);
        return;
    }
    m_datasets->show();
    m_folderLabel->setText(QFileInfo(m_datasetFolder).fileName());
    m_folderLabel->setToolTip(m_datasetFolder);
    const turb::Table results = loadResults();
    const QString variant = QString::fromStdString(preprocess().label());
    const QStringList found = datasets::find(m_datasetFolder);
    int unlabeled = 0, measured = 0;
    const QString base = m_datasetFolder + "/";
    for (const QString &p : found) {
        const datasets::Label l = datasets::lookup(p, results);
        const int rows = datasets::measuredRows(p, results, variant);
        QTreeWidgetItem *it = new QTreeWidgetItem;
        it->setText(0, p.startsWith(base) ? p.mid(base.size()) : QFileInfo(p).fileName());
        it->setToolTip(0, p);
        it->setData(0, Qt::UserRole, p);
        if (l.labeled()) {
            it->setText(1, l.category);
            if (l.origin == datasets::Origin::SameName)
                it->setToolTip(1, tr("taken over from %1").arg(l.from));
        } else {
            ++unlabeled;
            it->setText(1, QString::fromUtf8("\xe2\x9a\xa0 ") +
                               (l.origin == datasets::Origin::Conflict ? tr("conflict") : tr("unlabeled")));
            it->setToolTip(1, l.origin == datasets::Origin::Conflict
                                  ? tr("Datasets with this name carry different labels - open it and choose one.")
                                  : tr("Open it and choose its category once."));
            for (int c = 0; c < 4; ++c)
                it->setForeground(c, QColor("#9a6700"));
            it->setBackground(1, QColor("#fff8c5"));
        }
        it->setText(2, l.roi.isEmpty() ? tr("full") : QString("%1x%2").arg(l.roi.width()).arg(l.roi.height()));
        it->setText(3, rows > 0 ? QString::fromUtf8("\xe2\x9c\x93 %1").arg(rows) : QString());
        it->setToolTip(3, rows > 0 ? tr("%1 row(s) with preprocessing '%2' in the results file").arg(rows).arg(variant)
                                   : tr("not measured with preprocessing '%1'").arg(variant));
        measured += rows > 0;
        m_datasets->addTopLevelItem(it);
        if (p == m_path)
            m_datasets->setCurrentItem(it);
    }
    if (found.isEmpty())
        m_datasetSummary->setText(tr("No datasets found (image subfolders or video files)."));
    else
        m_datasetSummary->setText(
            tr("%1 dataset(s), %2 measured").arg(found.size()).arg(measured) +
            (unlabeled > 0 ? QString::fromUtf8(" - <span style='color:#9a6700'><b>\xe2\x9a\xa0 %1 unlabeled</b>: "
                                               "open each once and choose its category</span>")
                                 .arg(unlabeled)
                           : QString::fromUtf8(" - <span style='color:#1a7f37'>\xe2\x9c\x93 all labeled</span>")));
    m_runAllButton->setEnabled(!m_thread && !found.isEmpty());
}

void MainWindow::onDatasetClicked(QTreeWidgetItem *item)
{
    if (!item || m_thread)
        return;
    const QString p = item->data(0, Qt::UserRole).toString();
    if (p != m_path)
        openRecording(p);
}

void MainWindow::runAll()
{
    if (m_thread)
        return;
    if (m_resultsEdit->text().trimmed().isEmpty()) {
        QMessageBox::information(this, tr("Run all"), tr("Choose a results file."));
        return;
    }
    commitLabel();
    refreshDatasets();
    const turb::Table results = loadResults();
    const QString variant = QString::fromStdString(preprocess().label());
    QStringList todo, unlabeled;
    int skipped = 0;
    for (int i = 0; i < m_datasets->topLevelItemCount(); ++i) {
        const QString p = m_datasets->topLevelItem(i)->data(0, Qt::UserRole).toString();
        if (!datasets::lookup(p, results).labeled()) {
            unlabeled << m_datasets->topLevelItem(i)->text(0);
            continue;
        }
        if (m_skipMeasured->isChecked() && datasets::measuredRows(p, results, variant) > 0) {
            ++skipped;
            continue;
        }
        todo << p;
    }
    if (!unlabeled.isEmpty()) {
        QMessageBox::warning(this, tr("Run all"),
                             tr("%1 dataset(s) are not labeled:\n\n%2\n\nOpen each of them once (click it in the list) "
                                "and choose its category. Nothing was measured.")
                                 .arg(unlabeled.size())
                                 .arg(unlabeled.mid(0, 15).join("\n") +
                                      (unlabeled.size() > 15 ? "\n..." : "")));
        return;
    }
    if (todo.isEmpty()) {
        QMessageBox::information(this, tr("Run all"),
                                 tr("All %1 dataset(s) are already measured with preprocessing '%2'.\n\n"
                                    "Uncheck \"skip datasets already measured\" to measure them again.")
                                     .arg(skipped)
                                     .arg(variant));
        return;
    }
    if (QMessageBox::question(this, tr("Run all"),
                              tr("Measure %1 dataset(s) with preprocessing '%2'?%3")
                                  .arg(todo.size())
                                  .arg(variant)
                                  .arg(skipped > 0 ? tr("\n(%1 already measured are skipped.)").arg(skipped)
                                                   : QString())) != QMessageBox::Yes)
        return;
    m_queue = todo;
    m_batch = true;
    m_batchTotal = todo.size();
    m_batchDone = 0;
    m_batchFailed.clear();
    log(tr("RUN ALL: %1 dataset(s), preprocessing %2").arg(todo.size()).arg(variant));
    startNextInQueue();
}

void MainWindow::startNextInQueue()
{
    while (!m_queue.isEmpty()) {
        const QString p = m_queue.takeFirst();
        if (!openRecording(p, true)) {
            m_batchFailed << QFileInfo(p).fileName() + tr(": cannot be opened");
            ++m_batchDone;
            continue;
        }
        run();
        if (m_thread)
            return; // onDone continues with the next one
        m_batchFailed << QFileInfo(p).fileName() + tr(": not started");
        ++m_batchDone;
    }
    finishQueue(QString());
}

void MainWindow::finishQueue(const QString &why)
{
    const int left = m_queue.size();
    m_queue.clear();
    m_batch = false;
    refreshDatasets();
    QString text = tr("RUN ALL %1: %2 of %3 dataset(s) done")
                       .arg(why.isEmpty() ? tr("finished") : why)
                       .arg(m_batchDone - m_batchFailed.size())
                       .arg(m_batchTotal);
    if (left > 0)
        text += tr(", %1 not started").arg(left);
    if (!m_batchFailed.isEmpty())
        text += tr(", %1 failed:\n%2").arg(m_batchFailed.size()).arg(m_batchFailed.join("\n"));
    m_status->setText(text.section('\n', 0, 0));
    log(text);
    m_tabs->setCurrentIndex(1);
    if (!m_batchFailed.isEmpty())
        QMessageBox::warning(this, tr("Run all"), text);
}

void MainWindow::cancel()
{
    if (m_worker)
        m_worker->cancel();
}

void MainWindow::onProgress(int done, int total)
{
    m_progress->setValue(total > 0 ? int(100LL * done / total) : 0);
    const QString frames = tr("frame %1 / %2").arg(done).arg(total);
    m_status->setText(m_batch ? tr("dataset %1 / %2 (%3): %4")
                                    .arg(m_batchDone + 1)
                                    .arg(m_batchTotal)
                                    .arg(QFileInfo(m_path).fileName(), frames)
                              : frames);
}

void MainWindow::onDone(bool ok, const QString &message)
{
    m_thread = nullptr; // deleted via deleteLater when the thread finishes
    m_worker = nullptr;
    setRunning(false);
    m_status->setText(message);
    log(message);
    if (m_batch) {
        reloadResults();
        if (!ok && message.startsWith("Cancel")) {
            finishQueue(tr("cancelled"));
            return;
        }
        if (!ok)
            m_batchFailed << QFileInfo(m_path).fileName() + ": " + message;
        refreshDatasets();
        ++m_batchDone;
        QTimer::singleShot(0, this, &MainWindow::startNextInQueue);
        return;
    }
    if (!ok) {
        if (!message.startsWith("Cancel"))
            QMessageBox::warning(this, tr("Measurement"), message);
        return;
    }
    m_progress->setValue(100);
    reloadResults();
    if (!m_showFrames->isChecked())
        m_tabs->setCurrentIndex(1);
    m_results->scrollToBottom();
}

// ---------------------------------------------------------------------------------------

void MainWindow::reloadResults()
{
    turb::Table table;
    const std::string path = m_resultsEdit->text().trimmed().toStdString();
    if (QFileInfo::exists(m_resultsEdit->text().trimmed()))
        turb::loadTable(path, table, nullptr);

    // results: the CSV as it is
    m_results->clear();
    m_results->setColumnCount(int(table.header.size()));
    m_results->setRowCount(int(table.rows.size()));
    QStringList headers;
    for (const std::string &h : table.header)
        headers << QString::fromStdString(h);
    m_results->setHorizontalHeaderLabels(headers);
    for (int r = 0; r < int(table.rows.size()); ++r)
        for (int c = 0; c < int(table.rows[size_t(r)].size()) && c < int(table.header.size()); ++c) {
            QString text = QString::fromStdString(table.rows[size_t(r)][size_t(c)]);
            bool isNum = false;
            const double v = text.toDouble(&isNum);
            if (isNum && c >= table.column("measured_at") + 1)
                text = num(v);
            m_results->setItem(r, c, cell(text));
        }
    m_results->resizeColumnsToContents();
    if (!m_batch)
        refreshDatasets(); // the "Measured" column follows the results file
}
