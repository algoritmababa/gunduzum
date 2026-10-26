#include "ParameterOptimizer.h"

#include "../AlgorithmRegistry.h"
#include "../FrameSource.h"
#include "../Statistics.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

namespace turb {

using separability::Matrix;

namespace {

const double kNegInf = -std::numeric_limits<double>::infinity();

std::string formatValue(double v)
{
    std::ostringstream os;
    os.precision(6);
    os << v;
    return os.str();
}

std::string paramKey(const std::string &id, const ParamSet &p)
{
    std::ostringstream os;
    os.precision(12);
    os << id;
    for (const auto &kv : p.values())
        os << "|" << kv.first << "=" << kv.second;
    return os.str();
}

struct Evaluation {
    std::vector<std::string> keys;
    Matrix X;
    std::vector<double> fisher; // per column (0 for unusable columns)
    std::vector<size_t> usable;
    double score = kNegInf;
    double accuracy = 0.0;
    double fisherTop3 = 0.0;
    std::vector<std::vector<int> > confusion;
};

struct Candidate {
    std::string id;
    ParamSet params;
    std::string key;
};

struct SearchState {
    std::string id, name;
    std::vector<ParamSpec> specs;
    std::vector<size_t> tunable;                       // indices into specs
    std::map<std::string, std::pair<double, double> > range; // current numeric search range
    ParamSet initial, best;
    std::string initialKey, bestKey;
    double bestScore = kNegInf;
    int level = 0;
    size_t cursor = 0;
    bool done = false;
    int evaluations = 0;
    std::vector<Candidate> pending;
    std::vector<std::string> trace;
};

} // namespace

// =====================================================================================

std::string ParameterOptimizer::describeParams(const std::string &algorithmId, const ParamSet &params)
{
    AlgorithmPtr a = AlgorithmRegistry::instance().create(algorithmId);
    if (!a)
        return std::string();
    std::ostringstream os;
    bool first = true;
    for (const ParamSpec &s : a->parameterSpecs()) {
        const double v = params.get(s.key, s.defaultValue);
        if (!first)
            os << ", ";
        first = false;
        os << s.key << "=";
        if (s.type == ParamSpec::Choice && v >= 0 && size_t(v) < s.choices.size())
            os << s.choices[size_t(v)];
        else if (s.type == ParamSpec::Bool)
            os << (v != 0.0 ? "true" : "false");
        else
            os << formatValue(v);
    }
    return os.str();
}

bool ParameterOptimizer::buildSamples(const OptimizationConfig &config, std::vector<SampleWindow> &samples,
                                      std::string *error)
{
    samples.clear();
    for (size_t si = 0; si < config.sources.size(); ++si) {
        const LabeledSource &src = config.sources[si];
        std::string err;
        std::unique_ptr<FrameSource> fs = FrameSource::create(src.path, &err);
        if (!fs) {
            if (error) *error = err;
            return false;
        }
        const int count = fs->frameCount();
        std::vector<int> starts;
        int len = config.windowLength;
        if (len <= 0 || len >= count) {
            len = count;
            starts.push_back(0);
        } else {
            const int stride = config.windowStride > 0 ? config.windowStride : len;
            for (int s = 0; s + len <= count; s += stride)
                starts.push_back(s);
        }
        if (config.maxWindowsPerSource > 0 && int(starts.size()) > config.maxWindowsPerSource) {
            // keep windows spread evenly over the whole recording
            std::vector<int> picked;
            const int n = int(starts.size()), m = config.maxWindowsPerSource;
            for (int i = 0; i < m; ++i)
                picked.push_back(starts[size_t(m == 1 ? 0 : std::lround(double(i) * (n - 1) / (m - 1)))]);
            picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
            starts = picked;
        }
        for (int s : starts) {
            SampleWindow w;
            w.source = int(si);
            w.label = src.label;
            w.start = s;
            w.end = s + len - 1;
            samples.push_back(w);
        }
    }
    return true;
}

// =====================================================================================

namespace {

class OptimizerRun {
public:
    OptimizerRun(const OptimizationConfig &cfg, const ParameterOptimizer::ProgressCallback &progress,
                 const ParameterOptimizer::LogCallback &log, const std::atomic<bool> *cancel)
        : m_cfg(cfg), m_progress(progress), m_log(log), m_cancel(cancel)
    {
    }

    OptimizationReport run();

private:
    void log(const std::string &s)
    {
        if (m_log)
            m_log(s);
    }
    bool cancelled() const { return m_cancel && m_cancel->load(); }
    void report(const std::string &stage, int done, int total);

    bool evaluateBatch(const std::vector<Candidate> &batch, const std::string &stage);
    void scoreEvaluation(Evaluation &e) const;
    void scoreMatrix(const Matrix &X, double &score, double &accuracy, double &fisherTop3,
                     std::vector<std::vector<int> > *confusion, std::vector<double> *fisher,
                     std::vector<size_t> *usable) const;
    std::vector<double> candidateValues(const SearchState &st, const ParamSpec &s) const;
    void finishSweepStep(SearchState &st);
    AlgorithmOutcome makeOutcome(const SearchState &st) const;
    void runSelection(OptimizationReport &rep) const;

    const OptimizationConfig &m_cfg;
    ParameterOptimizer::ProgressCallback m_progress;
    ParameterOptimizer::LogCallback m_log;
    const std::atomic<bool> *m_cancel;

    std::vector<SampleWindow> m_samples;
    std::vector<int> m_y, m_groups;
    int m_classes = 0;
    std::map<std::string, Evaluation> m_cache;
    int m_evaluations = 0;
    int m_batch = 0;
    std::string m_error;
    std::mutex m_progressMutex;
};

void OptimizerRun::report(const std::string &stage, int done, int total)
{
    if (!m_progress)
        return;
    std::lock_guard<std::mutex> lock(m_progressMutex);
    OptimizationProgress p;
    p.stage = stage;
    p.batch = m_batch;
    p.samplesDone = done;
    p.samplesTotal = total;
    p.evaluations = m_evaluations;
    p.maxEvaluations = m_cfg.maxEvaluations;
    m_progress(p);
}

void OptimizerRun::scoreMatrix(const Matrix &X, double &score, double &accuracy, double &fisherTop3,
                               std::vector<std::vector<int> > *confusion, std::vector<double> *fisher,
                               std::vector<size_t> *usable) const
{
    const std::vector<size_t> cols = separability::usableColumns(X);
    std::vector<double> f(X.empty() ? 0 : X.front().size(), 0.0);
    std::vector<double> fu;
    for (size_t c : cols) {
        std::vector<double> col(X.size());
        for (size_t i = 0; i < X.size(); ++i)
            col[i] = X[i][c];
        f[c] = separability::fisherRatio(col, m_y, m_classes);
        fu.push_back(f[c]);
    }
    std::sort(fu.begin(), fu.end(), std::greater<double>());
    fisherTop3 = 0.0;
    const size_t top = std::min<size_t>(3, fu.size());
    for (size_t i = 0; i < top; ++i)
        fisherTop3 += fu[i] / double(top);

    const separability::CvResult cv = separability::nearestCentroidCv(X, m_y, m_groups, m_classes, cols);
    accuracy = cols.empty() ? 0.0 : cv.balancedAccuracy;
    if (cols.empty())
        score = kNegInf;
    else if (m_cfg.objective == OptimizationConfig::FisherTop3)
        score = fisherTop3;
    else // accuracy; the bounded Fisher term only breaks ties between equal accuracies
        score = accuracy + 0.01 * fisherTop3 / (1.0 + fisherTop3);
    if (confusion)
        *confusion = cv.confusion;
    if (fisher)
        *fisher = f;
    if (usable)
        *usable = cols;
}

void OptimizerRun::scoreEvaluation(Evaluation &e) const
{
    scoreMatrix(e.X, e.score, e.accuracy, e.fisherTop3, &e.confusion, &e.fisher, &e.usable);
}

bool OptimizerRun::evaluateBatch(const std::vector<Candidate> &batchIn, const std::string &stage)
{
    std::vector<Candidate> batch;
    std::set<std::string> seen;
    for (const Candidate &c : batchIn)
        if (!m_cache.count(c.key) && seen.insert(c.key).second)
            batch.push_back(c);
    if (batch.empty())
        return true;

    ++m_batch;
    {
        std::ostringstream os;
        os << "Data pass " << m_batch << ": " << batch.size() << " parameter set(s) on " << m_samples.size()
           << " window(s)";
        log(os.str());
    }

    const size_t nS = m_samples.size(), nC = batch.size();
    std::vector<Evaluation> evals(nC);
    for (Evaluation &e : evals)
        e.X.assign(nS, std::vector<double>());

    std::atomic<size_t> next(0);
    std::atomic<int> done(0);
    std::atomic<bool> failed(false);
    std::mutex errMutex;
    std::string err;

    auto worker = [&]() {
        AnalysisEngine engine;
        for (;;) {
            const size_t si = next.fetch_add(1);
            if (si >= nS || failed.load() || cancelled())
                return;
            const SampleWindow &w = m_samples[si];
            const LabeledSource &src = m_cfg.sources[size_t(w.source)];
            AnalysisConfig ac;
            ac.dataset.path = src.path;
            ac.dataset.frameRate = src.frameRate;
            ac.dataset.startFrame = w.start;
            ac.dataset.endFrame = w.end;
            ac.dataset.frameStep = std::max(1, m_cfg.frameStep);
            ac.preprocess = m_cfg.preprocess;
            ac.preprocess.roi = src.roi;
            ac.registration = m_cfg.registration;
            for (const Candidate &c : batch) {
                AlgorithmRun r;
                r.id = c.id;
                r.params = c.params;
                ac.algorithms.push_back(r);
            }
            const AnalysisOutput out = engine.run(ac, AnalysisEngine::ProgressCallback(), m_cancel);
            if (!out.ok || out.results.size() != nC) {
                if (!out.cancelled) {
                    std::lock_guard<std::mutex> lock(errMutex);
                    if (err.empty())
                        err = src.path + ": " + out.error;
                    failed = true;
                }
                return;
            }
            for (size_t c = 0; c < nC; ++c) {
                const AlgorithmResult &r = out.results[c];
                std::vector<double> &row = evals[c].X[si];
                if (m_cfg.useAllSummaryFeatures) {
                    for (const Feature &f : r.features)
                        row.push_back(f.value);
                } else {
                    row = r.featureVectorValues();
                }
                std::lock_guard<std::mutex> lock(errMutex); // feature names, set once per candidate
                if (evals[c].keys.empty()) {
                    if (m_cfg.useAllSummaryFeatures)
                        for (const Feature &f : r.features)
                            evals[c].keys.push_back(f.key);
                    else
                        evals[c].keys = r.featureVector;
                }
            }
            report(stage, ++done, int(nS));
        }
    };

    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const size_t nThreads = std::min<size_t>(nS, m_cfg.threads > 0 ? size_t(m_cfg.threads) : hw);
    std::vector<std::thread> pool;
    for (size_t t = 1; t < nThreads; ++t)
        pool.push_back(std::thread(worker));
    worker();
    for (std::thread &t : pool)
        t.join();

    if (cancelled())
        return false;
    if (failed) {
        m_error = err;
        return false;
    }
    for (size_t c = 0; c < nC; ++c) {
        scoreEvaluation(evals[c]);
        m_cache[batch[c].key] = std::move(evals[c]);
        ++m_evaluations;
    }
    return true;
}

std::vector<double> OptimizerRun::candidateValues(const SearchState &st, const ParamSpec &s) const
{
    std::vector<double> v;
    const double current = st.best.get(s.key, s.defaultValue);
    if (s.type == ParamSpec::Choice) {
        for (size_t i = 0; i < s.choices.size(); ++i)
            v.push_back(double(i));
    } else if (s.type == ParamSpec::Bool) {
        v.push_back(0.0);
        v.push_back(1.0);
    } else {
        const std::pair<double, double> r = st.range.at(s.key);
        const int g = std::max(2, m_cfg.gridPoints);
        const bool useLog = s.searchLog && r.first > 0.0 && r.second > 0.0;
        for (int i = 0; i < g; ++i) {
            const double t = double(i) / (g - 1);
            double x = useLog ? std::exp(std::log(r.first) + t * (std::log(r.second) - std::log(r.first)))
                              : r.first + t * (r.second - r.first);
            if (s.type == ParamSpec::Int)
                x = std::round(x);
            x = std::min(std::max(x, s.minValue), s.maxValue);
            v.push_back(x);
        }
        v.push_back(current);
    }
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end(), [](double a, double b) { return std::fabs(a - b) < 1e-12; }), v.end());
    return v;
}

void OptimizerRun::finishSweepStep(SearchState &st)
{
    // pick the best candidate of this step
    for (const Candidate &c : st.pending) {
        std::map<std::string, Evaluation>::const_iterator it = m_cache.find(c.key);
        if (it == m_cache.end())
            continue;
        if (it->second.score > st.bestScore + 1e-9) {
            const ParamSpec &s = st.specs[st.tunable[st.cursor]];
            std::ostringstream os;
            os << st.id << ": " << s.key << " -> " << formatValue(c.params.get(s.key, s.defaultValue))
               << "  score " << formatValue(it->second.score) << " (accuracy " << formatValue(it->second.accuracy)
               << ", level " << st.level << ")";
            st.trace.push_back(os.str());
            log(os.str());
            st.bestScore = it->second.score;
            st.best = c.params;
            st.bestKey = c.key;
        }
    }
    st.pending.clear();

    if (++st.cursor < st.tunable.size())
        return;
    st.cursor = 0;
    if (st.level + 1 >= m_cfg.refinementLevels) {
        st.done = true;
        return;
    }
    // recursive refinement: shrink every numeric range to one grid step around the best value
    ++st.level;
    const int g = std::max(2, m_cfg.gridPoints);
    for (size_t idx : st.tunable) {
        const ParamSpec &s = st.specs[idx];
        if (s.type != ParamSpec::Int && s.type != ParamSpec::Double)
            continue;
        std::pair<double, double> &r = st.range[s.key];
        const double v = st.best.get(s.key, s.defaultValue);
        const bool useLog = s.searchLog && r.first > 0.0 && r.second > 0.0 && v > 0.0;
        if (useLog) {
            const double step = (std::log(r.second) - std::log(r.first)) / (g - 1);
            r.first = std::max(s.searchMin, std::exp(std::log(v) - step));
            r.second = std::min(s.searchMax, std::exp(std::log(v) + step));
        } else {
            const double step = (r.second - r.first) / (g - 1);
            r.first = std::max(s.searchMin, v - step);
            r.second = std::min(s.searchMax, v + step);
        }
    }
}

AlgorithmOutcome OptimizerRun::makeOutcome(const SearchState &st) const
{
    AlgorithmOutcome o;
    o.id = st.id;
    o.name = st.name;
    o.initialParams = st.initial;
    o.bestParams = st.best;
    o.evaluations = st.evaluations;
    o.trace = st.trace;
    o.bestParamText = ParameterOptimizer::describeParams(st.id, st.best);

    std::map<std::string, Evaluation>::const_iterator ini = m_cache.find(st.initialKey);
    if (ini != m_cache.end()) {
        o.initialScore = ini->second.score;
        o.initialAccuracy = ini->second.accuracy;
    }
    std::map<std::string, Evaluation>::const_iterator it = m_cache.find(st.bestKey);
    if (it == m_cache.end())
        return o;
    const Evaluation &e = it->second;
    o.bestScore = e.score;
    o.bestAccuracy = e.accuracy;
    o.bestFisherTop3 = e.fisherTop3;
    o.featureKeys = e.keys;
    o.sampleFeatures = e.X;
    o.confusion = e.confusion;
    for (size_t c = 0; c < e.keys.size(); ++c) {
        FeatureSeparation fs;
        fs.key = e.keys[c];
        fs.fisher = c < e.fisher.size() ? e.fisher[c] : 0.0;
        for (int k = 0; k < m_classes; ++k) {
            std::vector<double> v;
            for (size_t i = 0; i < e.X.size(); ++i)
                if (m_y[i] == k && c < e.X[i].size())
                    v.push_back(e.X[i][c]);
            const SeriesStats s = stats::summarize(v);
            fs.classMean.push_back(s.mean);
            fs.classStd.push_back(s.stddev);
        }
        o.features.push_back(fs);
    }
    std::stable_sort(o.features.begin(), o.features.end(),
                     [](const FeatureSeparation &a, const FeatureSeparation &b) { return a.fisher > b.fisher; });
    return o;
}

void OptimizerRun::runSelection(OptimizationReport &rep) const
{
    // only algorithms that produced usable features take part
    std::vector<const AlgorithmOutcome *> pool;
    for (const AlgorithmOutcome &o : rep.algorithms)
        if (std::isfinite(o.bestScore) && !o.sampleFeatures.empty())
            pool.push_back(&o);
    if (pool.size() < 2)
        return;

    auto combinedScore = [&](const std::vector<const AlgorithmOutcome *> &set, double &acc) {
        Matrix X(m_samples.size());
        for (const AlgorithmOutcome *o : set)
            for (size_t i = 0; i < X.size(); ++i)
                X[i].insert(X[i].end(), o->sampleFeatures[i].begin(), o->sampleFeatures[i].end());
        double score = 0.0, f3 = 0.0;
        scoreMatrix(X, score, acc, f3, nullptr, nullptr, nullptr);
        return score;
    };
    auto names = [](const std::vector<const AlgorithmOutcome *> &set) {
        std::vector<std::string> n;
        for (const AlgorithmOutcome *o : set)
            n.push_back(o->id);
        return n;
    };

    std::vector<const AlgorithmOutcome *> current = pool;
    SelectionStep first;
    first.algorithms = names(current);
    first.score = combinedScore(current, first.accuracy);
    rep.selection.push_back(first);

    // recursive elimination: drop the algorithm whose removal keeps the score highest
    while (current.size() > 1) {
        double bestScore = kNegInf, bestAcc = 0.0;
        size_t bestIdx = 0;
        for (size_t i = 0; i < current.size(); ++i) {
            std::vector<const AlgorithmOutcome *> trial = current;
            trial.erase(trial.begin() + long(i));
            double acc = 0.0;
            const double sc = combinedScore(trial, acc);
            if (sc > bestScore) {
                bestScore = sc;
                bestAcc = acc;
                bestIdx = i;
            }
        }
        SelectionStep step;
        step.removed = current[bestIdx]->id;
        current.erase(current.begin() + long(bestIdx));
        step.algorithms = names(current);
        step.score = bestScore;
        step.accuracy = bestAcc;
        rep.selection.push_back(step);
    }

    // best subset; on ties the smaller set (later step) wins
    double best = kNegInf;
    for (size_t i = 0; i < rep.selection.size(); ++i)
        if (rep.selection[i].score >= best - 1e-9) {
            best = std::max(best, rep.selection[i].score);
            rep.bestSelectionStep = int(i);
        }
}

OptimizationReport OptimizerRun::run()
{
    OptimizationReport rep;
    const auto t0 = std::chrono::steady_clock::now();
    m_classes = int(m_cfg.classNames.size());
    if (m_classes < 2) {
        rep.error = "At least two classes are required.";
        return rep;
    }
    if (m_cfg.algorithmIds.empty()) {
        rep.error = "No candidate algorithm selected.";
        return rep;
    }

    std::string err;
    if (!ParameterOptimizer::buildSamples(m_cfg, m_samples, &err)) {
        rep.error = err;
        return rep;
    }
    std::vector<int> perClass(size_t(m_classes), 0);
    std::vector<std::set<int> > sourcesPerClass(static_cast<size_t>(m_classes));
    for (const SampleWindow &w : m_samples) {
        if (w.label < 0 || w.label >= m_classes) {
            rep.error = "Invalid class label.";
            return rep;
        }
        ++perClass[size_t(w.label)];
        sourcesPerClass[size_t(w.label)].insert(w.source);
        m_y.push_back(w.label);
    }
    for (int k = 0; k < m_classes; ++k)
        if (perClass[size_t(k)] < 2) {
            rep.error = "Class '" + m_cfg.classNames[size_t(k)] +
                        "' needs at least 2 samples (add recordings or use shorter windows).";
            return rep;
        }
    rep.samples = m_samples;

    // cross-validation groups: whole recordings when possible (windows of one recording are correlated)
    bool grouped = true;
    for (int k = 0; k < m_classes; ++k)
        grouped = grouped && sourcesPerClass[size_t(k)].size() >= 2;
    for (size_t i = 0; i < m_samples.size(); ++i)
        m_groups.push_back(grouped ? m_samples[i].source : int(i));
    {
        std::ostringstream os;
        if (grouped) {
            os << "Leave-one-recording-out cross-validation (" << std::set<int>(m_groups.begin(), m_groups.end()).size()
               << " folds), nearest-centroid classifier on z-scored features.";
        } else {
            os << "Leave-one-window-out cross-validation (" << m_samples.size()
               << " folds), nearest-centroid classifier on z-scored features.";
            rep.notes.push_back("At least one class has a single recording: windows of the same recording are "
                                "correlated, so the accuracy is optimistic. Add at least 2 recordings per class "
                                "for leave-one-recording-out validation.");
        }
        rep.validation = os.str();
    }
    rep.notes.push_back("The same cross-validation is used to choose the parameters (no nested validation): "
                        "confirm the selected configuration on recordings that were not used here.");
    {
        std::ostringstream os;
        os << m_samples.size() << " labeled window(s) from " << m_cfg.sources.size() << " recording(s); "
           << rep.validation;
        log(os.str());
    }

    // --- search states ------------------------------------------------------------------
    std::vector<SearchState> states;
    for (const std::string &id : m_cfg.algorithmIds) {
        AlgorithmPtr a = AlgorithmRegistry::instance().create(id);
        if (!a) {
            rep.error = "Unknown algorithm: " + id;
            return rep;
        }
        SearchState st;
        st.id = id;
        st.name = a->name();
        st.specs = a->parameterSpecs();
        std::map<std::string, ParamSet>::const_iterator ip = m_cfg.initialParams.find(id);
        st.initial = ParamSet::withDefaults(st.specs, ip != m_cfg.initialParams.end() ? ip->second : ParamSet());
        st.best = st.initial;
        st.initialKey = st.bestKey = paramKey(id, st.initial);
        for (size_t i = 0; i < st.specs.size(); ++i) {
            const ParamSpec &s = st.specs[i];
            if (!s.tunable)
                continue;
            if ((s.type == ParamSpec::Int || s.type == ParamSpec::Double) && !(s.searchMax > s.searchMin))
                continue;
            st.tunable.push_back(i);
            if (s.type == ParamSpec::Int || s.type == ParamSpec::Double)
                st.range[s.key] = std::make_pair(s.searchMin, s.searchMax);
        }
        states.push_back(st);
    }

    // initial parameter sets
    {
        std::vector<Candidate> batch;
        for (SearchState &st : states) {
            Candidate c;
            c.id = st.id;
            c.params = st.initial;
            c.key = st.initialKey;
            batch.push_back(c);
            ++st.evaluations;
        }
        if (!evaluateBatch(batch, "Initial parameters")) {
            rep.cancelled = cancelled();
            rep.error = rep.cancelled ? "Cancelled by user." : m_error;
            return rep;
        }
        for (SearchState &st : states) {
            st.bestScore = m_cache[st.initialKey].score;
            std::ostringstream os;
            os << st.id << ": initial score " << formatValue(st.bestScore) << " (accuracy "
               << formatValue(m_cache[st.initialKey].accuracy) << ")";
            st.trace.push_back(os.str());
            log(os.str());
            if (st.tunable.empty() || m_cfg.refinementLevels <= 0)
                st.done = true;
        }
    }

    // coordinate-wise, coarse-to-fine search; all algorithms advance together
    for (;;) {
        if (m_evaluations >= m_cfg.maxEvaluations) {
            bool open = false;
            for (const SearchState &st : states)
                open = open || !st.done;
            if (open) {
                rep.notes.push_back("Evaluation budget reached before the search finished; increase "
                                    "'max evaluations' for a more thorough search.");
                log("Evaluation budget reached.");
            }
            break;
        }
        std::vector<Candidate> batch;
        std::ostringstream stage;
        for (SearchState &st : states) {
            if (st.done)
                continue;
            const ParamSpec &s = st.specs[st.tunable[st.cursor]];
            for (double v : candidateValues(st, s)) {
                Candidate c;
                c.id = st.id;
                c.params = st.best;
                c.params.set(s.key, v);
                c.key = paramKey(st.id, c.params);
                st.pending.push_back(c);
                if (!m_cache.count(c.key))
                    ++st.evaluations;
                batch.push_back(c);
            }
            stage << (stage.tellp() > 0 ? ", " : "") << st.id << "." << s.key << " (L" << st.level << ")";
        }
        if (batch.empty())
            break;
        if (!evaluateBatch(batch, "Searching " + stage.str())) {
            rep.cancelled = cancelled();
            rep.error = rep.cancelled ? "Cancelled by user." : m_error;
            return rep;
        }
        for (SearchState &st : states)
            if (!st.done && !st.pending.empty())
                finishSweepStep(st);
    }

    for (const SearchState &st : states)
        rep.algorithms.push_back(makeOutcome(st));
    std::stable_sort(rep.algorithms.begin(), rep.algorithms.end(),
                     [](const AlgorithmOutcome &a, const AlgorithmOutcome &b) { return a.bestScore > b.bestScore; });

    if (m_cfg.selectAlgorithms) {
        report("Recursive algorithm elimination", 0, 0);
        runSelection(rep);
        if (rep.bestSelectionStep >= 0) {
            const SelectionStep &b = rep.selection[size_t(rep.bestSelectionStep)];
            std::ostringstream os;
            os << "Best algorithm set:";
            for (const std::string &n : b.algorithms)
                os << " " << n;
            os << "  score " << formatValue(b.score) << " (accuracy " << formatValue(b.accuracy) << ")";
            log(os.str());
        }
    }

    rep.evaluations = m_evaluations;
    rep.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    rep.ok = true;
    return rep;
}

} // namespace

OptimizationReport ParameterOptimizer::run(const OptimizationConfig &config, const ProgressCallback &progress,
                                           const LogCallback &log, const std::atomic<bool> *cancel)
{
    try {
        OptimizerRun r(config, progress, log, cancel);
        return r.run();
    } catch (const std::exception &e) {
        OptimizationReport rep;
        rep.error = std::string("Error: ") + e.what();
        return rep;
    }
}

} // namespace turb
