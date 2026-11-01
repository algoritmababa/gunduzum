#ifndef TRENDVIEW_H
#define TRENDVIEW_H

#include "TrendAnalysis.h"

#include <QStringList>
#include <QWidget>

#include <functional>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

// Trend analysis page: runs TrendAnalysis on a results CSV when the user presses Analyze
// and shows, per measurement, how it changes along the categories, whether the categories
// are internally consistent and how stable every recording is.
class TrendView : public QWidget
{
    Q_OBJECT

public:
    explicit TrendView(QWidget *parent = nullptr);

    void setCsvPath(const QString &path);
    // Supplies the category order (e.g. LOW, MEDIUM, HIGH) at the moment of the analysis.
    void setCategoryOrderProvider(const std::function<QStringList()> &provider) { m_orderProvider = provider; }

signals:
    void logMessage(const QString &message);

private slots:
    void browse();
    void analyze();
    void save();
    void showFeature(int row);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildToolbar();
    void fitDetail();
    void rebuildSummary();
    void rebuildList();
    QWidget *buildListRow(const turb::FeatureTrend &t) const;
    QWidget *buildGroupHeader(const turb::AlgorithmTrend &a) const;
    void selectAlgorithm(size_t algorithm);
    QWidget *buildDetail(const turb::FeatureTrend &t, size_t featureIndex) const;
    QString categoryColor(size_t k) const;

    std::function<QStringList()> m_orderProvider;
    turb::TrendReport m_report;
    bool m_perRecording = true;

    QLineEdit *m_csv;
    QComboBox *m_unit;
    QDoubleSpinBox *m_alpha;
    QPushButton *m_saveButton;
    QLabel *m_summaryTitle;
    QWidget *m_summaryChips;
    QWidget *m_scoreboard;   // one button per algorithm, coloured by its verdict
    QLabel *m_notes;
    QWidget *m_summaryCard;
    QStackedWidget *m_stack; // 0: empty state, 1: results
    QListWidget *m_list;
    std::vector<int> m_rowFeature;    // list row -> feature index (-1 = algorithm header)
    std::vector<int> m_algorithmRow;  // algorithm index -> list row of its best feature
    QScrollArea *m_scroll;
    QWidget *m_detailHost;
    QVBoxLayout *m_detailLayout;
    QWidget *m_detail = nullptr;
};

#endif
