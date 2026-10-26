#include "OptimizationWorker.h"

OptimizationWorker::OptimizationWorker(const turb::OptimizationConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_cancel(false)
{
}

void OptimizationWorker::run()
{
    m_timer.start();
    turb::ParameterOptimizer optimizer;
    const turb::OptimizationReport report = optimizer.run(
        m_config,
        // called serialized by the optimizer (possibly from its pool threads); throttled to ~10 Hz
        [this](const turb::OptimizationProgress &p) {
            const qint64 now = m_timer.elapsed();
            if (p.samplesDone != p.samplesTotal && m_lastEmit >= 0 && now - m_lastEmit < 100)
                return;
            m_lastEmit = now;
            emit progress(QString::fromStdString(p.stage), p.samplesDone, p.samplesTotal, p.evaluations,
                          p.maxEvaluations);
        },
        [this](const std::string &line) { emit logLine(QString::fromStdString(line)); }, &m_cancel);
    emit reportReady(report);
    emit finished();
}
