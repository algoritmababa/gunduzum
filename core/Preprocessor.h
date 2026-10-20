#ifndef TURB_PREPROCESSOR_H
#define TURB_PREPROCESSOR_H

#include "Types.h"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <string>

namespace turb {

struct PreprocessConfig {
    enum Denoise { DenoiseNone = 0, DenoiseGaussian, DenoiseMedian, DenoiseBilateral };
    enum Contrast { ContrastNone = 0, ContrastHistEq, ContrastClahe };
    enum Normalization {
        NormNone = 0,      // keep native intensity units (0..255 / 0..65535)
        NormFullScale,     // divide by the bit-depth full scale -> [0, 1]
        NormMinMaxFrame,   // per-frame min/max -> [0, 1]  (removes intensity fluctuation!)
        NormZScoreFrame    // per-frame zero mean / unit std (removes intensity fluctuation!)
    };

    Roi roi;
    Denoise denoise = DenoiseNone;
    int denoiseKernel = 3;       // Gaussian / median kernel size (odd); bilateral diameter
    double denoiseSigma = 1.0;   // Gaussian sigma; bilateral sigmaColor/sigmaSpace
    Contrast contrast = ContrastNone;
    double claheClipLimit = 2.0;
    int claheTileGrid = 8;
    Normalization normalization = NormNone;

    // true when the normalization destroys absolute intensity information
    bool isPerFrameNormalization() const
    {
        return normalization == NormMinMaxFrame || normalization == NormZScoreFrame;
    }
    std::string describe() const;
};

const char *denoiseName(PreprocessConfig::Denoise d);
const char *contrastName(PreprocessConfig::Contrast c);
const char *normalizationName(PreprocessConfig::Normalization n);

// Raw frame -> ROI crop -> grayscale -> CV_32F -> denoise -> contrast -> normalization.
// The ROI is applied first so that every later step only touches ROI pixels.
class Preprocessor {
public:
    explicit Preprocessor(const PreprocessConfig &cfg = PreprocessConfig());
    void setConfig(const PreprocessConfig &cfg);
    const PreprocessConfig &config() const { return m_cfg; }

    // Clamped ROI for a given source frame size.
    static cv::Rect effectiveRoi(const Roi &roi, const cv::Size &frameSize);

    // `out` is CV_32FC1 with the size of the effective ROI. Buffers are reused between calls.
    void process(const cv::Mat &raw, cv::Mat &out);

private:
    PreprocessConfig m_cfg;
    cv::Mat m_gray, m_tmp, m_u8, m_u16;
    cv::Ptr<cv::CLAHE> m_clahe;
};

} // namespace turb

#endif
