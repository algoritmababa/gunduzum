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

struct MeasureSettings {
    int windowLength = 0;      // frames per window, 0 = the whole recording is one window
    std::string frameCsvPath;  // optional: per-frame values are written here
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
    int frameCount = 0;
    std::vector<WindowResult> windows;
};

typedef std::function<void(int framesDone, int framesTotal)> ProgressFn;

// Reads the recording frame by frame (only the current frame is in memory),
// crops the ROI, converts to gray float and feeds every metric.
// Windows have exactly windowLength frames; a remainder shorter than that is dropped.
MeasureOutput measure(const Recording &rec, const MeasureSettings &settings, const ProgressFn &progress,
                      const std::atomic<bool> *cancel);

cv::Rect effectiveRoi(const Roi &roi, const cv::Size &frameSize);
// raw frame -> ROI crop -> grayscale -> CV_32F (no other preprocessing)
void toGray32f(const cv::Mat &raw, const cv::Rect &roi, cv::Mat &out);
std::string recordingId(const std::string &path);

} // namespace turb

#endif
