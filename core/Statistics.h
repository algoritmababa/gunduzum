#ifndef TURB_STATISTICS_H
#define TURB_STATISTICS_H

#include "Types.h"

#include <opencv2/core/core.hpp>

#include <string>
#include <vector>

namespace turb {

// Summary statistics of a scalar series.
// Variance / std are population statistics (divide by N).
struct SeriesStats {
    size_t count = 0;
    double mean = 0.0;
    double variance = 0.0;
    double stddev = 0.0;
    double rms = 0.0; // sqrt(mean(x^2))
    double min = 0.0;
    double max = 0.0;
    double p95 = 0.0;
};

namespace stats {

double nan();

// Non-finite values are ignored. Empty input gives count == 0 and NaN fields.
SeriesStats summarize(const std::vector<double> &values);

// Linear-interpolated percentile, p in [0, 100]. Copies the data.
double percentile(std::vector<double> values, double p);
double percentile(std::vector<float> values, double p);

// Consecutive differences x[i] - x[i-1] (size N-1).
std::vector<double> diff(const std::vector<double> &values);
std::vector<double> absValues(const std::vector<double> &values);

double mean(const std::vector<double> &values);

// x minus a centred moving average of the given window (window <= 1: x minus global mean).
std::vector<double> detrend(const std::vector<double> &values, int window);

// Appends <prefix>_mean, _variance, _std, _rms, _min, _max, _p95 features.
void appendSummaryFeatures(std::vector<Feature> &out, const std::string &keyPrefix,
                           const std::string &labelPrefix, const SeriesStats &s,
                           const std::string &unit = std::string());

// Percentile of the pixel values of a single-channel float/double Mat (uses nth_element).
double matPercentile(const cv::Mat &m, double p);

} // namespace stats

// Per-pixel running mean / variance (Welford) over a stream of equal-sized frames.
class PixelwiseRunningStats {
public:
    void reset();
    void add(const cv::Mat &frame32f);
    int count() const { return m_n; }
    cv::Mat mean() const { return m_mean; }      // CV_64F
    cv::Mat variance() const;                    // CV_64F, population

private:
    int m_n = 0;
    cv::Mat m_mean;
    cv::Mat m_m2;
    cv::Mat m_tmp, m_delta, m_delta2;
};

} // namespace turb

#endif
