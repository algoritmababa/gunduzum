#ifndef TURB_REGISTRATION_H
#define TURB_REGISTRATION_H

#include <opencv2/core/core.hpp>

#include <string>

namespace turb {

struct RegistrationConfig {
    enum Mode { None = 0, Translation, Affine };
    enum Reference { FirstFrame = 0, PreviousFrame, FixedFrame };

    Mode mode = None;
    Reference reference = FirstFrame;
    int fixedFrameIndex = 0;     // dataset frame index, used with FixedFrame
    int eccIterations = 50;      // affine (ECC) only
    double eccEpsilon = 1e-4;

    std::string describe() const;
};

// cv::phaseCorrelate multiplies the window into its inputs *in place* when the image size is
// already DFT-optimal (OpenCV 3.x and 4.x). This wrapper works on copies so that stored
// reference frames are never modified. bufA/bufB are reusable scratch buffers.
cv::Point2d phaseCorrelateSafe(const cv::Mat &reference, const cv::Mat &frame, const cv::Mat &window,
                               double *response, cv::Mat &bufA, cv::Mat &bufB);

const char *registrationModeName(RegistrationConfig::Mode m);
const char *registrationReferenceName(RegistrationConfig::Reference r);

// Removes global (camera / platform) motion from preprocessed CV_32F frames.
// Translation uses cv::phaseCorrelate (sub-pixel), affine uses cv::findTransformECC.
class Registrator {
public:
    explicit Registrator(const RegistrationConfig &cfg = RegistrationConfig());

    void reset();
    bool enabled() const { return m_cfg.mode != RegistrationConfig::None; }
    const RegistrationConfig &config() const { return m_cfg; }

    // For FixedFrame: the preprocessed reference frame must be supplied before the first apply().
    void setFixedReference(const cv::Mat &ref32f);

    // Aligns `frame` in place. Returns false if the estimation failed (frame left untouched).
    bool apply(cv::Mat &frame);

    // Last estimated global motion (frame relative to reference).
    double lastDx() const { return m_lastDx; }
    double lastDy() const { return m_lastDy; }
    int failureCount() const { return m_failures; }
    int frameCount() const { return m_frames; }

private:
    RegistrationConfig m_cfg;
    cv::Mat m_reference;
    cv::Mat m_window;
    cv::Mat m_warped;
    cv::Mat m_bufA, m_bufB;
    bool m_haveReference = false;
    double m_lastDx = 0.0, m_lastDy = 0.0;
    int m_failures = 0;
    int m_frames = 0;
};

} // namespace turb

#endif
