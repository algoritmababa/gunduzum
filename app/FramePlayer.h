#ifndef FRAMEPLAYER_H
#define FRAMEPLAYER_H

#include <QImage>
#include <QWidget>

#include <functional>
#include <memory>

namespace cv { class Mat; }

namespace turb { class FrameSource; }

class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

// Playback controls for a recording: first / previous / play-pause / next, a frame slider and
// a speed selector. Decodes one frame at a time and emits it for display; it is independent
// of the measurement (which reads the recording with its own reader).
class FramePlayer : public QWidget
{
    Q_OBJECT

public:
    explicit FramePlayer(QWidget *parent = nullptr);
    ~FramePlayer() override;

    // Opens the recording for playback (empty path = nothing to play). Shows frame 0.
    void setSource(const QString &path);
    void pause();
    bool isPlaying() const;
    // Optional change of every frame before it is shown (e.g. a preprocessing preview).
    void setFilter(const std::function<void(cv::Mat &)> &filter) { m_filter = filter; }
    // Shows the current frame again (after the filter changed).
    void refresh();

signals:
    void frameReady(const QImage &image, int frameIndex);

private slots:
    void togglePlay();
    void step(int delta);
    void tick();
    void onSlider(int value);
    void onSpeed(int index);

private:
    void showFrame(int index);
    void updateLabel();
    void emitFrame(cv::Mat &raw, int index);

    std::unique_ptr<turb::FrameSource> m_source;
    std::function<void(cv::Mat &)> m_filter;
    int m_count = 0;
    int m_current = -1;
    bool m_updatingSlider = false;
    QTimer *m_timer;
    QPushButton *m_first, *m_prev, *m_play, *m_next;
    QSlider *m_slider;
    QLabel *m_label;
    QComboBox *m_speed;
};

#endif
