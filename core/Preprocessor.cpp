#include "Preprocessor.h"

#include <sstream>

namespace turb {

const char *denoiseName(PreprocessConfig::Denoise d)
{
    switch (d) {
    case PreprocessConfig::DenoiseNone: return "None";
    case PreprocessConfig::DenoiseGaussian: return "Gaussian";
    case PreprocessConfig::DenoiseMedian: return "Median";
    case PreprocessConfig::DenoiseBilateral: return "Bilateral";
    }
    return "";
}

const char *contrastName(PreprocessConfig::Contrast c)
{
    switch (c) {
    case PreprocessConfig::ContrastNone: return "None";
    case PreprocessConfig::ContrastHistEq: return "Histogram equalization";
    case PreprocessConfig::ContrastClahe: return "CLAHE";
    }
    return "";
}

const char *normalizationName(PreprocessConfig::Normalization n)
{
    switch (n) {
    case PreprocessConfig::NormNone: return "None (native units)";
    case PreprocessConfig::NormFullScale: return "Full-scale [0,1]";
    case PreprocessConfig::NormMinMaxFrame: return "Per-frame min/max";
    case PreprocessConfig::NormZScoreFrame: return "Per-frame z-score";
    }
    return "";
}

std::string PreprocessConfig::describe() const
{
    std::ostringstream os;
    os << "grayscale, roi crop, denoise=" << denoiseName(denoise);
    if (denoise != DenoiseNone)
        os << "(k=" << denoiseKernel << ", sigma=" << denoiseSigma << ")";
    os << ", contrast=" << contrastName(contrast);
    if (contrast == ContrastClahe)
        os << "(clip=" << claheClipLimit << ", tiles=" << claheTileGrid << ")";
    os << ", normalization=" << normalizationName(normalization);
    return os.str();
}

Preprocessor::Preprocessor(const PreprocessConfig &cfg)
{
    setConfig(cfg);
}

void Preprocessor::setConfig(const PreprocessConfig &cfg)
{
    m_cfg = cfg;
    if (m_cfg.denoiseKernel < 1)
        m_cfg.denoiseKernel = 1;
    if (m_cfg.denoiseKernel % 2 == 0)
        ++m_cfg.denoiseKernel;
    m_clahe.release();
    if (m_cfg.contrast == PreprocessConfig::ContrastClahe) {
        const int g = std::max(1, m_cfg.claheTileGrid);
        m_clahe = cv::createCLAHE(m_cfg.claheClipLimit, cv::Size(g, g));
    }
}

cv::Rect Preprocessor::effectiveRoi(const Roi &roi, const cv::Size &frameSize)
{
    const cv::Rect full(0, 0, frameSize.width, frameSize.height);
    if (roi.isFullFrame())
        return full;
    return cv::Rect(roi.x, roi.y, roi.width, roi.height) & full;
}

static double fullScaleOf(int depth)
{
    switch (depth) {
    case CV_8U: return 255.0;
    case CV_8S: return 127.0;
    case CV_16U: return 65535.0;
    case CV_16S: return 32767.0;
    default: return 1.0; // float images are assumed to be already normalized
    }
}

void Preprocessor::process(const cv::Mat &raw, cv::Mat &out)
{
    CV_Assert(!raw.empty());
    const cv::Rect r = effectiveRoi(m_cfg.roi, raw.size());
    if (r.area() <= 0)
        CV_Error(cv::Error::StsBadArg, "ROI does not intersect the frame");

    // 1) ROI first: only a header, no pixel copy
    const cv::Mat roiView = raw(r);
    const int depth = raw.depth();

    // 2) grayscale
    const cv::Mat *gray = &roiView;
    if (roiView.channels() == 3) {
        cv::cvtColor(roiView, m_gray, cv::COLOR_BGR2GRAY);
        gray = &m_gray;
    } else if (roiView.channels() == 4) {
        cv::cvtColor(roiView, m_gray, cv::COLOR_BGRA2GRAY);
        gray = &m_gray;
    } else if (roiView.channels() != 1) {
        CV_Error(cv::Error::StsBadArg, "Unsupported channel count");
    }

    // 3) float
    gray->convertTo(out, CV_32F);

    // 4) denoise
    const int k = m_cfg.denoiseKernel;
    switch (m_cfg.denoise) {
    case PreprocessConfig::DenoiseGaussian:
        cv::GaussianBlur(out, out, cv::Size(k, k), m_cfg.denoiseSigma);
        break;
    case PreprocessConfig::DenoiseMedian:
        // medianBlur only supports float input for ksize 3 and 5
        cv::medianBlur(out, m_tmp, k > 5 ? 5 : (k < 3 ? 3 : k));
        cv::swap(out, m_tmp);
        break;
    case PreprocessConfig::DenoiseBilateral: {
        // sigma is given relative to full scale so it works for 8 and 16 bit data
        const double sc = m_cfg.denoiseSigma * fullScaleOf(depth) * 0.1;
        cv::bilateralFilter(out, m_tmp, k, sc, double(k));
        cv::swap(out, m_tmp);
        break;
    }
    case PreprocessConfig::DenoiseNone:
        break;
    }

    // 5) contrast normalization (output stays in native full-scale units)
    if (m_cfg.contrast != PreprocessConfig::ContrastNone) {
        const double fs = fullScaleOf(depth);
        if (m_cfg.contrast == PreprocessConfig::ContrastHistEq) {
            out.convertTo(m_u8, CV_8U, 255.0 / fs);
            cv::equalizeHist(m_u8, m_u8);
            m_u8.convertTo(out, CV_32F, fs / 255.0);
        } else {
            out.convertTo(m_u16, CV_16U, 65535.0 / fs);
            m_clahe->apply(m_u16, m_u16);
            m_u16.convertTo(out, CV_32F, fs / 65535.0);
        }
    }

    // 6) intensity normalization
    switch (m_cfg.normalization) {
    case PreprocessConfig::NormNone:
        break;
    case PreprocessConfig::NormFullScale:
        out *= 1.0 / fullScaleOf(depth);
        break;
    case PreprocessConfig::NormMinMaxFrame:
        cv::normalize(out, out, 0.0, 1.0, cv::NORM_MINMAX);
        break;
    case PreprocessConfig::NormZScoreFrame: {
        cv::Scalar m, s;
        cv::meanStdDev(out, m, s);
        const double sd = s[0] > 1e-12 ? s[0] : 1.0;
        out.convertTo(out, CV_32F, 1.0 / sd, -m[0] / sd);
        break;
    }
    }
}

} // namespace turb
