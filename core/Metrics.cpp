#include "Metrics.h"

#include "Stats.h"

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <cmath>

namespace turb {

void Metric::summarize(const std::vector<double> &perFrame, FeatureList &out) const
{
    const Summary s = turb::summarize(perFrame);
    out.push_back(std::make_pair(name() + "_mean", s.mean));
    out.push_back(std::make_pair(name() + "_std", s.std));
    out.push_back(std::make_pair(name() + "_p95", s.p95));
}

static void pushNan(std::vector<double> &values, int n)
{
    for (int i = 0; i < n; ++i)
        values.push_back(nan());
}

// =====================================================================================
// Jitter: phase correlation between consecutive frames
// =====================================================================================

std::string JitterMetric::description() const
{
    return "Displacement of the whole ROI between consecutive frames (phase correlation, sub-pixel).";
}

void JitterMetric::reset()
{
    m_prev.release();
}

void JitterMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    if (m_prev.empty() || m_prev.size() != gray32f.size()) {
        gray32f.copyTo(m_prev);
        pushNan(values, 3);
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

// =====================================================================================
// Centroid: movement of the segmented target between consecutive frames
// =====================================================================================

std::string CentroidMetric::description() const
{
    return "Movement of the target's centre between consecutive frames (Otsu segmentation, intensity-weighted "
           "centroid). Needs a distinct target in the ROI.";
}

void CentroidMetric::reset()
{
    m_polarity = 0;
    m_havePrev = false;
}

bool CentroidMetric::locate(const cv::Mat &gray32f, double &cx, double &cy)
{
    double mn = 0.0, mx = 0.0;
    cv::minMaxLoc(gray32f, &mn, &mx);
    if (mx - mn <= 1e-9)
        return false;
    gray32f.convertTo(m_u8, CV_8U, 255.0 / (mx - mn), -mn * 255.0 / (mx - mn));
    const double t8 = cv::threshold(m_u8, m_mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    const double level = mn + t8 * (mx - mn) / 255.0;

    // the target is the smaller of the two Otsu classes; decided once per window
    if (m_polarity == 0)
        m_polarity = cv::countNonZero(m_mask) <= int(m_mask.total() / 2) ? 1 : -1;
    if (m_polarity < 0)
        cv::bitwise_not(m_mask, m_mask);

    // largest connected region = the target
    std::vector<std::vector<cv::Point> > contours;
    m_work = m_mask.clone(); // findContours modifies its input in OpenCV 3.x
    cv::findContours(m_work, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    int best = -1;
    double bestArea = 0.0;
    for (size_t i = 0; i < contours.size(); ++i) {
        const double a = std::fabs(cv::contourArea(contours[i]));
        if (a > bestArea) {
            bestArea = a;
            best = int(i);
        }
    }
    if (best < 0 || bestArea < 4.0)
        return false;
    m_mask.setTo(0);
    cv::drawContours(m_mask, contours, best, cv::Scalar(255), cv::FILLED);
    cv::dilate(m_mask, m_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));

    // weights fall to zero at the threshold: no pixel locking of the mask edge
    if (m_polarity > 0)
        cv::subtract(gray32f, cv::Scalar(level), m_weights);
    else
        cv::subtract(cv::Scalar(level), gray32f, m_weights);
    cv::max(m_weights, 0.0, m_weights);
    m_work = cv::Mat::zeros(gray32f.size(), CV_32F);
    m_weights.copyTo(m_work, m_mask);
    const cv::Moments m = cv::moments(m_work, false);
    if (m.m00 <= 0.0)
        return false;
    cx = m.m10 / m.m00;
    cy = m.m01 / m.m00;
    return true;
}

void CentroidMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    double cx = 0.0, cy = 0.0;
    if (!locate(gray32f, cx, cy)) {
        m_havePrev = false;
        pushNan(values, 3);
        return;
    }
    values.push_back(m_havePrev ? std::sqrt((cx - m_px) * (cx - m_px) + (cy - m_py) * (cy - m_py)) : nan());
    values.push_back(cx);
    values.push_back(cy);
    m_px = cx;
    m_py = cy;
    m_havePrev = true;
}

// =====================================================================================
// Optical flow: sparse pyramidal Lucas-Kanade between consecutive frames
// =====================================================================================

std::string FlowMetric::description() const
{
    return "Mean local motion of corner points between consecutive frames (pyramidal Lucas-Kanade, "
           "forward-backward checked, brightness compensated).";
}

void FlowMetric::reset()
{
    m_haveScale = false;
    m_prev.release();
}

void FlowMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    // one fixed 8-bit mapping per window keeps brightness constancy (no per-frame stretch);
    // a global brightness change (flicker) is compensated so it does not look like motion
    const double level = cv::mean(gray32f)[0];
    if (!m_haveScale) {
        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(gray32f, &mn, &mx);
        const double range = mx - mn > 1e-9 ? mx - mn : 1.0;
        m_alpha = 255.0 / range;
        m_beta = -mn * m_alpha;
        m_refMean = level;
        m_haveScale = true;
    }
    const double gain = std::fabs(level) > 1e-12 ? m_refMean / level : 1.0;
    gray32f.convertTo(m_cur, CV_8U, m_alpha * gain, m_beta);
    if (m_prev.empty() || m_prev.size() != m_cur.size()) {
        m_cur.copyTo(m_prev);
        pushNan(values, 2);
        return;
    }

    std::vector<cv::Point2f> pts, next, back;
    cv::goodFeaturesToTrack(m_prev, pts, 200, 0.01, 5.0);
    double sum = 0.0;
    int valid = 0;
    if (!pts.empty()) {
        std::vector<unsigned char> st, stBack;
        std::vector<float> err, errBack;
        const cv::Size win(21, 21);
        const cv::TermCriteria crit(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, 0.01);
        cv::calcOpticalFlowPyrLK(m_prev, m_cur, pts, next, st, err, win, 3, crit);
        cv::calcOpticalFlowPyrLK(m_cur, m_prev, next, back, stBack, errBack, win, 3, crit);
        for (size_t i = 0; i < pts.size(); ++i) {
            if (!st[i] || !stBack[i])
                continue;
            const cv::Point2f fb = back[i] - pts[i];
            if (fb.x * fb.x + fb.y * fb.y > 0.25f) // forward-backward error > 0.5 px
                continue;
            const cv::Point2f f = next[i] - pts[i];
            sum += std::sqrt(double(f.x) * f.x + double(f.y) * f.y);
            ++valid;
        }
    }
    cv::swap(m_prev, m_cur);
    values.push_back(valid > 0 ? sum / valid : nan());
    values.push_back(double(valid));
}

// =====================================================================================
// Sharpness: variance of the Laplacian
// =====================================================================================

std::string SharpnessMetric::description() const
{
    return "Variance of the Laplacian of the ROI relative to the squared mean brightness (lower = more blur).";
}

void SharpnessMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    cv::Laplacian(gray32f, m_lap, CV_32F, 3);
    cv::Scalar mean, sd;
    cv::meanStdDev(m_lap, mean, sd);
    // relative to the brightness: a global gain change (flicker, exposure) cancels out
    const double level = cv::mean(gray32f)[0];
    values.push_back(std::fabs(level) > 1e-12 ? (sd[0] * sd[0]) / (level * level) : nan());
}

// =====================================================================================
// Edge strength: mean Sobel gradient magnitude
// =====================================================================================

std::string GradientMetric::description() const
{
    return "Mean Sobel gradient magnitude of the ROI relative to the mean brightness (lower = softer edges).";
}

void GradientMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    cv::Sobel(gray32f, m_gx, CV_32F, 1, 0, 3);
    cv::Sobel(gray32f, m_gy, CV_32F, 0, 1, 3);
    cv::magnitude(m_gx, m_gy, m_mag);
    const double level = cv::mean(gray32f)[0];
    values.push_back(std::fabs(level) > 1e-12 ? cv::mean(m_mag)[0] / std::fabs(level) : nan());
}

// =====================================================================================
// Frame difference: mean |I_t - I_(t-1)| / mean(I_t)
// =====================================================================================

std::string ChangeMetric::description() const
{
    return "Mean absolute difference to the previous frame, relative to the mean brightness "
           "(how much the image structure changes).";
}

void ChangeMetric::process(const cv::Mat &gray32f, std::vector<double> &values)
{
    if (m_prev.empty() || m_prev.size() != gray32f.size()) {
        gray32f.copyTo(m_prev);
        values.push_back(nan());
        return;
    }
    cv::absdiff(gray32f, m_prev, m_diff);
    const double level = std::fabs(cv::mean(gray32f)[0]);
    values.push_back(level > 1e-12 ? cv::mean(m_diff)[0] / level : nan());
    gray32f.copyTo(m_prev);
}

// =====================================================================================
// Brightness: mean ROI intensity, scintillation index
// =====================================================================================

std::string IntensityMetric::description() const
{
    return "Mean ROI brightness per frame; its variation over time and the scintillation index "
           "Var / Mean^2 over the window.";
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

// =====================================================================================

std::vector<std::unique_ptr<Metric> > createMetrics()
{
    std::vector<std::unique_ptr<Metric> > m;
    m.push_back(std::unique_ptr<Metric>(new JitterMetric));
    m.push_back(std::unique_ptr<Metric>(new CentroidMetric));
    m.push_back(std::unique_ptr<Metric>(new FlowMetric));
    m.push_back(std::unique_ptr<Metric>(new SharpnessMetric));
    m.push_back(std::unique_ptr<Metric>(new GradientMetric));
    m.push_back(std::unique_ptr<Metric>(new ChangeMetric));
    m.push_back(std::unique_ptr<Metric>(new IntensityMetric));
    return m;
}

std::vector<MetricInfo> metricInfos()
{
    std::vector<MetricInfo> infos;
    const std::vector<double> dummy(3, 1.0);
    for (const std::unique_ptr<Metric> &m : createMetrics()) {
        MetricInfo info;
        info.name = m->name();
        info.label = m->label();
        info.algorithm = m->algorithm();
        info.description = m->description();
        info.unit = m->unit();
        info.resolution = m->resolution();
        FeatureList f;
        m->summarize(dummy, f);
        for (const auto &kv : f)
            info.features.push_back(kv.first);
        infos.push_back(info);
    }
    return infos;
}

std::vector<std::string> featureNames()
{
    std::vector<std::string> names;
    for (const MetricInfo &m : metricInfos())
        names.insert(names.end(), m.features.begin(), m.features.end());
    return names;
}

static const MetricInfo *infoOf(const std::vector<MetricInfo> &all, const std::string &feature)
{
    for (const MetricInfo &m : all)
        if (std::find(m.features.begin(), m.features.end(), feature) != m.features.end())
            return &m;
    return nullptr;
}

std::string featureMetric(const std::string &feature)
{
    const std::vector<MetricInfo> all = metricInfos();
    const MetricInfo *m = infoOf(all, feature);
    return m ? m->name : std::string();
}

std::string featureLabel(const std::string &feature)
{
    if (feature == "scintillation_index")
        return "Scintillation index";
    const std::vector<MetricInfo> all = metricInfos();
    const MetricInfo *m = infoOf(all, feature);
    if (!m || feature.size() <= m->name.size() + 1)
        return feature;
    const std::string stat = feature.substr(m->name.size() + 1);
    const std::string statLabel =
        stat == "mean" ? "mean" : stat == "std" ? "variation (std)" : stat == "p95" ? "peaks (P95)" : stat;
    return m->label + " - " + statLabel;
}

std::string featureUnit(const std::string &feature)
{
    if (feature == "scintillation_index")
        return std::string();
    const std::vector<MetricInfo> all = metricInfos();
    const MetricInfo *m = infoOf(all, feature);
    return m ? m->unit : std::string();
}

Metric::Resolution featureResolution(const std::string &feature)
{
    const std::vector<MetricInfo> all = metricInfos();
    const MetricInfo *m = infoOf(all, feature);
    return m ? m->resolution : Metric::Resolution{0.0, 0.0};
}

std::string featureMeaning(const std::string &feature)
{
    if (feature == "scintillation_index")
        return "Normalized brightness flicker of the window (Var / Mean^2).";
    const std::vector<MetricInfo> all = metricInfos();
    const MetricInfo *m = infoOf(all, feature);
    return m ? m->description : std::string();
}

} // namespace turb
