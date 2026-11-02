#ifndef FRAMEPLAYER_H
#define FRAMEPLAYER_H

#include <QImage>
#include <QWidget>

#include <memory>

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

    std::unique_ptr<turb::FrameSource> m_source;
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
