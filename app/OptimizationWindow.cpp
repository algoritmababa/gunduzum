#include "OptimizationWindow.h"

#include "AlgorithmRegistry.h"
#include "DatasetConfig.h"
#include "MainWindow.h"
#include "OptimizationWorker.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
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
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>

#include <cmath>

using namespace turb;

namespace {

const int kPathRole = Qt::UserRole + 1;
const int kIdRole = Qt::UserRole + 2;

QString fmt(double v, int prec = 4)
{
    if (std::isnan(v))
        return QStringLiteral("NaN");
    if (std::isinf(v))
        return v > 0 ? QStringLiteral("inf") : QStringLiteral("-inf");
    return QString::number(v, 'g', prec);
}

QTableWidgetItem *roItem(const QString &text)
{
    QTableWidgetItem *it = new QTableWidgetItem(text);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

QSpinBox *makeSpin(int min, int max, int value, const QString &tip = QString())
{
    QSpinBox *s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(value);
    s->setToolTip(tip);
    return s;
}

QJsonValue jnum(double v)
{
    return std::isfinite(v) ? QJsonValue(v) : QJsonValue();
}

QJsonObject paramsToJson(const ParamSet &p)
{
    QJsonObject o;
    for (const auto &kv : p.values())
        o[QString::fromStdString(kv.first)] = kv.second;
    return o;
}

} // namespace

OptimizationWindow::OptimizationWindow(MainWindow *mainWindow)
    : QWidget(mainWindow, Qt::Window)
    , m_main(mainWindow)
{
    qRegisterMetaType<turb::OptimizationReport>("turb::OptimizationReport");
    setWindowTitle(tr("Parameter Optimization - labeled categories"));
    resize(1400, 900);

    QSplitter *split = new QSplitter(Qt::Horizontal, this);
    QHBoxLayout *root = new QHBoxLayout(this);
    root->addWidget(split);

    // ======================= setup panel =======================
    QWidget *left = new QWidget;
    QVBoxLayout *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_setupPanel = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(m_setupPanel);

    // 1. labeled recordings
    QGroupBox *data = new QGroupBox(tr("1. Labeled recordings (image folders / videos per category)"));
    QVBoxLayout *dv = new QVBoxLayout(data);
    QLabel *dataHelp = new QLabel(
        tr("Every recording is cut into windows; each window is one labeled sample. Use at least 2 "
           "recordings per category so that validation can leave whole recordings out. The ROI of a "
           "recording comes from its saved dataset config if present, otherwise from the main window."));
    dataHelp->setWordWrap(true);
    dv->addWidget(dataHelp);
    const char *defaults[kClasses] = {"LOW", "MEDIUM", "HIGH"};
    for (int c = 0; c < kClasses; ++c) {
        QGroupBox *box = new QGroupBox(tr("Category %1").arg(c + 1));
        QVBoxLayout *bv = new QVBoxLayout(box);
        QHBoxLayout *head = new QHBoxLayout;
        m_className[c] = new QLineEdit(defaults[c]);
        m_className[c]->setToolTip(tr("Category name (label)"));
        QPushButton *bf = new QPushButton(tr("Folder..."));
        QPushButton *bvid = new QPushButton(tr("Video..."));
        QPushButton *brm = new QPushButton(tr("Remove"));
        connect(bf, &QPushButton::clicked, this, [this, c]() { addFolder(c); });
        connect(bvid, &QPushButton::clicked, this, [this, c]() { addVideo(c); });
        connect(brm, &QPushButton::clicked, this, [this, c]() { removeSource(c); });
        head->addWidget(m_className[c], 1);
        head->addWidget(bf);
        head->addWidget(bvid);
        head->addWidget(brm);
        bv->addLayout(head);
        m_sources[c] = new QListWidget;
        m_sources[c]->setMinimumHeight(70);
        m_sources[c]->setMaximumHeight(110);
        m_sources[c]->setSelectionMode(QAbstractItemView::ExtendedSelection);
        bv->addWidget(m_sources[c]);
        dv->addWidget(box);
    }
    v->addWidget(data);

    // 2. samples
    QGroupBox *samp = new QGroupBox(tr("2. Samples"));
    QFormLayout *sf = new QFormLayout(samp);
    m_windowLength = makeSpin(0, 10000000, 300, tr("Frames per sample window (0 = whole recording is one sample)."));
    m_windowLength->setSpecialValueText(tr("whole recording"));
    m_windowStride = makeSpin(0, 10000000, 0, tr("Distance between window starts (0 = window length, no overlap)."));
    m_windowStride->setSpecialValueText(tr("= window length"));
    m_maxWindows = makeSpin(0, 100000, 8, tr("Windows kept per recording, spread evenly (0 = all). "
                                             "Fewer windows = faster search."));
    m_maxWindows->setSpecialValueText(tr("all"));
    m_frameStep = makeSpin(1, 1000, 1, tr("Use every n-th frame inside a window (speeds up the search; "
                                          "changes frame-to-frame features)."));
    sf->addRow(tr("Window length (frames)"), m_windowLength);
    sf->addRow(tr("Window stride"), m_windowStride);
    sf->addRow(tr("Max windows / recording"), m_maxWindows);
    sf->addRow(tr("Frame step"), m_frameStep);
    v->addWidget(samp);

    // 3. algorithms
    QGroupBox *algs = new QGroupBox(tr("3. Candidate algorithms"));
    QVBoxLayout *av = new QVBoxLayout(algs);
    m_algorithms = new QListWidget;
    for (const AlgorithmRegistry::Entry &e : AlgorithmRegistry::instance().entries()) {
        QListWidgetItem *it = new QListWidgetItem(QString::fromStdString(e.name));
        it->setData(kIdRole, QString::fromStdString(e.id));
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Checked);
        it->setToolTip(QString::fromStdString(e.id));
        m_algorithms->addItem(it);
    }
    m_algorithms->setMinimumHeight(170);
    av->addWidget(m_algorithms);
    m_startFromMain = new QCheckBox(tr("Start from the parameters set in the main window"));
    m_startFromMain->setChecked(true);
    av->addWidget(m_startFromMain);
    v->addWidget(algs);

    // 4. search
    QGroupBox *search = new QGroupBox(tr("4. Search"));
    QFormLayout *qf = new QFormLayout(search);
    m_levels = makeSpin(1, 6, 2, tr("Recursion depth of the coarse-to-fine search: level 0 sweeps the full search "
                                    "ranges, every further level repeats the sweep around the best values."));
    m_gridPoints = makeSpin(2, 15, 5, tr("Values tried per numeric parameter and level."));
    m_maxEvals = makeSpin(1, 100000, 300, tr("Upper limit of distinct parameter sets evaluated on the data."));
    m_objective = new QComboBox;
    m_objective->addItem(tr("Cross-validated accuracy (Fisher ratio breaks ties)"));
    m_objective->addItem(tr("Fisher ratio (mean of the 3 best features)"));
    m_featureSet = new QComboBox;
    m_featureSet->addItem(tr("Feature vector of each algorithm"));
    m_featureSet->addItem(tr("All summary features"));
    m_threads = makeSpin(0, 256, 0, tr("Windows processed in parallel (0 = number of CPU cores)."));
    m_threads->setSpecialValueText(tr("auto"));
    m_selectAlgorithms = new QCheckBox(tr("Recursive algorithm elimination (choose the best algorithm set)"));
    m_selectAlgorithms->setChecked(true);
    qf->addRow(tr("Refinement levels"), m_levels);
    qf->addRow(tr("Grid points"), m_gridPoints);
    qf->addRow(tr("Max evaluations"), m_maxEvals);
    qf->addRow(tr("Objective"), m_objective);
    qf->addRow(tr("Features"), m_featureSet);
    qf->addRow(tr("Threads"), m_threads);
    qf->addRow(m_selectAlgorithms);
    QLabel *preNote = new QLabel(tr("Preprocessing and registration settings are taken from the main window."));
    preNote->setWordWrap(true);
    qf->addRow(preNote);
    v->addWidget(search);

    QHBoxLayout *setupRow = new QHBoxLayout;
    QPushButton *bSave = new QPushButton(tr("Save setup..."));
    QPushButton *bLoad = new QPushButton(tr("Load setup..."));
    connect(bSave, &QPushButton::clicked, this, &OptimizationWindow::saveSetup);
    connect(bLoad, &QPushButton::clicked, this, &OptimizationWindow::loadSetup);
    setupRow->addWidget(bSave);
    setupRow->addWidget(bLoad);
    v->addLayout(setupRow);
    v->addStretch(1);
    scroll->setWidget(m_setupPanel);
    scroll->setMinimumWidth(m_setupPanel->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() + 4);
    lv->addWidget(scroll, 1);

    QGroupBox *run = new QGroupBox(tr("5. Run"));
    QVBoxLayout *rv = new QVBoxLayout(run);
    QHBoxLayout *btn = new QHBoxLayout;
    m_runButton = new QPushButton(tr("RUN OPTIMIZATION"));
    m_runButton->setMinimumHeight(32);
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setEnabled(false);
    connect(m_runButton, &QPushButton::clicked, this, &OptimizationWindow::runOptimization);
    connect(m_cancelButton, &QPushButton::clicked, this, &OptimizationWindow::cancelOptimization);
    btn->addWidget(m_runButton, 1);
    btn->addWidget(m_cancelButton);
    rv->addLayout(btn);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    rv->addWidget(m_progress);
    m_status = new QLabel(tr("Idle"));
    m_status->setWordWrap(true);
    rv->addWidget(m_status);
    lv->addWidget(run);
    split->addWidget(left);

    // ======================= results =======================
    m_tabs = new QTabWidget;

    QWidget *rank = new QWidget;
    QVBoxLayout *rkv = new QVBoxLayout(rank);
    m_ranking = new QTableWidget(0, 8);
    m_ranking->setHorizontalHeaderLabels({tr("Rank"), tr("Algorithm"), tr("Score"), tr("CV accuracy (initial -> best)"),
                                          tr("Top-3 Fisher"), tr("Best feature"), tr("Evaluations"),
                                          tr("Best parameters")});
    m_ranking->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    m_ranking->verticalHeader()->setVisible(false);
    m_ranking->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ranking->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_ranking, &QTableWidget::itemSelectionChanged, this, &OptimizationWindow::onRankingSelectionChanged);
    rkv->addWidget(m_ranking, 1);
    QHBoxLayout *rkb = new QHBoxLayout;
    QPushButton *bApply = new QPushButton(tr("Apply selected algorithm + parameters to main window"));
    QPushButton *bJson = new QPushButton(tr("Export report (JSON)..."));
    QPushButton *bCsv = new QPushButton(tr("Export sample features (CSV)..."));
    connect(bApply, &QPushButton::clicked, this, &OptimizationWindow::applySelected);
    connect(bJson, &QPushButton::clicked, this, &OptimizationWindow::exportReportJson);
    connect(bCsv, &QPushButton::clicked, this, &OptimizationWindow::exportSampleCsv);
    rkb->addWidget(bApply);
    rkb->addStretch(1);
    rkb->addWidget(bJson);
    rkb->addWidget(bCsv);
    rkv->addLayout(rkb);
    m_tabs->addTab(rank, tr("Algorithm ranking"));

    QWidget *feat = new QWidget;
    QVBoxLayout *fv = new QVBoxLayout(feat);
    m_featureTitle = new QLabel(tr("Select an algorithm in the ranking"));
    fv->addWidget(m_featureTitle);
    m_features = new QTableWidget;
    m_features->verticalHeader()->setVisible(false);
    fv->addWidget(m_features, 3);
    fv->addWidget(new QLabel(tr("Cross-validation confusion matrix (rows = true category, columns = predicted)")));
    m_confusion = new QPlainTextEdit;
    m_confusion->setReadOnly(true);
    QFont mono("Monospace");
    mono.setStyleHint(QFont::TypeWriter);
    m_confusion->setFont(mono);
    fv->addWidget(m_confusion, 1);
    m_tabs->addTab(feat, tr("Feature separation"));

    QWidget *sel = new QWidget;
    QVBoxLayout *slv = new QVBoxLayout(sel);
    QLabel *selHelp = new QLabel(tr("Recursive elimination: starting from all optimized algorithms, the algorithm "
                                    "whose removal keeps the combined score highest is dropped at every step. "
                                    "The bold row is the best set."));
    selHelp->setWordWrap(true);
    slv->addWidget(selHelp);
    m_selection = new QTableWidget(0, 5);
    m_selection->setHorizontalHeaderLabels({tr("Step"), tr("Removed"), tr("Algorithm set"), tr("Score"),
                                            tr("CV accuracy")});
    m_selection->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_selection->verticalHeader()->setVisible(false);
    slv->addWidget(m_selection, 1);
    m_tabs->addTab(sel, tr("Algorithm selection"));

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(20000);
    m_tabs->addTab(m_log, tr("Log"));

    split->addWidget(m_tabs);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes(QList<int>() << 600 << 800);
}

OptimizationWindow::~OptimizationWindow()
{
    if (m_worker)
        m_worker->cancel();
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

void OptimizationWindow::closeEvent(QCloseEvent *e)
{
    // the window is only hidden; a running optimization continues in the background
    e->accept();
}

// =====================================================================================
// Sources
// =====================================================================================

void OptimizationWindow::addSource(int cls, const QString &path)
{
    for (int c = 0; c < kClasses; ++c)
        for (int i = 0; i < m_sources[c]->count(); ++i)
            if (m_sources[c]->item(i)->data(kPathRole).toString() == path) {
                QMessageBox::information(this, tr("Recording"), tr("This recording is already in category %1.")
                                                                    .arg(m_className[c]->text()));
                return;
            }
    const bool hasConfig = QFileInfo::exists(DatasetConfig::sidecarPath(path));
    QListWidgetItem *it = new QListWidgetItem(QFileInfo(path).fileName() +
                                              (hasConfig ? tr("  [ROI from dataset config]") : QString()));
    it->setData(kPathRole, path);
    it->setToolTip(path);
    m_sources[cls]->addItem(it);
}

void OptimizationWindow::addFolder(int cls)
{
    QSettings s;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Image folder for %1").arg(m_className[cls]->text()),
                                                          s.value("optLastDir", QDir::homePath()).toString());
    if (dir.isEmpty())
        return;
    QDir parent(dir);
    parent.cdUp();
    s.setValue("optLastDir", parent.absolutePath());
    addSource(cls, dir);
}

void OptimizationWindow::addVideo(int cls)
{
    QSettings s;
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Videos for %1").arg(m_className[cls]->text()), s.value("optLastDir", QDir::homePath()).toString(),
        tr("Video (*.avi *.mp4 *.mov *.mkv *.mpg *.mpeg *.wmv *.m4v);;All files (*)"));
    if (files.isEmpty())
        return;
    s.setValue("optLastDir", QFileInfo(files.front()).absolutePath());
    for (const QString &f : files)
        addSource(cls, f);
}

void OptimizationWindow::removeSource(int cls)
{
    qDeleteAll(m_sources[cls]->selectedItems());
}

// =====================================================================================
// Configuration
// =====================================================================================

OptimizationConfig OptimizationWindow::buildConfig(QString *error) const
{
    OptimizationConfig oc;
    const AnalysisConfig mainCfg = m_main->currentConfig();
    oc.preprocess = mainCfg.preprocess;
    oc.registration = mainCfg.registration;

    for (int c = 0; c < kClasses; ++c) {
        const QString name = m_className[c]->text().trimmed();
        oc.classNames.push_back((name.isEmpty() ? tr("Category %1").arg(c + 1) : name).toStdString());
        if (m_sources[c]->count() == 0 && error && error->isEmpty())
            *error = tr("Category '%1' has no recording.").arg(QString::fromStdString(oc.classNames.back()));
        for (int i = 0; i < m_sources[c]->count(); ++i) {
            const QString path = m_sources[c]->item(i)->data(kPathRole).toString();
            LabeledSource src;
            src.path = path.toStdString();
            src.label = c;
            src.roi = mainCfg.preprocess.roi;
            src.frameRate = mainCfg.dataset.frameRate;
            // ROI saved with the recording wins over the main-window ROI
            const QString sidecar = DatasetConfig::sidecarPath(path);
            if (QFileInfo::exists(sidecar)) {
                AnalysisConfig dc;
                if (DatasetConfig::load(sidecar, dc, true, nullptr)) {
                    src.roi = dc.preprocess.roi;
                    if (dc.dataset.frameRate > 0)
                        src.frameRate = dc.dataset.frameRate;
                }
            }
            oc.sources.push_back(src);
        }
    }

    oc.windowLength = m_windowLength->value();
    oc.windowStride = m_windowStride->value();
    oc.maxWindowsPerSource = m_maxWindows->value();
    oc.frameStep = m_frameStep->value();

    for (int i = 0; i < m_algorithms->count(); ++i)
        if (m_algorithms->item(i)->checkState() == Qt::Checked)
            oc.algorithmIds.push_back(m_algorithms->item(i)->data(kIdRole).toString().toStdString());
    if (oc.algorithmIds.empty() && error && error->isEmpty())
        *error = tr("Select at least one candidate algorithm.");
    if (m_startFromMain->isChecked())
        oc.initialParams = m_main->allAlgorithmParameters();

    oc.refinementLevels = m_levels->value();
    oc.gridPoints = m_gridPoints->value();
    oc.maxEvaluations = m_maxEvals->value();
    oc.objective = OptimizationConfig::Objective(m_objective->currentIndex());
    oc.useAllSummaryFeatures = m_featureSet->currentIndex() == 1;
    oc.threads = m_threads->value();
    oc.selectAlgorithms = m_selectAlgorithms->isChecked();
    return oc;
}

void OptimizationWindow::saveSetup()
{
    QSettings s;
    const QString file = QFileDialog::getSaveFileName(this, tr("Save optimization setup"),
                                                      s.value("optLastDir", QDir::homePath()).toString() +
                                                          "/optimization_setup.json",
                                                      tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    QJsonObject root;
    root["format"] = "turbulence-optimization-setup";
    QJsonArray classes;
    for (int c = 0; c < kClasses; ++c) {
        QJsonArray paths;
        for (int i = 0; i < m_sources[c]->count(); ++i)
            paths.append(m_sources[c]->item(i)->data(kPathRole).toString());
        QJsonObject o;
        o["name"] = m_className[c]->text();
        o["recordings"] = paths;
        classes.append(o);
    }
    root["classes"] = classes;
    QJsonArray algs;
    for (int i = 0; i < m_algorithms->count(); ++i)
        if (m_algorithms->item(i)->checkState() == Qt::Checked)
            algs.append(m_algorithms->item(i)->data(kIdRole).toString());
    root["algorithms"] = algs;
    root["window_length"] = m_windowLength->value();
    root["window_stride"] = m_windowStride->value();
    root["max_windows"] = m_maxWindows->value();
    root["frame_step"] = m_frameStep->value();
    root["refinement_levels"] = m_levels->value();
    root["grid_points"] = m_gridPoints->value();
    root["max_evaluations"] = m_maxEvals->value();
    root["objective"] = m_objective->currentIndex();
    root["feature_set"] = m_featureSet->currentIndex();
    root["threads"] = m_threads->value();
    root["select_algorithms"] = m_selectAlgorithms->isChecked();
    root["start_from_main"] = m_startFromMain->isChecked();

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Save setup"), f.errorString());
        return;
    }
    f.write(QJsonDocument(root).toJson());
    log(tr("Setup saved to %1").arg(file));
}

void OptimizationWindow::loadSetup()
{
    QSettings s;
    const QString file = QFileDialog::getOpenFileName(this, tr("Load optimization setup"),
                                                      s.value("optLastDir", QDir::homePath()).toString(),
                                                      tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Load setup"), f.errorString());
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray classes = root["classes"].toArray();
    for (int c = 0; c < kClasses; ++c) {
        m_sources[c]->clear();
        if (c >= classes.size())
            continue;
        const QJsonObject o = classes[c].toObject();
        m_className[c]->setText(o["name"].toString(m_className[c]->text()));
        for (const QJsonValue &p : o["recordings"].toArray())
            addSource(c, p.toString());
    }
    if (root.contains("algorithms")) {
        QStringList ids;
        for (const QJsonValue &a : root["algorithms"].toArray())
            ids << a.toString();
        for (int i = 0; i < m_algorithms->count(); ++i)
            m_algorithms->item(i)->setCheckState(ids.contains(m_algorithms->item(i)->data(kIdRole).toString())
                                                     ? Qt::Checked
                                                     : Qt::Unchecked);
    }
    m_windowLength->setValue(root["window_length"].toInt(m_windowLength->value()));
    m_windowStride->setValue(root["window_stride"].toInt(m_windowStride->value()));
    m_maxWindows->setValue(root["max_windows"].toInt(m_maxWindows->value()));
    m_frameStep->setValue(root["frame_step"].toInt(m_frameStep->value()));
    m_levels->setValue(root["refinement_levels"].toInt(m_levels->value()));
    m_gridPoints->setValue(root["grid_points"].toInt(m_gridPoints->value()));
    m_maxEvals->setValue(root["max_evaluations"].toInt(m_maxEvals->value()));
    m_objective->setCurrentIndex(root["objective"].toInt(m_objective->currentIndex()));
    m_featureSet->setCurrentIndex(root["feature_set"].toInt(m_featureSet->currentIndex()));
    m_threads->setValue(root["threads"].toInt(m_threads->value()));
    m_selectAlgorithms->setChecked(root["select_algorithms"].toBool(m_selectAlgorithms->isChecked()));
    m_startFromMain->setChecked(root["start_from_main"].toBool(m_startFromMain->isChecked()));
    log(tr("Setup loaded from %1").arg(file));
}

// =====================================================================================
// Running
// =====================================================================================

void OptimizationWindow::setRunning(bool running)
{
    m_setupPanel->setEnabled(!running);
    m_runButton->setEnabled(!running);
    m_cancelButton->setEnabled(running);
}

void OptimizationWindow::runOptimization()
{
    if (m_thread)
        return;
    QString err;
    const OptimizationConfig oc = buildConfig(&err);
    if (!err.isEmpty()) {
        QMessageBox::information(this, tr("Optimization"), err);
        return;
    }

    m_reportClassNames = oc.classNames;
    m_reportSources.clear();
    for (const LabeledSource &s : oc.sources)
        m_reportSources.push_back(s.path);

    m_thread = new QThread(this);
    m_worker = new OptimizationWorker(oc);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &OptimizationWorker::run);
    connect(m_worker, &OptimizationWorker::progress, this, &OptimizationWindow::onProgress);
    connect(m_worker, &OptimizationWorker::logLine, this, &OptimizationWindow::onLog);
    connect(m_worker, &OptimizationWorker::reportReady, this, &OptimizationWindow::onReport);
    connect(m_worker, &OptimizationWorker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, this, &OptimizationWindow::onWorkerFinished);

    m_log->clear();
    log(tr("Optimization started: %1 recording(s), %2 candidate algorithm(s).")
            .arg(oc.sources.size())
            .arg(oc.algorithmIds.size()));
    log(tr("Preprocessing: %1; registration: %2")
            .arg(QString::fromStdString(oc.preprocess.describe()), QString::fromStdString(oc.registration.describe())));
    m_progress->setValue(0);
    m_status->setText(tr("Preparing samples..."));
    m_tabs->setCurrentWidget(m_log);
    setRunning(true);
    m_thread->start();
}

void OptimizationWindow::cancelOptimization()
{
    if (m_worker) {
        m_worker->cancel();
        m_status->setText(tr("Cancelling..."));
    }
}

void OptimizationWindow::onProgress(const QString &stage, int done, int total, int evaluations, int maxEvaluations)
{
    m_progress->setValue(total > 0 ? 100 * done / total : 0);
    m_status->setText(tr("%1\nwindows %2/%3, evaluated parameter sets %4/%5")
                          .arg(stage)
                          .arg(done)
                          .arg(total)
                          .arg(evaluations)
                          .arg(maxEvaluations));
}

void OptimizationWindow::onLog(const QString &line)
{
    log(line);
}

void OptimizationWindow::onWorkerFinished()
{
    m_worker->deleteLater();
    m_thread->deleteLater();
    m_worker = nullptr;
    m_thread = nullptr;
    setRunning(false);
}

void OptimizationWindow::log(const QString &line)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + line);
}

// =====================================================================================
// Results
// =====================================================================================

void OptimizationWindow::onReport(const OptimizationReport &rep)
{
    if (!rep.ok) {
        m_status->setText(rep.cancelled ? tr("Cancelled") : tr("Error: %1").arg(QString::fromStdString(rep.error)));
        log(tr("Optimization stopped: %1").arg(QString::fromStdString(rep.error)));
        if (!rep.cancelled)
            QMessageBox::warning(this, tr("Optimization"), QString::fromStdString(rep.error));
        return;
    }
    m_report = rep;
    m_progress->setValue(100);
    m_status->setText(tr("Done: %1 parameter sets evaluated in %2 s")
                          .arg(rep.evaluations)
                          .arg(rep.elapsedSeconds, 0, 'f', 1));
    log(tr("Validation: %1").arg(QString::fromStdString(rep.validation)));
    for (const std::string &n : rep.notes)
        log(tr("Note: %1").arg(QString::fromStdString(n)));

    m_ranking->setRowCount(0);
    for (size_t i = 0; i < rep.algorithms.size(); ++i) {
        const AlgorithmOutcome &o = rep.algorithms[i];
        const int row = m_ranking->rowCount();
        m_ranking->insertRow(row);
        m_ranking->setItem(row, 0, roItem(QString::number(i + 1)));
        m_ranking->setItem(row, 1, roItem(QString::fromStdString(o.name)));
        m_ranking->setItem(row, 2, roItem(fmt(o.bestScore)));
        m_ranking->setItem(row, 3, roItem(tr("%1 -> %2").arg(fmt(o.initialAccuracy, 3), fmt(o.bestAccuracy, 3))));
        m_ranking->setItem(row, 4, roItem(fmt(o.bestFisherTop3)));
        m_ranking->setItem(row, 5, roItem(o.features.empty()
                                              ? QString()
                                              : QString::fromStdString(o.features.front().key) + " (" +
                                                    fmt(o.features.front().fisher) + ")"));
        m_ranking->setItem(row, 6, roItem(QString::number(o.evaluations)));
        QTableWidgetItem *p = roItem(QString::fromStdString(o.bestParamText));
        p->setToolTip(QString::fromStdString(o.bestParamText));
        m_ranking->setItem(row, 7, p);
    }
    m_ranking->resizeColumnsToContents();
    m_ranking->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);

    m_selection->setRowCount(0);
    for (size_t i = 0; i < rep.selection.size(); ++i) {
        const SelectionStep &s = rep.selection[i];
        QStringList names;
        for (const std::string &n : s.algorithms)
            names << QString::fromStdString(n);
        const int row = m_selection->rowCount();
        m_selection->insertRow(row);
        m_selection->setItem(row, 0, roItem(QString::number(i)));
        m_selection->setItem(row, 1, roItem(QString::fromStdString(s.removed)));
        m_selection->setItem(row, 2, roItem(names.join(", ")));
        m_selection->item(row, 2)->setToolTip(names.join("\n"));
        m_selection->setItem(row, 3, roItem(fmt(s.score)));
        m_selection->setItem(row, 4, roItem(fmt(s.accuracy, 3)));
        if (int(i) == rep.bestSelectionStep)
            for (int c = 0; c < 5; ++c) {
                QFont f = m_selection->item(row, c)->font();
                f.setBold(true);
                m_selection->item(row, c)->setFont(f);
            }
    }
    m_selection->resizeColumnsToContents();
    m_selection->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);

    if (m_ranking->rowCount() > 0)
        m_ranking->selectRow(0);
    m_tabs->setCurrentIndex(0);
}

void OptimizationWindow::onRankingSelectionChanged()
{
    const QList<QTableWidgetItem *> sel = m_ranking->selectedItems();
    if (!sel.isEmpty())
        showFeatures(sel.front()->row());
}

void OptimizationWindow::showFeatures(int idx)
{
    if (idx < 0 || idx >= int(m_report.algorithms.size()))
        return;
    const AlgorithmOutcome &o = m_report.algorithms[size_t(idx)];
    const int k = int(m_reportClassNames.size());
    m_featureTitle->setText(tr("%1 - best parameters: %2")
                                .arg(QString::fromStdString(o.name), QString::fromStdString(o.bestParamText)));
    m_features->clear();
    m_features->setColumnCount(2 + k);
    QStringList headers;
    headers << tr("Feature") << tr("Fisher ratio");
    for (const std::string &c : m_reportClassNames)
        headers << tr("%1 mean +- std").arg(QString::fromStdString(c));
    m_features->setHorizontalHeaderLabels(headers);
    m_features->setRowCount(int(o.features.size()));
    for (int r = 0; r < int(o.features.size()); ++r) {
        const FeatureSeparation &f = o.features[size_t(r)];
        m_features->setItem(r, 0, roItem(QString::fromStdString(f.key)));
        m_features->setItem(r, 1, roItem(fmt(f.fisher)));
        for (int c = 0; c < k && c < int(f.classMean.size()); ++c)
            m_features->setItem(r, 2 + c,
                                roItem(fmt(f.classMean[size_t(c)], 5) + " +- " + fmt(f.classStd[size_t(c)], 3)));
    }
    m_features->resizeColumnsToContents();

    QString text;
    QTextStream ts(&text);
    int w = 10;
    for (const std::string &c : m_reportClassNames)
        w = std::max(w, int(c.size()) + 2);
    ts << QString(w, ' ');
    for (const std::string &c : m_reportClassNames)
        ts << QString::fromStdString(c).rightJustified(w);
    ts << "\n";
    for (int t = 0; t < k && t < int(o.confusion.size()); ++t) {
        ts << QString::fromStdString(m_reportClassNames[size_t(t)]).leftJustified(w);
        for (int p = 0; p < k && p < int(o.confusion[size_t(t)].size()); ++p)
            ts << QString::number(o.confusion[size_t(t)][size_t(p)]).rightJustified(w);
        ts << "\n";
    }
    ts << "\n" << tr("Balanced accuracy: %1").arg(fmt(o.bestAccuracy, 3)) << "\n";
    ts << tr("Search history:") << "\n";
    for (const std::string &line : o.trace)
        ts << "  " << QString::fromStdString(line) << "\n";
    m_confusion->setPlainText(text);
}

void OptimizationWindow::applySelected()
{
    const QList<QTableWidgetItem *> sel = m_ranking->selectedItems();
    if (sel.isEmpty() || m_report.algorithms.empty())
        return;
    const AlgorithmOutcome &o = m_report.algorithms[size_t(sel.front()->row())];
    m_main->applyAlgorithmParameters(o.id, o.bestParams);
    log(tr("Applied to main window: %1 (%2)").arg(QString::fromStdString(o.id), QString::fromStdString(o.bestParamText)));
}

void OptimizationWindow::exportReportJson()
{
    if (!m_report.ok)
        return;
    QSettings s;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export optimization report"),
                                                      s.value("lastExportDir", QDir::homePath()).toString() +
                                                          "/optimization_report.json",
                                                      tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    QJsonObject root;
    root["format"] = "turbulence-optimization-report";
    root["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    QJsonArray cls;
    for (const std::string &c : m_reportClassNames)
        cls.append(QString::fromStdString(c));
    root["classes"] = cls;
    root["validation"] = QString::fromStdString(m_report.validation);
    QJsonArray notes;
    for (const std::string &n : m_report.notes)
        notes.append(QString::fromStdString(n));
    root["notes"] = notes;
    root["evaluations"] = m_report.evaluations;
    root["elapsed_seconds"] = m_report.elapsedSeconds;

    QJsonArray algs;
    for (const AlgorithmOutcome &o : m_report.algorithms) {
        QJsonObject a;
        a["id"] = QString::fromStdString(o.id);
        a["name"] = QString::fromStdString(o.name);
        a["score"] = jnum(o.bestScore);
        a["cv_accuracy"] = jnum(o.bestAccuracy);
        a["initial_score"] = jnum(o.initialScore);
        a["initial_cv_accuracy"] = jnum(o.initialAccuracy);
        a["fisher_top3"] = jnum(o.bestFisherTop3);
        a["evaluations"] = o.evaluations;
        a["best_parameters"] = paramsToJson(o.bestParams);
        a["initial_parameters"] = paramsToJson(o.initialParams);
        a["best_parameters_text"] = QString::fromStdString(o.bestParamText);
        QJsonArray feats;
        for (const FeatureSeparation &f : o.features) {
            QJsonObject fo;
            fo["key"] = QString::fromStdString(f.key);
            fo["fisher"] = jnum(f.fisher);
            QJsonArray means, stds;
            for (double m : f.classMean)
                means.append(jnum(m));
            for (double sd : f.classStd)
                stds.append(jnum(sd));
            fo["class_mean"] = means;
            fo["class_std"] = stds;
            feats.append(fo);
        }
        a["features"] = feats;
        QJsonArray conf;
        for (const std::vector<int> &row : o.confusion) {
            QJsonArray r;
            for (int x : row)
                r.append(x);
            conf.append(r);
        }
        a["confusion"] = conf;
        algs.append(a);
    }
    root["algorithms"] = algs;

    QJsonArray selection;
    for (size_t i = 0; i < m_report.selection.size(); ++i) {
        const SelectionStep &st = m_report.selection[i];
        QJsonObject so;
        QJsonArray names;
        for (const std::string &n : st.algorithms)
            names.append(QString::fromStdString(n));
        so["algorithms"] = names;
        so["removed"] = QString::fromStdString(st.removed);
        so["score"] = jnum(st.score);
        so["cv_accuracy"] = jnum(st.accuracy);
        so["best"] = int(i) == m_report.bestSelectionStep;
        selection.append(so);
    }
    root["algorithm_selection"] = selection;

    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export"), f.errorString());
        return;
    }
    f.write(QJsonDocument(root).toJson());
    s.setValue("lastExportDir", QFileInfo(file).absolutePath());
    log(tr("Report written: %1").arg(file));
}

void OptimizationWindow::exportSampleCsv()
{
    if (!m_report.ok)
        return;
    QSettings s;
    const QString file = QFileDialog::getSaveFileName(this, tr("Export sample features"),
                                                      s.value("lastExportDir", QDir::homePath()).toString() +
                                                          "/optimization_samples.csv",
                                                      tr("CSV (*.csv)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export"), f.errorString());
        return;
    }
    // one row per labeled window; features of every algorithm at its best parameters
    QTextStream ts(&f);
    ts << "sample,label,class,recording,start_frame,end_frame";
    for (const AlgorithmOutcome &o : m_report.algorithms)
        for (const std::string &k : o.featureKeys)
            ts << "," << QString::fromStdString(o.id + "." + k);
    ts << "\n";
    for (size_t i = 0; i < m_report.samples.size(); ++i) {
        const SampleWindow &w = m_report.samples[i];
        QString rec = w.source < int(m_reportSources.size()) ? QString::fromStdString(m_reportSources[size_t(w.source)])
                                                             : QString();
        rec.replace('"', "\"\"");
        ts << i << "," << w.label << ","
           << (w.label < int(m_reportClassNames.size()) ? QString::fromStdString(m_reportClassNames[size_t(w.label)])
                                                        : QString())
           << ",\"" << rec << "\"," << w.start << "," << w.end;
        for (const AlgorithmOutcome &o : m_report.algorithms)
            for (size_t c = 0; c < o.featureKeys.size(); ++c) {
                const double v = i < o.sampleFeatures.size() && c < o.sampleFeatures[i].size()
                                     ? o.sampleFeatures[i][c]
                                     : std::nan("");
                ts << "," << (std::isfinite(v) ? QString::number(v, 'g', 10) : QStringLiteral("NaN"));
            }
        ts << "\n";
    }
    s.setValue("lastExportDir", QFileInfo(file).absolutePath());
    log(tr("Sample feature CSV written: %1").arg(file));
}
