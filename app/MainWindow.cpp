#include "MainWindow.h"

#include "FrameSource.h"
#include "MeasureWorker.h"
#include "Metrics.h"
#include "ResultTable.h"
#include "RoiView.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
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

// Raw frame -> 8-bit RGB QImage for display (min/max stretch for > 8-bit data).
QImage toQImage(const cv::Mat &m)
{
    cv::Mat u8, rgb;
    if (m.depth() == CV_8U)
        u8 = m;
    else
        cv::normalize(m, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
    if (u8.channels() == 1)
        cv::cvtColor(u8, rgb, cv::COLOR_GRAY2RGB);
    else if (u8.channels() == 4)
        cv::cvtColor(u8, rgb, cv::COLOR_BGRA2RGB);
    else
        cv::cvtColor(u8, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
}

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
    QHBoxLayout *roiRow = new QHBoxLayout(roiBox);
    m_roiLabel = new QLabel;
    QPushButton *bFull = new QPushButton(tr("Full frame"));
    connect(bFull, &QPushButton::clicked, this, &MainWindow::setFullFrame);
    roiRow->addWidget(m_roiLabel, 1);
    roiRow->addWidget(bFull);
    iv->addWidget(roiBox);

    QGroupBox *outBox = new QGroupBox(tr("4. Measurement"));
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
    connect(m_resultsEdit, &QLineEdit::editingFinished, this, &MainWindow::reloadResults);
    resRow->addWidget(m_resultsEdit, 1);
    resRow->addWidget(bRes);
    of->addRow(tr("Results file"), resRow);
    m_frameCsv = new QCheckBox(tr("Also save per-frame values"));
    of->addRow(m_frameCsv);
    iv->addWidget(outBox);

    QGroupBox *metricsBox = new QGroupBox(tr("Measured per frame"));
    QVBoxLayout *mv = new QVBoxLayout(metricsBox);
    for (const std::unique_ptr<turb::Metric> &m : turb::createMetrics()) {
        QLabel *l = new QLabel(QString("<b>%1</b>: %2")
                                   .arg(QString::fromStdString(m->name()), QString::fromStdString(m->description())));
        l->setWordWrap(true);
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
    m_view = new RoiView;
    connect(m_view, &RoiView::roiSelected, this, &MainWindow::onRoiSelected);
    m_tabs->addTab(m_view, tr("Image / ROI"));

    m_results = new QTableWidget;
    m_results->verticalHeader()->setVisible(false);
    m_tabs->addTab(m_results, tr("Results"));

    QWidget *sumPage = new QWidget;
    QVBoxLayout *sv = new QVBoxLayout(sumPage);
    QLabel *sumHelp = new QLabel(
        tr("Mean +- std (number of windows) per category, from the results file. "
           "<b>Trend</b>: do the means rise or fall along the category order? "
           "<b>Separation</b>: clear = neighbouring categories differ by more than their spreads."));
    sumHelp->setWordWrap(true);
    sv->addWidget(sumHelp);
    m_summary = new QTableWidget;
    m_summary->verticalHeader()->setVisible(false);
    sv->addWidget(m_summary, 1);
    QPushButton *bReload = new QPushButton(tr("Reload results file"));
    connect(bReload, &QPushButton::clicked, this, &MainWindow::reloadResults);
    sv->addWidget(bReload, 0, Qt::AlignRight);
    m_tabs->addTab(sumPage, tr("Category summary"));

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
    m_path = path;
    m_pathEdit->setText(path);
    m_info->setText(QString::fromStdString(src->description()));
    m_view->setImage(toQImage(first));
    // the ROI drawn last time for this recording is remembered
    m_roi = QSettings().value("roi/" + path).toRect();
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
}

void MainWindow::setFullFrame()
{
    m_roi = QRect();
    if (!m_path.isEmpty())
        QSettings().remove("roi/" + m_path);
    m_view->setRoi(m_roi);
    updateRoiLabel();
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

void MainWindow::chooseResultsFile()
{
    const QString f = QFileDialog::getSaveFileName(this, tr("Results file (rows are appended)"), m_resultsEdit->text(),
                                                   tr("CSV (*.csv)"), nullptr, QFileDialog::DontConfirmOverwrite);
    if (f.isEmpty())
        return;
    m_resultsEdit->setText(f);
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
    if (m_frameCsv->isChecked()) {
        const QFileInfo rf(results);
        settings.frameCsvPath = (rf.absolutePath() + "/" + rf.completeBaseName() + "_" +
                                 QString::fromStdString(turb::recordingId(rec.path)) + "_" +
                                 QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + "_frames.csv")
                                    .toStdString();
    }
    QSettings().setValue("resultsFile", results);
    QSettings().setValue("windowLength", m_window->value());

    m_thread = new QThread(this);
    m_worker = new MeasureWorker(rec, settings, results);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::started, m_worker, &MeasureWorker::run);
    connect(m_worker, &MeasureWorker::progress, this, &MainWindow::onProgress);
    connect(m_worker, &MeasureWorker::done, this, &MainWindow::onDone);
    connect(m_worker, &MeasureWorker::done, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);

    log(tr("Measuring %1  [category: %2]").arg(m_path, rec.category.empty() ? tr("unlabeled")
                                                                              : QString::fromStdString(rec.category)));
    if (!settings.frameCsvPath.empty())
        log(tr("Per-frame values -> %1").arg(QString::fromStdString(settings.frameCsvPath)));
    m_progress->setValue(0);
    m_status->setText(tr("Running..."));
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

    // category summary
    std::vector<std::string> preferred;
    for (const QString &c : categoryOrder())
        preferred.push_back(c.toStdString());
    const std::vector<std::string> cats = turb::categoriesIn(table, preferred);
    const std::vector<turb::CategoryStats> stats = turb::categorySummary(table, cats);
    m_summary->clear();
    m_summary->setColumnCount(int(cats.size()) + 3);
    m_summary->setRowCount(int(stats.size()));
    QStringList sh;
    sh << tr("Feature");
    for (const std::string &c : cats)
        sh << QString::fromStdString(c);
    sh << tr("Trend") << tr("Separation");
    m_summary->setHorizontalHeaderLabels(sh);
    for (int r = 0; r < int(stats.size()); ++r) {
        const turb::CategoryStats &st = stats[size_t(r)];
        m_summary->setItem(r, 0, cell(QString::fromStdString(st.feature)));
        for (int c = 0; c < int(cats.size()); ++c)
            m_summary->setItem(r, c + 1,
                               cell(st.count[size_t(c)] > 0
                                        ? QString("%1 +- %2  (%3)")
                                              .arg(num(st.mean[size_t(c)]), num(st.std[size_t(c)]))
                                              .arg(st.count[size_t(c)])
                                        : QString("-")));
        const QString trend = st.trend == "increasing" ? QString::fromUtf8("\xe2\x86\x91 increasing")
                              : st.trend == "decreasing" ? QString::fromUtf8("\xe2\x86\x93 decreasing")
                                                         : QString::fromStdString(st.trend);
        m_summary->setItem(r, int(cats.size()) + 1, cell(trend));
        m_summary->setItem(r, int(cats.size()) + 2, cell(QString::fromStdString(st.separation)));
    }
    m_summary->resizeColumnsToContents();
}
