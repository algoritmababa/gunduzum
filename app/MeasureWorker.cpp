#include "MeasureWorker.h"

#include "ImageUtils.h"
#include "ResultTable.h"

#include <QElapsedTimer>

MeasureWorker::MeasureWorker(const turb::Recording &rec, const turb::MeasureSettings &settings,
                             const QString &resultsPath, bool showFrames, bool showPreprocessed)
    : m_rec(rec)
    , m_settings(settings)
    , m_resultsPath(resultsPath)
    , m_showFrames(showFrames)
    , m_showPreprocessed(showPreprocessed)
    , m_cancel(false)
{
}

void MeasureWorker::run()
{
    QElapsedTimer timer;
    timer.start();
    qint64 last = -1000, lastFrame = -1000;
    turb::FrameFn frameView;
    if (m_showFrames)
        frameView = [&](int index, const cv::Mat &raw) {
            // converting every frame would slow the measurement down: ~15 frames per second
            if (timer.elapsed() - lastFrame < 66)
                return;
            lastFrame = timer.elapsed();
            if (m_showPreprocessed && m_settings.preprocess.clahe) {
                cv::Mat shown = raw.clone();
                turb::previewPreprocess(shown, turb::effectiveRoi(m_rec.roi, raw.size()), m_settings.preprocess);
                emit frame(toQImage(shown), index);
                return;
            }
            emit frame(toQImage(raw), index);
        };
    const turb::MeasureOutput out = turb::measure(
        m_rec, m_settings,
        [&](int doneFrames, int total) {
            // at most ~20 updates per second reach the GUI
            if (doneFrames == total || timer.elapsed() - last >= 50) {
                last = timer.elapsed();
                emit progress(doneFrames, total);
            }
        },
        &m_cancel, frameView);

    if (!out.ok) {
        emit done(false, QString::fromStdString(out.error));
        return;
    }
    std::string err;
    int replaced = 0;
    if (!m_replace.isEmpty()) {
        std::vector<std::string> sources;
        for (const QString &s : m_replace)
            sources.push_back(s.toStdString());
        replaced = turb::removeRows(m_resultsPath.toStdString(), sources, out.preprocess, &err);
        if (replaced < 0) {
            emit done(false, QString::fromStdString(err));
            return;
        }
    }
    if (!turb::appendResults(m_resultsPath.toStdString(), m_rec, out, &err)) {
        emit done(false, QString::fromStdString(err));
        return;
    }
    int frames = 0;
    for (const turb::WindowResult &w : out.windows)
        frames += w.frames;
    emit done(true, tr("%1: %2 window(s), %3 frames measured in %4 s")
                        .arg(QString::fromStdString(out.recordingId))
                        .arg(out.windows.size())
                        .arg(frames)
                        .arg(timer.elapsed() / 1000.0, 0, 'f', 1) +
                (replaced > 0 ? tr(" (replaced %1 earlier row(s))").arg(replaced) : QString()));
}
