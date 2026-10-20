#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "AnalysisEngine.h"
#include "ResultExporter.h"

#include <QMainWindow>

#include <map>
#include <string>
#include <vector>

class AnalysisWorker;
class ParameterEditor;
class RoiPreviewWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QThread;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

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
    void openDatasetFromEdit();
    void saveDatasetConfig();
    void loadDatasetConfig();
    void setFullFrameRoi();
    void onRoiSelected(const QRect &roi);
    void onRoiSpinChanged();
    void updatePreview();
    void onAlgorithmItemChanged(QTreeWidgetItem *item, int column);
    void resetParameters();
    void runAnalysis();
    void cancelAnalysis();
    void onFrameProgress(int processed, int total, int frameIndex);
    void onResult(const turb::AnalysisOutput &out);
    void onWorkerFinished();
    void exportFrameCsv();
    void exportSummaryCsv();
    void exportJson();
    void copyFeatureVector();
    void exportComparisonCsv();
    void exportFeatureMatrix();
    void removeComparisonColumn();
    void clearComparison();
    void renameComparisonColumn(int column);

private:
    // When false (first phase), checking an algorithm unchecks the others.
    static const bool kAllowMultipleAlgorithms = false;

    void buildUi();
    QWidget *buildConfigPanel();
    QWidget *buildResultPanel();
    void populateAlgorithmTree();

    bool openDataset(const QString &path, bool loadSidecar);
    turb::AnalysisConfig currentConfig() const;
    void applyConfig(const turb::AnalysisConfig &c);
    std::vector<std::string> checkedAlgorithmIds() const;
    void showParametersFor(const std::string &id);
    void storeCurrentParameters();
    void setRunning(bool running);

    void showResults(const std::vector<turb::AlgorithmResult> &results);
    void addToComparison(const std::vector<turb::AlgorithmResult> &results);
    void rebuildComparisonTable();
    QString exportBaseName(const turb::AlgorithmResult &r) const;
    void log(const QString &msg);

    // --- dataset ---
    QLineEdit *m_datasetEdit;
    QLabel *m_datasetInfo;
    QDoubleSpinBox *m_fpsSpin;
    QSpinBox *m_startSpin, *m_endSpin, *m_stepSpin;
    // --- ROI ---
    QSpinBox *m_roiX, *m_roiY, *m_roiW, *m_roiH;
    QSpinBox *m_previewFrameSpin;
    QCheckBox *m_previewProcessed;
    // --- preprocessing ---
    QComboBox *m_denoiseCombo, *m_contrastCombo, *m_normCombo;
    QSpinBox *m_denoiseKernel, *m_claheTiles;
    QDoubleSpinBox *m_denoiseSigma, *m_claheClip;
    // --- registration ---
    QComboBox *m_regModeCombo, *m_regRefCombo;
    QSpinBox *m_regIndexSpin;
    // --- algorithm ---
    QTreeWidget *m_algTree;
    QLabel *m_algDescription;
    ParameterEditor *m_paramEditor;
    // --- run ---
    QPushButton *m_runButton, *m_cancelButton;
    QProgressBar *m_progress;
    QLabel *m_statusLabel, *m_frameLabel;
    QWidget *m_configPanel;
    QWidget *m_runPanel;
    // --- results ---
    QTabWidget *m_tabs;
    RoiPreviewWidget *m_preview;
    QLabel *m_resultTitle;
    QTableWidget *m_summaryTable;
    QLineEdit *m_featureVectorEdit;
    QTableWidget *m_metaTable;
    QPlainTextEdit *m_notesEdit;
    QTableWidget *m_compTable;
    QCheckBox *m_autoCompare;
    QPlainTextEdit *m_log;

    // --- state ---
    QString m_openedDataset;
    int m_frameCount = 0;
    QSize m_frameSize;
    std::string m_currentParamAlg; // algorithm whose parameters are shown in the editor
    std::map<std::string, turb::ParamSet> m_paramsById;
    std::vector<turb::AlgorithmResult> m_lastResults;
    std::vector<turb::LabeledResult> m_comparison;
    int m_runCounter = 0;
    bool m_updatingUi = false;

    QThread *m_thread = nullptr;
    AnalysisWorker *m_worker = nullptr;
};

#endif
