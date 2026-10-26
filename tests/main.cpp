// Self-checking tests on synthetic image sequences with known ground truth.
// Usage: turbulence_tests [work_dir]

#include "FrameSource.h"
#include "Measurement.h"
#include "ResultTable.h"
#include "Stats.h"

#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>

#include <cmath>
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

static Truth makeTruth(int n, double jitter, double blur, double gainNoise, unsigned seed)
{
    cv::RNG rng(seed);
    Truth t;
    for (int i = 0; i < n; ++i) {
        t.dx.push_back(rng.gaussian(jitter));
        t.dy.push_back(rng.gaussian(jitter));
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
        CHECK(head.size() == 2 + 3 + 1 + 1, "frame csv columns " << line);
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
        for (int r = 0; r < 2; ++r) {
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
    CHECK(t.rows.size() == 12, "12 windows appended " << t.rows.size());
    CHECK(t.rows[0][size_t(t.column("note"))] == "synthetic, run 0, \"quoted\"", "note round trip");

    const std::vector<std::string> order = categoriesIn(t, {"LOW", "MEDIUM", "HIGH"});
    CHECK(order == std::vector<std::string>({"LOW", "MEDIUM", "HIGH"}), "category order");
    for (const CategoryStats &s : categorySummary(t, order)) {
        std::printf("  %-20s %-11s %-11s", s.feature.c_str(), s.trend.c_str(), s.separation.c_str());
        for (size_t i = 0; i < order.size(); ++i)
            std::printf("  %s %.4g", order[i].c_str(), s.mean[i]);
        std::printf("\n");
        if (s.feature == "jitter_mean")
            CHECK(s.trend == "increasing", "jitter rises with the category");
        if (s.feature == "sharpness_mean")
            CHECK(s.trend == "decreasing" && s.separation == "clear", "sharpness falls with the category");
        if (s.feature == "scintillation_index")
            CHECK(s.trend == "increasing", "scintillation rises with the category");
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

    std::cout << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
