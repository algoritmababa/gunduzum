#ifndef TURB_TRENDANALYSIS_H
#define TURB_TRENDANALYSIS_H

#include "ResultTable.h"

#include <string>
#include <vector>

namespace turb {

// Does a measured feature change consistently along the ordered categories
// (e.g. LOW -> MEDIUM -> HIGH)? Runs on the results table, on demand.
//
// Per feature:
//   - unit values: one value per recording (mean of its windows) or per window
//   - category mean / std (sample std, n-1) / n
//   - Spearman rank correlation rho between the value and the category rank (1, 2, 3, ...)
//   - permutation p-value of |rho| (category labels shuffled over the units),
//     Holm-adjusted over all features
//   - effect size d between the first and the last category (Cohen's d, pooled std)
//   - verdict combining monotonic category means and the adjusted p-value
struct TrendOptions {
    std::vector<std::string> categoryOrder = {"LOW", "MEDIUM", "HIGH"};
    bool perRecording = true; // false: every window is a unit (optimistic, windows are correlated)
    int permutations = 10000;
    double alpha = 0.05;
    unsigned seed = 12345;    // fixed seed: the same table always gives the same p-values
};

enum class TrendVerdict {
    ConsistentIncrease,
    ConsistentDecrease,
    OrderedUntested,         // ordered means, but too few recordings for a statistical test (e.g. 1 per category)
    BelowResolution,         // ordered, but the change is smaller than the algorithm can resolve
    SignificantNotMonotonic, // significant rank correlation, but the category means are not ordered
    NotSignificant,          // ordered means, but could be chance
    NoTrend,
    InsufficientData
};

const char *verdictName(TrendVerdict v);

// Does an algorithm (metric) help to tell the categories apart? Derived from its features:
//   Useful        - at least one feature changes consistently (tested and significant)
//   Promising     - ordered along the categories, but too few recordings to test yet
//   NotConfirmed  - tested, but no feature is both ordered and significant
//   NoBenefit     - no feature changes in the same direction along the categories
//   InsufficientData
enum class AlgorithmVerdict { Useful, Promising, NotConfirmed, NoBenefit, InsufficientData };

const char *algorithmVerdictName(AlgorithmVerdict v);

// One analysis unit: a recording (its windows averaged) or a single window.
struct TrendUnit {
    std::string id;     // recording name (+ window number for window units)
    std::string source; // path
    std::string note;
    int category = 0;   // index into TrendReport::categories
    int windows = 0;    // windows averaged into this unit
    std::vector<double> value;     // per feature (featureNames() order)
    std::vector<double> windowCv;  // per feature: std / |mean| over the windows (NaN with < 2 windows)
    std::vector<std::vector<double> > windowValues; // per feature: the values of the windows
};

struct FeatureTrend {
    std::string feature;
    std::vector<double> mean;               // per category, mean of its units
    std::vector<int> count;                 // per category, number of units
    // Spread inside the categories. Normally over the units (recordings); when a category has a
    // single recording, over the windows of the recordings instead (windowSpread = true).
    bool windowSpread = false;
    std::vector<double> std;                // sample std (n-1)
    std::vector<double> minValue, maxValue; // value range
    std::vector<int> spreadCount;           // values the spread is computed from
    // per category: how many of those values lie inside the value range of a neighbouring
    // category (0 everywhere = the categories are internally consistent and do not mix)
    std::vector<int> overlapping;
    bool tested = false;                    // false: too few units for the permutation test
    double rho = 0.0;
    double pValue = 1.0;                    // NaN when not tested
    double pHolm = 1.0;                     // NaN when not tested
    double effectSize = 0.0; // (mean_last - mean_first) / pooled std
    double change = 0.0;     // mean_last - mean_first
    double resolution = 0.0; // smallest change the algorithm resolves here (feature unit)
    bool belowResolution = false;
    bool monotonic = false;
    int direction = 0;       // +1 means increase along the order, -1 decrease, 0 neither
    TrendVerdict verdict = TrendVerdict::InsufficientData;
};

struct AlgorithmTrend {
    std::string metric;        // metric name (feature prefix)
    std::string label;         // e.g. "Jitter"
    std::string algorithm;     // e.g. "Phase correlation"
    std::vector<size_t> features; // indices into TrendReport::features
    int best = -1;             // most informative feature (index into TrendReport::features)
    int consistent = 0;        // features with a consistent (tested) trend
    int untested = 0;          // features ordered along the categories but not tested
    AlgorithmVerdict verdict = AlgorithmVerdict::InsufficientData;
};

struct TrendReport {
    bool ok = false;
    std::string error;
    std::vector<std::string> categories; // categories used, in order
    int units = 0;                       // recordings (or windows) used
    std::vector<int> unitsPerCategory;
    double minPossibleP = 1.0;           // smallest p-value this amount of data can give
    bool testable = false;               // every category has >= 2 units: trends can be tested
    std::vector<FeatureTrend> features;
    std::vector<AlgorithmTrend> algorithms; // one per metric, in metric order
    std::vector<TrendUnit> unitList;
    std::vector<std::string> notes;
};

TrendReport analyzeTrends(const Table &table, const TrendOptions &options);

bool writeTrendCsv(const TrendReport &report, const std::string &path, std::string *error);

// Statistics helpers (exposed for tests)
std::vector<double> averageRanks(const std::vector<double> &v);
double spearman(const std::vector<double> &x, const std::vector<double> &y);
std::vector<double> holmAdjust(const std::vector<double> &p);

} // namespace turb

#endif
