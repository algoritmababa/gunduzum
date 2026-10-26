#ifndef MEASUREWORKER_H
#define MEASUREWORKER_H

#include "Measurement.h"

#include <QObject>
#include <QString>

#include <atomic>

// Measures one recording on a worker thread and appends the rows to the results file.
class MeasureWorker : public QObject
{
    Q_OBJECT

public:
    MeasureWorker(const turb::Recording &rec, const turb::MeasureSettings &settings, const QString &resultsPath);
    void cancel() { m_cancel = true; }

public slots:
    void run();

signals:
    void progress(int framesDone, int framesTotal);
    // ok = rows were appended; message describes the result or the error
    void done(bool ok, const QString &message);

private:
    turb::Recording m_rec;
    turb::MeasureSettings m_settings;
    QString m_resultsPath;
    std::atomic<bool> m_cancel;
};

#endif
