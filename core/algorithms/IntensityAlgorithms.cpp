// Algorithms 1 (Temporal Intensity Variance) and 2 (Scintillation Index).

#include "Algorithms.h"

#include <cmath>
#include <sstream>

namespace turb {

static const char *kPerFrameNormNote =
    "Preprocessing uses per-frame normalization: absolute intensity fluctuations are removed, "
    "intensity features are not physically meaningful in this configuration.";

// =====================================================================================
// 1. Temporal Intensity Variance
// =====================================================================================

std::string TemporalIntensityVariance::description() const
{
    return "I(t) = mean ROI intensity per frame. Reports temporal statistics of I(t), frame-to-frame "
           "differences and the mean per-pixel temporal variance inside the ROI.";
}

std::vector<ParamSpec> TemporalIntensityVariance::parameterSpecs() const
{
    return {
        ParamSpec::makeInt("detrend_window", "Detrend window (frames)", 0, 0, 100000,
                           "rms_fluctuation is computed on I(t) minus a centred moving average of this many "
                           "frames (removes slow illumination drift). 0 = subtract the global mean only.").tune(0, 200),
        ParamSpec::makeBool("pixel_statistics", "Per-pixel temporal statistics", true,
                            "Also compute the temporal variance of every ROI pixel (running Welford, ROI-sized "
                            "buffers only) and report its spatial mean.").fixed(),
    };
}

void TemporalIntensityVariance::begin(const AnalysisContext &ctx)
{
    m_ctx = ctx;
    m_intensity.clear();
    m_time.clear();
    m_index.clear();
    m_intensity.reserve(size_t(ctx.frameCount));
    m_time.reserve(size_t(ctx.frameCount));
    m_index.reserve(size_t(ctx.frameCount));
    m_pixel.reset();
}

void TemporalIntensityVariance::processFrame(int, const cv::Mat &frame, const FrameInfo &info)
{
    m_intensity.push_back(cv::mean(frame)[0]);
    m_time.push_back(info.timestamp);
    m_index.push_back(info.index);
    if (m_params.getBool("pixel_statistics", true))
        m_pixel.add(frame);
}

AlgorithmResult TemporalIntensityVariance::finish()
{
    AlgorithmResult r = makeResult();
    const SeriesStats s = stats::summarize(m_intensity);
    const std::vector<double> d = stats::diff(m_intensity);
    const SeriesStats sd = stats::summarize(d);
    const SeriesStats sad = stats::summarize(stats::absValues(d));
    const SeriesStats sdet = stats::summarize(stats::detrend(m_intensity, m_params.getInt("detrend_window", 0)));

    r.features = {
        {"mean_intensity", "Mean Intensity", s.mean, ""},
        {"intensity_variance", "Intensity Variance", s.variance, ""},
        {"intensity_std", "Intensity Std", s.stddev, ""},
        {"rms_fluctuation", "RMS Fluctuation (detrended)", sdet.rms, ""},
        {"min_intensity", "Min Intensity", s.min, ""},
        {"max_intensity", "Max Intensity", s.max, ""},
        {"p95_intensity", "P95 Intensity", s.p95, ""},
        {"mean_temporal_difference", "Mean |I(t) - I(t-1)|", sad.mean, ""},
        {"temporal_difference_variance", "Variance of I(t) - I(t-1)", sd.variance, ""},
        {"rms_intensity", "RMS Intensity", s.rms, ""},
        {"coefficient_of_variation", "Coefficient of Variation (std/mean)",
         std::fabs(s.mean) > 1e-12 ? s.stddev / s.mean : stats::nan(), ""},
    };
    if (m_pixel.count() > 1) {
        cv::Mat var = m_pixel.variance();
        cv::Mat sdev;
        cv::sqrt(var, sdev);
        r.features.push_back({"mean_pixel_temporal_variance", "Mean Per-Pixel Temporal Variance", cv::mean(var)[0], ""});
        r.features.push_back({"mean_pixel_temporal_std", "Mean Per-Pixel Temporal Std", cv::mean(sdev)[0], ""});
    }
    r.featureVector = {"mean_intensity", "intensity_variance", "intensity_std", "rms_fluctuation",
                       "min_intensity", "max_intensity", "p95_intensity", "mean_temporal_difference",
                       "temporal_difference_variance"};

    r.frameColumns = {"frame", "timestamp", "mean_intensity", "frame_difference"};
    r.frameRows.reserve(m_intensity.size());
    for (size_t i = 0; i < m_intensity.size(); ++i)
        r.frameRows.push_back({double(m_index[i]), m_time[i], m_intensity[i], i == 0 ? stats::nan() : d[i - 1]});

    if (m_ctx.perFrameNormalization)
        r.notes.push_back(kPerFrameNormNote);
    if (m_intensity.size() < 2)
        r.notes.push_back("Fewer than 2 frames: temporal statistics are undefined.");
    return r;
}

// =====================================================================================
// 2. Scintillation Index
// =====================================================================================

std::string ScintillationIndex::description() const
{
    return "Normalized intensity fluctuation: SI = Var(I) / Mean(I)^2 of the ROI mean intensity, plus the "
           "spatial mean of the per-pixel scintillation index Var(I_p) / Mean(I_p)^2.";
}

std::vector<ParamSpec> ScintillationIndex::parameterSpecs() const
{
    return {
        ParamSpec::makeDouble("min_mean", "Minimum mean intensity", 1e-6, 0.0, 1e6, 0.001, 6,
                              "Division-by-zero guard: if Mean(I) is below this value the index is reported as "
                              "NaN; pixels whose temporal mean is below it are excluded from the pixel index."),
        ParamSpec::makeBool("pixel_statistics", "Per-pixel scintillation index", true,
                            "Compute the scintillation index of every ROI pixel (running Welford) and report its "
                            "spatial mean.").fixed(),
    };
}

void ScintillationIndex::begin(const AnalysisContext &ctx)
{
    m_ctx = ctx;
    m_intensity.clear();
    m_time.clear();
    m_index.clear();
    m_intensity.reserve(size_t(ctx.frameCount));
    m_time.reserve(size_t(ctx.frameCount));
    m_index.reserve(size_t(ctx.frameCount));
    m_pixel.reset();
}

void ScintillationIndex::processFrame(int, const cv::Mat &frame, const FrameInfo &info)
{
    m_intensity.push_back(cv::mean(frame)[0]);
    m_time.push_back(info.timestamp);
    m_index.push_back(info.index);
    if (m_params.getBool("pixel_statistics", true))
        m_pixel.add(frame);
}

AlgorithmResult ScintillationIndex::finish()
{
    AlgorithmResult r = makeResult();
    const double eps = m_params.get("min_mean", 1e-6);
    const SeriesStats s = stats::summarize(m_intensity);
    const bool meanOk = s.count > 0 && std::fabs(s.mean) >= eps && std::fabs(s.mean) > 0.0;

    std::vector<double> norm(m_intensity.size(), stats::nan());
    if (meanOk)
        for (size_t i = 0; i < m_intensity.size(); ++i)
            norm[i] = (m_intensity[i] - s.mean) / s.mean;
    const SeriesStats sn = stats::summarize(norm);
    const SeriesStats sabs = stats::summarize(stats::absValues(norm));

    r.features = {
        {"mean_intensity", "Mean Intensity", s.mean, ""},
        {"intensity_variance", "Intensity Variance", s.variance, ""},
        {"intensity_std", "Intensity Std", s.stddev, ""},
        {"scintillation_index", "Scintillation Index Var(I)/Mean(I)^2",
         meanOk ? s.variance / (s.mean * s.mean) : stats::nan(), ""},
        {"rms_normalized_fluctuation", "RMS Normalized Fluctuation", meanOk ? sn.rms : stats::nan(), ""},
        {"p95_normalized_fluctuation", "P95 |Normalized Fluctuation|", meanOk ? sabs.p95 : stats::nan(), ""},
        {"max_normalized_fluctuation", "Max |Normalized Fluctuation|", meanOk ? sabs.max : stats::nan(), ""},
        {"min_intensity", "Min Intensity", s.min, ""},
        {"max_intensity", "Max Intensity", s.max, ""},
    };
    if (!meanOk)
        r.notes.push_back("Mean intensity is below the minimum mean threshold: normalized features are NaN "
                          "(division by zero avoided).");

    if (m_pixel.count() > 1) {
        const cv::Mat mean = m_pixel.mean();
        const cv::Mat var = m_pixel.variance();
        double sum = 0.0;
        long long n = 0;
        for (int y = 0; y < mean.rows; ++y) {
            const double *pm = mean.ptr<double>(y);
            const double *pv = var.ptr<double>(y);
            for (int x = 0; x < mean.cols; ++x) {
                if (std::fabs(pm[x]) < eps || pm[x] == 0.0)
                    continue;
                sum += pv[x] / (pm[x] * pm[x]);
                ++n;
            }
        }
        r.features.push_back({"mean_pixel_scintillation_index", "Mean Per-Pixel Scintillation Index",
                              n > 0 ? sum / double(n) : stats::nan(), ""});
        r.features.push_back({"pixel_index_valid_ratio", "Per-Pixel Index Valid Pixel Ratio",
                              double(n) / double(mean.total()), ""});
    }
    r.featureVector = {"mean_intensity", "intensity_variance", "intensity_std", "scintillation_index",
                       "rms_normalized_fluctuation", "p95_normalized_fluctuation"};

    r.frameColumns = {"frame", "timestamp", "mean_intensity", "normalized_fluctuation"};
    r.frameRows.reserve(m_intensity.size());
    for (size_t i = 0; i < m_intensity.size(); ++i)
        r.frameRows.push_back({double(m_index[i]), m_time[i], m_intensity[i], norm[i]});

    if (m_ctx.perFrameNormalization)
        r.notes.push_back(kPerFrameNormNote);
    return r;
}

} // namespace turb
