// Self-checking tests on synthetic image sequences with known ground truth.
// Usage: turbulence_tests [work_dir]   (default: ./turbulence_test_data)

#include "AlgorithmRegistry.h"
#include "AnalysisEngine.h"
#include "FrameSource.h"
#include "ResultExporter.h"
#include "Statistics.h"
#include "optimization/ParameterOptimizer.h"
#include "optimization/Separability.h"

#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

using namespace turb;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                                                         \
    do {                                                                                         \
        ++g_checks;                                                                              \
        if (!(cond)) {                                                                           \
            ++g_failures;                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << msg << "\n";           \
        }                                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol, msg) \
    CHECK(std::fabs(double(a) - double(b)) <= (tol), msg << " (" << (a) << " vs " << (b) << ")")

static void makeDir(const std::string &p)
{
#ifdef _WIN32
    _mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0755);
#endif
}

static std::string frameName(const std::string &dir, int i)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "/frame_%d.png", i); // no zero padding: exercises natural sort
    return dir + buf;
}

// Textured background + bright disk target; frame i is the base shifted by (dx[i], dy[i]),
// multiplied by gain[i] and blurred with sigma blur[i].
struct Truth {
    std::vector<double> dx, dy, gain, blur;
};

static cv::Mat makeBase(int size)
{
    cv::Mat noise(size, size, CV_32F);
    cv::RNG rng(12345);
    rng.fill(noise, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(noise, noise, cv::Size(0, 0), 1.0);
    cv::normalize(noise, noise, 40.0, 120.0, cv::NORM_MINMAX);
    cv::circle(noise, cv::Point(size / 2, size / 2), size / 8, cv::Scalar(230), -1, cv::LINE_AA);
    return noise;
}

static void writeSequence(const std::string &dir, const Truth &t, int size)
{
    makeDir(dir);
    const int pad = 16;
    const cv::Mat base = makeBase(size + 2 * pad);
    for (size_t i = 0; i < t.dx.size(); ++i) {
        const cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, t.dx[i], 0, 1, t.dy[i]);
        cv::Mat shifted, f;
        cv::warpAffine(base, shifted, M, base.size(), cv::INTER_CUBIC, cv::BORDER_REFLECT);
        f = shifted(cv::Rect(pad, pad, size, size)).clone();
        if (t.blur[i] > 0)
            cv::GaussianBlur(f, f, cv::Size(0, 0), t.blur[i]);
        f *= t.gain[i];
        cv::Mat u16;
        f.convertTo(u16, CV_16U, 256.0); // 16-bit to keep sub-pixel information
        cv::imwrite(frameName(dir, int(i)), u16);
    }
}

static AnalysisConfig baseConfig(const std::string &dir, const std::string &alg)
{
    AnalysisConfig c;
    c.dataset.path = dir;
    c.dataset.frameRate = 100.0;
    AlgorithmRun r;
    r.id = alg;
    c.algorithms.push_back(r);
    return c;
}

static AlgorithmResult runOne(const AnalysisConfig &c)
{
    AnalysisEngine e;
    AnalysisOutput o = e.run(c, AnalysisEngine::ProgressCallback(), nullptr);
    CHECK(o.ok, "engine run failed: " << o.error);
    if (!o.ok || o.results.empty())
        return AlgorithmResult();
    return o.results.front();
}

static double feat(const AlgorithmResult &r, const std::string &k)
{
    const Feature *f = r.feature(k);
    CHECK(f != nullptr, "missing feature " << k);
    return f ? f->value : stats::nan();
}

static int col(const AlgorithmResult &r, const std::string &name)
{
    for (size_t i = 0; i < r.frameColumns.size(); ++i)
        if (r.frameColumns[i] == name)
            return int(i);
    return -1;
}

static void testStatistics()
{
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i)
        v.push_back(i);
    const SeriesStats s = stats::summarize(v);
    CHECK_NEAR(s.mean, 50.5, 1e-12, "mean");
    CHECK_NEAR(s.variance, 833.25, 1e-9, "population variance");
    CHECK_NEAR(s.p95, 95.05, 1e-9, "p95");
    CHECK_NEAR(s.min, 1, 0, "min");
    CHECK_NEAR(s.max, 100, 0, "max");
    CHECK_NEAR(s.rms, std::sqrt(338350.0 / 100.0), 1e-9, "rms");
    v.push_back(stats::nan());
    CHECK(stats::summarize(v).count == 100, "NaN ignored");
    CHECK(std::isnan(stats::summarize(std::vector<double>()).mean), "empty -> NaN");

    CHECK(naturalLess("frame_2.png", "frame_10.png"), "natural sort");
    CHECK(!naturalLess("frame_10.png", "frame_2.png"), "natural sort reverse");

    PixelwiseRunningStats p;
    cv::Mat a(2, 2, CV_32F, cv::Scalar(1)), b(2, 2, CV_32F, cv::Scalar(3));
    p.add(a);
    p.add(b);
    CHECK_NEAR(p.variance().at<double>(0, 0), 1.0, 1e-12, "pixel variance");
}


static void testSeparability()
{
    using namespace separability;
    // perfectly separated feature vs. pure noise
    const std::vector<int> y = {0, 0, 0, 1, 1, 1, 2, 2, 2};
    const std::vector<double> good = {1, 1.1, 0.9, 5, 5.1, 4.9, 9, 9.1, 8.9};
    const std::vector<double> bad = {1, 5, 9, 1, 5, 9, 1, 5, 9};
    CHECK(fisherRatio(good, y, 3) > 100.0, "fisher of separated feature " << fisherRatio(good, y, 3));
    CHECK_NEAR(fisherRatio(bad, y, 3), 0.0, 1e-12, "fisher of non-separating feature");
    Matrix X;
    for (size_t i = 0; i < y.size(); ++i)
        X.push_back({good[i], bad[i], 3.0 /* constant -> unusable */});
    const std::vector<size_t> cols = usableColumns(X);
    CHECK(cols.size() == 2, "constant column dropped");
    std::vector<int> groups;
    for (size_t i = 0; i < y.size(); ++i)
        groups.push_back(int(i));
    CHECK_NEAR(nearestCentroidCv(X, y, groups, 3, std::vector<size_t>{0}).balancedAccuracy, 1.0, 1e-12,
               "cv accuracy of separated feature");
    CHECK(nearestCentroidCv(X, y, groups, 3, std::vector<size_t>{1}).balancedAccuracy < 0.5,
          "cv accuracy of noise feature");
}

// 3 classes x 2 recordings; the classes differ in jitter amplitude and blur.
static void testOptimizer(const std::string &work)
{
    const double jitter[3] = {0.15, 0.7, 1.6};
    const double blur[3] = {0.0, 0.8, 1.6};
    cv::RNG rng(99);
    OptimizationConfig oc;
    oc.classNames = {"LOW", "MEDIUM", "HIGH"};
    for (int k = 0; k < 3; ++k)
        for (int rec = 0; rec < 2; ++rec) {
            Truth t;
            for (int i = 0; i < 48; ++i) {
                t.dx.push_back(rng.gaussian(jitter[k]));
                t.dy.push_back(rng.gaussian(jitter[k]));
                t.gain.push_back(1.0 + rng.gaussian(0.01));
                t.blur.push_back(blur[k]);
            }
            const std::string dir = work + "/opt_c" + std::to_string(k) + "_r" + std::to_string(rec);
            writeSequence(dir, t, 64);
            LabeledSource src;
            src.path = dir;
            src.label = k;
            oc.sources.push_back(src);
        }
    oc.windowLength = 16;
    oc.maxWindowsPerSource = 3;
    oc.algorithmIds = {"phase_correlation", "laplacian_variance", "temporal_intensity_variance"};
    oc.refinementLevels = 2;
    oc.gridPoints = 3;
    oc.maxEvaluations = 60;
    oc.threads = 2;

    std::vector<std::string> logLines;
    int progressCalls = 0;
    ParameterOptimizer opt;
    const OptimizationReport rep = opt.run(
        oc, [&](const OptimizationProgress &) { ++progressCalls; },
        [&](const std::string &l) { logLines.push_back(l); }, nullptr);
    CHECK(rep.ok, "optimizer run: " << rep.error);
    if (!rep.ok)
        return;
    CHECK(rep.samples.size() == 18, "sample windows " << rep.samples.size());
    CHECK(rep.validation.find("recording-out") != std::string::npos, "grouped validation");
    CHECK(rep.algorithms.size() == 3, "outcomes");
    CHECK(progressCalls > 0 && !logLines.empty(), "progress / log callbacks");
    CHECK(rep.evaluations <= oc.maxEvaluations + 20, "evaluation budget " << rep.evaluations);
    for (const AlgorithmOutcome &o : rep.algorithms) {
        CHECK(o.bestScore >= o.initialScore - 1e-12, "search never gets worse " << o.id);
        CHECK(o.sampleFeatures.size() == rep.samples.size(), "feature rows " << o.id);
        std::cout << "  optimizer: " << o.id << " accuracy " << o.initialAccuracy << " -> " << o.bestAccuracy
                  << " (" << o.evaluations << " evals)  " << o.bestParamText << "\n";
    }
    const AlgorithmOutcome &top = rep.algorithms.front();
    CHECK(top.bestAccuracy >= 0.9, "best algorithm separates the classes " << top.id << " " << top.bestAccuracy);
    CHECK(rep.selection.size() == 3 && rep.bestSelectionStep >= 0, "recursive elimination steps");
    CHECK(rep.selection.back().algorithms.size() == 1, "elimination ends with one algorithm");

    // cancellation
    std::atomic<bool> cancel(true);
    const OptimizationReport rc = opt.run(oc, ParameterOptimizer::ProgressCallback(),
                                          ParameterOptimizer::LogCallback(), &cancel);
    CHECK(!rc.ok && rc.cancelled, "optimizer cancellation");

    // a class with a single short sample is rejected
    OptimizationConfig bad = oc;
    bad.sources.resize(5);
    bad.windowLength = 0;
    CHECK(!opt.run(bad, ParameterOptimizer::ProgressCallback(), ParameterOptimizer::LogCallback(), nullptr).ok,
          "too few samples rejected");
}

int main(int argc, char **argv)
{
    const std::string work = argc > 1 ? argv[1] : "turbulence_test_data";
    makeDir(work);
    testStatistics();

    const int N = 40, size = 128;
    cv::RNG rng(7);

    // --- motion dataset: known sub-pixel shifts, constant gain, no blur ---------------------
    Truth motion;
    for (int i = 0; i < N; ++i) {
        motion.dx.push_back(i == 0 ? 0.0 : rng.uniform(-2.0, 2.0));
        motion.dy.push_back(i == 0 ? 0.0 : rng.uniform(-2.0, 2.0));
        motion.gain.push_back(1.0);
        motion.blur.push_back(0.0);
    }
    const std::string motionDir = work + "/motion";
    writeSequence(motionDir, motion, size);

    {   // source ordering / count
        std::string err;
        std::unique_ptr<FrameSource> src = FrameSource::create(motionDir, &err);
        CHECK(src && src->frameCount() == N, "frame count " << err);
        ImageSequenceSource *seq = dynamic_cast<ImageSequenceSource *>(src.get());
        CHECK(seq && seq->files()[10].find("frame_10.png") != std::string::npos, "natural order");
    }

    {   // Phase correlation, first-frame reference: dx/dy follow the known shift
        AlgorithmResult r = runOne(baseConfig(motionDir, "phase_correlation"));
        const int cdx = col(r, "dx"), cdy = col(r, "dy");
        CHECK(r.frameRows.size() == size_t(N - 1), "phase rows " << r.frameRows.size());
        double maxErr = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const int i = int(row[0]);
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdx)] - motion.dx[size_t(i)]));
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdy)] - motion.dy[size_t(i)]));
        }
        // cv::phaseCorrelate's 5x5 weighted-centroid peak estimate with a Hanning window is biased
        // towards zero for whole-ROI shifts; a few tenths of a pixel is the expected accuracy.
        CHECK(maxErr < 0.5, "phase correlation sub-pixel error " << maxErr);
        std::cout << "phase correlation (hanning) max error: " << maxErr << " px\n";
        CHECK(r.featureVector.size() == 11, "phase feature vector size");
        CHECK_NEAR(r.frameRows[0][1], 0.01, 1e-12, "timestamp from frame rate");
    }

    {   // without window the sub-pixel estimate is much less biased on this data
        AnalysisConfig c = baseConfig(motionDir, "phase_correlation");
        c.algorithms[0].params.set("hanning", 0);
        AlgorithmResult r = runOne(c);
        const int cdx = col(r, "dx"), cdy = col(r, "dy");
        double maxErr = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const size_t i = size_t(row[0]);
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdx)] - motion.dx[i]));
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdy)] - motion.dy[i]));
        }
        std::cout << "phase correlation (no window) max error: " << maxErr << " px\n";
        CHECK(maxErr < 0.3, "phase correlation without window " << maxErr);
    }

    {   // Phase correlation with previous / average / fixed reference
        AnalysisConfig c = baseConfig(motionDir, "phase_correlation");
        c.algorithms[0].params.set("reference", 1);
        AlgorithmResult r = runOne(c);
        const int cdx = col(r, "dx");
        double maxErr = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const size_t i = size_t(row[0]);
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdx)] - (motion.dx[i] - motion.dx[i - 1])));
        }
        CHECK(maxErr < 0.5, "previous-frame displacement error " << maxErr);

        c.algorithms[0].params.set("reference", 3); // average
        r = runOne(c);
        CHECK(r.frameRows.size() == size_t(N), "average reference rows");
        CHECK(feat(r, "rms_displacement") > 0.5, "average reference displacement");

        c.algorithms[0].params.set("reference", 2); // fixed #5
        c.algorithms[0].params.set("reference_index", 5);
        r = runOne(c);
        CHECK(r.frameRows.size() == size_t(N - 1), "fixed reference skips the reference frame");
        const int cdx2 = col(r, "dx");
        maxErr = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const size_t i = size_t(row[0]);
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdx2)] - (motion.dx[i] - motion.dx[5])));
        }
        CHECK(maxErr < 0.5, "fixed-frame displacement error " << maxErr);
    }

    {   // Registration (translation) removes the global shift
        AnalysisConfig c = baseConfig(motionDir, "phase_correlation");
        c.registration.mode = RegistrationConfig::Translation;
        AlgorithmResult r = runOne(c);
        CHECK(feat(r, "rms_displacement") < 0.1, "registered rms displacement " << feat(r, "rms_displacement"));
        c.registration.mode = RegistrationConfig::Affine;
        r = runOne(c);
        CHECK(feat(r, "rms_displacement") < 0.15, "affine registered rms displacement " << feat(r, "rms_displacement"));
    }

    {   // Centroid jitter follows the target shift
        AnalysisConfig c = baseConfig(motionDir, "centroid_jitter");
        c.algorithms[0].params.set("reference", 0); // first valid frame
        c.algorithms[0].params.set("weighting", 1); // binary-mask centroids are pixel-locked
        AlgorithmResult r = runOne(c);
        CHECK_NEAR(feat(r, "valid_detection_ratio"), 1.0, 1e-12, "centroid detection ratio");
        const int cdx = col(r, "delta_x"), cdy = col(r, "delta_y");
        double maxErr = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const size_t i = size_t(row[0]);
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdx)] - motion.dx[i]));
            maxErr = std::max(maxErr, std::fabs(row[size_t(cdy)] - motion.dy[i]));
        }
        CHECK(maxErr < 0.1, "centroid error " << maxErr);
        std::cout << "centroid (intensity weighted) max error: " << maxErr << " px\n";
        CHECK_NEAR(feat(r, "mean_centroid_x"), size / 2 + stats::mean(motion.dx), 0.3, "centroid x in image coords");

        c.algorithms[0].params.set("segmentation", 0);
        c.algorithms[0].params.set("threshold", 180.0 * 256.0);
        c.algorithms[0].params.set("weighting", 1);
        r = runOne(c);
        CHECK_NEAR(feat(r, "valid_detection_ratio"), 1.0, 1e-12, "binary threshold detection");
        c.algorithms[0].params.set("segmentation", 2);
        c.algorithms[0].params.set("adaptive_block", 61);
        c.algorithms[0].params.set("adaptive_c", -20);
        r = runOne(c);
        CHECK(feat(r, "valid_detection_ratio") > 0.9, "adaptive threshold detection");

        // ROI offset: centroid stays in source coordinates
        c = baseConfig(motionDir, "centroid_jitter");
        c.preprocess.roi.x = 20;
        c.preprocess.roi.y = 30;
        c.preprocess.roi.width = 90;
        c.preprocess.roi.height = 90;
        r = runOne(c);
        CHECK_NEAR(feat(r, "mean_centroid_x"), size / 2 + stats::mean(motion.dx), 0.3, "centroid with ROI offset");
        CHECK(r.roi.x == 20 && r.roi.width == 90, "roi in result");
    }

    {   // Optical flow, first-frame reference: per-frame mean flow == shift
        AnalysisConfig c = baseConfig(motionDir, "optical_flow");
        c.algorithms[0].params.set("reference", 1);
        AlgorithmResult r = runOne(c);
        const int cfx = col(r, "mean_flow_x"), cfy = col(r, "mean_flow_y");
        double maxErr = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const size_t i = size_t(row[0]);
            maxErr = std::max(maxErr, std::fabs(row[size_t(cfx)] - motion.dx[i]));
            maxErr = std::max(maxErr, std::fabs(row[size_t(cfy)] - motion.dy[i]));
        }
        CHECK(maxErr < 0.25, "optical flow error " << maxErr);
        CHECK(feat(r, "valid_vector_ratio") > 0.5, "valid vector ratio " << feat(r, "valid_vector_ratio"));

        c.algorithms[0].params.set("reference", 0);
        c.algorithms[0].params.set("remove_global_motion", 1);
        r = runOne(c);
        CHECK(feat(r, "mean_flow") < 0.3, "global motion removed " << feat(r, "mean_flow"));
    }

    // --- intensity / blur dataset: no motion, known gain and blur ----------------------------
    Truth ib;
    for (int i = 0; i < N; ++i) {
        ib.dx.push_back(0.0);
        ib.dy.push_back(0.0);
        ib.gain.push_back(1.0 + 0.1 * std::sin(i * 0.7));
        ib.blur.push_back(i < N / 2 ? 0.0 : 2.0);
    }
    const std::string ibDir = work + "/intensity_blur";
    writeSequence(ibDir, ib, size);

    {   // Temporal intensity: I(t) proportional to gain
        AlgorithmResult r = runOne(baseConfig(ibDir, "temporal_intensity_variance"));
        const int ci = col(r, "mean_intensity");
        const double i0 = r.frameRows[0][size_t(ci)] / ib.gain[0];
        double maxRel = 0;
        for (const std::vector<double> &row : r.frameRows) {
            const size_t i = size_t(row[0]);
            maxRel = std::max(maxRel, std::fabs(row[size_t(ci)] / (i0 * ib.gain[i]) - 1.0));
        }
        CHECK(maxRel < 0.01, "intensity follows gain " << maxRel);
        std::vector<double> I;
        for (const std::vector<double> &row : r.frameRows)
            I.push_back(row[size_t(ci)]);
        const SeriesStats s = stats::summarize(I);
        CHECK_NEAR(feat(r, "intensity_variance"), s.variance, 1e-6 * s.variance, "intensity variance");
        CHECK_NEAR(feat(r, "rms_fluctuation"), s.stddev, 1e-6 * s.stddev, "rms fluctuation without detrend");
        CHECK(feat(r, "mean_pixel_temporal_variance") > 0, "pixel temporal variance");
        CHECK(r.featureVector.size() == 9, "intensity feature vector size");

        // Scintillation: SI = var / mean^2
        AlgorithmResult sci = runOne(baseConfig(ibDir, "scintillation_index"));
        CHECK_NEAR(feat(sci, "scintillation_index"), s.variance / (s.mean * s.mean), 1e-9, "scintillation index");
        CHECK_NEAR(feat(sci, "rms_normalized_fluctuation"), s.stddev / s.mean, 1e-9, "rms normalized fluctuation");

        // per-frame z-score normalization -> mean 0 -> guarded division
        AnalysisConfig c = baseConfig(ibDir, "scintillation_index");
        c.preprocess.normalization = PreprocessConfig::NormZScoreFrame;
        c.algorithms[0].params.set("min_mean", 1e-3);
        sci = runOne(c);
        CHECK(std::isnan(feat(sci, "scintillation_index")), "division by zero guarded");
        CHECK(!sci.notes.empty(), "notes on guarded division");
    }

    {   // Laplacian: blurred half is less sharp
        AlgorithmResult r = runOne(baseConfig(ibDir, "laplacian_variance"));
        const int c = col(r, "laplacian_variance");
        double sharp = 0, blurred = 0;
        for (const std::vector<double> &row : r.frameRows)
            (int(row[0]) < N / 2 ? sharp : blurred) += row[size_t(c)] / ib.gain[size_t(row[0])] / ib.gain[size_t(row[0])];
        CHECK(sharp > 3 * blurred, "laplacian variance drops with blur " << sharp << " " << blurred);

        AlgorithmResult g = runOne(baseConfig(ibDir, "gradient_magnitude_variance"));
        const int cg = col(g, "mean_gradient");
        CHECK(g.frameRows[0][size_t(cg)] > g.frameRows[N - 1][size_t(cg)], "gradient drops with blur");
        CHECK(g.featureVector.size() == 8, "gradient feature vector size");
    }

    {   // Structure analysis: identical frames -> zero difference
        Truth still;
        for (int i = 0; i < 10; ++i) {
            still.dx.push_back(0);
            still.dy.push_back(0);
            still.gain.push_back(1);
            still.blur.push_back(0);
        }
        const std::string stillDir = work + "/still";
        writeSequence(stillDir, still, 64);
        AlgorithmResult r = runOne(baseConfig(stillDir, "structure_analysis"));
        CHECK_NEAR(feat(r, "mean_absolute_difference"), 0.0, 1e-9, "MAD of identical frames");
        CHECK_NEAR(feat(r, "temporal_variance"), 0.0, 1e-6, "temporal variance of identical frames");

        r = runOne(baseConfig(motionDir, "structure_analysis"));
        CHECK(feat(r, "mean_absolute_difference") > 1.0, "MAD with motion");
        CHECK(feat(r, "mean_correlation") < 1.0, "correlation with motion");
        AnalysisConfig c = baseConfig(motionDir, "structure_analysis");
        c.algorithms[0].params.set("reference", 3);
        r = runOne(c);
        CHECK(r.frameRows.size() == size_t(N), "structure average reference");
    }

    {   // Multiple-algorithm mode: every algorithm independently on the same stream
        AnalysisConfig c = baseConfig(motionDir, "temporal_intensity_variance");
        c.algorithms.clear();
        for (const AlgorithmRegistry::Entry &e : AlgorithmRegistry::instance().entries()) {
            AlgorithmRun run;
            run.id = e.id;
            c.algorithms.push_back(run);
        }
        c.dataset.startFrame = 2;
        c.dataset.endFrame = 30;
        c.dataset.frameStep = 2;
        c.preprocess.denoise = PreprocessConfig::DenoiseMedian;
        c.preprocess.contrast = PreprocessConfig::ContrastClahe;
        c.preprocess.normalization = PreprocessConfig::NormFullScale;
        AnalysisEngine e;
        AnalysisOutput o = e.run(c, AnalysisEngine::ProgressCallback(), nullptr);
        CHECK(o.ok && o.results.size() == 8, "multi-algorithm run " << o.error);
        for (const AlgorithmResult &r : o.results) {
            CHECK(!r.features.empty(), "features for " << r.algorithmId);
            CHECK(r.frameColumns.size() >= 3 && r.frameColumns[0] == "frame", "frame columns " << r.algorithmId);
            for (const std::vector<double> &row : r.frameRows)
                CHECK(row.size() == r.frameColumns.size(), "row width " << r.algorithmId);
        }
        CHECK(o.results[0].frameRows.size() == 15, "frame range/step " << o.results[0].frameRows.size());

        // exports
        std::string err;
        CHECK(exporter::writeFrameCsv(o.results[0], work + "/frames.csv", &err), err);
        CHECK(exporter::writeSummaryCsv(o.results[0], work + "/summary.csv", &err), err);
        CHECK(exporter::writeJson(o.results, work + "/all.json", &err), err);
        std::vector<LabeledResult> runs;
        for (const AlgorithmResult &r : o.results)
            runs.push_back({r.algorithmId, r});
        CHECK(exporter::writeComparisonCsv(runs, work + "/comparison.csv", &err), err);
        CHECK(exporter::writeFeatureMatrixCsv(runs, false, work + "/matrix.csv", &err), err);
    }

    {   // cancellation
        std::atomic<bool> cancel(true);
        AnalysisEngine e;
        AnalysisOutput o = e.run(baseConfig(motionDir, "laplacian_variance"), AnalysisEngine::ProgressCallback(), &cancel);
        CHECK(!o.ok && o.cancelled, "cancellation");
    }

    testSeparability();
    testOptimizer(work);

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
