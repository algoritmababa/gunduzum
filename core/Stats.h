#ifndef TURB_STATS_H
#define TURB_STATS_H

#include <vector>

namespace turb {

// Summary of a per-frame series. NaN values are ignored; std is the population std (divide by N).
struct Summary {
    int count = 0;
    double mean = 0.0;
    double std = 0.0;
    double p95 = 0.0;
};

double nan();
Summary summarize(const std::vector<double> &values);
double percentile(std::vector<double> values, double p); // linear interpolation, p in [0, 100]

} // namespace turb

#endif
