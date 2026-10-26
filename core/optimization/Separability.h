#ifndef TURB_SEPARABILITY_H
#define TURB_SEPARABILITY_H

#include <string>
#include <vector>

namespace turb {

// Class-separability measures for labeled feature matrices.
// X is samples x features (row-major), y the class label of every sample (0..K-1),
// groups the recording (source) every sample comes from.
namespace separability {

typedef std::vector<std::vector<double> > Matrix;

// Multi-class Fisher ratio of one feature:
//   F = sum_k n_k (mu_k - mu)^2 / sum_k sum_{i in k} (x_i - mu_k)^2
// 0 = no separation; large = class means far apart compared with the spread inside the classes.
// Non-finite values are ignored. Capped at 1e6 when the within-class spread is zero.
double fisherRatio(const std::vector<double> &x, const std::vector<int> &y, int classCount);

// Columns without NaN / Inf and with non-zero variance.
std::vector<size_t> usableColumns(const Matrix &X);

struct CvResult {
    double balancedAccuracy = 0.0; // mean per-class recall over the held-out predictions
    int folds = 0;
    int predictions = 0;
    std::vector<std::vector<int> > confusion; // [true][predicted]
};

// Leave-one-group-out cross-validation of a nearest-centroid classifier on z-scored features
// (mean / std estimated on the training fold only). Pass groups = sample index for
// leave-one-sample-out. Folds whose training part misses a class still predict, but only
// among the classes present.
CvResult nearestCentroidCv(const Matrix &X, const std::vector<int> &y, const std::vector<int> &groups,
                           int classCount, const std::vector<size_t> &columns);

} // namespace separability
} // namespace turb

#endif
