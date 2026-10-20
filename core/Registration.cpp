#include "Registration.h"

#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/video/tracking.hpp>

#include <sstream>

namespace turb {

cv::Point2d phaseCorrelateSafe(const cv::Mat &reference, const cv::Mat &frame, const cv::Mat &window,
                               double *response, cv::Mat &bufA, cv::Mat &bufB)
{
    if (window.empty())
        return cv::phaseCorrelate(reference, frame, cv::noArray(), response);
    reference.copyTo(bufA);
    frame.copyTo(bufB);
    return cv::phaseCorrelate(bufA, bufB, window, response);
}

const char *registrationModeName(RegistrationConfig::Mode m)
{
    switch (m) {
    case RegistrationConfig::None: return "None";
    case RegistrationConfig::Translation: return "Translation";
    case RegistrationConfig::Affine: return "Affine";
    }
    return "";
}

const char *registrationReferenceName(RegistrationConfig::Reference r)
{
    switch (r) {
    case RegistrationConfig::FirstFrame: return "First frame";
    case RegistrationConfig::PreviousFrame: return "Previous frame";
    case RegistrationConfig::FixedFrame: return "Fixed frame";
    }
    return "";
}

std::string RegistrationConfig::describe() const
{
    std::ostringstream os;
    os << registrationModeName(mode);
    if (mode != None) {
        os << ", reference=" << registrationReferenceName(reference);
        if (reference == FixedFrame)
            os << " #" << fixedFrameIndex;
    }
    return os.str();
}

Registrator::Registrator(const RegistrationConfig &cfg)
    : m_cfg(cfg)
{
}

void Registrator::reset()
{
    if (m_cfg.reference != RegistrationConfig::FixedFrame) {
        m_reference.release();
        m_haveReference = false;
    }
    m_lastDx = m_lastDy = 0.0;
    m_failures = 0;
    m_frames = 0;
}

void Registrator::setFixedReference(const cv::Mat &ref32f)
{
    ref32f.copyTo(m_reference);
    m_haveReference = true;
}

bool Registrator::apply(cv::Mat &frame)
{
    if (!enabled())
        return true;
    ++m_frames;

    if (!m_haveReference) {
        // first frame becomes the reference (FirstFrame / PreviousFrame)
        frame.copyTo(m_reference);
        m_haveReference = true;
        m_lastDx = m_lastDy = 0.0;
        return true;
    }
    if (m_reference.size() != frame.size()) {
        ++m_failures;
        return false;
    }

    bool ok = true;
    if (m_cfg.mode == RegistrationConfig::Translation) {
        if (m_window.size() != frame.size())
            cv::createHanningWindow(m_window, frame.size(), CV_32F);
        // shift of `frame` relative to the reference
        const cv::Point2d s = phaseCorrelateSafe(m_reference, frame, m_window, nullptr, m_bufA, m_bufB);
        m_lastDx = s.x;
        m_lastDy = s.y;
        const cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, -s.x, 0, 1, -s.y);
        cv::warpAffine(frame, m_warped, M, frame.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        cv::swap(frame, m_warped);
    } else {
        cv::Mat warp = cv::Mat::eye(2, 3, CV_32F);
        try {
            const cv::TermCriteria crit(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, m_cfg.eccIterations,
                                        m_cfg.eccEpsilon);
            cv::findTransformECC(m_reference, frame, warp, cv::MOTION_AFFINE, crit, cv::noArray());
            m_lastDx = warp.at<float>(0, 2);
            m_lastDy = warp.at<float>(1, 2);
            cv::warpAffine(frame, m_warped, warp, frame.size(), cv::INTER_LINEAR | cv::WARP_INVERSE_MAP,
                           cv::BORDER_REPLICATE);
            cv::swap(frame, m_warped);
        } catch (const cv::Exception &) {
            ok = false; // ECC did not converge; keep the unregistered frame
        }
    }
    if (!ok)
        ++m_failures;

    if (m_cfg.reference == RegistrationConfig::PreviousFrame)
        frame.copyTo(m_reference);
    return ok;
}

} // namespace turb
