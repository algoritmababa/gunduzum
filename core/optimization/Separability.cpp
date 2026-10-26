#include "Separability.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace turb {
namespace separability {

double fisherRatio(const std::vector<double> &x, const std::vector<int> &y, int classCount)
{
    std::vector<double> sum(size_t(classCount), 0.0);
    std::vector<int> n(size_t(classCount), 0);
    double total = 0.0;
    int count = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (!std::isfinite(x[i]) || y[i] < 0 || y[i] >= classCount)
            continue;
        sum[size_t(y[i])] += x[i];
        ++n[size_t(y[i])];
        total += x[i];
        ++count;
    }
    if (count == 0)
        return 0.0;
    const double mu = total / count;
    std::vector<double> muK(size_t(classCount), 0.0);
    double between = 0.0;
    int present = 0;
    for (int k = 0; k < classCount; ++k) {
        if (n[size_t(k)] == 0)
            continue;
        ++present;
        muK[size_t(k)] = sum[size_t(k)] / n[size_t(k)];
        between += n[size_t(k)] * (muK[size_t(k)] - mu) * (muK[size_t(k)] - mu);
    }
    if (present < 2)
        return 0.0;
    double within = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (!std::isfinite(x[i]) || y[i] < 0 || y[i] >= classCount)
            continue;
        const double d = x[i] - muK[size_t(y[i])];
        within += d * d;
    }
    // relative tolerance: a zero within-class spread means perfectly constant classes
    const double scale = std::max(1e-300, between + within);
    if (within <= 1e-15 * scale)
        return between > 1e-15 * scale ? 1e6 : 0.0;
    return std::min(1e6, between / within);
}

std::vector<size_t> usableColumns(const Matrix &X)
{
    std::vector<size_t> cols;
    if (X.empty())
        return cols;
    const size_t m = X.front().size();
    for (size_t c = 0; c < m; ++c) {
        bool ok = true;
        double mn = std::numeric_limits<double>::infinity(), mx = -mn;
        for (const std::vector<double> &row : X) {
            if (c >= row.size() || !std::isfinite(row[c])) {
                ok = false;
                break;
            }
            mn = std::min(mn, row[c]);
            mx = std::max(mx, row[c]);
        }
        if (ok && mx - mn > 1e-12 * std::max(1.0, std::max(std::fabs(mn), std::fabs(mx))))
            cols.push_back(c);
    }
    return cols;
}

CvResult nearestCentroidCv(const Matrix &X, const std::vector<int> &y, const std::vector<int> &groups,
                           int classCount, const std::vector<size_t> &columns)
{
    CvResult r;
    r.confusion.assign(size_t(classCount), std::vector<int>(size_t(classCount), 0));
    if (X.empty() || columns.empty())
        return r;

    const std::set<int> groupSet(groups.begin(), groups.end());
    const size_t d = columns.size();
    for (int g : groupSet) {
        // training statistics (z-score) on the training fold only
        std::vector<double> mean(d, 0.0), sd(d, 0.0);
        int nTrain = 0;
        for (size_t i = 0; i < X.size(); ++i) {
            if (groups[i] == g)
                continue;
            ++nTrain;
            for (size_t j = 0; j < d; ++j)
                mean[j] += X[i][columns[j]];
        }
        if (nTrain == 0)
            continue;
        for (size_t j = 0; j < d; ++j)
            mean[j] /= nTrain;
        for (size_t i = 0; i < X.size(); ++i) {
            if (groups[i] == g)
                continue;
            for (size_t j = 0; j < d; ++j) {
                const double v = X[i][columns[j]] - mean[j];
                sd[j] += v * v;
            }
        }
        for (size_t j = 0; j < d; ++j) {
            sd[j] = std::sqrt(sd[j] / nTrain);
            if (!(sd[j] > 1e-12))
                sd[j] = 1.0;
        }

        std::vector<std::vector<double> > centroid(size_t(classCount), std::vector<double>(d, 0.0));
        std::vector<int> nk(size_t(classCount), 0);
        for (size_t i = 0; i < X.size(); ++i) {
            if (groups[i] == g)
                continue;
            const size_t k = size_t(y[i]);
            ++nk[k];
            for (size_t j = 0; j < d; ++j)
                centroid[k][j] += (X[i][columns[j]] - mean[j]) / sd[j];
        }
        for (int k = 0; k < classCount; ++k)
            if (nk[size_t(k)] > 0)
                for (size_t j = 0; j < d; ++j)
                    centroid[size_t(k)][j] /= nk[size_t(k)];

        ++r.folds;
        for (size_t i = 0; i < X.size(); ++i) {
            if (groups[i] != g)
                continue;
            int best = -1;
            double bestDist = std::numeric_limits<double>::infinity();
            for (int k = 0; k < classCount; ++k) {
                if (nk[size_t(k)] == 0)
                    continue;
                double dist = 0.0;
                for (size_t j = 0; j < d; ++j) {
                    const double v = (X[i][columns[j]] - mean[j]) / sd[j] - centroid[size_t(k)][j];
                    dist += v * v;
                }
                if (dist < bestDist) {
                    bestDist = dist;
                    best = k;
                }
            }
            if (best >= 0) {
                ++r.confusion[size_t(y[i])][size_t(best)];
                ++r.predictions;
            }
        }
    }

    double recallSum = 0.0;
    int classesSeen = 0;
    for (int k = 0; k < classCount; ++k) {
        int rowTotal = 0;
        for (int p = 0; p < classCount; ++p)
            rowTotal += r.confusion[size_t(k)][size_t(p)];
        if (rowTotal == 0)
            continue;
        ++classesSeen;
        recallSum += double(r.confusion[size_t(k)][size_t(k)]) / rowTotal;
    }
    r.balancedAccuracy = classesSeen > 0 ? recallSum / classesSeen : 0.0;
    return r;
}

} // namespace separability
} // namespace turb
