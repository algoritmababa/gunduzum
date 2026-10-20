#include "FrameSource.h"

#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/videoio/videoio.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <sys/stat.h>

namespace turb {

bool isDirectory(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

bool naturalLess(const std::string &a, const std::string &b)
{
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[j];
        if (std::isdigit(ca) && std::isdigit(cb)) {
            size_t ie = i, je = j;
            while (ie < a.size() && std::isdigit((unsigned char)a[ie])) ++ie;
            while (je < b.size() && std::isdigit((unsigned char)b[je])) ++je;
            // compare numeric runs ignoring leading zeros
            size_t is = i, js = j;
            while (is + 1 < ie && a[is] == '0') ++is;
            while (js + 1 < je && b[js] == '0') ++js;
            if (ie - is != je - js)
                return ie - is < je - js;
            const int c = a.compare(is, ie - is, b, js, je - js);
            if (c != 0)
                return c < 0;
            i = ie;
            j = je;
        } else {
            const int la = std::tolower(ca), lb = std::tolower(cb);
            if (la != lb)
                return la < lb;
            ++i;
            ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

double FrameSource::effectiveFrameRate() const
{
    if (m_userFps > 0.0)
        return m_userFps;
    return nativeFrameRate();
}

double FrameSource::timestamp(int index) const
{
    const double fps = effectiveFrameRate();
    return fps > 0.0 ? double(index) / fps : double(index);
}

std::unique_ptr<FrameSource> FrameSource::create(const std::string &path, std::string *error)
{
    std::unique_ptr<FrameSource> src;
    if (isDirectory(path))
        src.reset(new ImageSequenceSource);
    else
        src.reset(new VideoSource);
    if (!src->open(path, error))
        return std::unique_ptr<FrameSource>();
    return src;
}

// ---------------------------------------------------------------------------

static bool hasImageExtension(const std::string &file)
{
    const size_t dot = file.find_last_of('.');
    if (dot == std::string::npos)
        return false;
    std::string ext = file.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    static const char *exts[] = {"png", "tif", "tiff", "bmp", "jpg", "jpeg", "pgm", "ppm", "pbm", "jp2", "exr"};
    for (const char *e : exts)
        if (ext == e)
            return true;
    return false;
}

static std::string baseName(const std::string &path)
{
    const size_t s = path.find_last_of("/\\");
    return s == std::string::npos ? path : path.substr(s + 1);
}

bool ImageSequenceSource::open(const std::string &path, std::string *error)
{
    m_dir = path;
    m_files.clear();
    m_pos = 0;

    std::vector<cv::String> all;
    try {
        cv::glob(path + "/*", all, false);
    } catch (const cv::Exception &e) {
        if (error) *error = std::string("Cannot list directory: ") + e.what();
        return false;
    }
    for (const cv::String &f : all)
        if (hasImageExtension(f))
            m_files.push_back(f);
    std::sort(m_files.begin(), m_files.end(),
              [](const std::string &a, const std::string &b) { return naturalLess(baseName(a), baseName(b)); });

    if (m_files.empty()) {
        if (error) *error = "No image files found in " + path;
        return false;
    }
    cv::Mat first;
    if (!readAt(0, first)) {
        if (error) *error = "Cannot read first image: " + m_files.front();
        return false;
    }
    m_size = first.size();
    return true;
}

std::string ImageSequenceSource::description() const
{
    std::ostringstream os;
    os << "Image sequence: " << m_files.size() << " files, " << m_size.width << "x" << m_size.height;
    return os.str();
}

bool ImageSequenceSource::readAt(int index, cv::Mat &frame)
{
    if (index < 0 || index >= int(m_files.size()))
        return false;
    frame = cv::imread(m_files[size_t(index)], cv::IMREAD_ANYDEPTH | cv::IMREAD_ANYCOLOR);
    return !frame.empty();
}

bool ImageSequenceSource::seek(int index)
{
    if (index < 0 || index > int(m_files.size()))
        return false;
    m_pos = index;
    return true;
}

bool ImageSequenceSource::next(cv::Mat &frame, int *index)
{
    if (m_pos >= int(m_files.size()))
        return false;
    if (index) *index = m_pos;
    return readAt(m_pos++, frame);
}

// ---------------------------------------------------------------------------

VideoSource::VideoSource() {}
VideoSource::~VideoSource() {}

bool VideoSource::open(const std::string &path, std::string *error)
{
    m_path = path;
    m_cap.reset(new cv::VideoCapture(path));
    if (!m_cap->isOpened()) {
        if (error) *error = "Cannot open video or directory: " + path;
        return false;
    }
    m_count = int(m_cap->get(cv::CAP_PROP_FRAME_COUNT));
    m_fps = m_cap->get(cv::CAP_PROP_FPS);
    if (!std::isfinite(m_fps) || m_fps <= 0.0 || m_fps > 1e5)
        m_fps = 0.0;
    m_size = cv::Size(int(m_cap->get(cv::CAP_PROP_FRAME_WIDTH)), int(m_cap->get(cv::CAP_PROP_FRAME_HEIGHT)));
    m_pos = 0;
    if (m_count <= 0) {
        if (error) *error = "Video reports no frames: " + path;
        return false;
    }
    return true;
}

std::string VideoSource::description() const
{
    std::ostringstream os;
    os << "Video: " << m_count << " frames, " << m_size.width << "x" << m_size.height;
    if (m_fps > 0)
        os << ", " << m_fps << " fps";
    return os.str();
}

bool VideoSource::readAt(int index, cv::Mat &frame)
{
    if (!seek(index))
        return false;
    return next(frame, nullptr);
}

bool VideoSource::seek(int index)
{
    if (!m_cap || index < 0 || index > m_count)
        return false;
    if (index != m_pos) {
        m_cap->set(cv::CAP_PROP_POS_FRAMES, double(index));
        m_pos = index;
    }
    return true;
}

bool VideoSource::next(cv::Mat &frame, int *index)
{
    if (!m_cap || m_pos >= m_count)
        return false;
    if (!m_cap->read(frame) || frame.empty())
        return false;
    if (index) *index = m_pos;
    ++m_pos;
    return true;
}

} // namespace turb
