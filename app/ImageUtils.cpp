#include "ImageUtils.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>

QImage toQImage(const cv::Mat &frame, int maxSide)
{
    if (frame.empty())
        return QImage();
    cv::Mat src = frame;
    const int side = std::max(frame.cols, frame.rows);
    if (maxSide > 0 && side > maxSide) {
        const double s = double(maxSide) / side;
        cv::resize(frame, src, cv::Size(), s, s, cv::INTER_AREA);
    }
    cv::Mat u8, rgb;
    if (src.depth() == CV_8U)
        u8 = src;
    else
        cv::normalize(src, u8, 0, 255, cv::NORM_MINMAX, CV_8U);
    if (u8.channels() == 1)
        cv::cvtColor(u8, rgb, cv::COLOR_GRAY2RGB);
    else if (u8.channels() == 4)
        cv::cvtColor(u8, rgb, cv::COLOR_BGRA2RGB);
    else
        cv::cvtColor(u8, rgb, cv::COLOR_BGR2RGB);
    return QImage(rgb.data, rgb.cols, rgb.rows, int(rgb.step), QImage::Format_RGB888).copy();
}
