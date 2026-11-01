#ifndef TURB_METRICS_H
#define TURB_METRICS_H

#include <opencv2/core/core.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace turb {

typedef std::vector<std::pair<std::string, double> > FeatureList; // ordered name -> value

// One measurement = one visible effect of turbulence, one number per frame.
//
// Every metric works the same way:
//   reset() at the start of a window, process() for every frame of the window
//   (gray CV_32F ROI image), summarize() at the end of the window.
// The default summary is <name>_mean, <name>_std, <name>_p95 of the per-frame values.
// Metrics have no tunable parameters: every recording is measured the same way.
//
// Adding a metric = one subclass + one line in createMetrics().
class Metric {
public:
    virtual ~Metric() {}

    virtual std::string name() const = 0;        // short id, used as column prefix
    virtual std::string label() const = 0;       // human readable name, e.g. "Jitter"
    virtual std::string algorithm() const = 0;   // image processing algorithm, e.g. "Phase correlation"
    virtual std::string description() const = 0; // one sentence: what it measures
    virtual std::string unit() const { return std::string(); }

    // Smallest change of a summary feature (between the first and the last category) that this
    // metric can resolve; smaller changes are not counted as an effect. A change must exceed
    // both the absolute value (in the feature's unit) and the relative fraction of the larger
    // category mean. This describes the measurement, not the turbulence.
    struct Resolution {
        double absolute;
        double relative;
    };
    virtual Resolution resolution() const { return {0.0, 0.02}; }

    // Per-frame columns written to the frame-level CSV (the first one is summarized).
    virtual std::vector<std::string> frameColumns() const { return {name()}; }

    virtual void reset() = 0;
    // Appends frameColumns().size() values (NaN when undefined, e.g. the first frame).
    virtual void process(const cv::Mat &gray32f, std::vector<double> &values) = 0;

    // Summary of the per-frame values of the primary column.
    virtual void summarize(const std::vector<double> &perFrame, FeatureList &out) const;
};

// --- Motion -----------------------------------------------------------------------------

// Displacement of the whole ROI between consecutive frames (cv::phaseCorrelate, sub-pixel).
class JitterMetric : public Metric {
public:
    std::string name() const override { return "jitter"; }
    std::string label() const override { return "Jitter"; }
    std::string algorithm() const override { return "Phase correlation"; }
    std::string description() const override;
    std::string unit() const override { return "px"; }
    Resolution resolution() const override { return {0.1, 0.0}; }
    std::vector<std::string> frameColumns() const override { return {"jitter", "dx", "dy"}; }
    void reset() override;
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    cv::Mat m_prev, m_window, m_a, m_b;
};

// Movement of the target's centre between consecutive frames. The target is segmented with
// Otsu (bright or dark target chosen automatically: the smaller class), the centroid is
// intensity weighted (sub-pixel, no pixel locking).
class CentroidMetric : public Metric {
public:
    std::string name() const override { return "centroid"; }
    std::string label() const override { return "Centroid"; }
    std::string algorithm() const override { return "Centroid jitter (Otsu + moments)"; }
    std::string description() const override;
    std::string unit() const override { return "px"; }
    Resolution resolution() const override { return {0.1, 0.0}; }
    std::vector<std::string> frameColumns() const override { return {"centroid", "cx", "cy"}; }
    void reset() override;
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    bool locate(const cv::Mat &gray32f, double &cx, double &cy);

    int m_polarity = 0; // +1 bright target, -1 dark target, 0 not decided yet (per window)
    bool m_havePrev = false;
    double m_px = 0.0, m_py = 0.0;
    cv::Mat m_u8, m_mask, m_work, m_weights;
};

// Local apparent motion between consecutive frames: sparse pyramidal Lucas-Kanade flow of
// corner points with a forward-backward check; the value is the mean flow magnitude.
class FlowMetric : public Metric {
public:
    std::string name() const override { return "flow"; }
    std::string label() const override { return "Optical flow"; }
    std::string algorithm() const override { return "Optical flow (pyramidal Lucas-Kanade)"; }
    std::string description() const override;
    std::string unit() const override { return "px"; }
    Resolution resolution() const override { return {0.1, 0.0}; }
    std::vector<std::string> frameColumns() const override { return {"flow", "flow_points"}; }
    void reset() override;
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    bool m_haveScale = false;
    double m_alpha = 1.0, m_beta = 0.0; // fixed 8-bit mapping per window (brightness constancy)
    double m_refMean = 0.0;             // frames are gain-compensated to the first frame's brightness
    cv::Mat m_prev, m_cur;
};

// --- Blur / structure --------------------------------------------------------------------

// Variance of the Laplacian of the frame relative to the squared mean brightness
// (higher = sharper; global brightness changes cancel out).
class SharpnessMetric : public Metric {
public:
    std::string name() const override { return "sharpness"; }
    std::string label() const override { return "Sharpness"; }
    std::string algorithm() const override { return "Laplacian variance"; }
    std::string description() const override;
    void reset() override {}
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    cv::Mat m_lap;
};

// Mean Sobel gradient magnitude of the frame relative to the mean brightness
// (higher = stronger edges; global brightness changes cancel out).
class GradientMetric : public Metric {
public:
    std::string name() const override { return "gradient"; }
    std::string label() const override { return "Edge strength"; }
    std::string algorithm() const override { return "Gradient magnitude (Sobel)"; }
    std::string description() const override;
    void reset() override {}
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    cv::Mat m_gx, m_gy, m_mag;
};

// Mean absolute difference to the previous frame, relative to the mean brightness.
class ChangeMetric : public Metric {
public:
    std::string name() const override { return "change"; }
    std::string label() const override { return "Frame difference"; }
    std::string algorithm() const override { return "Temporal structure (frame difference)"; }
    std::string description() const override;
    Resolution resolution() const override { return {0.002, 0.0}; }
    void reset() override { m_prev.release(); }
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    cv::Mat m_prev, m_diff;
};

// --- Brightness -------------------------------------------------------------------------

// Mean ROI intensity per frame (temporal intensity variance); the summary adds the
// scintillation index Var(I) / Mean(I)^2 of the window.
class IntensityMetric : public Metric {
public:
    std::string name() const override { return "intensity"; }
    std::string label() const override { return "Brightness"; }
    std::string algorithm() const override { return "Temporal intensity variance + scintillation index"; }
    std::string description() const override;
    void reset() override {}
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;
    void summarize(const std::vector<double> &perFrame, FeatureList &out) const override;
};

// All metrics, in output column order.
std::vector<std::unique_ptr<Metric> > createMetrics();

// Static description of a metric and the summary features it produces.
struct MetricInfo {
    std::string name, label, algorithm, description, unit;
    Metric::Resolution resolution;
    std::vector<std::string> features;
};
std::vector<MetricInfo> metricInfos();

// Names of all summary features, in column order (derived from the metrics).
std::vector<std::string> featureNames();

// Metric a summary feature belongs to ("" if unknown).
std::string featureMetric(const std::string &feature);
// Readable name ("Jitter - mean") and unit ("px") of a summary feature.
std::string featureLabel(const std::string &feature);
std::string featureUnit(const std::string &feature);
// What the feature tells, in one short sentence.
std::string featureMeaning(const std::string &feature);
// Smallest change the feature's metric can resolve (see Metric::resolution).
Metric::Resolution featureResolution(const std::string &feature);

} // namespace turb

#endif
