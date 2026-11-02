#ifndef TURB_MEASUREMENT_H
#define TURB_MEASUREMENT_H

#include "Metrics.h"

#include <opencv2/core/core.hpp>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace turb {

// Region of interest in image coordinates; width/height <= 0 = full frame.
struct Roi {
    int x = 0, y = 0, width = 0, height = 0;
};

// One recording (image folder or video) with its label.
struct Recording {
    std::string path;
    std::string category; // e.g. LOW / MEDIUM / HIGH, empty = unlabeled
    std::string note;     // free text (time, weather, camera settings, ...)
    Roi roi;
};

// Optional preprocessing, applied to the ROI crop of every frame before the metrics see it.
// CLAHE = contrast limited adaptive histogram equalization: the ROI is divided into
// tileGrid x tileGrid tiles, the histogram of every tile is equalized (clipped at clipLimit)
// and the tiles are blended. It works frame by frame, so it also changes the temporal
// brightness fluctuations (scintillation) - compare the results with and without it.
struct Preprocess {
    bool clahe = false;
    double clipLimit = 2.0; // OpenCV clip limit (relative to a flat histogram)
    int tileGrid = 8;       // tiles per side of the ROI

    // Stored in the results table ("none" / "clahe:clip=2,tiles=8").
    std::string label() const;
};

struct MeasureSettings {
    int windowLength = 0;      // frames per window, 0 = the whole recording is one window
    std::string frameCsvPath;  // optional: per-frame values are written here
    Preprocess preprocess;
};

// One window = one row of the results table.
struct WindowResult {
    int window = 0;
    int startFrame = 0;
    int endFrame = 0; // inclusive
    int frames = 0;   // frames actually processed
    FeatureList features;
};

struct MeasureOutput {
    bool ok = false;
    bool cancelled = false;
    std::string error;
    std::string recordingId; // file / folder name
    cv::Rect roi;            // ROI actually used (clamped to the image)
    std::string preprocess;  // Preprocess::label() of the settings used
    int frameCount = 0;
    std::vector<WindowResult> windows;
};

typedef std::function<void(int framesDone, int framesTotal)> ProgressFn;
// Optional view of every frame as it is measured (raw decoded frame, valid only during the call).
typedef std::function<void(int frameIndex, const cv::Mat &raw)> FrameFn;

// Reads the recording frame by frame (only the current frame is in memory),
// crops the ROI, converts to gray float and feeds every metric.
// Windows have exactly windowLength frames; a remainder shorter than that is dropped.
MeasureOutput measure(const Recording &rec, const MeasureSettings &settings, const ProgressFn &progress,
                      const std::atomic<bool> *cancel, const FrameFn &frameView = FrameFn());

cv::Rect effectiveRoi(const Roi &roi, const cv::Size &frameSize);
// raw frame -> ROI crop -> grayscale (-> CLAHE when enabled) -> CV_32F.
// CLAHE needs 8- or 16-bit images; other depths throw a cv::Exception.
void toGray32f(const cv::Mat &raw, const cv::Rect &roi, cv::Mat &out, const Preprocess &pp = Preprocess());
// For display: replaces the ROI of a raw frame with what the metrics see after the
// preprocessing (gray, CLAHE). Does nothing when no preprocessing is enabled.
void previewPreprocess(cv::Mat &frame, const cv::Rect &roi, const Preprocess &pp);
std::string recordingId(const std::string &path);

} // namespace turb

#endif
