#ifndef OPTIMIZATIONWORKER_H
#define OPTIMIZATIONWORKER_H

#include "optimization/ParameterOptimizer.h"

#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <atomic>

Q_DECLARE_METATYPE(turb::OptimizationReport)

// Runs the ParameterOptimizer on a worker thread (same pattern as AnalysisWorker).
class OptimizationWorker : public QObject
{
    Q_OBJECT

public:
    explicit OptimizationWorker(const turb::OptimizationConfig &config, QObject *parent = nullptr);
    void cancel() { m_cancel = true; }

public slots:
    void run();

signals:
    void progress(const QString &stage, int samplesDone, int samplesTotal, int evaluations, int maxEvaluations);
    void logLine(const QString &line);
    void reportReady(const turb::OptimizationReport &report);
    void finished();

private:
    turb::OptimizationConfig m_config;
    std::atomic<bool> m_cancel;
    QElapsedTimer m_timer;
    qint64 m_lastEmit = -1;
};

#endif
