#include "ReferenceFrame.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <sstream>

namespace turb {

std::vector<std::string> ReferenceFrame::modeNames()
{
    return {"First frame", "Previous frame", "Fixed frame", "Average frame"};
}

ParamSpec ReferenceFrame::modeParam(int defaultMode)
{
    return ParamSpec::makeChoice("reference", "Reference", modeNames(), defaultMode,
                                 "Reference image each frame is compared with. 'Previous' measures "
                                 "frame-to-frame change; 'Fixed'/'Average' need an extra pass over the data.");
}

ParamSpec ReferenceFrame::fixedIndexParam()
{
    return ParamSpec::makeInt("reference_index", "Fixed reference frame #", 0, 0, 10000000,
                              "Dataset frame index used when Reference = Fixed frame "
                              "(the first processed frame at or after this index).");
}

void ReferenceFrame::configure(Mode mode, int fixedIndex)
{
    m_mode = mode;
    m_fixedIndex = fixedIndex;
    reset();
}

std::string ReferenceFrame::describe() const
{
    std::ostringstream os;
    os << modeNames()[size_t(m_mode)];
    if (m_mode == Fixed)
        os << " #" << (m_refIndex >= 0 ? m_refIndex : m_fixedIndex);
    if (m_mode == Average)
        os << " (" << m_sumCount << " frames)";
    return os.str();
}

void ReferenceFrame::reset()
{
    m_ref.release();
    m_sum.release();
    m_sumCount = 0;
    m_refIndex = -1;
    m_have = false;
}

void ReferenceFrame::accumulate(const cv::Mat &frame, const FrameInfo &info)
{
    if (m_mode == Average) {
        if (m_sum.empty())
            m_sum = cv::Mat::zeros(frame.size(), CV_64F);
        cv::accumulate(frame, m_sum);
        ++m_sumCount;
    } else if (m_mode == Fixed && !m_have && info.index >= m_fixedIndex) {
        frame.copyTo(m_ref);
        m_refIndex = info.index;
        m_have = true;
    }
}

void ReferenceFrame::finalizePreparation()
{
    if (m_mode == Average && m_sumCount > 0) {
        m_sum.convertTo(m_ref, CV_32F, 1.0 / double(m_sumCount));
        m_sum.release();
        m_have = true;
    }
}

bool ReferenceFrame::reference(const cv::Mat &frame, const FrameInfo &info, cv::Mat &ref)
{
    if ((m_mode == First || m_mode == Previous) && !m_have) {
        frame.copyTo(m_ref);
        m_refIndex = info.index;
        m_have = true;
        return false;
    }
    if (!m_have)
        return false;
    ref = m_ref;
    return true;
}

void ReferenceFrame::update(const cv::Mat &frame)
{
    if (m_mode == Previous)
        frame.copyTo(m_ref);
}

bool ReferenceFrame::isReferenceFrame(const FrameInfo &info) const
{
    return (m_mode == First || m_mode == Fixed) && info.index == m_refIndex;
}

} // namespace turb
