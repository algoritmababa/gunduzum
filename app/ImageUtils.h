#ifndef IMAGEUTILS_H
#define IMAGEUTILS_H

#include <opencv2/core/core.hpp>

#include <QImage>

// Raw frame (any depth / channel count) -> 8-bit RGB QImage for display, with a min/max
// stretch for > 8-bit data. maxSide > 0 downscales large frames first (previews, playback).
// Safe to call from a worker thread.
QImage toQImage(const cv::Mat &frame, int maxSide = 0);

#endif
