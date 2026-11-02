#include "Measurement.h"

#include "FrameSource.h"
#include "Stats.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <algorithm>
#include <fstream>
#include <locale>
#include <sstream>

namespace turb {

cv::Rect effectiveRoi(const Roi &roi, const cv::Size &frameSize)
{
    const cv::Rect full(0, 0, frameSize.width, frameSize.height);
    if (roi.width <= 0 || roi.height <= 0)
        return full;
    return cv::Rect(roi.x, roi.y, roi.width, roi.height) & full;
}

std::string Preprocess::label() const
{
    if (!clahe)
        return "none";
    std::ostringstream os;
    os.imbue(std::locale::classic());
    os << "clahe:clip=" << clipLimit << ",tiles=" << tileGrid;
    return os.str();
}

// ROI crop -> gray in the frame's own depth (a header into raw when it is already gray)
static cv::Mat grayRoi(const cv::Mat &raw, const cv::Rect &roi)
{
    const cv::Mat view = raw(roi); // header only, no copy
    if (view.channels() == 1)
        return view;
    cv::Mat gray;
    cv::cvtColor(view, gray, view.channels() == 4 ? cv::COLOR_BGRA2GRAY : cv::COLOR_BGR2GRAY);
    return gray;
}

static void applyClahe(const cv::Mat &gray, cv::Mat &out, const Preprocess &pp)
{
    if (gray.depth() != CV_8U && gray.depth() != CV_16U)
        CV_Error(cv::Error::StsUnsupportedFormat, "CLAHE needs 8-bit or 16-bit images");
    const int tiles = std::max(1, pp.tileGrid);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE(pp.clipLimit, cv::Size(tiles, tiles));
    clahe->apply(gray, out);
}

void toGray32f(const cv::Mat &raw, const cv::Rect &roi, cv::Mat &out, const Preprocess &pp)
{
    const cv::Mat gray = grayRoi(raw, roi);
    if (!pp.clahe) {
        gray.convertTo(out, CV_32F);
        return;
    }
    cv::Mat eq;
    applyClahe(gray, eq, pp);
    eq.convertTo(out, CV_32F);
}

void previewPreprocess(cv::Mat &frame, const cv::Rect &roi, const Preprocess &pp)
{
    if (!pp.clahe || frame.empty())
        return;
    const cv::Rect r = roi & cv::Rect(0, 0, frame.cols, frame.rows);
    if (r.area() <= 0)
        return;
    cv::Mat eq;
    applyClahe(grayRoi(frame, r), eq, pp);
    if (frame.channels() == 1) {
        eq.copyTo(frame(r));
        return;
    }
    cv::Mat colored;
    cv::cvtColor(eq, colored, frame.channels() == 4 ? cv::COLOR_GRAY2BGRA : cv::COLOR_GRAY2BGR);
    colored.copyTo(frame(r));
}

std::string recordingId(const std::string &path)
{
    std::string p = path;
    while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
        p.pop_back();
    const size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

MeasureOutput measure(const Recording &rec, const MeasureSettings &settings, const ProgressFn &progress,
                      const std::atomic<bool> *cancel, const FrameFn &frameView)
{
    MeasureOutput out;
    out.recordingId = recordingId(rec.path);
    out.preprocess = settings.preprocess.label();

    std::string err;
    std::unique_ptr<FrameSource> src = FrameSource::create(rec.path, &err);
    if (!src) {
        out.error = err;
        return out;
    }
    const int n = src->frameCount();
    out.frameCount = n;
    const int len = (settings.windowLength <= 0 || settings.windowLength >= n) ? n : settings.windowLength;
    const int windowCount = n / len;
    const int total = windowCount * len;

    std::vector<std::unique_ptr<Metric> > metrics = createMetrics();
    std::ofstream frameCsv;
    if (!settings.frameCsvPath.empty()) {
        frameCsv.open(settings.frameCsvPath.c_str(), std::ios::out | std::ios::trunc);
        if (!frameCsv) {
            out.error = "Cannot write " + settings.frameCsvPath;
            return out;
        }
        frameCsv << "window,frame";
        for (const std::unique_ptr<Metric> &m : metrics)
            for (const std::string &c : m->frameColumns())
                frameCsv << "," << c;
        frameCsv << "\n";
        frameCsv.imbue(std::locale::classic());
        frameCsv.precision(10);
    }

    try {
        cv::Mat raw, gray;
        std::vector<double> values;
        // per metric: per-frame values of its primary column inside the current window
        std::vector<std::vector<double> > series(metrics.size());
        int processed = 0;
        bool ended = false;

        for (int w = 0; w < windowCount && !ended; ++w) {
            for (size_t i = 0; i < metrics.size(); ++i) {
                metrics[i]->reset();
                series[i].clear();
            }
            WindowResult wr;
            wr.window = w;
            wr.startFrame = w * len;
            for (int f = wr.startFrame; f < wr.startFrame + len; ++f) {
                if (cancel && cancel->load()) {
                    out.cancelled = true;
                    out.error = "Cancelled.";
                    return out;
                }
                if (!src->next(raw, nullptr)) {
                    ended = true; // video containers sometimes over-report the frame count
                    break;
                }
                if (out.roi.area() == 0) {
                    out.roi = effectiveRoi(rec.roi, raw.size());
                    if (out.roi.area() <= 0) {
                        out.error = "ROI is outside the image.";
                        return out;
                    }
                    if (settings.preprocess.clahe && raw.depth() != CV_8U && raw.depth() != CV_16U) {
                        out.error = "CLAHE needs 8-bit or 16-bit images.";
                        return out;
                    }
                }
                if (frameView)
                    frameView(f, raw);
                toGray32f(raw, out.roi, gray, settings.preprocess);
                if (frameCsv.is_open())
                    frameCsv << w << "," << f;
                for (size_t i = 0; i < metrics.size(); ++i) {
                    values.clear();
                    metrics[i]->process(gray, values);
                    series[i].push_back(values.empty() ? nan() : values.front());
                    if (frameCsv.is_open())
                        for (double v : values)
                            frameCsv << "," << v;
                }
                if (frameCsv.is_open())
                    frameCsv << "\n";
                wr.endFrame = f;
                ++wr.frames;
                ++processed;
                if (progress)
                    progress(processed, total);
            }
            if (wr.frames < 2)
                break;
            for (size_t i = 0; i < metrics.size(); ++i)
                metrics[i]->summarize(series[i], wr.features);
            out.windows.push_back(wr);
        }
    } catch (const cv::Exception &e) {
        out.error = std::string("OpenCV error: ") + e.what();
        return out;
    }

    if (out.windows.empty()) {
        out.error = "No complete window could be measured (recording too short or unreadable).";
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace turb
