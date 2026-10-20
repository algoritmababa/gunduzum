#ifndef TURB_REFERENCEFRAME_H
#define TURB_REFERENCEFRAME_H

#include "Types.h"

#include <opencv2/core/core.hpp>

#include <string>
#include <vector>

namespace turb {

// Shared reference-frame handling for algorithms that compare each frame with a reference.
// Only the single reference image (or the running sum for the average) is kept in memory.
class ReferenceFrame {
public:
    enum Mode { First = 0, Previous, Fixed, Average };

    static std::vector<std::string> modeNames();
    static ParamSpec modeParam(int defaultMode = First);
    static ParamSpec fixedIndexParam();

    void configure(Mode mode, int fixedIndex);
    Mode mode() const { return m_mode; }
    std::string describe() const;

    // Fixed and Average need a preliminary pass to build the reference.
    int passCount() const { return (m_mode == Fixed || m_mode == Average) ? 2 : 1; }
    bool isMeasurementPass(int pass) const { return pass == passCount() - 1; }

    void reset();
    // Call for every frame of the preparation pass (pass 0 of a 2-pass run).
    void accumulate(const cv::Mat &frame, const FrameInfo &info);
    // Call once after the preparation pass.
    void finalizePreparation();

    // Measurement pass: returns true if a reference is available for this frame.
    // For First / Previous the very first frame only initializes the reference.
    bool reference(const cv::Mat &frame, const FrameInfo &info, cv::Mat &ref);
    // Measurement pass: call after the frame was compared (updates Previous).
    void update(const cv::Mat &frame);

    // True if `info` is the frame that defines the reference (comparison is trivially zero).
    bool isReferenceFrame(const FrameInfo &info) const;
    int referenceIndex() const { return m_refIndex; }

private:
    Mode m_mode = First;
    int m_fixedIndex = 0;
    cv::Mat m_ref;
    cv::Mat m_sum; // CV_64F running sum (Average)
    int m_sumCount = 0;
    int m_refIndex = -1;
    bool m_have = false;
};

} // namespace turb

#endif
