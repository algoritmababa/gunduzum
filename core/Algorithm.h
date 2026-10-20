#ifndef TURB_ALGORITHM_H
#define TURB_ALGORITHM_H

#include "Types.h"

#include <opencv2/core/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace turb {

// Information the engine gives to an algorithm before the first frame.
struct AnalysisContext {
    int frameCount = 0;          // number of frames that will be delivered per pass
    int firstFrameIndex = 0;     // dataset index of the first delivered frame
    int frameStep = 1;
    double frameRate = 0.0;      // 0 = unknown (timestamps are frame indices)
    cv::Size frameSize;          // size of the preprocessed (ROI) frames
    cv::Point roiOffset;         // top-left of the ROI in source-image coordinates
    int sourceDepth = CV_8U;     // depth of the raw source images
    bool perFrameNormalization = false; // preprocessing removed absolute intensity information
    bool registrationEnabled = false;
};

// Base class of every feature extraction algorithm.
//
// Life cycle (driven by AnalysisEngine, always on the worker thread):
//   configure(params) -> begin(ctx) ->
//     for pass in [0, passCount()):  beginPass(pass); processFrame(...) for every frame
//   -> finish()
//
// Frames are delivered sequentially as preprocessed, registered CV_32FC1 ROI images.
// The Mat is only valid during the call; algorithms must copy what they need to keep
// and should keep as little as possible (a reference frame, scalar series, ...).
class Algorithm {
public:
    virtual ~Algorithm() {}

    virtual std::string id() const = 0;    // stable machine id, e.g. "phase_correlation"
    virtual std::string name() const = 0;  // display name
    virtual std::string description() const = 0;
    virtual AlgorithmGroup group() const = 0;

    virtual std::vector<ParamSpec> parameterSpecs() const { return std::vector<ParamSpec>(); }
    void configure(const ParamSet &params) { m_params = ParamSet::withDefaults(parameterSpecs(), params); }
    const ParamSet &parameters() const { return m_params; }

    // Number of sequential passes over the data the algorithm needs with the current
    // parameters (e.g. 2 when an average or fixed reference frame must be built first).
    virtual int passCount() const { return 1; }

    virtual void begin(const AnalysisContext &ctx) = 0;
    virtual void beginPass(int /*pass*/) {}
    virtual void processFrame(int pass, const cv::Mat &frame, const FrameInfo &info) = 0;
    virtual AlgorithmResult finish() = 0;

protected:
    // Helper for subclasses: result skeleton with id/name/group filled in.
    AlgorithmResult makeResult() const
    {
        AlgorithmResult r;
        r.algorithmId = id();
        r.algorithmName = name();
        r.group = group();
        return r;
    }

    ParamSet m_params;
};

typedef std::unique_ptr<Algorithm> AlgorithmPtr;

} // namespace turb

#endif
