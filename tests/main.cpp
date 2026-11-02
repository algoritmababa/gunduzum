// Self-checking tests on synthetic image sequences with known ground truth.
// Usage: turbulence_tests [work_dir]

#include "FrameSource.h"
#include "Measurement.h"
#include "ResultTable.h"
#include "Stats.h"
#include "TrendAnalysis.h"

#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

using namespace turb;

static int g_checks = 0, g_failures = 0;

#define CHECK(cond, msg)                                                                  \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            ++g_failures;                                                                 \
            std::cerr << "FAIL line " << __LINE__ << ": " << msg << "\n";                 \
        }                                                                                 \
    } while (0)
#define CHECK_NEAR(a, b, tol, msg) CHECK(std::fabs(double(a) - double(b)) <= (tol), msg << " (" << (a) << " vs " << (b) << ")")

static void makeDir(const std::string &p)
{
#ifdef _WIN32
    _mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0755);
#endif
}

// Textured scene + bright disk; frame i = scene shifted by (dx, dy), blurred, times gain.
struct Truth {
    std::vector<double> dx, dy, gain, blur;
};

static void writeSequence(const std::string &dir, const Truth &t, int size)
{
    makeDir(dir);
    const int pad = 16;
    cv::Mat base(size + 2 * pad, size + 2 * pad, CV_32F);
    cv::RNG rng(12345);
    rng.fill(base, cv::RNG::UNIFORM, 0.0, 1.0);
    cv::GaussianBlur(base, base, cv::Size(0, 0), 1.0);
    cv::normalize(base, base, 40.0, 120.0, cv::NORM_MINMAX);
    cv::circle(base, cv::Point(base.cols / 2, base.rows / 2), size / 8, cv::Scalar(230), -1, cv::LINE_AA);
    for (size_t i = 0; i < t.dx.size(); ++i) {
        const cv::Mat M = (cv::Mat_<double>(2, 3) << 1, 0, t.dx[i], 0, 1, t.dy[i]);
        cv::Mat shifted, f, u16;
        cv::warpAffine(base, shifted, M, base.size(), cv::INTER_CUBIC, cv::BORDER_REFLECT);
        f = shifted(cv::Rect(pad, pad, size, size)).clone();
        if (t.blur[i] > 0)
            cv::GaussianBlur(f, f, cv::Size(0, 0), t.blur[i]);
        f *= t.gain[i];
        f.convertTo(u16, CV_16U, 256.0);
        char name[64];
        std::snprintf(name, sizeof(name), "/frame_%d.png", int(i)); // unpadded: tests natural sort
        cv::imwrite(dir + name, u16);
    }
}

static Truth makeTruth(int n, double jitter, double blur, double gainNoise, unsigned seed, bool wholePixels = false)
{
    cv::RNG rng(seed);
    Truth t;
    for (int i = 0; i < n; ++i) {
        // whole-pixel shifts avoid the slight smoothing of sub-pixel interpolation
        t.dx.push_back(wholePixels ? std::round(rng.gaussian(jitter)) : rng.gaussian(jitter));
        t.dy.push_back(wholePixels ? std::round(rng.gaussian(jitter)) : rng.gaussian(jitter));
        t.gain.push_back(1.0 + rng.gaussian(gainNoise));
        t.blur.push_back(blur);
    }
    return t;
}

static double feature(const WindowResult &w, const std::string &name)
{
    for (const auto &kv : w.features)
        if (kv.first == name)
            return kv.second;
    return nan();
}

// One effect changes along LOW -> MEDIUM -> HIGH, everything else stays the same.
// The analysis must call the algorithms sensitive to that effect useful - and only those.
static TrendReport runScenario(const std::string &work, const std::string &name, const double jit[3],
                               const double blur[3], const double flicker[3], bool wholePixels)
{
    const std::string results = work + "/scenario_" + name + ".csv";
    std::remove(results.c_str());
    const char *cats[3] = {"LOW", "MEDIUM", "HIGH"};
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 3; ++r) {
            const std::string dir = work + "/sc_" + name + "_" + cats[k] + "_" + std::to_string(r);
            writeSequence(dir, makeTruth(32, jit[k], blur[k], flicker[k], unsigned(500 + 10 * k + r), wholePixels), 64);
            Recording rec;
            rec.path = dir;
            rec.category = cats[k];
            MeasureSettings ms;
            ms.windowLength = 16;
            std::string err;
            const MeasureOutput out = measure(rec, ms, ProgressFn(), nullptr);
            CHECK(out.ok && appendResults(results, rec, out, &err), name << " " << err);
        }
    Table t;
    std::string err;
    loadTable(results, t, &err);
    const TrendReport rep = analyzeTrends(t, TrendOptions());
    std::printf("  scenario %-8s", name.c_str());
    for (const AlgorithmTrend &a : rep.algorithms)
        std::printf(" %s=%s", a.metric.c_str(), algorithmVerdictName(a.verdict));
    std::printf("\n");
    return rep;
}

static AlgorithmVerdict algorithmVerdict(const TrendReport &rep, const std::string &metric)
{
    for (const AlgorithmTrend &a : rep.algorithms)
        if (a.metric == metric)
            return a.verdict;
    return AlgorithmVerdict::InsufficientData;
}

static void testScenarios(const std::string &work)
{
    const std::vector<std::string> motion = {"jitter", "centroid", "flow"};
    const std::vector<std::string> blurMetrics = {"sharpness", "gradient"};
    {
        const double jit[3] = {0.3, 1.2, 2.5}, blur[3] = {0, 0, 0}, fl[3] = {0.005, 0.005, 0.005};
        const TrendReport r = runScenario(work, "jitter", jit, blur, fl, true);
        for (const std::string &m : motion)
            CHECK(algorithmVerdict(r, m) == AlgorithmVerdict::Useful, "jitter scenario: " << m << " useful");
        CHECK(algorithmVerdict(r, "change") == AlgorithmVerdict::Useful, "jitter scenario: change useful");
        for (const std::string &m : {std::string("sharpness"), std::string("gradient"), std::string("intensity")})
            CHECK(algorithmVerdict(r, m) != AlgorithmVerdict::Useful, "jitter scenario: " << m << " not useful");
    }
    {
        const double jit[3] = {0, 0, 0}, blur[3] = {0.0, 0.9, 1.8}, fl[3] = {0.005, 0.005, 0.005};
        const TrendReport r = runScenario(work, "blur", jit, blur, fl, true);
        for (const std::string &m : blurMetrics)
            CHECK(algorithmVerdict(r, m) == AlgorithmVerdict::Useful, "blur scenario: " << m << " useful");
        for (const std::string &m : {std::string("jitter"), std::string("centroid"), std::string("flow"),
                                     std::string("intensity")})
            CHECK(algorithmVerdict(r, m) != AlgorithmVerdict::Useful, "blur scenario: " << m << " not useful");
    }
    {
        const double jit[3] = {0, 0, 0}, blur[3] = {0, 0, 0}, fl[3] = {0.002, 0.01, 0.04};
        const TrendReport r = runScenario(work, "flicker", jit, blur, fl, true);
        CHECK(algorithmVerdict(r, "intensity") == AlgorithmVerdict::Useful, "flicker scenario: intensity useful");
        CHECK(algorithmVerdict(r, "change") == AlgorithmVerdict::Useful, "flicker scenario: change useful");
        for (const std::string &m : {std::string("jitter"), std::string("centroid"), std::string("sharpness"),
                                     std::string("gradient")})
            CHECK(algorithmVerdict(r, m) != AlgorithmVerdict::Useful, "flicker scenario: " << m << " not useful");
    }
}

int main(int argc, char **argv)
{
    const std::string work = argc > 1 ? argv[1] : "turbulence_test_data";
    makeDir(work);

    // --- statistics ---------------------------------------------------------------------
    {
        std::vector<double> v;
        for (int i = 1; i <= 100; ++i)
            v.push_back(i);
        v.push_back(nan());
        const Summary s = summarize(v);
        CHECK(s.count == 100, "NaN ignored");
        CHECK_NEAR(s.mean, 50.5, 1e-12, "mean");
        CHECK_NEAR(s.std, std::sqrt(833.25), 1e-9, "population std");
        CHECK_NEAR(s.p95, 95.05, 1e-9, "p95");
        CHECK(naturalLess("frame_2.png", "frame_10.png"), "natural sort");
        CHECK(parseCsvLine("a,\"b,\"\"c\"\"\",d") == std::vector<std::string>({"a", "b,\"c\"", "d"}), "csv parse");
    }

    // --- jitter: frame-to-frame displacement follows the known shifts ------------------------
    const int N = 40;
    const Truth motion = makeTruth(N, 1.0, 0.0, 0.0, 7);
    const std::string motionDir = work + "/motion";
    writeSequence(motionDir, motion, 128);
    {
        Recording rec;
        rec.path = motionDir;
        const std::string frames = work + "/frames.csv";
        MeasureSettings ms;
        ms.frameCsvPath = frames;
        const MeasureOutput out = measure(rec, ms, ProgressFn(), nullptr);
        CHECK(out.ok && out.windows.size() == 1, "whole recording = one window " << out.error);
        CHECK(out.windows[0].frames == N, "all frames processed");

        // compare per-frame dx/dy with the truth (Hanning window: a few tenths of a pixel)
        std::ifstream in(frames.c_str());
        std::string line;
        std::getline(in, line);
        const std::vector<std::string> head = parseCsvLine(line);
        size_t frameCols = 2;
        for (const std::unique_ptr<Metric> &m : createMetrics())
            frameCols += m->frameColumns().size();
        CHECK(head.size() == frameCols, "frame csv columns " << line);
        double maxErr = 0.0;
        int rows = 0;
        while (std::getline(in, line)) {
            const std::vector<std::string> f = parseCsvLine(line);
            const int i = std::atoi(f[1].c_str());
            ++rows;
            if (i == 0)
                continue;
            const double dx = std::atof(f[3].c_str()), dy = std::atof(f[4].c_str());
            maxErr = std::max(maxErr, std::fabs(dx - (motion.dx[size_t(i)] - motion.dx[size_t(i) - 1])));
            maxErr = std::max(maxErr, std::fabs(dy - (motion.dy[size_t(i)] - motion.dy[size_t(i) - 1])));
        }
        CHECK(rows == N, "frame csv rows " << rows);
        CHECK(maxErr < 0.6, "frame-to-frame displacement error " << maxErr); // Hanning bias: a few tenths of a px
        std::cout << "jitter: max frame-to-frame error " << maxErr << " px\n";

        // ROI is clamped and used
        rec.roi.x = 100;
        rec.roi.y = 100;
        rec.roi.width = 100;
        rec.roi.height = 100;
        const MeasureOutput roiOut = measure(rec, MeasureSettings(), ProgressFn(), nullptr);
        CHECK(roiOut.ok && roiOut.roi == cv::Rect(100, 100, 28, 28), "ROI clamped to the image");

        // windows: 40 frames / 16 -> 2 windows, remainder dropped
        rec.roi = Roi();
        MeasureSettings w16;
        w16.windowLength = 16;
        const MeasureOutput wo = measure(rec, w16, ProgressFn(), nullptr);
        CHECK(wo.ok && wo.windows.size() == 2, "two windows of 16 frames");
        CHECK(wo.windows[1].startFrame == 16 && wo.windows[1].endFrame == 31, "second window range");

        // optional frame view: called once per measured frame with the raw frame, in order
        {
            std::vector<int> seen;
            cv::Size frameSize;
            const MeasureOutput fo = measure(rec, w16, ProgressFn(), nullptr, [&](int index, const cv::Mat &raw) {
                seen.push_back(index);
                frameSize = raw.size();
            });
            CHECK(fo.ok && seen.size() == 32 && seen.front() == 0 && seen.back() == 31, "frame view called per frame");
            CHECK(frameSize == cv::Size(128, 128), "frame view gets the full raw frame");
        }

        // cancellation
        std::atomic<bool> stop(true);
        const MeasureOutput c = measure(rec, MeasureSettings(), ProgressFn(), &stop);
        CHECK(!c.ok && c.cancelled, "cancel");
    }

    // --- three categories: the metrics respond to the effect they are meant for ----------
    // LOW / MEDIUM / HIGH differ in jitter amplitude and blur; HIGH also flickers.
    const std::string results = work + "/results.csv";
    std::remove(results.c_str());
    const char *cats[3] = {"LOW", "MEDIUM", "HIGH"};
    const double jit[3] = {0.2, 0.8, 1.6}, blur[3] = {0.0, 0.8, 1.6}, flicker[3] = {0.002, 0.01, 0.04};
    for (int k = 0; k < 3; ++k)
        for (int r = 0; r < 3; ++r) {
            const std::string dir = work + "/" + cats[k] + "_" + std::to_string(r);
            writeSequence(dir, makeTruth(32, jit[k], blur[k], flicker[k], unsigned(100 + 10 * k + r)), 64);
            Recording rec;
            rec.path = dir;
            rec.category = cats[k];
            rec.note = "synthetic, run " + std::to_string(r) + ", \"quoted\"";
            MeasureSettings ms;
            ms.windowLength = 16;
            const MeasureOutput out = measure(rec, ms, ProgressFn(), nullptr);
            std::string err;
            CHECK(out.ok && appendResults(results, rec, out, &err), "append " << err);
            const WindowResult &w = out.windows.front();
            const double si = feature(w, "scintillation_index");
            CHECK(std::isfinite(si) && si > 0, "scintillation index " << si);
            // SI is Var/Mean^2 of the intensity series
            CHECK_NEAR(si, std::pow(feature(w, "intensity_std") / feature(w, "intensity_mean"), 2), 1e-9 * (1 + si),
                       "SI formula");
        }

    Table t;
    std::string err;
    CHECK(loadTable(results, t, &err), err);
    CHECK(t.header == resultColumns(), "header");
    CHECK(t.rows.size() == 18, "18 windows appended " << t.rows.size());
    CHECK(t.rows[0][size_t(t.column("note"))] == "synthetic, run 0, \"quoted\"", "note round trip");

    // --- trend analysis ---------------------------------------------------------------------
    {
        // helpers
        const std::vector<double> r = averageRanks({10, 20, 20, 5});
        CHECK(r == std::vector<double>({2, 3.5, 3.5, 1}), "average ranks with ties");
        CHECK_NEAR(spearman({1, 2, 3, 4}, {10, 20, 30, 40}), 1.0, 1e-12, "spearman +1");
        CHECK_NEAR(spearman({1, 2, 3, 4}, {4, 3, 2, 1}), -1.0, 1e-12, "spearman -1");
        const std::vector<double> h = holmAdjust({0.01, 0.04, 0.03});
        CHECK_NEAR(h[0], 0.03, 1e-12, "holm smallest");
        CHECK_NEAR(h[2], 0.06, 1e-12, "holm middle");
        CHECK_NEAR(h[1], 0.06, 1e-12, "holm monotone");
    }
    {
        // 3 recordings per category: the effects the data were built with must be found,
        // a measurement without a built-in difference must not
        const TrendReport rep = analyzeTrends(t, TrendOptions());
        CHECK(rep.ok, rep.error);
        CHECK(rep.units == 9 && rep.unitsPerCategory == std::vector<int>({3, 3, 3}), "recordings as units");
        for (const FeatureTrend &f : rep.features) {
            std::printf("  %-20s rho %+.2f  p(Holm) %.4f  d %+8.2f  %s\n", f.feature.c_str(), f.rho, f.pHolm,
                        f.effectSize, verdictName(f.verdict));
            if (f.feature == "jitter_mean" || f.feature == "jitter_std" || f.feature == "scintillation_index")
                CHECK(f.verdict == TrendVerdict::ConsistentIncrease, f.feature << " increases");
            if (f.feature == "sharpness_mean")
                CHECK(f.verdict == TrendVerdict::ConsistentDecrease, "sharpness decreases");
            if (f.feature == "intensity_mean") // gain noise only, no built-in level difference
                CHECK(f.verdict != TrendVerdict::ConsistentIncrease && f.verdict != TrendVerdict::ConsistentDecrease,
                      "no false trend for intensity_mean");
        }
        // per-recording details used by the GUI
        CHECK(rep.unitList.size() == 9, "unit list");
        for (const TrendUnit &u : rep.unitList)
            CHECK(u.windows == 2 && u.value.size() == featureNames().size() && u.windowCv.size() == u.value.size(),
                  "unit " << u.id);
        for (const FeatureTrend &f : rep.features) {
            int overlap = 0;
            for (int n : f.overlapping)
                overlap += n;
            for (size_t k = 0; k < f.count.size(); ++k)
                CHECK(f.minValue[k] <= f.mean[k] && f.mean[k] <= f.maxValue[k], "range " << f.feature);
            if (f.feature == "jitter_mean" || f.feature == "sharpness_mean")
                CHECK(overlap == 0, f.feature << ": categories do not overlap");
            if (f.feature == "intensity_mean")
                CHECK(overlap > 0, "intensity_mean: categories overlap");
        }
        CHECK(featureLabel("jitter_mean") == "Jitter - mean" && featureUnit("jitter_mean") == "px", "labels");
        CHECK(featureLabel("scintillation_index") == "Scintillation index", "label of scintillation index");

        // algorithm report: one entry per metric, best feature inside the algorithm
        CHECK(rep.algorithms.size() == metricInfos().size(), "one algorithm entry per metric");
        for (const AlgorithmTrend &a : rep.algorithms) {
            CHECK(a.best >= 0 && std::find(a.features.begin(), a.features.end(), size_t(a.best)) != a.features.end(),
                  a.metric << " best feature");
            CHECK(a.verdict == AlgorithmVerdict::Useful, a.metric << " useful on the combined data");
        }

        // reproducible
        const TrendReport again = analyzeTrends(t, TrendOptions());
        CHECK(again.features[0].pValue == rep.features[0].pValue, "fixed seed");

        // window units: 18 units, note about optimism
        TrendOptions wopt;
        wopt.perRecording = false;
        const TrendReport wrep = analyzeTrends(t, wopt);
        CHECK(wrep.ok && wrep.units == 18 && !wrep.notes.empty(), "window units");

        std::string err2;
        CHECK(writeTrendCsv(rep, work + "/trend.csv", &err2), err2);

        // shuffled labels: no consistent trend should survive
        Table shuffled = t;
        const int cc = t.column("category");
        const char *perm[9] = {"MEDIUM", "HIGH", "LOW", "HIGH", "LOW", "MEDIUM", "LOW", "MEDIUM", "HIGH"};
        for (std::vector<std::string> &row : shuffled.rows) {
            const std::string src = row[size_t(t.column("source"))];
            const int k = src.find("LOW") != std::string::npos ? 0 : (src.find("MEDIUM") != std::string::npos ? 1 : 2);
            const int rr = src.back() - '0';
            row[size_t(cc)] = perm[k * 3 + rr];
        }
        int falsePositives = 0;
        for (const FeatureTrend &f : analyzeTrends(shuffled, TrendOptions()).features)
            falsePositives += f.verdict == TrendVerdict::ConsistentIncrease ||
                              f.verdict == TrendVerdict::ConsistentDecrease;
        CHECK(falsePositives == 0, "shuffled labels give no consistent trend (" << falsePositives << ")");
        for (const AlgorithmTrend &a : analyzeTrends(shuffled, TrendOptions()).algorithms)
            CHECK(a.verdict != AlgorithmVerdict::Useful, "shuffled labels: " << a.metric << " not useful");

        // one recording per category: explored (direction, window spread), but not tested
        Table few;
        few.header = t.header;
        for (const std::vector<std::string> &row : t.rows)
            if (row[size_t(t.column("source"))].back() == '0')
                few.rows.push_back(row);
        const TrendReport frep = analyzeTrends(few, TrendOptions());
        CHECK(frep.ok && !frep.testable && frep.units == 3 && !frep.notes.empty(), "single recording per category");
        for (const FeatureTrend &f : frep.features) {
            CHECK(!f.tested && std::isnan(f.pHolm), f.feature << " not tested");
            CHECK(f.windowSpread && f.spreadCount == std::vector<int>({2, 2, 2}), f.feature << " spread from windows");
            CHECK(f.verdict != TrendVerdict::ConsistentIncrease && f.verdict != TrendVerdict::ConsistentDecrease,
                  f.feature << " never 'consistent' without a test");
            if (f.feature == "jitter_mean" || f.feature == "scintillation_index")
                CHECK(f.verdict == TrendVerdict::OrderedUntested && f.direction == 1, f.feature << " rising, untested");
            if (f.feature == "sharpness_mean")
                CHECK(f.verdict == TrendVerdict::OrderedUntested && f.direction == -1, "sharpness falling, untested");
            if (f.feature == "jitter_mean")
                CHECK(std::isfinite(f.effectSize) && f.overlapping == std::vector<int>({0, 0, 0}),
                      "effect size and overlap from windows");
        }
        // two categories with one recording each still give a direction
        Table two_rec;
        two_rec.header = t.header;
        for (const std::vector<std::string> &row : few.rows)
            if (row[size_t(t.column("category"))] != "MEDIUM")
                two_rec.rows.push_back(row);
        const TrendReport trec = analyzeTrends(two_rec, TrendOptions());
        CHECK(trec.ok && trec.categories.size() == 2 && trec.features[0].verdict == TrendVerdict::OrderedUntested,
              "two categories, one recording each");
        // trend CSV of an untested analysis
        std::string err3;
        CHECK(writeTrendCsv(frep, work + "/trend_single.csv", &err3), err3);
        Table trendCsv;
        CHECK(loadTable(work + "/trend_single.csv", trendCsv, &err3), err3);
        for (const std::vector<std::string> &row : trendCsv.rows)
            CHECK(row.size() == trendCsv.header.size(), "trend csv row width");
        CHECK(trendCsv.rows[0][size_t(trendCsv.column("verdict"))] == "ordered, not tested", "verdict with comma");

        // custom order with a missing category is still analyzed (LOW vs HIGH)
        TrendOptions two;
        two.categoryOrder = {"LOW", "HIGH", "EXTREME"};
        const TrendReport trep = analyzeTrends(t, two);
        CHECK(trep.ok && trep.categories.size() == 2 && !trep.notes.empty(), "partial order");
    }

    // a results file with other columns is refused
    {
        const std::string other = work + "/other.csv";
        std::ofstream(other.c_str()) << "a,b,c\n1,2,3\n";
        Recording rec;
        rec.path = motionDir;
        const MeasureOutput out = measure(rec, MeasureSettings(), ProgressFn(), nullptr);
        CHECK(!appendResults(other, rec, out, &err), "different header refused");
    }

    // --- CLAHE preprocessing ----------------------------------------------------------------
    {
        // the same scene as 8-bit images: as recorded, and with lower contrast + an offset (haze)
        const std::string ref = work + "/clahe_ref", hazy = work + "/clahe_hazy";
        makeDir(ref);
        makeDir(hazy);
        for (int i = 0; i < N; ++i) {
            const std::string name = "/frame_" + std::to_string(i) + ".png";
            const cv::Mat src = cv::imread(motionDir + name, cv::IMREAD_ANYDEPTH);
            cv::Mat a, b;
            src.convertTo(a, CV_8U, 1.0 / 256.0);
            src.convertTo(b, CV_8U, 0.6 / 256.0, 60.0);
            cv::imwrite(ref + name, a);
            cv::imwrite(hazy + name, b);
        }
        MeasureSettings plain, eq;
        eq.preprocess.clahe = true;
        eq.preprocess.clipLimit = 2.0;
        eq.preprocess.tileGrid = 4;
        CHECK(plain.preprocess.label() == "none", "label none");
        CHECK(eq.preprocess.label() == "clahe:clip=2,tiles=4", "label clahe " << eq.preprocess.label());
        Recording r1, r2;
        r1.path = ref;
        r2.path = hazy;
        const MeasureOutput p1 = measure(r1, plain, ProgressFn(), nullptr), p2 = measure(r2, plain, ProgressFn(), nullptr);
        const MeasureOutput e1 = measure(r1, eq, ProgressFn(), nullptr), e2 = measure(r2, eq, ProgressFn(), nullptr);
        CHECK(p1.ok && p2.ok && e1.ok && e2.ok, "clahe measurements " << e1.error);
        CHECK(e1.preprocess == "clahe:clip=2,tiles=4" && p1.preprocess == "none", "preprocess stored in output");
        auto relDiff = [](const MeasureOutput &a, const MeasureOutput &b, const char *f) {
            const double x = feature(a.windows[0], f), y = feature(b.windows[0], f);
            return std::fabs(x - y) / std::fabs(x);
        };
        const double plainDiff = relDiff(p1, p2, "gradient_mean"), eqDiff = relDiff(e1, e2, "gradient_mean");
        std::cout << "clahe: gradient difference hazy vs. original " << plainDiff << " without, " << eqDiff
                  << " with CLAHE\n";
        CHECK(eqDiff < plainDiff, "CLAHE reduces contrast / offset differences " << eqDiff << " vs " << plainDiff);
        {
            // a high clip limit (close to full histogram equalization) removes them almost completely
            MeasureSettings strong;
            strong.preprocess.clahe = true;
            strong.preprocess.clipLimit = 40.0;
            strong.preprocess.tileGrid = 4;
            const MeasureOutput s1 = measure(r1, strong, ProgressFn(), nullptr), s2 = measure(r2, strong, ProgressFn(), nullptr);
            const double strongDiff = relDiff(s1, s2, "gradient_mean");
            std::cout << "clahe: clip 40 -> " << strongDiff << "\n";
            CHECK(strongDiff < 0.1 * plainDiff, "strong CLAHE evens out contrast / offset " << strongDiff);
        }
        // jitter stays measurable after CLAHE
        CHECK(relDiff(p1, e1, "jitter_mean") < 0.25, "jitter after CLAHE " << relDiff(p1, e1, "jitter_mean"));
        // 16-bit images are supported
        Recording r16;
        r16.path = motionDir;
        CHECK(measure(r16, eq, ProgressFn(), nullptr).ok, "CLAHE on 16-bit images");

        // preview changes only the ROI
        cv::Mat frame = cv::imread(hazy + "/frame_0.png", cv::IMREAD_COLOR), before = frame.clone();
        previewPreprocess(frame, cv::Rect(10, 10, 50, 40), eq.preprocess);
        cv::Mat diff;
        cv::absdiff(frame, before, diff);
        cv::Mat outside = diff.clone();
        outside(cv::Rect(10, 10, 50, 40)).setTo(0);
        CHECK(cv::countNonZero(diff.reshape(1)) > 0 && cv::countNonZero(outside.reshape(1)) == 0,
              "preview changes the ROI only");

        // results: variant column, upgrade of an old file, one variant at a time
        const std::string mixed = work + "/mixed.csv";
        {
            std::vector<std::string> oldCols = resultColumns();
            oldCols.erase(std::find(oldCols.begin(), oldCols.end(), "preprocess"));
            std::ofstream o(mixed.c_str());
            for (size_t i = 0; i < oldCols.size(); ++i)
                o << (i ? "," : "") << oldCols[i];
            o << "\n";
            for (size_t i = 0; i < oldCols.size(); ++i)
                o << (i ? "," : "") << (oldCols[i] == "note" ? "\"a, b\"" : "1");
            o << "\n";
        }
        r1.category = "LOW";
        CHECK(appendResults(mixed, r1, p1, &err), "old results file upgraded " << err);
        CHECK(appendResults(mixed, r1, e1, &err), "append clahe rows " << err);
        Table m;
        CHECK(loadTable(mixed, m, &err) && m.header == resultColumns(), "upgraded header");
        CHECK(m.rows.size() == 3 && m.rows[0][size_t(m.column("preprocess"))] == "none" &&
                  m.rows[0][size_t(m.column("note"))] == "a, b",
              "old row keeps its values, preprocess = none");
        const std::vector<std::string> variants = preprocessVariants(m);
        CHECK(variants.size() == 2 && variants[0] == "none" && variants[1] == "clahe:clip=2,tiles=4", "variants");
        CHECK(filterPreprocess(m, "none").rows.size() == 2 && filterPreprocess(m, variants[1]).rows.size() == 1,
              "filter by variant");
        Table noColumn;
        noColumn.header = {"recording", "category"};
        noColumn.rows.push_back({"x", "LOW"});
        CHECK(preprocessVariants(noColumn) == std::vector<std::string>(1, "none") &&
                  filterPreprocess(noColumn, "none").rows.size() == 1 &&
                  filterPreprocess(noColumn, variants[1]).rows.empty(),
              "table without the column counts as none");
        // labels: one category per recording, relabeling rewrites its rows only
        bool found = false;
        CHECK(lastValue(m, ref, "category", &found) == "LOW" && found, "last category of a recording");
        CHECK(lastValue(m, ref + "/", "category", &found) == "LOW" && found, "trailing slash ignored");
        lastValue(m, hazy, "category", &found);
        CHECK(!found, "recording without rows");
        CHECK(countRowsToRelabel(m, ref, "HIGH") == 2 && countRowsToRelabel(m, ref, "LOW") == 0, "rows to relabel");
        CHECK(relabelSource(mixed, ref, "HIGH", &err) == 2, "relabel " << err);
        Table m2;
        CHECK(loadTable(mixed, m2, &err) && m2.rows.size() == 3, "relabeled file");
        CHECK(m2.rows[1][size_t(m2.column("category"))] == "HIGH" && m2.rows[2][size_t(m2.column("category"))] == "HIGH" &&
                  m2.rows[0][size_t(m2.column("category"))] == "1" && m2.rows[0][size_t(m2.column("note"))] == "a, b",
              "only the recording's rows changed");
        CHECK(relabelSource(mixed, ref, "HIGH", &err) == 0, "nothing to relabel");
        // measuring again replaces the earlier rows of the same preprocessing only
        CHECK(removeRows(mixed, std::vector<std::string>(1, ref + "/"), "none", &err) == 1, "remove rows " << err);
        Table m3;
        CHECK(loadTable(mixed, m3, &err) && m3.rows.size() == 2 &&
                  m3.rows[1][size_t(m3.column("preprocess"))] == "clahe:clip=2,tiles=4" &&
                  m3.rows[0][size_t(m3.column("note"))] == "a, b",
              "other rows kept");
        CHECK(removeRows(mixed, std::vector<std::string>(1, ref), "none", &err) == 0, "nothing left to remove");
        CHECK(removeRows(work + "/does_not_exist.csv", std::vector<std::string>(1, ref), "none", &err) == 0,
              "no file yet");
        Table both = t;
        for (const std::vector<std::string> &row : m.rows)
            both.rows.push_back(row);
        bool warned = false;
        for (const std::string &n : analyzeTrends(both, TrendOptions()).notes)
            warned = warned || n.find("different preprocessing") != std::string::npos;
        CHECK(warned, "mixed preprocessing is reported");
    }

    // --- numbers do not depend on the system language -------------------------------------
    // The GUI runs with the user's locale (Qt calls setlocale): in Turkish / German the decimal
    // separator is ',' and strtod("0.0123") gives 0. Reading and writing must not care.
    {
        const TrendReport before = analyzeTrends(t, TrendOptions());
        const char *commaLocales[] = {"tr_TR.UTF-8", "tr_TR.utf8", "de_DE.UTF-8", "de_DE.utf8", "tr_TR", "de_DE"};
        const char *used = nullptr;
        for (const char *l : commaLocales)
            if (std::setlocale(LC_ALL, l) && std::strtod("0.5", nullptr) == 0.0) {
                used = l;
                break;
            }
        if (!used) {
            std::setlocale(LC_ALL, "C");
            std::cout << "locale: no comma-decimal locale installed, check skipped\n";
        } else {
            std::cout << "locale: checked with " << used << "\n";
            CHECK_NEAR(parseNumber("0.0123"), 0.0123, 1e-15, "parse with comma locale");
            CHECK(formatNumber(0.5) == "0.5", "format with comma locale " << formatNumber(0.5));
            const TrendReport after = analyzeTrends(t, TrendOptions());
            bool same = after.ok && after.features.size() == before.features.size();
            for (size_t f = 0; same && f < after.features.size(); ++f)
                for (size_t k = 0; k < after.features[f].mean.size(); ++k)
                    same = same && after.features[f].mean[k] == before.features[f].mean[k];
            CHECK(same, "trend analysis independent of the locale");
            const std::string loc = work + "/locale.csv";
            std::remove(loc.c_str());
            Recording rec;
            rec.path = motionDir;
            MeasureSettings eq;
            eq.preprocess.clahe = true;
            eq.preprocess.clipLimit = 2.5;
            const MeasureOutput out = measure(rec, eq, ProgressFn(), nullptr);
            CHECK(out.preprocess == "clahe:clip=2.5,tiles=8", "label with comma locale " << out.preprocess);
            CHECK(appendResults(loc, rec, out, &err), err);
            Table lt;
            loadTable(loc, lt, &err);
            const std::string sh = lt.rows[0][size_t(lt.column("sharpness_mean"))];
            CHECK(sh.find(',') == std::string::npos && parseNumber(sh) > 0, "written with '.' " << sh);
            std::setlocale(LC_ALL, "C");
        }
    }

    testScenarios(work);

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
