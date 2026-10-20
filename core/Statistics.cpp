#include "Statistics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace turb {
namespace stats {

double nan()
{
    return std::numeric_limits<double>::quiet_NaN();
}

template <typename T>
static double percentileImpl(std::vector<T> &v, double p)
{
    v.erase(std::remove_if(v.begin(), v.end(), [](T x) { return !std::isfinite(double(x)); }), v.end());
    if (v.empty())
        return nan();
    p = std::min(std::max(p, 0.0), 100.0);
    const double pos = p / 100.0 * double(v.size() - 1);
    const size_t lo = size_t(std::floor(pos));
    const size_t hi = std::min(lo + 1, v.size() - 1);
    std::nth_element(v.begin(), v.begin() + lo, v.end());
    const double vlo = double(v[lo]);
    if (hi == lo)
        return vlo;
    // the smallest element of the upper partition is the next order statistic
    const double vhi = double(*std::min_element(v.begin() + lo + 1, v.end()));
    return vlo + (pos - double(lo)) * (vhi - vlo);
}

double percentile(std::vector<double> values, double p)
{
    return percentileImpl(values, p);
}

double percentile(std::vector<float> values, double p)
{
    return percentileImpl(values, p);
}

SeriesStats summarize(const std::vector<double> &values)
{
    SeriesStats s;
    double sum = 0.0, sumSq = 0.0;
    double mn = std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();
    for (double v : values) {
        if (!std::isfinite(v))
            continue;
        ++s.count;
        sum += v;
        sumSq += v * v;
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    if (s.count == 0) {
        s.mean = s.variance = s.stddev = s.rms = s.min = s.max = s.p95 = nan();
        return s;
    }
    const double n = double(s.count);
    s.mean = sum / n;
    // second pass for a numerically stable variance
    double acc = 0.0;
    for (double v : values)
        if (std::isfinite(v))
            acc += (v - s.mean) * (v - s.mean);
    s.variance = acc / n;
    s.stddev = std::sqrt(s.variance);
    s.rms = std::sqrt(sumSq / n);
    s.min = mn;
    s.max = mx;
    s.p95 = percentile(values, 95.0);
    return s;
}

std::vector<double> diff(const std::vector<double> &values)
{
    std::vector<double> d;
    if (values.size() < 2)
        return d;
    d.reserve(values.size() - 1);
    for (size_t i = 1; i < values.size(); ++i)
        d.push_back(values[i] - values[i - 1]);
    return d;
}

std::vector<double> absValues(const std::vector<double> &values)
{
    std::vector<double> a(values.size());
    for (size_t i = 0; i < values.size(); ++i)
        a[i] = std::fabs(values[i]);
    return a;
}

double mean(const std::vector<double> &values)
{
    return summarize(values).mean;
}

std::vector<double> detrend(const std::vector<double> &values, int window)
{
    std::vector<double> out(values.size());
    if (values.empty())
        return out;
    if (window <= 1) {
        const double m = mean(values);
        for (size_t i = 0; i < values.size(); ++i)
            out[i] = values[i] - m;
        return out;
    }
    // prefix sums for an O(N) centred moving average (window shrinks at the edges)
    std::vector<double> prefix(values.size() + 1, 0.0);
    for (size_t i = 0; i < values.size(); ++i)
        prefix[i + 1] = prefix[i] + values[i];
    const int half = window / 2;
    const int n = int(values.size());
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - half);
        const int b = std::min(n - 1, i + half);
        const double avg = (prefix[b + 1] - prefix[a]) / double(b - a + 1);
        out[i] = values[i] - avg;
    }
    return out;
}

void appendSummaryFeatures(std::vector<Feature> &out, const std::string &keyPrefix, const std::string &labelPrefix,
                           const SeriesStats &s, const std::string &unit)
{
    out.push_back({keyPrefix + "_mean", "Mean " + labelPrefix, s.mean, unit});
    out.push_back({keyPrefix + "_variance", labelPrefix + " Variance", s.variance, unit.empty() ? "" : unit + "^2"});
    out.push_back({keyPrefix + "_std", "Std " + labelPrefix, s.stddev, unit});
    out.push_back({keyPrefix + "_rms", "RMS " + labelPrefix, s.rms, unit});
    out.push_back({keyPrefix + "_min", "Min " + labelPrefix, s.min, unit});
    out.push_back({keyPrefix + "_max", "Max " + labelPrefix, s.max, unit});
    out.push_back({keyPrefix + "_p95", "P95 " + labelPrefix, s.p95, unit});
}

double matPercentile(const cv::Mat &m, double p)
{
    CV_Assert(m.channels() == 1);
    std::vector<float> v;
    v.reserve(m.total());
    cv::Mat f;
    if (m.depth() == CV_32F)
        f = m;
    else
        m.convertTo(f, CV_32F);
    for (int r = 0; r < f.rows; ++r) {
        const float *row = f.ptr<float>(r);
        v.insert(v.end(), row, row + f.cols);
    }
    return percentile(std::move(v), p);
}

} // namespace stats

void PixelwiseRunningStats::reset()
{
    m_n = 0;
    m_mean.release();
    m_m2.release();
}

void PixelwiseRunningStats::add(const cv::Mat &frame32f)
{
    frame32f.convertTo(m_tmp, CV_64F);
    if (m_n == 0 || m_mean.size() != m_tmp.size()) {
        m_n = 1;
        m_tmp.copyTo(m_mean);
        m_m2 = cv::Mat::zeros(m_tmp.size(), CV_64F);
        return;
    }
    ++m_n;
    cv::subtract(m_tmp, m_mean, m_delta);
    cv::scaleAdd(m_delta, 1.0 / double(m_n), m_mean, m_mean);
    cv::subtract(m_tmp, m_mean, m_delta2);
    cv::multiply(m_delta, m_delta2, m_tmp);
    cv::add(m_m2, m_tmp, m_m2);
}

cv::Mat PixelwiseRunningStats::variance() const
{
    if (m_n == 0)
        return cv::Mat();
    return m_m2 / double(m_n);
}

} // namespace turb
