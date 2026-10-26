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
//
// Adding a metric = one subclass + one line in createMetrics().
class Metric {
public:
    virtual ~Metric() {}

    virtual std::string name() const = 0;        // short id, used as column prefix
    virtual std::string description() const = 0; // one sentence: what it measures
    virtual std::string unit() const { return std::string(); }

    // Per-frame columns written to the frame-level CSV (the first one is summarized).
    virtual std::vector<std::string> frameColumns() const { return {name()}; }

    virtual void reset() = 0;
    // Appends frameColumns().size() values (NaN when undefined, e.g. the first frame).
    virtual void process(const cv::Mat &gray32f, std::vector<double> &values) = 0;

    // Summary of the per-frame values of the primary column.
    virtual void summarize(const std::vector<double> &perFrame, FeatureList &out) const;
};

// Kayma / titreşim: apparent displacement between consecutive frames
// (cv::phaseCorrelate, sub-pixel, Hanning window).
class JitterMetric : public Metric {
public:
    std::string name() const override { return "jitter"; }
    std::string description() const override;
    std::string unit() const override { return "px"; }
    std::vector<std::string> frameColumns() const override { return {"jitter", "dx", "dy"}; }
    void reset() override;
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    cv::Mat m_prev, m_window, m_a, m_b;
};

// Bulanıklık: variance of the Laplacian of the frame (higher = sharper).
class SharpnessMetric : public Metric {
public:
    std::string name() const override { return "sharpness"; }
    std::string description() const override;
    void reset() override {}
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;

private:
    cv::Mat m_lap;
};

// Parlaklık dalgalanması: mean ROI intensity per frame; the summary adds the
// scintillation index Var(I) / Mean(I)^2 of the window.
class IntensityMetric : public Metric {
public:
    std::string name() const override { return "intensity"; }
    std::string description() const override;
    void reset() override {}
    void process(const cv::Mat &gray32f, std::vector<double> &values) override;
    void summarize(const std::vector<double> &perFrame, FeatureList &out) const override;
};

// All metrics, in output column order.
std::vector<std::unique_ptr<Metric> > createMetrics();

// Names of all summary features, in column order (derived from the metrics).
std::vector<std::string> featureNames();

} // namespace turb

#endif
