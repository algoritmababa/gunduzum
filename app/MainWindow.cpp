#include "MainWindow.h"

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
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
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

    QGroupBox *recBox = new QGroupBox(tr("1. Recording"));
    QVBoxLayout *rv = new QVBoxLayout(recBox);
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

    QGroupBox *labelBox = new QGroupBox(tr("2. Label"));
    QFormLayout *lf = new QFormLayout(labelBox);
    m_category = new QComboBox;
    m_category->setEditable(true);
    m_category->addItems(QStringList() << "LOW" << "MEDIUM" << "HIGH");
    m_category->setCurrentIndex(-1);
    m_category->setToolTip(tr("Turbulence category of this recording (empty = unlabeled). "
                              "The order of the list is used as LOW -> HIGH in the summary."));
    m_note = new QLineEdit;
    m_note->setPlaceholderText(tr("time, weather, exposure, distance ..."));
    lf->addRow(tr("Category"), m_category);
    lf->addRow(tr("Note"), m_note);
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
    lv->addWidget(m_inputs, 1);
    QHBoxLayout *runRow = new QHBoxLayout;
    m_runButton = new QPushButton(tr("RUN"));
    m_runButton->setMinimumHeight(34);
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setEnabled(false);
    connect(m_runButton, &QPushButton::clicked, this, &MainWindow::run);
    connect(m_cancelButton, &QPushButton::clicked, this, &MainWindow::cancel);
    runRow->addWidget(m_runButton, 1);
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
    split->setSizes(QList<int>() << 430 << 870);
    setCentralWidget(split);

    updateRoiLabel();
    preprocessChanged();
    reloadResults();
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

bool MainWindow::openRecording(const QString &path)
{
    if (path.isEmpty())
        return false;
    std::string err;
    std::unique_ptr<turb::FrameSource> src = turb::FrameSource::create(path.toStdString(), &err);
    cv::Mat first;
    if (!src || !src->readAt(0, first)) {
        QMessageBox::warning(this, tr("Recording"), err.empty() ? tr("Cannot read the first frame.")
                                                                : QString::fromStdString(err));
        return false;
    }
    m_path = QDir::cleanPath(path);
    m_pathEdit->setText(m_path);
    m_info->setText(QString::fromStdString(src->description()));
    m_view->setImage(toQImage(first));
    m_player->setSource(m_path);
    m_applyRoi->setEnabled(true);
    rememberRecording(m_path);
    // the ROI drawn last time for this recording (or applied from another one) is remembered
    m_roi = QSettings().value("roi/" + m_path).toRect();
    m_view->setRoi(m_roi);
    updateRoiLabel();
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

void MainWindow::cancel()
{
    if (m_worker)
        m_worker->cancel();
}

void MainWindow::onProgress(int done, int total)
{
    m_progress->setValue(total > 0 ? int(100LL * done / total) : 0);
    m_status->setText(tr("frame %1 / %2").arg(done).arg(total));
}

void MainWindow::onDone(bool ok, const QString &message)
{
    m_thread = nullptr; // deleted via deleteLater when the thread finishes
    m_worker = nullptr;
    setRunning(false);
    m_status->setText(message);
    log(message);
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
}
