#ifndef TURB_ANALYSISENGINE_H
#define TURB_ANALYSISENGINE_H

#include "Preprocessor.h"
#include "Registration.h"
#include "Types.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace turb {

struct DatasetSpec {
    std::string path;        // image directory or video file
    double frameRate = 0.0;  // Hz; 0 = native video rate, or frame index as timestamp
    int startFrame = 0;
    int endFrame = -1;       // inclusive; -1 = last frame
    int frameStep = 1;
};

struct AlgorithmRun {
    std::string id;
    ParamSet params;
};

struct AnalysisConfig {
    DatasetSpec dataset;
    PreprocessConfig preprocess;
    RegistrationConfig registration;
    // Single-algorithm mode uses one entry. Several entries are run independently on the
    // same frame stream (multiple-algorithm mode) - each frame is decoded only once per pass.
    std::vector<AlgorithmRun> algorithms;
};

struct AnalysisProgress {
    int pass = 0;
    int passCount = 1;
    int processed = 0;   // frames processed over all passes
    int total = 0;       // frames to process over all passes
    int frameIndex = 0;  // current dataset frame index
    std::string status;
};

struct AnalysisOutput {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<AlgorithmResult> results;
    double elapsedSeconds = 0.0;
};

// Sequential pipeline: read -> preprocess (ROI first) -> register -> algorithms.
// Only the current frame (plus whatever reference an algorithm/registration needs)
// is in memory at any time. Thread-agnostic: call run() from a worker thread.
class AnalysisEngine {
public:
    typedef std::function<void(const AnalysisProgress &)> ProgressCallback;

    AnalysisOutput run(const AnalysisConfig &config, const ProgressCallback &progress,
                       const std::atomic<bool> *cancel) const;

    // Reads one preprocessed frame (for previews / validation).
    static bool loadPreprocessedFrame(const AnalysisConfig &config, int index, cv::Mat &out, std::string *error);
};

std::string currentDateTimeIso();

} // namespace turb

#endif
