#include "TrendAnalysis.h"

#include "Metrics.h"
#include "Stats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>

namespace turb {

const char *verdictName(TrendVerdict v)
{
    switch (v) {
    case TrendVerdict::ConsistentIncrease: return "consistent increase";
    case TrendVerdict::ConsistentDecrease: return "consistent decrease";
    case TrendVerdict::SignificantNotMonotonic: return "significant, not monotonic";
    case TrendVerdict::NotSignificant: return "ordered, not significant";
    case TrendVerdict::NoTrend: return "no consistent trend";
    case TrendVerdict::InsufficientData: return "insufficient data";
    }
    return "";
}

std::vector<double> averageRanks(const std::vector<double> &v)
{
    std::vector<size_t> idx(v.size());
    std::iota(idx.begin(), idx.end(), size_t(0));
    std::sort(idx.begin(), idx.end(), [&v](size_t a, size_t b) { return v[a] < v[b]; });
    std::vector<double> rank(v.size());
    for (size_t i = 0; i < idx.size();) {
        size_t j = i;
        while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]])
            ++j;
        const double r = (double(i) + double(j)) / 2.0 + 1.0; // ties share the average rank
        for (size_t k = i; k <= j; ++k)
            rank[idx[k]] = r;
        i = j + 1;
    }
    return rank;
}

static double pearson(const std::vector<double> &a, const std::vector<double> &b)
{
    const size_t n = a.size();
    if (n < 2)
        return 0.0;
    const double ma = std::accumulate(a.begin(), a.end(), 0.0) / n;
    const double mb = std::accumulate(b.begin(), b.end(), 0.0) / n;
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return (saa > 0 && sbb > 0) ? sab / std::sqrt(saa * sbb) : 0.0;
}

double spearman(const std::vector<double> &x, const std::vector<double> &y)
{
    return pearson(averageRanks(x), averageRanks(y));
}

std::vector<double> holmAdjust(const std::vector<double> &p)
{
    const size_t m = p.size();
    std::vector<size_t> idx(m);
    std::iota(idx.begin(), idx.end(), size_t(0));
    std::sort(idx.begin(), idx.end(), [&p](size_t a, size_t b) { return p[a] < p[b]; });
    std::vector<double> adj(m, 1.0);
    double running = 0.0;
    for (size_t k = 0; k < m; ++k) {
        running = std::max(running, std::min(1.0, double(m - k) * p[idx[k]]));
        adj[idx[k]] = running;
    }
    return adj;
}

static double sampleStd(const std::vector<double> &v, double mean)
{
    if (v.size() < 2)
        return nan();
    double acc = 0.0;
    for (double x : v)
        acc += (x - mean) * (x - mean);
    return std::sqrt(acc / double(v.size() - 1));
}

TrendReport analyzeTrends(const Table &table, const TrendOptions &opt)
{
    TrendReport rep;
    const int cCat = table.column("category"), cSrc = table.column("source");
    if (cCat < 0 || cSrc < 0) {
        rep.error = "The table has no 'category' / 'source' column (not a results file?).";
        return rep;
    }

    // categories of the order that are present
    for (const std::string &c : opt.categoryOrder)
        for (const std::vector<std::string> &r : table.rows)
            if (size_t(cCat) < r.size() && r[size_t(cCat)] == c) {
                rep.categories.push_back(c);
                break;
            }
    {
        int ignored = 0;
        for (const std::vector<std::string> &r : table.rows) {
            if (size_t(cCat) >= r.size() || r[size_t(cCat)].empty())
                continue;
            if (std::find(rep.categories.begin(), rep.categories.end(), r[size_t(cCat)]) == rep.categories.end())
                ++ignored;
        }
        if (ignored > 0) {
            std::ostringstream os;
            os << ignored << " labeled row(s) with a category outside the order were ignored.";
            rep.notes.push_back(os.str());
        }
    }
    if (rep.categories.size() < 2) {
        rep.error = "At least two of the ordered categories need labeled measurements.";
        return rep;
    }
    const size_t K = rep.categories.size();

    // analysis units: recordings (source + category) or single windows
    const std::vector<std::string> features = featureNames();
    std::vector<int> unitCat;                       // category rank of every unit (0-based)
    std::vector<std::vector<double> > unitValues;   // [unit][feature]
    {
        const int cRec = table.column("recording"), cNote = table.column("note"), cWin = table.column("window");
        auto field = [](const std::vector<std::string> &r, int c) {
            return (c >= 0 && size_t(c) < r.size()) ? r[size_t(c)] : std::string();
        };
        std::map<std::pair<std::string, int>, size_t> unitOf;
        std::vector<std::vector<std::vector<double> > > acc; // [unit][feature] -> window values
        for (const std::vector<std::string> &r : table.rows) {
            if (size_t(std::max(cCat, cSrc)) >= r.size())
                continue;
            const std::vector<std::string>::const_iterator it =
                std::find(rep.categories.begin(), rep.categories.end(), r[size_t(cCat)]);
            if (it == rep.categories.end())
                continue;
            const int cat = int(it - rep.categories.begin());
            size_t u;
            const std::pair<std::string, int> key(r[size_t(cSrc)], cat);
            if (opt.perRecording && unitOf.count(key)) {
                u = unitOf[key];
            } else {
                u = acc.size();
                acc.push_back(std::vector<std::vector<double> >(features.size()));
                unitCat.push_back(cat);
                TrendUnit info;
                info.id = field(r, cRec).empty() ? recordingId(r[size_t(cSrc)]) : field(r, cRec);
                if (!opt.perRecording)
                    info.id += " #" + field(r, cWin);
                info.source = r[size_t(cSrc)];
                info.note = field(r, cNote);
                info.category = cat;
                rep.unitList.push_back(info);
                if (opt.perRecording)
                    unitOf[key] = u;
            }
            ++rep.unitList[u].windows;
            for (size_t f = 0; f < features.size(); ++f) {
                const int col = table.column(features[f]);
                if (col >= 0 && size_t(col) < r.size())
                    acc[u][f].push_back(std::strtod(r[size_t(col)].c_str(), nullptr));
            }
        }
        for (size_t u = 0; u < acc.size(); ++u) {
            std::vector<double> vals;
            for (const std::vector<double> &v : acc[u]) {
                const Summary s = summarize(v);
                vals.push_back(s.mean);
                rep.unitList[u].windowCv.push_back(s.count >= 2 && std::fabs(s.mean) > 0 ? s.std / std::fabs(s.mean)
                                                                                         : nan());
            }
            rep.unitList[u].value = vals;
            unitValues.push_back(vals);
        }
    }
    rep.units = int(unitValues.size());
    rep.unitsPerCategory.assign(K, 0);
    for (int c : unitCat)
        ++rep.unitsPerCategory[size_t(c)];

    // smallest attainable p: 1 / (number of distinct label arrangements), capped by the permutations
    {
        double logArr = std::lgamma(double(rep.units) + 1.0);
        for (int n : rep.unitsPerCategory)
            logArr -= std::lgamma(double(n) + 1.0);
        const double arrangements = std::exp(logArr);
        rep.minPossibleP = std::max(1.0 / std::max(1.0, arrangements / 2.0), // |rho| is symmetric
                                    1.0 / (opt.permutations + 1.0));
    }
    const bool enough = *std::min_element(rep.unitsPerCategory.begin(), rep.unitsPerCategory.end()) >= 2;
    if (!enough)
        rep.notes.push_back("Every category needs at least 2 " +
                            std::string(opt.perRecording ? "recordings" : "windows") +
                            " to judge the spread inside the category.");
    if (rep.minPossibleP > opt.alpha)
        rep.notes.push_back("With this few units no result can reach significance (smallest possible p is about " +
                            std::to_string(rep.minPossibleP) + "). Add recordings.");
    if (!opt.perRecording)
        rep.notes.push_back("Window-level analysis: windows of the same recording are correlated, "
                            "the p-values are optimistic.");

    // per feature
    std::mt19937 rng(opt.seed);
    std::vector<double> pValues;
    for (size_t f = 0; f < features.size(); ++f) {
        FeatureTrend ft;
        ft.feature = features[f];
        std::vector<double> x, y;
        std::vector<std::vector<double> > perCat(K);
        for (size_t u = 0; u < unitValues.size(); ++u) {
            const double v = unitValues[u][f];
            if (!std::isfinite(v))
                continue;
            x.push_back(v);
            y.push_back(double(unitCat[u] + 1));
            perCat[size_t(unitCat[u])].push_back(v);
        }
        for (size_t k = 0; k < K; ++k) {
            const double m = perCat[k].empty() ? nan() : summarize(perCat[k]).mean;
            ft.mean.push_back(m);
            ft.std.push_back(sampleStd(perCat[k], m));
            ft.count.push_back(int(perCat[k].size()));
            ft.minValue.push_back(perCat[k].empty() ? nan() : *std::min_element(perCat[k].begin(), perCat[k].end()));
            ft.maxValue.push_back(perCat[k].empty() ? nan() : *std::max_element(perCat[k].begin(), perCat[k].end()));
        }
        // units that fall inside the range of a neighbouring category (with data)
        for (size_t k = 0; k < K; ++k) {
            int n = 0;
            for (double v : perCat[k]) {
                bool inside = false;
                for (int dk = -1; dk <= 1; dk += 2) {
                    long j = long(k) + dk;
                    while (j >= 0 && j < long(K) && ft.count[size_t(j)] == 0)
                        j += dk; // skip empty categories
                    if (j >= 0 && j < long(K) && v >= ft.minValue[size_t(j)] && v <= ft.maxValue[size_t(j)])
                        inside = true;
                }
                n += inside ? 1 : 0;
            }
            ft.overlapping.push_back(n);
        }

        // monotonic category means
        bool up = true, down = true;
        int present = 0;
        double prev = nan();
        for (size_t k = 0; k < K; ++k) {
            if (ft.count[k] == 0)
                continue;
            if (present > 0) {
                up = up && ft.mean[k] > prev;
                down = down && ft.mean[k] < prev;
            }
            prev = ft.mean[k];
            ++present;
        }
        ft.monotonic = present >= 2 && (up || down);
        ft.direction = !ft.monotonic ? 0 : (up ? 1 : -1);

        // effect size between the first and the last category
        const size_t a = 0, b = K - 1;
        if (ft.count[a] >= 2 && ft.count[b] >= 2) {
            const double sp = std::sqrt((ft.std[a] * ft.std[a] + ft.std[b] * ft.std[b]) / 2.0);
            ft.effectSize = sp > 0 ? (ft.mean[b] - ft.mean[a]) / sp : nan();
        } else {
            ft.effectSize = nan();
        }

        // rank correlation and permutation test
        if (x.size() >= 3) {
            ft.rho = spearman(x, y);
            const std::vector<double> rx = averageRanks(x);
            std::vector<double> ry = averageRanks(y);
            int extreme = 0;
            for (int p = 0; p < opt.permutations; ++p) {
                std::shuffle(ry.begin(), ry.end(), rng);
                if (std::fabs(pearson(rx, ry)) >= std::fabs(ft.rho) - 1e-12)
                    ++extreme;
            }
            ft.pValue = (extreme + 1.0) / (opt.permutations + 1.0);
        } else {
            ft.rho = nan();
            ft.pValue = 1.0;
        }
        pValues.push_back(ft.pValue);
        rep.features.push_back(ft);
    }

    // multiple-testing correction and verdict
    const std::vector<double> adj = holmAdjust(pValues);
    for (size_t f = 0; f < rep.features.size(); ++f) {
        FeatureTrend &ft = rep.features[f];
        ft.pHolm = adj[f];
        const bool sig = ft.pHolm < opt.alpha;
        if (!enough || !std::isfinite(ft.rho))
            ft.verdict = TrendVerdict::InsufficientData;
        else if (sig && ft.monotonic && ft.direction * ft.rho > 0)
            ft.verdict = ft.direction > 0 ? TrendVerdict::ConsistentIncrease : TrendVerdict::ConsistentDecrease;
        else if (sig)
            ft.verdict = TrendVerdict::SignificantNotMonotonic;
        else if (ft.monotonic)
            ft.verdict = TrendVerdict::NotSignificant;
        else
            ft.verdict = TrendVerdict::NoTrend;
    }
    rep.ok = true;
    return rep;
}

static std::string num(double v)
{
    if (!std::isfinite(v))
        return "NaN";
    std::ostringstream os;
    os.precision(8);
    os << v;
    return os.str();
}

bool writeTrendCsv(const TrendReport &rep, const std::string &path, std::string *error)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc);
    if (!f) {
        if (error) *error = "Cannot write " + path;
        return false;
    }
    f << "feature";
    for (const std::string &c : rep.categories)
        f << "," << csvEscape(c + "_mean") << "," << csvEscape(c + "_std") << "," << csvEscape(c + "_n");
    f << ",spearman_rho,p_value,p_holm,effect_size_d,monotonic,verdict\n";
    for (const FeatureTrend &t : rep.features) {
        f << t.feature;
        for (size_t k = 0; k < rep.categories.size(); ++k)
            f << "," << num(t.mean[k]) << "," << num(t.std[k]) << "," << t.count[k];
        f << "," << num(t.rho) << "," << num(t.pValue) << "," << num(t.pHolm) << "," << num(t.effectSize) << ","
          << (t.monotonic ? (t.direction > 0 ? "increasing" : "decreasing") : "no") << ","
          << verdictName(t.verdict) << "\n";
    }
    f.flush();
    if (!f) {
        if (error) *error = "Write error: " + path;
        return false;
    }
    return true;
}

} // namespace turb
