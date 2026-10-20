#ifndef TURB_FRAMESOURCE_H
#define TURB_FRAMESOURCE_H

#include <opencv2/core/core.hpp>

#include <memory>
#include <string>
#include <vector>

namespace cv { class VideoCapture; }

namespace turb {

// Sequential frame reader. Only one frame is decoded at a time; nothing is cached.
class FrameSource {
public:
    virtual ~FrameSource() {}

    virtual bool open(const std::string &path, std::string *error) = 0;
    virtual int frameCount() const = 0;
    virtual cv::Size frameSize() const = 0;
    virtual std::string description() const = 0;

    // Random access read (used for reference / preview frames).
    // The returned Mat is the raw decoded image (any depth / channel count).
    virtual bool readAt(int index, cv::Mat &frame) = 0;

    // Sequential read starting at seek(). Returns false at the end.
    virtual bool seek(int index) = 0;
    virtual bool next(cv::Mat &frame, int *index) = 0;

    // Timestamp in seconds. Uses the configured frame rate if > 0, otherwise the
    // native frame rate (video), otherwise the frame index.
    virtual double timestamp(int index) const;
    void setFrameRate(double fps) { m_userFps = fps; }
    double effectiveFrameRate() const;

    // Creates an ImageSequenceSource for directories and a VideoSource for files.
    static std::unique_ptr<FrameSource> create(const std::string &path, std::string *error);

protected:
    virtual double nativeFrameRate() const { return 0.0; }
    double m_userFps = 0.0;
};

// A directory of image files, natural-sorted by file name (frame_2 < frame_10).
class ImageSequenceSource : public FrameSource {
public:
    bool open(const std::string &path, std::string *error) override;
    int frameCount() const override { return int(m_files.size()); }
    cv::Size frameSize() const override { return m_size; }
    std::string description() const override;
    bool readAt(int index, cv::Mat &frame) override;
    bool seek(int index) override;
    bool next(cv::Mat &frame, int *index) override;

    const std::vector<std::string> &files() const { return m_files; }

private:
    std::string m_dir;
    std::vector<std::string> m_files;
    cv::Size m_size;
    int m_pos = 0;
};

class VideoSource : public FrameSource {
public:
    VideoSource();
    ~VideoSource() override;
    bool open(const std::string &path, std::string *error) override;
    int frameCount() const override { return m_count; }
    cv::Size frameSize() const override { return m_size; }
    std::string description() const override;
    bool readAt(int index, cv::Mat &frame) override;
    bool seek(int index) override;
    bool next(cv::Mat &frame, int *index) override;

protected:
    double nativeFrameRate() const override { return m_fps; }

private:
    std::string m_path;
    std::unique_ptr<cv::VideoCapture> m_cap;
    int m_count = 0;
    cv::Size m_size;
    double m_fps = 0.0;
    int m_pos = 0;
};

bool naturalLess(const std::string &a, const std::string &b);
bool isDirectory(const std::string &path);

} // namespace turb

#endif
