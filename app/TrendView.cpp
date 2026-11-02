#include "TrendView.h"

#include "Metrics.h"
#include "ResultTable.h"

#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegExp>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

using namespace turb;

namespace {

// ---- look ---------------------------------------------------------------------------

const char *kStyle = R"(
QWidget#trendRoot { background: #f4f6f8; color: #1f2328; }
QWidget#trendRoot QLabel { color: #1f2328; background: transparent; }
QFrame#card { background: #ffffff; border: 1px solid #dfe3e8; border-radius: 10px; }
QLabel#title { font-size: 17px; font-weight: 600; }
QLabel#section { font-size: 12px; font-weight: 600; color: #57606a; letter-spacing: 0.5px; }
QLabel#muted { color: #57606a; }
QLabel#small { color: #57606a; font-size: 11px; }
QFrame#tile { background: #f6f8fa; border: 1px solid #e5e8eb; border-radius: 8px; }
QLabel#tileValue { font-size: 20px; font-weight: 600; }
QLabel#tileCaption { color: #57606a; font-size: 11px; }
QLabel#notes { background: #fff8e1; border: 1px solid #f3d27a; border-radius: 8px; padding: 8px; color: #6b4e00; }
QPushButton#primary { background: #2563eb; color: white; border: none; border-radius: 6px;
                      padding: 7px 22px; font-weight: 600; }
QPushButton#primary:hover { background: #1d4ed8; }
QPushButton#secondary { background: #ffffff; color: #1f2328; border: 1px solid #c9d1d9; border-radius: 6px;
                        padding: 6px 14px; }
QPushButton#secondary:disabled { color: #a0a7af; }
QListWidget#features { background: #ffffff; border: 1px solid #dfe3e8; border-radius: 10px; outline: none; }
QListWidget#features::item { border-bottom: 1px solid #eef0f2; }
QListWidget#features::item:selected { background: #e8f0fe; }
QTableWidget#units { background: #ffffff; border: none; gridline-color: #eef0f2; color: #1f2328; }
QHeaderView::section { background: #f6f8fa; color: #57606a; border: none; border-bottom: 1px solid #dfe3e8;
                       padding: 5px; font-weight: 600; }
)";

struct VerdictStyle {
    QString text, fg, bg, icon, shortText;
};

VerdictStyle verdictStyle(TrendVerdict v, int direction = 0)
{
    switch (v) {
    case TrendVerdict::OrderedUntested:
        return {direction < 0 ? QObject::tr("Decrease (untested)") : QObject::tr("Increase (untested)"), "#0969da",
                "#ddf4ff", QString::fromUtf8(direction < 0 ? "\xe2\x86\x93" : "\xe2\x86\x91"),
                QObject::tr("untested")};
    case TrendVerdict::ConsistentIncrease:
        return {QObject::tr("Consistent increase"), "#1a7f37", "#dafbe1", QString::fromUtf8("\xe2\x86\x91"), QObject::tr("consistent")};
    case TrendVerdict::ConsistentDecrease:
        return {QObject::tr("Consistent decrease"), "#1a7f37", "#dafbe1", QString::fromUtf8("\xe2\x86\x93"), QObject::tr("consistent")};
    case TrendVerdict::NotSignificant:
        return {QObject::tr("Ordered, not significant"), "#9a6700", "#fff8c5", "~", QObject::tr("not significant")};
    case TrendVerdict::BelowResolution:
        return {QObject::tr("Below resolution"), "#57606a", "#eaeef2", QString::fromUtf8("\xe2\x89\x88"),
                QObject::tr("too small")};
    case TrendVerdict::SignificantNotMonotonic:
        return {QObject::tr("Significant, not ordered"), "#8250df", "#fbefff", "!", QObject::tr("not ordered")};
    case TrendVerdict::NoTrend:
        return {QObject::tr("No consistent trend"), "#57606a", "#eaeef2", QString::fromUtf8("\xe2\x80\x93"), QObject::tr("no trend")};
    case TrendVerdict::InsufficientData:
        return {QObject::tr("Insufficient data"), "#6e7781", "#f6f8fa", "?", QObject::tr("too few data")};
    }
    return {QString(), "#000", "#fff", QString(), QString()};
}

VerdictStyle algorithmStyle(AlgorithmVerdict v)
{
    switch (v) {
    case AlgorithmVerdict::Useful:
        return {QObject::tr("Useful"), "#1a7f37", "#dafbe1", QString::fromUtf8("\xe2\x9c\x93"), QObject::tr("useful")};
    case AlgorithmVerdict::Promising:
        return {QObject::tr("Promising (untested)"), "#0969da", "#ddf4ff", QString::fromUtf8("\xe2\x97\x94"),
                QObject::tr("promising")};
    case AlgorithmVerdict::NotConfirmed:
        return {QObject::tr("Not confirmed"), "#9a6700", "#fff8c5", "~", QObject::tr("not confirmed")};
    case AlgorithmVerdict::NoBenefit:
        return {QObject::tr("No benefit"), "#57606a", "#eaeef2", QString::fromUtf8("\xe2\x9c\x95"),
                QObject::tr("no benefit")};
    case AlgorithmVerdict::InsufficientData:
        return {QObject::tr("Insufficient data"), "#6e7781", "#f6f8fa", "?", QObject::tr("too few data")};
    }
    return {QString(), "#000", "#fff", QString(), QString()};
}

QLabel *badge(const QString &text, const QString &fg, const QString &bg, bool bold = true)
{
    QLabel *l = new QLabel(text);
    l->setStyleSheet(QString("QLabel { color: %1; background: %2; border-radius: 10px; padding: 3px 10px; %3 }")
                         .arg(fg, bg, bold ? "font-weight: 600;" : ""));
    return l;
}

QFrame *card()
{
    QFrame *f = new QFrame;
    f->setObjectName("card");
    return f;
}

QLabel *label(const QString &text, const char *object = nullptr, bool wrap = false)
{
    QLabel *l = new QLabel(text);
    if (object)
        l->setObjectName(object);
    l->setWordWrap(wrap);
    return l;
}

QString fmt(double v)
{
    if (!std::isfinite(v))
        return QString::fromUtf8("\xe2\x80\x93");
    const double a = std::fabs(v);
    if (a != 0.0 && (a >= 1e5 || a < 1e-3))
        return QString::number(v, 'e', 2);
    return QString::number(v, 'g', 4);
}

// relative change from a to b: "+315%", or "x36" for very large factors
QString change(double a, double b)
{
    if (!std::isfinite(a) || !std::isfinite(b) || a == 0.0)
        return QString::fromUtf8("\xe2\x80\x93");
    const double pct = (b - a) / std::fabs(a) * 100.0;
    if (std::fabs(pct) >= 1000.0 && a * b > 0)
        return QString::fromUtf8("\xc3\x97") + QString::number(b / a, 'g', 3);
    return (pct >= 0 ? "+" : "") + QString::number(pct, 'f', std::fabs(pct) < 10 ? 1 : 0) + "%";
}

QString withUnit(const QString &v, const std::string &unit)
{
    return unit.empty() ? v : v + " " + QString::fromStdString(unit);
}

// plain-language explanation of the verdict
QString sentence(const FeatureTrend &t, const TrendReport &rep, double alpha)
{
    const QString name = QString::fromStdString(featureLabel(t.feature));
    const QString first = QString::fromStdString(rep.categories.front());
    const QString last = QString::fromStdString(rep.categories.back());
    const QString p = QString::number(t.pHolm, 'g', 2);
    const QString spreadWord = t.windowSpread ? QObject::tr("between the windows") : QObject::tr("inside the categories");
    switch (t.verdict) {
    case TrendVerdict::OrderedUntested:
        return QObject::tr("%1 %2 at every step from %3 to %4%5. With so few recordings per category this cannot be "
                           "tested yet: the difference might come from the recordings themselves (day, light, "
                           "camera) rather than from the category. Add recordings to confirm it - at least 2 per "
                           "category, about 3 for a significant result.")
            .arg(name, t.direction > 0 ? QObject::tr("rises") : QObject::tr("falls"), first, last,
                 std::isfinite(t.effectSize)
                     ? QObject::tr(" (the %1-%2 difference is %3 times the spread %4)")
                           .arg(first, last, fmt(std::fabs(t.effectSize)), spreadWord)
                     : QString());
    case TrendVerdict::ConsistentIncrease:
    case TrendVerdict::ConsistentDecrease:
        return QObject::tr("%1 %2 at every step from %3 to %4. A trend this strong would appear by chance "
                           "with probability %5 (below %6), and the %3-%4 difference is %7 times the spread "
                           "inside the categories.")
            .arg(name, t.verdict == TrendVerdict::ConsistentIncrease ? QObject::tr("rises") : QObject::tr("falls"),
                 first, last, p, QString::number(alpha), fmt(std::fabs(t.effectSize)));
    case TrendVerdict::NotSignificant:
        return QObject::tr("The category means of %1 are in order, but with this amount of data the trend could "
                           "be chance (p = %2). More recordings per category are needed to confirm it.")
            .arg(name, p);
    case TrendVerdict::BelowResolution:
        return QObject::tr("%1 changes in order from %2 to %3, but the total change (%4) is smaller than this "
                           "algorithm can reliably resolve (%5). It is not counted as an effect.")
            .arg(name, first, last, fmt(t.change), fmt(t.resolution));
    case TrendVerdict::SignificantNotMonotonic:
        return QObject::tr("%1 is clearly related to the category order (rho = %2, p = %3), but the means do not "
                           "change in the same direction at every step - see the arrows below.")
            .arg(name, fmt(t.rho), p);
    case TrendVerdict::NoTrend:
        return t.tested ? QObject::tr("%1 does not change consistently along the categories (p = %2).").arg(name, p)
                        : QObject::tr("%1 does not change in the same direction at every step between the "
                                      "categories.").arg(name);
    case TrendVerdict::InsufficientData:
        return QObject::tr("Not enough data: at least two categories need values for %1.").arg(name);
    }
    return QString();
}

QFrame *tile(const QString &value, const QString &caption, const QString &tooltip, const QString &color = QString())
{
    QFrame *f = new QFrame;
    f->setObjectName("tile");
    f->setToolTip(tooltip);
    QVBoxLayout *v = new QVBoxLayout(f);
    v->setContentsMargins(12, 8, 12, 8);
    v->setSpacing(2);
    QLabel *val = label(value, "tileValue");
    if (!color.isEmpty())
        val->setStyleSheet(QString("color: %1;").arg(color));
    v->addWidget(val);
    v->addWidget(label(caption, "tileCaption"));
    return f;
}

} // namespace

// =====================================================================================

TrendView::TrendView(QWidget *parent)
    : QWidget(parent)
{
    setObjectName("trendRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(kStyle);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    root->addWidget(buildToolbar());

    // summary strip
    m_summaryCard = card();
    QVBoxLayout *sv = new QVBoxLayout(m_summaryCard);
    sv->setContentsMargins(14, 10, 14, 10);
    QHBoxLayout *sh = new QHBoxLayout;
    m_summaryTitle = label(QString(), "title");
    sh->addWidget(m_summaryTitle);
    sh->addStretch(1);
    m_summaryChips = new QWidget;
    new QHBoxLayout(m_summaryChips);
    m_summaryChips->layout()->setContentsMargins(0, 0, 0, 0);
    sh->addWidget(m_summaryChips);
    sv->addLayout(sh);
    m_scoreboard = new QWidget;
    QHBoxLayout *sb = new QHBoxLayout(m_scoreboard);
    sb->setContentsMargins(0, 2, 0, 2);
    sb->setSpacing(6);
    sv->addWidget(m_scoreboard);
    m_notes = label(QString(), "notes", true);
    sv->addWidget(m_notes);
    m_summaryCard->hide();
    root->addWidget(m_summaryCard);

    // empty state / results
    m_stack = new QStackedWidget;
    QFrame *empty = card();
    QVBoxLayout *ev = new QVBoxLayout(empty);
    ev->addStretch(1);
    QLabel *eTitle = label(tr("Which algorithms follow the turbulence categories?"), "title");
    eTitle->setAlignment(Qt::AlignCenter);
    ev->addWidget(eTitle);
    QLabel *eText = label(tr("Measure labeled recordings first (category LOW / MEDIUM / HIGH), then press "
                             "<b>Analyze</b>.<br>For every measurement you will see how it changes from one "
                             "category to the next, whether the recordings of a category agree with each other, "
                             "and whether the trend could be chance.<br><br>"
                             "One recording per category is enough to explore; about 3 per category are needed "
                             "to confirm a trend statistically."),
                          "muted", true);
    eText->setAlignment(Qt::AlignCenter);
    ev->addWidget(eText);
    ev->addStretch(1);
    m_stack->addWidget(empty);

    QSplitter *split = new QSplitter;
    split->setChildrenCollapsible(false);
    QWidget *left = new QWidget;
    QVBoxLayout *lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->addWidget(label(tr("MEASUREMENTS"), "section"));
    m_list = new QListWidget;
    m_list->setObjectName("features");
    m_list->setMinimumWidth(390);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::currentRowChanged, this, &TrendView::showFeature);
    lv->addWidget(m_list, 1);
    split->addWidget(left);

    QScrollArea *scroll = new QScrollArea;
    m_scroll = scroll;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->installEventFilter(this);
    QWidget *detailHost = new QWidget;
    m_detailHost = detailHost;
    detailHost->setObjectName("trendRoot");
    detailHost->setAttribute(Qt::WA_StyledBackground, true);
    m_detailLayout = new QVBoxLayout(detailHost);
    m_detailLayout->setContentsMargins(0, 0, 4, 0);
    scroll->setWidget(detailHost);
    split->addWidget(scroll);
    split->setStretchFactor(1, 1);
    split->setSizes(QList<int>() << 400 << 800);
    m_stack->addWidget(split);
    root->addWidget(m_stack, 1);
}

QWidget *TrendView::buildToolbar()
{
    QFrame *bar = card();
    QVBoxLayout *rows = new QVBoxLayout(bar);
    rows->setContentsMargins(12, 8, 12, 8);
    rows->setSpacing(6);
    QHBoxLayout *h = new QHBoxLayout;
    QHBoxLayout *h2 = new QHBoxLayout; // analysis options
    rows->addLayout(h);
    rows->addLayout(h2);
    h->addWidget(label(tr("Results CSV"), "muted"));
    m_csv = new QLineEdit;
    m_csv->setPlaceholderText(tr("results file with labeled measurements"));
    h->addWidget(m_csv, 1);
    QPushButton *browse = new QPushButton(tr("Browse..."));
    browse->setObjectName("secondary");
    connect(browse, &QPushButton::clicked, this, &TrendView::browse);
    h->addWidget(browse);
    h2->addWidget(label(tr("Compare"), "muted"));
    m_unit = new QComboBox;
    m_unit->addItem(tr("recordings"));
    m_unit->addItem(tr("windows (optimistic)"));
    m_unit->setToolTip(tr("Recordings (recommended): the windows of a recording are averaged, because they are "
                          "not independent. Windows: every window counts - the significance is overstated."));
    h2->addWidget(m_unit);
    h2->addSpacing(12);
    h2->addWidget(label(tr("Preprocessing"), "muted"));
    m_preprocess = new QComboBox;
    m_preprocess->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_preprocess->addItem("none");
    m_preprocess->setToolTip(tr("Rows measured with this preprocessing are analyzed. Measure the same recordings "
                                "with and without CLAHE into one results file and switch here to compare the "
                                "verdicts. The list is filled from the file when you press Analyze."));
    connect(m_preprocess, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
            [this](int) { analyze(); });
    h2->addWidget(m_preprocess);
    h2->addSpacing(12);
    h2->addWidget(label(QString::fromUtf8("\xce\xb1"), "muted"));
    m_alpha = new QDoubleSpinBox;
    m_alpha->setRange(0.001, 0.2);
    m_alpha->setDecimals(3);
    m_alpha->setSingleStep(0.01);
    m_alpha->setValue(0.05);
    m_alpha->setToolTip(tr("Significance level: a trend counts as consistent when its corrected chance "
                           "probability is below this value."));
    h2->addWidget(m_alpha);
    h2->addStretch(1);
    h->addSpacing(12);
    QPushButton *analyze = new QPushButton(tr("Analyze"));
    analyze->setObjectName("primary");
    analyze->setCursor(Qt::PointingHandCursor);
    connect(analyze, &QPushButton::clicked, this, &TrendView::analyze);
    h->addWidget(analyze);
    m_saveButton = new QPushButton(tr("Save CSV..."));
    m_saveButton->setObjectName("secondary");
    m_saveButton->setEnabled(false);
    connect(m_saveButton, &QPushButton::clicked, this, &TrendView::save);
    h->addWidget(m_saveButton);
    return bar;
}

void TrendView::setCsvPath(const QString &path)
{
    m_csv->setText(path);
}

QString TrendView::categoryColor(size_t k) const
{
    static const char *colors[] = {"#2563eb", "#d97706", "#dc2626", "#7c3aed", "#0d9488", "#db2777"};
    return colors[k % 6];
}

// =====================================================================================

void TrendView::browse()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Results CSV"), m_csv->text(), tr("CSV (*.csv)"));
    if (!f.isEmpty())
        m_csv->setText(f);
}

void TrendView::analyze()
{
    const QString path = m_csv->text().trimmed();
    Table table;
    std::string err;
    if (path.isEmpty() || !loadTable(path.toStdString(), table, &err)) {
        QMessageBox::warning(this, tr("Trend analysis"),
                             err.empty() ? tr("Choose a results CSV.") : QString::fromStdString(err));
        return;
    }
    // one preprocessing variant at a time
    {
        const QString wanted = m_preprocess->currentText();
        const std::vector<std::string> variants = preprocessVariants(table);
        m_preprocess->clear();
        int select = -1;
        for (size_t i = 0; i < variants.size(); ++i) {
            const QString v = QString::fromStdString(variants[i]);
            m_preprocess->addItem(v);
            if (v == wanted)
                select = int(i);
            else if (select < 0 && v == "none")
                select = int(i);
        }
        if (m_preprocess->count() == 0)
            m_preprocess->addItem("none");
        m_preprocess->setCurrentIndex(std::max(0, select));
        m_preprocess->setEnabled(m_preprocess->count() > 1);
        m_variant = m_preprocess->currentText();
        table = filterPreprocess(table, m_variant.toStdString());
    }
    TrendOptions opt;
    if (m_orderProvider) {
        opt.categoryOrder.clear();
        for (const QString &c : m_orderProvider())
            opt.categoryOrder.push_back(c.toStdString());
    }
    opt.perRecording = m_unit->currentIndex() == 0;
    opt.alpha = m_alpha->value();
    m_perRecording = opt.perRecording;
    m_report = analyzeTrends(table, opt);
    m_saveButton->setEnabled(m_report.ok);
    if (!m_report.ok) {
        m_summaryCard->hide();
        m_stack->setCurrentIndex(0);
        QMessageBox::information(this, tr("Trend analysis"), QString::fromStdString(m_report.error));
        emit logMessage(tr("Trend analysis: %1").arg(QString::fromStdString(m_report.error)));
        return;
    }
    rebuildSummary();
    rebuildList();
    m_stack->setCurrentIndex(1);
    m_list->setCurrentRow(1); // first feature of the first (most useful) algorithm

    QStringList useful;
    for (const AlgorithmTrend &a : m_report.algorithms)
        if (a.verdict == AlgorithmVerdict::Useful || a.verdict == AlgorithmVerdict::Promising)
            useful << QString::fromStdString(a.label);
    emit logMessage(tr("Trend analysis of %1 [preprocessing: %4]: %2 unit(s); useful / promising algorithms: %3")
                        .arg(QFileInfo(path).fileName())
                        .arg(m_report.units)
                        .arg(useful.isEmpty() ? tr("none") : useful.join(", "))
                        .arg(m_variant));
}

void TrendView::save()
{
    if (!m_report.ok)
        return;
    const QFileInfo src(m_csv->text().trimmed());
    const QString f = QFileDialog::getSaveFileName(this, tr("Save trend analysis"),
                                                   src.absolutePath() + "/" + src.completeBaseName() + "_trend" +
                                                       (m_variant == "none" ? QString()
                                                                            : "_" + QString(m_variant).replace(
                                                                                        QRegExp("[^A-Za-z0-9.=]+"), "_")) +
                                                       ".csv",
                                                   tr("CSV (*.csv)"));
    if (f.isEmpty())
        return;
    std::string err;
    if (writeTrendCsv(m_report, f.toStdString(), &err))
        emit logMessage(tr("Trend analysis saved: %1").arg(f));
    else
        QMessageBox::warning(this, tr("Trend analysis"), QString::fromStdString(err));
}

// =====================================================================================

void TrendView::rebuildSummary()
{
    QStringList parts;
    for (size_t k = 0; k < m_report.categories.size(); ++k)
        parts << QString("<span style='color:%1'>&#9679;</span> %2 %3")
                     .arg(categoryColor(k), QString::fromStdString(m_report.categories[k]))
                     .arg(m_report.unitsPerCategory[k]);
    m_summaryTitle->setText(tr("%1 %2 &nbsp;&nbsp;<span style='font-size:13px; font-weight:400'>%3</span>")
                                .arg(m_report.units)
                                .arg(m_perRecording ? tr("recordings") : tr("windows"))
                                .arg(parts.join("&nbsp;&nbsp; ") + "&nbsp;&nbsp; " +
                                     tr("preprocessing: %1").arg(m_variant.toHtmlEscaped())));

    // algorithm counters
    QLayout *chips = m_summaryChips->layout();
    while (QLayoutItem *it = chips->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const AlgorithmVerdict order[] = {AlgorithmVerdict::Useful, AlgorithmVerdict::Promising,
                                      AlgorithmVerdict::NotConfirmed, AlgorithmVerdict::NoBenefit,
                                      AlgorithmVerdict::InsufficientData};
    for (AlgorithmVerdict v : order) {
        int n = 0;
        for (const AlgorithmTrend &a : m_report.algorithms)
            n += a.verdict == v;
        if (n == 0)
            continue;
        const VerdictStyle st = algorithmStyle(v);
        chips->addWidget(badge(tr("%1  %2 %3").arg(st.icon).arg(n).arg(st.shortText), st.fg, st.bg));
    }

    // scoreboard: one button per algorithm
    QLayout *sb = m_scoreboard->layout();
    while (QLayoutItem *it = sb->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    sb->addWidget(label(tr("ALGORITHMS"), "section"));
    for (size_t i = 0; i < m_report.algorithms.size(); ++i) {
        const AlgorithmTrend &a = m_report.algorithms[i];
        const VerdictStyle st = algorithmStyle(a.verdict);
        QPushButton *b = new QPushButton(st.icon + " " + QString::fromStdString(a.label));
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QString("%1 - %2").arg(QString::fromStdString(a.algorithm), st.text));
        b->setStyleSheet(QString("QPushButton { color: %1; background: %2; border: 1px solid %1; border-radius: 12px;"
                                 " padding: 3px 10px; font-weight: 600; } QPushButton:hover { background: #ffffff; }")
                             .arg(st.fg, st.bg));
        connect(b, &QPushButton::clicked, this, [this, i]() { selectAlgorithm(i); });
        static_cast<QHBoxLayout *>(sb)->addWidget(b);
    }
    static_cast<QHBoxLayout *>(sb)->addStretch(1);

    QStringList notes;
    for (const std::string &n : m_report.notes)
        notes << QString::fromUtf8("\xe2\x9a\xa0 ") + QString::fromStdString(n).toHtmlEscaped();
    m_notes->setText(notes.join("<br>"));
    m_notes->setVisible(!notes.isEmpty());
    m_summaryCard->show();
}

QWidget *TrendView::buildListRow(const FeatureTrend &t) const
{
    QWidget *w = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 9, 12, 9);
    v->setSpacing(4);
    QHBoxLayout *top = new QHBoxLayout;
    QLabel *name = label(QString::fromStdString(featureLabel(t.feature)));
    name->setStyleSheet("font-weight: 600;");
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(name, 1);
    const VerdictStyle st = verdictStyle(t.verdict, t.direction);
    QLabel *b = badge(st.icon + " " + st.shortText, st.fg, st.bg);
    b->setToolTip(st.text);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top->addWidget(b, 0, Qt::AlignVCenter);
    v->addLayout(top);

    // value flow LOW -> MEDIUM -> HIGH, coloured per category
    QStringList flow;
    for (size_t k = 0; k < m_report.categories.size(); ++k)
        flow << QString("<span style='color:%1'>%2</span>").arg(categoryColor(k), fmt(t.mean[k]));
    int overlap = 0;
    for (int n : t.overlapping)
        overlap += n;
    const QString unit = QString::fromStdString(featureUnit(t.feature));
    const QString consistency =
        overlap == 0 ? QString("<span style='color:#1a7f37'>&#9679; categories apart</span>")
                     : QString("<span style='color:#9a6700'>&#9681; %1 overlapping</span>").arg(overlap);
    QLabel *f = label(flow.join(" &rarr; ") + (unit.isEmpty() ? "" : " " + unit) + "&nbsp;&nbsp;&nbsp;" + consistency,
                      "small", true);
    f->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    v->addWidget(f);
    return w;
}

QWidget *TrendView::buildGroupHeader(const AlgorithmTrend &a) const
{
    QWidget *w = new QWidget;
    w->setStyleSheet("background: #f6f8fa;");
    QVBoxLayout *v = new QVBoxLayout(w);
    v->setContentsMargins(12, 8, 12, 8);
    v->setSpacing(2);
    QHBoxLayout *top = new QHBoxLayout;
    QLabel *name = label(QString::fromStdString(a.label).toUpper());
    name->setStyleSheet("font-weight: 700; letter-spacing: 0.5px;");
    name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(name, 1);
    const VerdictStyle st = algorithmStyle(a.verdict);
    QLabel *b = badge(st.icon + " " + st.text, st.fg, st.bg);
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    top->addWidget(b, 0, Qt::AlignVCenter);
    v->addLayout(top);
    QString line = QString::fromStdString(a.algorithm);
    if (a.best >= 0)
        line += tr("  -  best: %1").arg(QString::fromStdString(featureLabel(m_report.features[size_t(a.best)].feature)));
    QLabel *l = label(line, "small", true);
    l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    v->addWidget(l);
    return w;
}

void TrendView::rebuildList()
{
    m_list->clear();
    m_rowFeature.clear();
    m_algorithmRow.assign(m_report.algorithms.size(), -1);

    // most useful algorithms first, metric order otherwise
    std::vector<size_t> order;
    for (size_t i = 0; i < m_report.algorithms.size(); ++i)
        order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
        return int(m_report.algorithms[a].verdict) < int(m_report.algorithms[b].verdict);
    });

    const int rowH = 2 * m_list->fontMetrics().height() + 34;
    for (size_t ai : order) {
        const AlgorithmTrend &a = m_report.algorithms[ai];
        QListWidgetItem *head = new QListWidgetItem(m_list);
        head->setFlags(Qt::ItemIsEnabled); // not selectable
        head->setSizeHint(QSize(0, rowH));
        m_list->setItemWidget(head, buildGroupHeader(a));
        m_rowFeature.push_back(-1);
        for (size_t f : a.features) {
            QListWidgetItem *it = new QListWidgetItem(m_list);
            it->setSizeHint(QSize(0, rowH));
            m_list->setItemWidget(it, buildListRow(m_report.features[f]));
            if (int(f) == a.best)
                m_algorithmRow[ai] = m_list->count() - 1;
            m_rowFeature.push_back(int(f));
        }
    }
}

void TrendView::selectAlgorithm(size_t algorithm)
{
    if (algorithm >= m_algorithmRow.size() || m_algorithmRow[algorithm] < 0)
        return;
    const int row = m_algorithmRow[algorithm];
    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->item(row > 0 ? row - 1 : row), QAbstractItemView::PositionAtTop);
}

void TrendView::showFeature(int row)
{
    if (row < 0 || row >= int(m_rowFeature.size()) || m_rowFeature[size_t(row)] < 0)
        return;
    const size_t fi = size_t(m_rowFeature[size_t(row)]);
    if (m_detail) {
        m_detailLayout->removeWidget(m_detail);
        m_detail->hide(); // deleted later, but must not be painted any more
        m_detail->deleteLater();
    }
    m_detail = buildDetail(m_report.features[fi], fi);
    m_detailLayout->addWidget(m_detail);
    fitDetail();
}

// QScrollArea does not account for word-wrapped labels (height-for-width) when it sizes its
// widget, so the page would be squeezed into the viewport. Give the page the height its
// layout needs at the current width; the scroll area then scrolls instead of clipping.
void TrendView::fitDetail()
{
    if (!m_detail)
        return;
    const QMargins mg = m_detailLayout->contentsMargins();
    const int w = m_scroll->viewport()->width() - mg.left() - mg.right();
    const int h = m_detail->hasHeightForWidth() ? m_detail->heightForWidth(w) : m_detail->sizeHint().height();
    if (h > 0) {
        m_detail->setMinimumHeight(h);
        // the scroll area sizes its direct child: it must know the full height to scroll
        m_detailHost->setMinimumHeight(h + mg.top() + mg.bottom());
    }
}

bool TrendView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_scroll->viewport() && event->type() == QEvent::Resize)
        fitDetail();
    return QWidget::eventFilter(watched, event);
}

QWidget *TrendView::buildDetail(const FeatureTrend &t, size_t fi) const
{
    const size_t K = m_report.categories.size();
    const std::string unit = featureUnit(t.feature);
    QWidget *page = new QWidget;
    QVBoxLayout *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(10);

    // --- header + verdict -----------------------------------------------------------------
    QFrame *head = card();
    QVBoxLayout *hv = new QVBoxLayout(head);
    hv->setContentsMargins(16, 12, 16, 14);
    QHBoxLayout *titleRow = new QHBoxLayout;
    titleRow->addWidget(label(QString::fromStdString(featureLabel(t.feature)) +
                                  (unit.empty() ? QString() : QString(" (%1)").arg(QString::fromStdString(unit))),
                              "title"));
    titleRow->addStretch(1);
    const VerdictStyle st = verdictStyle(t.verdict, t.direction);
    titleRow->addWidget(badge(st.icon + " " + st.text.toUpper(), st.fg, st.bg));
    hv->addLayout(titleRow);
    hv->addWidget(label(QString::fromStdString(featureMeaning(t.feature)) + "  <i>(" +
                            QString::fromStdString(t.feature) + ")</i>",
                        "muted", true));
    // coloured box; the padding lives in the frame layout so that word wrapping measures correctly
    QFrame *explainBox = new QFrame;
    explainBox->setStyleSheet(QString("QFrame { background: %1; border-radius: 8px; }").arg(st.bg));
    QVBoxLayout *eb = new QVBoxLayout(explainBox);
    eb->setContentsMargins(12, 9, 12, 9);
    QLabel *explain = label(sentence(t, m_report, m_alpha->value()), nullptr, true);
    explain->setStyleSheet(QString("QLabel { color: %1; }")
                               .arg(st.fg == "#57606a" || st.fg == "#6e7781" ? "#1f2328" : st.fg));
    eb->addWidget(explain);
    hv->addWidget(explainBox);

    // key numbers
    QHBoxLayout *tiles = new QHBoxLayout;
    tiles->setSpacing(8);
    tiles->addWidget(tile(fmt(t.rho), tr("trend strength (rho)"),
                          tr("Spearman rank correlation between the value and the category order: "
                             "+1 = always rising, -1 = always falling, 0 = no relation.")));
    if (t.tested)
        tiles->addWidget(tile(QString::number(t.pHolm, 'g', 2), tr("chance probability (p)"),
                              tr("Probability of a trend at least this strong when the category labels are shuffled "
                                 "(permutation test, Holm-corrected over all tested measurements)."),
                              t.pHolm < m_alpha->value() ? "#1a7f37" : "#9a6700"));
    else
        tiles->addWidget(tile(QString::fromUtf8("\xe2\x80\x93"), tr("not tested yet"),
                              tr("The permutation test needs at least 2 recordings in every category."), "#0969da"));
    tiles->addWidget(tile(fmt(t.effectSize),
                          tr("%1-%2 difference (d)")
                              .arg(QString::fromStdString(m_report.categories.front()),
                                   QString::fromStdString(m_report.categories.back())),
                          t.windowSpread
                              ? tr("Difference between the first and last category divided by the spread between the "
                                   "windows of the recordings (single recording per category).")
                              : tr("Difference between the first and last category divided by the spread inside the "
                                   "categories (Cohen's d). Above ~1 the categories barely overlap.")));
    tiles->addWidget(tile(change(t.mean.front(), t.mean.back()),
                          tr("total change (resolvable: %1)").arg(withUnit(fmt(t.resolution), unit)),
                          tr("Change of the mean from the first to the last category. Changes smaller than %1 are "
                             "below what this algorithm can reliably measure and are not counted.")
                              .arg(withUnit(fmt(t.resolution), unit)),
                          t.belowResolution ? "#57606a" : QString()));
    hv->addLayout(tiles);
    v->addWidget(head);

    // --- categories: values, steps, consistency inside the category -------------------------
    QFrame *cats = card();
    QVBoxLayout *cv = new QVBoxLayout(cats);
    cv->setContentsMargins(16, 12, 16, 14);
    cv->addWidget(label(tr("HOW IT CHANGES ACROSS THE CATEGORIES"), "section"));
    QHBoxLayout *flow = new QHBoxLayout;
    flow->setSpacing(6);
    for (size_t k = 0; k < K; ++k) {
        if (k > 0) {
            // step arrow with relative change; green when it goes the overall way, red against it
            const double d = t.mean[k] - t.mean[k - 1];
            const int overall = t.rho > 0 ? 1 : (t.rho < 0 ? -1 : 0);
            const bool with = overall != 0 && std::isfinite(d) && d * overall > 0;
            QLabel *arrow = label(QString("<div style='text-align:center'><span style='font-size:20px'>&rarr;"
                                          "</span><br><b>%1</b></div>")
                                      .arg(change(t.mean[k - 1], t.mean[k])));
            arrow->setAlignment(Qt::AlignCenter);
            arrow->setStyleSheet(QString("color: %1;").arg(with ? "#1a7f37" : "#cf222e"));
            arrow->setToolTip(with ? tr("Changes in the same direction as the overall trend")
                                   : tr("Goes against the overall trend"));
            flow->addWidget(arrow);
        }
        QFrame *c = new QFrame;
        c->setObjectName("tile");
        c->setStyleSheet(QString("QFrame#tile { border-top: 4px solid %1; }").arg(categoryColor(k)));
        // cards share the width equally; their texts wrap instead of widening the page
        c->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QVBoxLayout *kv = new QVBoxLayout(c);
        kv->setContentsMargins(12, 8, 12, 10);
        kv->setSpacing(3);
        QLabel *name = label(QString::fromStdString(m_report.categories[k]));
        name->setStyleSheet(QString("font-weight: 700; color: %1;").arg(categoryColor(k)));
        kv->addWidget(name);
        kv->addWidget(label(withUnit(fmt(t.mean[k]), unit), "tileValue", true));
        const double cvPct = std::isfinite(t.std[k]) && t.mean[k] != 0 ? 100.0 * t.std[k] / std::fabs(t.mean[k])
                                                                       : std::nan("");
        kv->addWidget(label(QString::fromUtf8("\xc2\xb1 %1  (CV %2)")
                                .arg(fmt(t.std[k]),
                                     std::isfinite(cvPct) ? QString::number(cvPct, 'f', 1) + "%"
                                                          : QString::fromUtf8("\xe2\x80\x93")),
                            "small", true));
        kv->addWidget(label(tr("range %1 ... %2").arg(fmt(t.minValue[k]), fmt(t.maxValue[k])), "small", true));
        kv->addWidget(label(t.windowSpread ? tr("%1 recording(s), %2 windows").arg(t.count[k]).arg(t.spreadCount[k])
                                           : tr("n = %1").arg(t.count[k]),
                            "small", true));
        if (t.count[k] == 0)
            kv->addWidget(badge(tr("no data"), "#6e7781", "#eaeef2", false));
        else if (t.spreadCount[k] < 2)
            kv->addWidget(badge(tr("spread unknown"), "#6e7781", "#eaeef2", false));
        else if (t.overlapping[k] == 0)
            kv->addWidget(badge(QString::fromUtf8("\xe2\x9c\x93 ") + tr("consistent"), "#1a7f37", "#dafbe1", false));
        else {
            QLabel *b = badge(tr("%1/%2 overlap").arg(t.overlapping[k]).arg(t.spreadCount[k]), "#9a6700", "#fff8c5",
                              false);
            b->setToolTip(tr("%1 of the %2 %3 of this category lie inside the value range of a neighbouring "
                             "category.")
                              .arg(t.overlapping[k])
                              .arg(t.spreadCount[k])
                              .arg(t.windowSpread ? tr("windows") : tr("recordings")));
            kv->addWidget(b);
        }
        flow->addWidget(c, 1);
    }
    cv->addLayout(flow);
    cv->addWidget(label(t.windowSpread
                            ? tr("<b>consistent</b>: all windows of the category stay outside the value range of the "
                                 "neighbouring categories (single recording per category, so the windows are used). "
                                 "<b>CV</b>: spread between the windows relative to the mean.")
                            : tr("<b>consistent</b>: all recordings of the category stay outside the value range of "
                                 "the neighbouring categories. <b>CV</b>: spread inside the category relative to its "
                                 "mean."),
                        "small", true));
    v->addWidget(cats);

    // --- recordings -----------------------------------------------------------------------
    QFrame *rec = card();
    QVBoxLayout *rv = new QVBoxLayout(rec);
    rv->setContentsMargins(16, 12, 16, 14);
    rv->addWidget(label(m_perRecording ? tr("RECORDINGS") : tr("WINDOWS"), "section"));
    QTableWidget *table = new QTableWidget;
    table->setObjectName("units");
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels(QStringList() << tr("Recording") << tr("Category") << tr("Value")
                                                   << tr("Stability inside the recording") << tr("Note"));
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setShowGrid(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // units sorted by category, then value
    std::vector<size_t> idx;
    for (size_t u = 0; u < m_report.unitList.size(); ++u)
        idx.push_back(u);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const TrendUnit &ua = m_report.unitList[a], &ub = m_report.unitList[b];
        if (ua.category != ub.category)
            return ua.category < ub.category;
        return ua.value[fi] < ub.value[fi];
    });
    table->setRowCount(int(idx.size()));
    for (int r = 0; r < int(idx.size()); ++r) {
        const TrendUnit &u = m_report.unitList[idx[size_t(r)]];
        const size_t k = size_t(u.category);
        const double val = u.value[fi];
        // inside the range of a neighbouring category?
        bool outlier = false;
        for (int dk = -1; dk <= 1; dk += 2) {
            const long j = long(k) + dk;
            if (j >= 0 && j < long(K) && t.count[size_t(j)] > 0 && val >= t.minValue[size_t(j)] &&
                val <= t.maxValue[size_t(j)])
                outlier = true;
        }
        QTableWidgetItem *name = new QTableWidgetItem(QString::fromStdString(u.id));
        name->setToolTip(QString::fromStdString(u.source));
        table->setItem(r, 0, name);
        QTableWidgetItem *cat = new QTableWidgetItem(QString::fromUtf8("\xe2\x97\x8f ") +
                                                     QString::fromStdString(m_report.categories[k]));
        cat->setForeground(QColor(categoryColor(k)));
        table->setItem(r, 1, cat);
        QTableWidgetItem *value =
            new QTableWidgetItem(withUnit(fmt(val), unit) + (outlier ? QString::fromUtf8("   \xe2\x9a\xa0") : ""));
        if (outlier) {
            value->setBackground(QColor("#fff8c5"));
            value->setToolTip(tr("This value lies inside the range of a neighbouring category."));
        }
        table->setItem(r, 2, value);
        const double wcv = u.windowCv[fi];
        table->setItem(r, 3,
                       new QTableWidgetItem(std::isfinite(wcv)
                                                ? tr("%1% variation over %2 windows")
                                                      .arg(QString::number(100.0 * wcv, 'f', 1))
                                                      .arg(u.windows)
                                                : (u.windows < 2 ? tr("single window") : QString())));
        table->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(u.note)));
    }
    table->resizeColumnsToContents();
    table->resizeRowsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    // tall enough for the rows (up to ~12); more rows scroll inside the table
    const int rowH = table->fontMetrics().height() + 14;
    table->setMinimumHeight(std::min((int(idx.size()) + 1) * rowH + 8, 13 * rowH));
    rv->addWidget(table);
    rv->addWidget(label(tr("<b>Stability inside the recording</b>: how much the value varies between the windows of "
                           "the same recording (lower = more stable). "
                           "<span style='background:#fff8c5'>&nbsp;&#9888;&nbsp;</span> the value falls into the "
                           "range of a neighbouring category."),
                        "small", true));
    v->addWidget(rec);
    v->addStretch(1);
    return page;
}
