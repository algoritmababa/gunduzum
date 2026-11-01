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
    SignificantNotMonotonic, // significant rank correlation, but the category means are not ordered
    NotSignificant,          // ordered means, but could be chance
    NoTrend,
    InsufficientData
};

const char *verdictName(TrendVerdict v);

struct FeatureTrend {
    std::string feature;
    std::vector<double> mean, std;
    std::vector<int> count;
    double rho = 0.0;
    double pValue = 1.0;
    double pHolm = 1.0;
    double effectSize = 0.0; // (mean_last - mean_first) / pooled std
    bool monotonic = false;
    int direction = 0;       // +1 means increase along the order, -1 decrease, 0 neither
    TrendVerdict verdict = TrendVerdict::InsufficientData;
};

struct TrendReport {
    bool ok = false;
    std::string error;
    std::vector<std::string> categories; // categories used, in order
    int units = 0;                       // recordings (or windows) used
    std::vector<int> unitsPerCategory;
    double minPossibleP = 1.0;           // smallest p-value this amount of data can give
    std::vector<FeatureTrend> features;
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
