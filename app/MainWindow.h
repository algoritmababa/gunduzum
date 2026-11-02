#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ResultTable.h"

#include <QMainWindow>

class FramePlayer;
class MeasureWorker;
class RoiView;
class TrendView;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;

// Measurement tool: open a recording, draw the ROI, give it a category, RUN.
// Every run appends one row per window to the shared results CSV.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(class QCloseEvent *e) override;

private slots:
    void browseFolder();
    void browseVideo();
    void chooseResultsFile();
    void onRoiSelected(const QRect &roi);
    void setFullFrame();
    void run();
    void cancel();
    void onProgress(int done, int total);
    void onDone(bool ok, const QString &message);
    void reloadResults();
    void applyRoiToOthers();
    void showFrame(const QImage &image, int frameIndex);
    void chooseDatasetFolder();
    void refreshDatasets();
    void onDatasetClicked(QTreeWidgetItem *item);
    void runAll();

private:
    bool openRecording(const QString &path, bool quiet = false);
    void startNextInQueue();
    void finishQueue(const QString &why);
    turb::Table loadResults() const;
    void updateRoiLabel();
    void setRunning(bool running);
    void log(const QString &msg);
    void rememberRecording(const QString &path);
    QStringList knownRecordings() const;
    QStringList categoryOrder() const;
    turb::Preprocess preprocess() const;
    // Labels are stored per dataset (once): loaded when it is opened, saved when changed.
    void loadLabel();
    void commitLabel();
    void updateLabelState(const QString &origin = QString());
    void preprocessChanged();

    // inputs
    QLabel *m_folderLabel;
    QTreeWidget *m_datasets;   // datasets of the dataset folder
    QLabel *m_datasetSummary;
    QString m_datasetFolder;
    QLineEdit *m_pathEdit;
    QLabel *m_info;
    QComboBox *m_category;
    QLineEdit *m_note;
    QLabel *m_labelState;
    bool m_loadingLabel = false;
    QLabel *m_roiLabel;
    QSpinBox *m_window;
    QLineEdit *m_resultsEdit;
    QCheckBox *m_frameCsv;
    QCheckBox *m_showFrames;
    QCheckBox *m_clahe;
    QDoubleSpinBox *m_clipLimit;
    QSpinBox *m_tiles;
    QCheckBox *m_previewPre;
    QPushButton *m_applyRoi;
    QWidget *m_inputs;
    QPushButton *m_runButton, *m_runAllButton, *m_cancelButton;
    QCheckBox *m_skipMeasured;
    QCheckBox *m_replaceRows;
    // RUN ALL: datasets still to measure, counters of the batch
    QStringList m_queue;
    bool m_batch = false;
    int m_batchTotal = 0, m_batchDone = 0;
    QStringList m_batchFailed;
    QProgressBar *m_progress;
    QLabel *m_status;
    // outputs
    QTabWidget *m_tabs;
    RoiView *m_view;
    FramePlayer *m_player;
    QTableWidget *m_results;
    TrendView *m_trendView;
    QPlainTextEdit *m_log;

    QString m_path;
    QRect m_roi; // empty = full frame
    QThread *m_thread = nullptr;
    MeasureWorker *m_worker = nullptr;
};

#endif
