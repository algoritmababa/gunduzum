// Algorithms 3 (Laplacian Variance), 4 (Gradient Magnitude Variance) and
// 8 (Image Structure / Temporal Structure Analysis).

#include "Algorithms.h"

#include <cmath>

namespace turb {

static std::vector<double> rowsPrefix(int index, double t)
{
    return {double(index), t};
}

// =====================================================================================
// 3. Laplacian Variance
// =====================================================================================

std::string LaplacianVariance::description() const
{
    return "Sharpness per frame = spatial variance of cv::Laplacian over the ROI. Blur caused by turbulence "
           "lowers the value; its temporal fluctuation reflects changing blur.";
}

std::vector<ParamSpec> LaplacianVariance::parameterSpecs() const
{
    return {
        ParamSpec::makeChoice("ksize", "Laplacian aperture", {"1", "3", "5", "7"}, 1,
                              "Aperture size passed to cv::Laplacian (1 = 3x3 4-neighbour kernel)."),
    };
}

void LaplacianVariance::begin(const AnalysisContext &ctx)
{
    m_sharpness.clear();
    m_time.clear();
    m_index.clear();
    m_sharpness.reserve(size_t(ctx.frameCount));
}

void LaplacianVariance::processFrame(int, const cv::Mat &frame, const FrameInfo &info)
{
    static const int ksizes[] = {1, 3, 5, 7};
    const int k = ksizes[m_params.getInt("ksize", 1)];
    cv::Laplacian(frame, m_lap, CV_32F, k);
    cv::Scalar mean, sd;
    cv::meanStdDev(m_lap, mean, sd);
    m_sharpness.push_back(sd[0] * sd[0]);
    m_time.push_back(info.timestamp);
    m_index.push_back(info.index);
}

AlgorithmResult LaplacianVariance::finish()
{
    AlgorithmResult r = makeResult();
    const SeriesStats s = stats::summarize(m_sharpness);
    const SeriesStats sad = stats::summarize(stats::absValues(stats::diff(m_sharpness)));
    r.features = {
        {"mean_sharpness", "Mean Sharpness (Laplacian var)", s.mean, ""},
        {"sharpness_variance", "Sharpness Variance", s.variance, ""},
        {"sharpness_std", "Sharpness Std", s.stddev, ""},
        {"rms_sharpness", "RMS Sharpness", s.rms, ""},
        {"min_sharpness", "Min Sharpness", s.min, ""},
        {"max_sharpness", "Max Sharpness", s.max, ""},
        {"p95_sharpness", "P95 Sharpness", s.p95, ""},
        {"sharpness_temporal_variation", "Mean |S(t) - S(t-1)|", sad.mean, ""},
        {"sharpness_cv", "Sharpness Coefficient of Variation",
         std::fabs(s.mean) > 1e-12 ? s.stddev / s.mean : stats::nan(), ""},
    };
    r.featureVector = {"mean_sharpness", "sharpness_variance", "sharpness_std", "rms_sharpness",
                       "min_sharpness", "max_sharpness", "p95_sharpness"};
    r.frameColumns = {"frame", "timestamp", "laplacian_variance"};
    for (size_t i = 0; i < m_sharpness.size(); ++i) {
        std::vector<double> row = rowsPrefix(m_index[i], m_time[i]);
        row.push_back(m_sharpness[i]);
        r.frameRows.push_back(row);
    }
    return r;
}

// =====================================================================================
// 4. Gradient Magnitude Variance
// =====================================================================================

std::string GradientMagnitudeVariance::description() const
{
    return "Sobel gradients Gx, Gy; G = sqrt(Gx^2 + Gy^2). G(t) = mean gradient magnitude of the ROI per "
           "frame; also the spatial variance of G inside each frame (edge-strength spread).";
}

std::vector<ParamSpec> GradientMagnitudeVariance::parameterSpecs() const
{
    return {
        ParamSpec::makeChoice("ksize", "Sobel aperture", {"3", "5", "7"}, 0, "Kernel size passed to cv::Sobel."),
    };
}

void GradientMagnitudeVariance::begin(const AnalysisContext &ctx)
{
    m_meanGrad.clear();
    m_spatialVar.clear();
    m_time.clear();
    m_index.clear();
    m_meanGrad.reserve(size_t(ctx.frameCount));
    m_spatialVar.reserve(size_t(ctx.frameCount));
}

void GradientMagnitudeVariance::processFrame(int, const cv::Mat &frame, const FrameInfo &info)
{
    static const int ksizes[] = {3, 5, 7};
    const int k = ksizes[m_params.getInt("ksize", 0)];
    cv::Sobel(frame, m_gx, CV_32F, 1, 0, k);
    cv::Sobel(frame, m_gy, CV_32F, 0, 1, k);
    cv::magnitude(m_gx, m_gy, m_mag);
    cv::Scalar mean, sd;
    cv::meanStdDev(m_mag, mean, sd);
    m_meanGrad.push_back(mean[0]);
    m_spatialVar.push_back(sd[0] * sd[0]);
    m_time.push_back(info.timestamp);
    m_index.push_back(info.index);
}

AlgorithmResult GradientMagnitudeVariance::finish()
{
    AlgorithmResult r = makeResult();
    const SeriesStats s = stats::summarize(m_meanGrad);
    const SeriesStats sad = stats::summarize(stats::absValues(stats::diff(m_meanGrad)));
    const SeriesStats sv = stats::summarize(m_spatialVar);
    r.features = {
        {"mean_gradient", "Mean Gradient Magnitude", s.mean, ""},
        {"gradient_variance", "Gradient Variance (temporal, of G(t))", s.variance, ""},
        {"gradient_std", "Gradient Std (temporal)", s.stddev, ""},
        {"rms_gradient", "RMS Gradient", s.rms, ""},
        {"min_gradient", "Min Gradient", s.min, ""},
        {"max_gradient", "Max Gradient", s.max, ""},
        {"p95_gradient", "P95 Gradient", s.p95, ""},
        {"temporal_gradient_variation", "Mean |G(t) - G(t-1)|", sad.mean, ""},
        {"mean_spatial_gradient_variance", "Mean Spatial Variance of G", sv.mean, ""},
        {"std_spatial_gradient_variance", "Std Spatial Variance of G", sv.stddev, ""},
    };
    r.featureVector = {"mean_gradient", "gradient_variance", "gradient_std", "rms_gradient",
                       "min_gradient", "max_gradient", "p95_gradient", "temporal_gradient_variation"};
    r.frameColumns = {"frame", "timestamp", "mean_gradient", "spatial_gradient_variance"};
    for (size_t i = 0; i < m_meanGrad.size(); ++i) {
        std::vector<double> row = rowsPrefix(m_index[i], m_time[i]);
        row.push_back(m_meanGrad[i]);
        row.push_back(m_spatialVar[i]);
        r.frameRows.push_back(row);
    }
    return r;
}

// =====================================================================================
// 8. Image Structure / Temporal Structure Analysis
// =====================================================================================

std::string TemporalStructureAnalysis::description() const
{
    return "Compares each frame with a reference: MAD = mean|I_t - I_ref|, MSD = mean((I_t - I_ref)^2), "
           "per-frame pixel P95 / max of |I_t - I_ref|, correlation with the reference, frame-to-frame "
           "difference, spatial variance and per-pixel temporal variance.";
}

std::vector<ParamSpec> TemporalStructureAnalysis::parameterSpecs() const
{
    return {
        ReferenceFrame::modeParam(ReferenceFrame::First),
        ReferenceFrame::fixedIndexParam(),
        ParamSpec::makeBool("pixel_p95", "Per-frame pixel P95 of |difference|", true,
                            "Compute the 95th percentile of |I_t - I_ref| over the ROI pixels of each frame "
                            "(partial sort per frame)."),
    };
}

int TemporalStructureAnalysis::passCount() const
{
    ReferenceFrame tmp;
    tmp.configure(ReferenceFrame::Mode(m_params.getInt("reference", 0)), 0);
    return tmp.passCount();
}

void TemporalStructureAnalysis::begin(const AnalysisContext &ctx)
{
    m_ref.configure(ReferenceFrame::Mode(m_params.getInt("reference", 0)), m_params.getInt("reference_index", 0));
    m_pixel.reset();
    m_prev.release();
    std::vector<double> *series[] = {&m_time, &m_mad, &m_msd, &m_f2f, &m_spatialVar, &m_p95, &m_max, &m_corr};
    for (std::vector<double> *v : series) {
        v->clear();
        v->reserve(size_t(ctx.frameCount));
    }
    m_index.clear();
}

void TemporalStructureAnalysis::beginPass(int pass)
{
    if (pass > 0)
        m_ref.finalizePreparation();
}

void TemporalStructureAnalysis::processFrame(int pass, const cv::Mat &frame, const FrameInfo &info)
{
    if (!m_ref.isMeasurementPass(pass)) {
        m_ref.accumulate(frame, info);
        return;
    }
    // reference-independent quantities
    cv::Scalar mean, sd;
    cv::meanStdDev(frame, mean, sd);
    const double spatialVar = sd[0] * sd[0];
    double f2f = stats::nan();
    if (!m_prev.empty()) {
        cv::absdiff(frame, m_prev, m_absDiff);
        f2f = cv::mean(m_absDiff)[0];
    }
    frame.copyTo(m_prev);
    m_pixel.add(frame);

    double mad = stats::nan(), msd = stats::nan(), p95 = stats::nan(), mx = stats::nan(), corr = stats::nan();
    cv::Mat ref;
    if (m_ref.reference(frame, info, ref) && !m_ref.isReferenceFrame(info)) {
        cv::subtract(frame, ref, m_diff);
        m_absDiff = cv::abs(m_diff);
        mad = cv::mean(m_absDiff)[0];
        cv::multiply(m_diff, m_diff, m_sq);
        msd = cv::mean(m_sq)[0];
        double mn = 0.0;
        cv::minMaxLoc(m_absDiff, &mn, &mx);
        if (m_params.getBool("pixel_p95", true))
            p95 = stats::matPercentile(m_absDiff, 95.0);
        cv::matchTemplate(frame, ref, m_ccoeff, cv::TM_CCOEFF_NORMED);
        corr = m_ccoeff.at<float>(0, 0);
        m_ref.update(frame);
    }

    m_index.push_back(info.index);
    m_time.push_back(info.timestamp);
    m_mad.push_back(mad);
    m_msd.push_back(msd);
    m_f2f.push_back(f2f);
    m_spatialVar.push_back(spatialVar);
    m_p95.push_back(p95);
    m_max.push_back(mx);
    m_corr.push_back(corr);
}

AlgorithmResult TemporalStructureAnalysis::finish()
{
    AlgorithmResult r = makeResult();
    const SeriesStats smad = stats::summarize(m_mad);
    const SeriesStats smsd = stats::summarize(m_msd);
    const SeriesStats sf2f = stats::summarize(m_f2f);
    const SeriesStats ssv = stats::summarize(m_spatialVar);
    const SeriesStats sp95 = stats::summarize(m_p95);
    const SeriesStats smax = stats::summarize(m_max);
    const SeriesStats scorr = stats::summarize(m_corr);
    double temporalVar = stats::nan();
    if (m_pixel.count() > 1)
        temporalVar = cv::mean(m_pixel.variance())[0];

    r.features = {
        {"mean_absolute_difference", "Mean Absolute Difference (MAD)", smad.mean, ""},
        {"std_absolute_difference", "Std of MAD", smad.stddev, ""},
        {"p95_absolute_difference", "P95 of MAD", smad.p95, ""},
        {"mean_squared_difference", "Mean Squared Difference (MSD)", smsd.mean, ""},
        {"rms_difference", "RMS Difference sqrt(MSD)", std::sqrt(smsd.mean), ""},
        {"p95_difference", "Mean Pixel P95 |I - I_ref|", sp95.mean, ""},
        {"max_difference", "Max Pixel |I - I_ref|", smax.max, ""},
        {"mean_frame_to_frame_difference", "Mean |I_t - I_(t-1)|", sf2f.mean, ""},
        {"std_frame_to_frame_difference", "Std |I_t - I_(t-1)|", sf2f.stddev, ""},
        {"spatial_variance", "Mean Spatial Variance", ssv.mean, ""},
        {"spatial_variance_std", "Std Spatial Variance", ssv.stddev, ""},
        {"temporal_variance", "Mean Per-Pixel Temporal Variance", temporalVar, ""},
        {"mean_correlation", "Mean Correlation with Reference", scorr.mean, ""},
        {"min_correlation", "Min Correlation with Reference", scorr.min, ""},
        {"compared_frames", "Frames Compared with Reference", double(smad.count), ""},
    };
    r.featureVector = {"mean_absolute_difference", "mean_squared_difference", "spatial_variance",
                       "temporal_variance", "rms_difference", "p95_difference", "max_difference"};
    r.frameColumns = {"frame", "timestamp", "mad", "msd", "frame_to_frame_difference", "spatial_variance",
                      "p95_difference", "max_difference", "correlation"};
    for (size_t i = 0; i < m_index.size(); ++i)
        r.frameRows.push_back({double(m_index[i]), m_time[i], m_mad[i], m_msd[i], m_f2f[i], m_spatialVar[i],
                               m_p95[i], m_max[i], m_corr[i]});
    r.notes.push_back("Reference: " + m_ref.describe());
    return r;
}

} // namespace turb
