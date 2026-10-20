#include "AlgorithmRegistry.h"

#include "algorithms/Algorithms.h"

#include <algorithm>

namespace turb {

const AlgorithmRegistry &AlgorithmRegistry::instance()
{
    static const AlgorithmRegistry registry;
    return registry;
}

template <typename T> void AlgorithmRegistry::add(int priority)
{
    T probe;
    Entry e;
    e.id = probe.id();
    e.name = probe.name();
    e.group = probe.group();
    e.priority = priority;
    e.factory = []() { return AlgorithmPtr(new T); };
    m_entries.push_back(e);
}

AlgorithmRegistry::AlgorithmRegistry()
{
    add<TemporalIntensityVariance>(1);
    add<LaplacianVariance>(2);
    add<PhaseCorrelationDisplacement>(3);
    add<CentroidJitter>(4);
    add<OpticalFlowLK>(5);
    add<ScintillationIndex>(6);
    add<GradientMagnitudeVariance>(7);
    add<TemporalStructureAnalysis>(8);
    std::stable_sort(m_entries.begin(), m_entries.end(),
                     [](const Entry &a, const Entry &b) { return a.priority < b.priority; });
}

AlgorithmPtr AlgorithmRegistry::create(const std::string &id) const
{
    const Entry *e = find(id);
    return e ? e->factory() : AlgorithmPtr();
}

const AlgorithmRegistry::Entry *AlgorithmRegistry::find(const std::string &id) const
{
    for (const Entry &e : m_entries)
        if (e.id == id)
            return &e;
    return nullptr;
}

} // namespace turb
