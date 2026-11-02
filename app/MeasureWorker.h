#ifndef MEASUREWORKER_H
#define MEASUREWORKER_H

#include "Measurement.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>

// Measures one recording on a worker thread and appends the rows to the results file.
class MeasureWorker : public QObject
{
    Q_OBJECT

public:
    // showFrames: send the frames being measured to the GUI (throttled to ~15 per second);
    // showPreprocessed: those frames show the ROI after the preprocessing (what the metrics see)
    MeasureWorker(const turb::Recording &rec, const turb::MeasureSettings &settings, const QString &resultsPath,
                  bool showFrames = false, bool showPreprocessed = false);
    void cancel() { m_cancel = true; }

public slots:
    void run();

signals:
    void progress(int framesDone, int framesTotal);
    void frame(const QImage &image, int frameIndex);
    // ok = rows were appended; message describes the result or the error
    void done(bool ok, const QString &message);

private:
    turb::Recording m_rec;
    turb::MeasureSettings m_settings;
    QString m_resultsPath;
    bool m_showFrames;
    bool m_showPreprocessed;
    std::atomic<bool> m_cancel;
};

#endif
