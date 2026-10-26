#ifndef OPTIMIZATIONWINDOW_H
#define OPTIMIZATIONWINDOW_H

#include "optimization/ParameterOptimizer.h"

#include <QWidget>

class MainWindow;
class OptimizationWorker;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QThread;

// Window for optimizing algorithm selection and parameters on recordings labeled with
// three categories. The search itself runs in ParameterOptimizer on a worker thread.
class OptimizationWindow : public QWidget
{
    Q_OBJECT

public:
    explicit OptimizationWindow(MainWindow *mainWindow);
    ~OptimizationWindow() override;

protected:
    void closeEvent(class QCloseEvent *e) override;

private slots:
    void addFolder(int cls);
    void addVideo(int cls);
    void removeSource(int cls);
    void saveSetup();
    void loadSetup();
    void runOptimization();
    void cancelOptimization();
    void onProgress(const QString &stage, int done, int total, int evaluations, int maxEvaluations);
    void onLog(const QString &line);
    void onReport(const turb::OptimizationReport &report);
    void onWorkerFinished();
    void onRankingSelectionChanged();
    void applySelected();
    void exportReportJson();
    void exportSampleCsv();

private:
    static const int kClasses = 3;

    void addSource(int cls, const QString &path);
    turb::OptimizationConfig buildConfig(QString *error) const;
    void setRunning(bool running);
    void showFeatures(int outcomeIndex);
    void log(const QString &line);

    MainWindow *m_main;

    QLineEdit *m_className[kClasses];
    QListWidget *m_sources[kClasses];
    QSpinBox *m_windowLength, *m_windowStride, *m_maxWindows, *m_frameStep;
    QListWidget *m_algorithms;
    QCheckBox *m_startFromMain;
    QSpinBox *m_levels, *m_gridPoints, *m_maxEvals, *m_threads;
    QComboBox *m_objective, *m_featureSet;
    QCheckBox *m_selectAlgorithms;
    QWidget *m_setupPanel;
    QPushButton *m_runButton, *m_cancelButton;
    QProgressBar *m_progress;
    QLabel *m_status;

    QTabWidget *m_tabs;
    QTableWidget *m_ranking, *m_features, *m_selection;
    QLabel *m_featureTitle;
    QPlainTextEdit *m_confusion;
    QPlainTextEdit *m_log;

    turb::OptimizationReport m_report;
    std::vector<std::string> m_reportClassNames;
    std::vector<std::string> m_reportSources;
    QThread *m_thread = nullptr;
    OptimizationWorker *m_worker = nullptr;
};

#endif
