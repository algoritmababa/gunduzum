#ifndef ANALYSISWORKER_H
#define ANALYSISWORKER_H

#include "AnalysisEngine.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>

Q_DECLARE_METATYPE(turb::AnalysisOutput)

// Runs the AnalysisEngine on a worker thread. Only coarse, throttled signals are sent
// to the GUI: progress, current frame, status, result and error.
class AnalysisWorker : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisWorker(const turb::AnalysisConfig &config, QObject *parent = nullptr);

    // Thread-safe; may be called from the GUI thread while run() executes.
    void cancel() { m_cancel = true; }

public slots:
    void run();

signals:
    void progress(int percent);
    void frameProgress(int processed, int total, int frameIndex);
    void status(const QString &text);
    void resultReady(const turb::AnalysisOutput &output);
    void error(const QString &message);
    void finished();

private:
    turb::AnalysisConfig m_config;
    std::atomic<bool> m_cancel;
    QElapsedTimer m_timer;
    qint64 m_lastEmit = -1;
    std::string m_lastStatus;
};

#endif
