#ifndef TURB_RESULTEXPORTER_H
#define TURB_RESULTEXPORTER_H

#include "Types.h"

#include <ostream>
#include <string>
#include <vector>

namespace turb {

// A finished run plus a user-facing label (used by comparisons).
struct LabeledResult {
    std::string label;
    AlgorithmResult result;
};

// CSV / JSON writers. NaN is written as "NaN" in CSV and null in JSON.
namespace exporter {

std::string formatValue(double v, int precision = 10);

// frame,timestamp,<algorithm columns...>
bool writeFrameCsv(const AlgorithmResult &r, const std::string &path, std::string *error);
void writeFrameCsv(const AlgorithmResult &r, std::ostream &os);

// feature,value
bool writeSummaryCsv(const AlgorithmResult &r, const std::string &path, std::string *error);
void writeSummaryCsv(const AlgorithmResult &r, std::ostream &os);

// Experiment metadata + summary results. One result -> object, several -> {"experiments": [...]}.
bool writeJson(const std::vector<AlgorithmResult> &results, const std::string &path, std::string *error);
void writeJson(const std::vector<AlgorithmResult> &results, std::ostream &os);

// Wide comparison table: algorithm,feature,<run 1>,<run 2>,...
bool writeComparisonCsv(const std::vector<LabeledResult> &runs, const std::string &path, std::string *error);

// Feature matrix for later feature selection / classification:
// one row per run: label,dataset,algorithm,<feature vector keys...>
// (`fullSummary` = all summary features instead of only the feature-vector keys)
bool writeFeatureMatrixCsv(const std::vector<LabeledResult> &runs, bool fullSummary, const std::string &path,
                           std::string *error);

} // namespace exporter
} // namespace turb

#endif
