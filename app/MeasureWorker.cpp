#include "MeasureWorker.h"

#include "ResultTable.h"

#include <QElapsedTimer>

MeasureWorker::MeasureWorker(const turb::Recording &rec, const turb::MeasureSettings &settings,
                             const QString &resultsPath)
    : m_rec(rec)
    , m_settings(settings)
    , m_resultsPath(resultsPath)
    , m_cancel(false)
{
}

void MeasureWorker::run()
{
    QElapsedTimer timer;
    timer.start();
    qint64 last = -1000;
    const turb::MeasureOutput out = turb::measure(
        m_rec, m_settings,
        [&](int doneFrames, int total) {
            // at most ~20 updates per second reach the GUI
            if (doneFrames == total || timer.elapsed() - last >= 50) {
                last = timer.elapsed();
                emit progress(doneFrames, total);
            }
        },
        &m_cancel);

    if (!out.ok) {
        emit done(false, QString::fromStdString(out.error));
        return;
    }
    std::string err;
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
                        .arg(timer.elapsed() / 1000.0, 0, 'f', 1));
}
