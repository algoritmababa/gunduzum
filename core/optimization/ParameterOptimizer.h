#ifndef TURB_PARAMETEROPTIMIZER_H
#define TURB_PARAMETEROPTIMIZER_H

#include "../AnalysisEngine.h"
#include "Separability.h"

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace turb {

// A labeled recording (image folder or video) of one class.
struct LabeledSource {
    std::string path;
    int label = 0;
    Roi roi;                 // full frame when empty
    double frameRate = 0.0;  // only used for timestamps
};

// One labeled sample = a window of consecutive frames of a source.
struct SampleWindow {
    int source = 0;
    int label = 0;
    int start = 0;
    int end = 0; // inclusive
};

struct OptimizationConfig {
    std::vector<std::string> classNames;      // e.g. LOW / MEDIUM / HIGH
    std::vector<LabeledSource> sources;
    PreprocessConfig preprocess;              // the ROI is taken from each source
    RegistrationConfig registration;

    // sampling: every source is cut into windows of windowLength frames
    int windowLength = 300;   // 0 = the whole recording is one sample
    int windowStride = 0;     // 0 = windowLength (non-overlapping)
    int maxWindowsPerSource = 10; // 0 = unlimited (windows are spread evenly over the recording)
    int frameStep = 1;

    std::vector<std::string> algorithmIds;              // candidate algorithms
    std::map<std::string, ParamSet> initialParams;      // starting point (defaults if missing)

    enum Objective { CvAccuracy = 0, FisherTop3 };
    Objective objective = CvAccuracy;
    bool useAllSummaryFeatures = false; // false: only each algorithm's feature-vector entries

    int refinementLevels = 2;   // recursion depth of the coarse-to-fine parameter search
    int gridPoints = 5;         // values per numeric parameter and level
    int maxEvaluations = 300;   // budget of distinct parameter sets evaluated on the data
    int threads = 0;            // 0 = hardware concurrency
    bool selectAlgorithms = true; // recursive algorithm elimination on the optimized algorithms
};

struct FeatureSeparation {
    std::string key;
    double fisher = 0.0;
    std::vector<double> classMean, classStd;
};

struct AlgorithmOutcome {
    std::string id;
    std::string name;
    ParamSet initialParams, bestParams;
    double initialScore = 0.0, bestScore = 0.0;
    double initialAccuracy = 0.0, bestAccuracy = 0.0;
    double bestFisherTop3 = 0.0;
    int evaluations = 0;
    std::vector<std::string> featureKeys;            // columns of sampleFeatures
    separability::Matrix sampleFeatures;             // samples x features of the best parameter set
    std::vector<FeatureSeparation> features;         // sorted by Fisher ratio, descending
    std::vector<std::vector<int> > confusion;        // CV confusion matrix of the best parameter set
    std::vector<std::string> trace;                  // search history (improvements)
    std::string bestParamText;
};

struct SelectionStep {
    std::vector<std::string> algorithms; // remaining set
    std::string removed;                 // algorithm removed to reach this set ("" for the first step)
    double score = 0.0;
    double accuracy = 0.0;
};

struct OptimizationReport {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::vector<SampleWindow> samples;
    std::vector<AlgorithmOutcome> algorithms; // sorted by best score, descending
    std::vector<SelectionStep> selection;
    int bestSelectionStep = -1;
    std::string validation;                   // description of the cross-validation scheme
    std::vector<std::string> notes;
    int evaluations = 0;
    double elapsedSeconds = 0.0;
};

struct OptimizationProgress {
    std::string stage;     // human readable
    int batch = 0;         // current data pass
    int samplesDone = 0;   // inside the current data pass
    int samplesTotal = 0;
    int evaluations = 0;   // distinct parameter sets evaluated so far
    int maxEvaluations = 0;
};

// Searches, for every candidate algorithm, the parameter set whose features separate the
// labeled classes best, then recursively eliminates algorithms from the combined feature set.
//
// Parameter search (per algorithm, all algorithms advance together):
//   level 0: coordinate-wise sweep over every tunable parameter: all values of Choice / Bool
//            parameters, gridPoints values over the search range of numeric parameters;
//   level n: the same sweep with numeric ranges shrunk around the current best value
//            (one grid step on each side) - recursive coarse-to-fine refinement.
// All candidate parameter sets of one step are evaluated in a single pass over the data
// (multiple-algorithm engine run), windows are processed in parallel.
class ParameterOptimizer {
public:
    typedef std::function<void(const OptimizationProgress &)> ProgressCallback;
    typedef std::function<void(const std::string &)> LogCallback;

    OptimizationReport run(const OptimizationConfig &config, const ProgressCallback &progress,
                           const LogCallback &log, const std::atomic<bool> *cancel);

    // Builds the sample windows (opens every source to read its frame count).
    static bool buildSamples(const OptimizationConfig &config, std::vector<SampleWindow> &samples,
                             std::string *error);

    static std::string describeParams(const std::string &algorithmId, const ParamSet &params);
};

} // namespace turb

#endif
