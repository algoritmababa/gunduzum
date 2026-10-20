#include "AnalysisWorker.h"

AnalysisWorker::AnalysisWorker(const turb::AnalysisConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_cancel(false)
{
}

void AnalysisWorker::run()
{
    m_timer.start();
    emit status(tr("Running..."));

    turb::AnalysisEngine engine;
    const turb::AnalysisOutput out = engine.run(
        m_config,
        [this](const turb::AnalysisProgress &p) {
            // throttle to ~20 updates per second so the GUI event queue is never flooded
            const qint64 now = m_timer.elapsed();
            const bool last = p.processed == p.total;
            if (!last && m_lastEmit >= 0 && now - m_lastEmit < 50)
                return;
            m_lastEmit = now;
            emit progress(p.total > 0 ? int(100LL * p.processed / p.total) : 0);
            emit frameProgress(p.processed, p.total, p.frameIndex);
            if (p.status != m_lastStatus) {
                m_lastStatus = p.status;
                emit status(QString::fromStdString(p.status));
            }
        },
        &m_cancel);

    if (!out.ok)
        emit error(QString::fromStdString(out.error));
    emit resultReady(out);
    emit finished();
}
