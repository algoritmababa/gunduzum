#include "Metrics.h"

#include "Stats.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <cmath>

namespace turb {

void Metric::summarize(const std::vector<double> &perFrame, FeatureList &out) const
{
    const Summary s = turb::summarize(perFrame);
    out.push_back(std::make_pair(name() + "_mean", s.mean));
    out.push_back(std::make_pair(name() + "_std", s.std));
    out.push_back(std::make_pair(name() + "_p95", s.p95));
}

// -------------------------------------------------------------------------------------

std::string JitterMetric::description() const
{
    return "Displacement of the ROI between consecutive frames (phase correlation, sub-pixel).";
}

void JitterMetric::reset()
{
    m_prev.release();
}

void JitterMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    if (m_prev.empty() || m_prev.size() != gray32f.size()) {
        gray32f.copyTo(m_prev);
        values.push_back(nan());
        values.push_back(nan());
        values.push_back(nan());
        return;
    }
    if (m_window.size() != gray32f.size())
        cv::createHanningWindow(m_window, gray32f.size(), CV_32F);
    // cv::phaseCorrelate multiplies the window into its inputs in place when the size is
    // already DFT-optimal (OpenCV 3.x / 4.x), so it gets copies.
    m_prev.copyTo(m_a);
    gray32f.copyTo(m_b);
    const cv::Point2d d = cv::phaseCorrelate(m_a, m_b, m_window);
    gray32f.copyTo(m_prev);
    values.push_back(std::sqrt(d.x * d.x + d.y * d.y));
    values.push_back(d.x);
    values.push_back(d.y);
}

// -------------------------------------------------------------------------------------

std::string SharpnessMetric::description() const
{
    return "Variance of the Laplacian of the ROI (lower = more blur).";
}

void SharpnessMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    cv::Laplacian(gray32f, m_lap, CV_32F, 3);
    cv::Scalar mean, sd;
    cv::meanStdDev(m_lap, mean, sd);
    values.push_back(sd[0] * sd[0]);
}

// -------------------------------------------------------------------------------------

std::string IntensityMetric::description() const
{
    return "Mean ROI brightness; scintillation index = Var / Mean^2 over the window.";
}

void IntensityMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    values.push_back(cv::mean(gray32f)[0]);
}

void IntensityMetric::summarize(const std::vector<double> &perFrame, FeatureList &out) const
{
    Metric::summarize(perFrame, out);
    const Summary s = turb::summarize(perFrame);
    const bool ok = s.count > 1 && std::fabs(s.mean) > 1e-9;
    out.push_back(std::make_pair(std::string("scintillation_index"), ok ? (s.std * s.std) / (s.mean * s.mean) : nan()));
}

// -------------------------------------------------------------------------------------

std::vector<std::unique_ptr<Metric> > createMetrics()
{
    std::vector<std::unique_ptr<Metric> > m;
    m.push_back(std::unique_ptr<Metric>(new JitterMetric));
    m.push_back(std::unique_ptr<Metric>(new SharpnessMetric));
    m.push_back(std::unique_ptr<Metric>(new IntensityMetric));
    return m;
}

std::vector<std::string> featureNames()
{
    std::vector<std::string> names;
    const std::vector<double> dummy(3, 1.0);
    for (const std::unique_ptr<Metric> &m : createMetrics()) {
        FeatureList f;
        m->summarize(dummy, f);
        for (const auto &kv : f)
            names.push_back(kv.first);
    }
    return names;
}

} // namespace turb
