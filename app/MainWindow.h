#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "Measurement.h"

#include <QMainWindow>

class MeasureWorker;
class RoiView;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QThread;

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

private:
    bool openRecording(const QString &path);
    void updateRoiLabel();
    void setRunning(bool running);
    void log(const QString &msg);
    QStringList categoryOrder() const;

    // inputs
    QLineEdit *m_pathEdit;
    QLabel *m_info;
    QComboBox *m_category;
    QLineEdit *m_note;
    QLabel *m_roiLabel;
    QSpinBox *m_window;
    QLineEdit *m_resultsEdit;
    QCheckBox *m_frameCsv;
    QWidget *m_inputs;
    QPushButton *m_runButton, *m_cancelButton;
    QProgressBar *m_progress;
    QLabel *m_status;
    // outputs
    QTabWidget *m_tabs;
    RoiView *m_view;
    QTableWidget *m_results;
    QTableWidget *m_summary;
    QPlainTextEdit *m_log;

    QString m_path;
    QRect m_roi; // empty = full frame
    QThread *m_thread = nullptr;
    MeasureWorker *m_worker = nullptr;
};

#endif
