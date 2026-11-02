#include "FramePlayer.h"

#include "FrameSource.h"
#include "ImageUtils.h"

#include <opencv2/core/core.hpp>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>

#include <algorithm>

namespace {
const int kSpeeds[] = {2, 5, 10, 25, 50, 0}; // frames per second, 0 = as fast as possible
}

FramePlayer::FramePlayer(QWidget *parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
{
    QHBoxLayout *h = new QHBoxLayout(this);
    h->setContentsMargins(4, 2, 4, 2);
    auto button = [this, h](const QString &text, const QString &tip) {
        QPushButton *b = new QPushButton(text);
        b->setToolTip(tip);
        b->setFixedWidth(40);
        h->addWidget(b);
        return b;
    };
    m_first = button(QString::fromUtf8("\xe2\x8f\xae"), tr("First frame"));
    m_prev = button(QString::fromUtf8("\xe2\x97\x80"), tr("Previous frame"));
    m_play = button(QString::fromUtf8("\xe2\x96\xb6"), tr("Play / pause the recording frame by frame"));
    m_next = button(QString::fromUtf8("\xe2\x96\xb6|"), tr("Next frame"));
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, 0);
    h->addWidget(m_slider, 1);
    m_label = new QLabel(tr("no recording"));
    m_label->setMinimumWidth(130);
    h->addWidget(m_label);
    m_speed = new QComboBox;
    for (int fps : kSpeeds)
        m_speed->addItem(fps > 0 ? tr("%1 fps").arg(fps) : tr("max"));
    m_speed->setCurrentIndex(3);
    m_speed->setToolTip(tr("Playback speed"));
    h->addWidget(m_speed);

    connect(m_first, &QPushButton::clicked, this, [this]() {
        pause();
        showFrame(0);
    });
    connect(m_prev, &QPushButton::clicked, this, [this]() { step(-1); });
    connect(m_next, &QPushButton::clicked, this, [this]() { step(1); });
    connect(m_play, &QPushButton::clicked, this, &FramePlayer::togglePlay);
    connect(m_slider, &QSlider::valueChanged, this, &FramePlayer::onSlider);
    connect(m_speed, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            &FramePlayer::onSpeed);
    connect(m_timer, &QTimer::timeout, this, &FramePlayer::tick);
    onSpeed(m_speed->currentIndex());
    setEnabled(false);
}

FramePlayer::~FramePlayer() {}

void FramePlayer::setSource(const QString &path)
{
    pause();
    m_source.reset();
    m_count = 0;
    m_current = -1;
    if (!path.isEmpty()) {
        std::string err;
        m_source = turb::FrameSource::create(path.toStdString(), &err);
        if (m_source)
            m_count = m_source->frameCount();
    }
    setEnabled(m_count > 0);
    m_updatingSlider = true;
    m_slider->setRange(0, std::max(0, m_count - 1));
    m_slider->setValue(0);
    m_updatingSlider = false;
    if (m_count > 0)
        showFrame(0);
    else
        m_label->setText(tr("no recording"));
}

bool FramePlayer::isPlaying() const
{
    return m_timer->isActive();
}

void FramePlayer::pause()
{
    m_timer->stop();
    m_play->setText(QString::fromUtf8("\xe2\x96\xb6"));
}

void FramePlayer::togglePlay()
{
    if (!m_source)
        return;
    if (isPlaying()) {
        pause();
        return;
    }
    if (m_current >= m_count - 1)
        showFrame(0); // restart at the end
    m_source->seek(m_current + 1);
    m_play->setText(QString::fromUtf8("\xe2\x8f\xb8"));
    m_timer->start();
}

void FramePlayer::step(int delta)
{
    pause();
    showFrame(std::min(std::max(m_current + delta, 0), m_count - 1));
}

void FramePlayer::tick()
{
    // sequential decoding while playing (fast for videos)
    cv::Mat raw;
    int index = 0;
    if (!m_source || !m_source->next(raw, &index)) {
        pause();
        return;
    }
    m_current = index;
    emitFrame(raw, index);
    m_updatingSlider = true;
    m_slider->setValue(index);
    m_updatingSlider = false;
    updateLabel();
}

void FramePlayer::onSlider(int value)
{
    if (m_updatingSlider)
        return;
    pause();
    showFrame(value);
}

void FramePlayer::onSpeed(int index)
{
    const int fps = kSpeeds[index];
    m_timer->setInterval(fps > 0 ? 1000 / fps : 0);
}

void FramePlayer::showFrame(int index)
{
    if (!m_source || index < 0 || index >= m_count)
        return;
    cv::Mat raw;
    if (!m_source->readAt(index, raw))
        return;
    m_current = index;
    emitFrame(raw, index);
    m_updatingSlider = true;
    m_slider->setValue(index);
    m_updatingSlider = false;
    updateLabel();
}

void FramePlayer::emitFrame(cv::Mat &raw, int index)
{
    if (m_filter) {
        try {
            m_filter(raw);
        } catch (const cv::Exception &) {
            // show the frame unchanged when the filter cannot handle it
        }
    }
    emit frameReady(toQImage(raw), index);
}

void FramePlayer::refresh()
{
    if (isPlaying() || m_current < 0)
        return;
    const int index = m_current;
    showFrame(index);
}

void FramePlayer::updateLabel()
{
    m_label->setText(tr("frame %1 / %2").arg(m_current + 1).arg(m_count));
}
