#include "Stats.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace turb {

double nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

double percentile(std::vector<double> v, double p)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](double x) { return !std::isfinite(x); }), v.end());
    if (v.empty())
        return nan();
    std::sort(v.begin(), v.end());
    const double pos = std::min(std::max(p, 0.0), 100.0) / 100.0 * double(v.size() - 1);
    const size_t lo = size_t(pos);
    const size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (pos - double(lo)) * (v[hi] - v[lo]);
}

Summary summarize(const std::vector<double> &values)
{
    Summary s;
    double sum = 0.0;
    for (double v : values)
        if (std::isfinite(v)) {
            sum += v;
            ++s.count;
        }
    if (s.count == 0) {
        s.mean = s.std = s.p95 = nan();
        return s;
    }
    s.mean = sum / s.count;
    double acc = 0.0;
    for (double v : values)
        if (std::isfinite(v))
            acc += (v - s.mean) * (v - s.mean);
    s.std = std::sqrt(acc / s.count);
    s.p95 = percentile(values, 95.0);
    return s;
}

} // namespace turb
