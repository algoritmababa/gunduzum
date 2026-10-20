#ifndef TURB_ALGORITHMS_H
#define TURB_ALGORITHMS_H

#include "../Algorithm.h"
#include "../ReferenceFrame.h"
#include "../Statistics.h"

#include <opencv2/imgproc/imgproc.hpp>

namespace turb {

// 1. Temporal Intensity Variance ------------------------------------------------
class TemporalIntensityVariance : public Algorithm {
public:
    std::string id() const override { return "temporal_intensity_variance"; }
    std::string name() const override { return "Temporal Intensity Variance"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::Intensity; }
    std::vector<ParamSpec> parameterSpecs() const override;
    void begin(const AnalysisContext &ctx) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    AnalysisContext m_ctx;
    std::vector<double> m_intensity, m_time;
    std::vector<int> m_index;
    PixelwiseRunningStats m_pixel;
};

// 2. Scintillation Index ----------------------------------------------------------
class ScintillationIndex : public Algorithm {
public:
    std::string id() const override { return "scintillation_index"; }
    std::string name() const override { return "Scintillation Index"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::Intensity; }
    std::vector<ParamSpec> parameterSpecs() const override;
    void begin(const AnalysisContext &ctx) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    AnalysisContext m_ctx;
    std::vector<double> m_intensity, m_time;
    std::vector<int> m_index;
    PixelwiseRunningStats m_pixel;
};

// 3. Laplacian Variance ------------------------------------------------------------
class LaplacianVariance : public Algorithm {
public:
    std::string id() const override { return "laplacian_variance"; }
    std::string name() const override { return "Laplacian Variance"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::QualityStructure; }
    std::vector<ParamSpec> parameterSpecs() const override;
    void begin(const AnalysisContext &ctx) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    std::vector<double> m_sharpness, m_time;
    std::vector<int> m_index;
    cv::Mat m_lap;
};

// 4. Gradient Magnitude Variance -----------------------------------------------------
class GradientMagnitudeVariance : public Algorithm {
public:
    std::string id() const override { return "gradient_magnitude_variance"; }
    std::string name() const override { return "Gradient Magnitude Variance"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::QualityStructure; }
    std::vector<ParamSpec> parameterSpecs() const override;
    void begin(const AnalysisContext &ctx) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    std::vector<double> m_meanGrad, m_spatialVar, m_time;
    std::vector<int> m_index;
    cv::Mat m_gx, m_gy, m_mag;
};

// 5. Phase Correlation / Image Displacement -------------------------------------------
class PhaseCorrelationDisplacement : public Algorithm {
public:
    std::string id() const override { return "phase_correlation"; }
    std::string name() const override { return "Phase Correlation"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::MotionGeometric; }
    std::vector<ParamSpec> parameterSpecs() const override;
    int passCount() const override;
    void begin(const AnalysisContext &ctx) override;
    void beginPass(int pass) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    ReferenceFrame m_ref;
    cv::Mat m_window, m_bufA, m_bufB;
    std::vector<double> m_dx, m_dy, m_response, m_time;
    std::vector<int> m_index;
    std::vector<char> m_valid;
};

// 6. Centroid Jitter ---------------------------------------------------------------------
class CentroidJitter : public Algorithm {
public:
    std::string id() const override { return "centroid_jitter"; }
    std::string name() const override { return "Centroid Jitter"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::MotionGeometric; }
    std::vector<ParamSpec> parameterSpecs() const override;
    void begin(const AnalysisContext &ctx) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    bool segment(const cv::Mat &frame, double &cx, double &cy, double &area);

    AnalysisContext m_ctx;
    std::vector<double> m_cx, m_cy, m_area, m_time;
    std::vector<int> m_index;
    std::vector<char> m_valid;
    cv::Mat m_u8, m_mask, m_work, m_weights, m_kernel;
};

// 7. Optical Flow (sparse pyramidal Lucas-Kanade) -------------------------------------------
class OpticalFlowLK : public Algorithm {
public:
    std::string id() const override { return "optical_flow"; }
    std::string name() const override { return "Optical Flow"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::MotionGeometric; }
    std::vector<ParamSpec> parameterSpecs() const override;
    void begin(const AnalysisContext &ctx) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    void toU8(const cv::Mat &frame, cv::Mat &u8);
    void detect(const cv::Mat &u8, std::vector<cv::Point2f> &pts) const;

    bool m_haveScale = false;
    double m_alpha = 1.0, m_beta = 0.0;
    cv::Mat m_refU8, m_curU8;
    std::vector<cv::Point2f> m_refPts;

    // per-frame series
    std::vector<double> m_time, m_meanMag, m_meanFx, m_meanFy, m_maxMag, m_tracked, m_validCount;
    std::vector<int> m_index;
    // pooled over all valid vectors
    std::vector<float> m_mags;
    double m_sumFx = 0, m_sumFy = 0, m_sumFx2 = 0, m_sumFy2 = 0;
    long long m_totalTracked = 0, m_totalValid = 0;
};

// 8. Image Structure / Temporal Structure Analysis ---------------------------------------------
class TemporalStructureAnalysis : public Algorithm {
public:
    std::string id() const override { return "structure_analysis"; }
    std::string name() const override { return "Image Structure Analysis"; }
    std::string description() const override;
    AlgorithmGroup group() const override { return AlgorithmGroup::QualityStructure; }
    std::vector<ParamSpec> parameterSpecs() const override;
    int passCount() const override;
    void begin(const AnalysisContext &ctx) override;
    void beginPass(int pass) override;
    void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) override;
    AlgorithmResult finish() override;

private:
    ReferenceFrame m_ref;
    PixelwiseRunningStats m_pixel;
    cv::Mat m_prev, m_diff, m_absDiff, m_sq, m_ccoeff;
    std::vector<double> m_time, m_mad, m_msd, m_f2f, m_spatialVar, m_p95, m_max, m_corr;
    std::vector<int> m_index;
};

} // namespace turb

#endif
