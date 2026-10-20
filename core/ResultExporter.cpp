#include "ResultExporter.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace turb {
namespace exporter {

std::string formatValue(double v, int precision)
{
    if (std::isnan(v))
        return "NaN";
    if (std::isinf(v))
        return v > 0 ? "Inf" : "-Inf";
    std::ostringstream os;
    os.precision(precision);
    os << v;
    return os.str();
}

static std::string csvField(const std::string &s)
{
    if (s.find_first_of(",\"\n\r") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"')
            out += '"';
        out += c;
    }
    return out + "\"";
}

static std::string jsonString(const std::string &s)
{
    std::ostringstream os;
    os << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': os << "\\\""; break;
        case '\\': os << "\\\\"; break;
        case '\n': os << "\\n"; break;
        case '\r': os << "\\r"; break;
        case '\t': os << "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                os << buf;
            } else {
                os << c;
            }
        }
    }
    os << '"';
    return os.str();
}

static std::string jsonNumber(double v)
{
    return std::isfinite(v) ? formatValue(v, 12) : "null";
}

template <typename Fn>
static bool writeFile(const std::string &path, std::string *error, Fn fn)
{
    std::ofstream f(path.c_str(), std::ios::out | std::ios::trunc);
    if (!f) {
        if (error) *error = "Cannot open file for writing: " + path;
        return false;
    }
    fn(f);
    f.flush();
    if (!f) {
        if (error) *error = "Write error: " + path;
        return false;
    }
    return true;
}

void writeFrameCsv(const AlgorithmResult &r, std::ostream &os)
{
    for (size_t i = 0; i < r.frameColumns.size(); ++i)
        os << (i ? "," : "") << csvField(r.frameColumns[i]);
    os << "\n";
    for (const std::vector<double> &row : r.frameRows) {
        for (size_t i = 0; i < row.size(); ++i) {
            if (i)
                os << ",";
            // frame index as integer, timestamp with fixed precision
            if (i == 0)
                os << (long long)row[i];
            else
                os << formatValue(row[i]);
        }
        os << "\n";
    }
}

bool writeFrameCsv(const AlgorithmResult &r, const std::string &path, std::string *error)
{
    return writeFile(path, error, [&](std::ostream &os) { writeFrameCsv(r, os); });
}

void writeSummaryCsv(const AlgorithmResult &r, std::ostream &os)
{
    os << "feature,value\n";
    for (const Feature &f : r.features)
        os << csvField(f.key) << "," << formatValue(f.value) << "\n";
}

bool writeSummaryCsv(const AlgorithmResult &r, const std::string &path, std::string *error)
{
    return writeFile(path, error, [&](std::ostream &os) { writeSummaryCsv(r, os); });
}

static void writeJsonObject(const AlgorithmResult &r, std::ostream &os, const std::string &ind)
{
    const std::string in1 = ind + "  ", in2 = ind + "    ";
    os << ind << "{\n";
    os << in1 << "\"dataset\": " << jsonString(r.dataset) << ",\n";
    os << in1 << "\"algorithm\": " << jsonString(r.algorithmName) << ",\n";
    os << in1 << "\"algorithm_id\": " << jsonString(r.algorithmId) << ",\n";
    os << in1 << "\"algorithm_group\": " << jsonString(algorithmGroupName(r.group)) << ",\n";
    os << in1 << "\"roi\": {\"x\": " << r.roi.x << ", \"y\": " << r.roi.y << ", \"width\": " << r.roi.width
       << ", \"height\": " << r.roi.height << "},\n";

    os << in1 << "\"metadata\": {";
    for (size_t i = 0; i < r.metadata.size(); ++i)
        os << (i ? ",\n" : "\n") << in2 << jsonString(r.metadata[i].first) << ": " << jsonString(r.metadata[i].second);
    os << "\n" << in1 << "},\n";

    os << in1 << "\"parameters\": {";
    for (size_t i = 0; i < r.parameters.size(); ++i)
        os << (i ? ",\n" : "\n") << in2 << jsonString(r.parameters[i].first) << ": " << jsonNumber(r.parameters[i].second);
    os << "\n" << in1 << "},\n";

    os << in1 << "\"features\": {";
    for (size_t i = 0; i < r.features.size(); ++i)
        os << (i ? ",\n" : "\n") << in2 << jsonString(r.features[i].key) << ": " << jsonNumber(r.features[i].value);
    os << "\n" << in1 << "},\n";

    os << in1 << "\"feature_vector\": {\n" << in2 << "\"keys\": [";
    for (size_t i = 0; i < r.featureVector.size(); ++i)
        os << (i ? ", " : "") << jsonString(r.featureVector[i]);
    os << "],\n" << in2 << "\"values\": [";
    const std::vector<double> v = r.featureVectorValues();
    for (size_t i = 0; i < v.size(); ++i)
        os << (i ? ", " : "") << jsonNumber(v[i]);
    os << "]\n" << in1 << "},\n";

    os << in1 << "\"frame_count\": " << r.frameRows.size() << ",\n";
    os << in1 << "\"notes\": [";
    for (size_t i = 0; i < r.notes.size(); ++i)
        os << (i ? ",\n" : "\n") << in2 << jsonString(r.notes[i]);
    os << (r.notes.empty() ? "" : "\n" + in1) << "]\n";
    os << ind << "}";
}

void writeJson(const std::vector<AlgorithmResult> &results, std::ostream &os)
{
    if (results.size() == 1) {
        writeJsonObject(results.front(), os, "");
        os << "\n";
        return;
    }
    os << "{\n  \"experiments\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        writeJsonObject(results[i], os, "    ");
        os << (i + 1 < results.size() ? ",\n" : "\n");
    }
    os << "  ]\n}\n";
}

bool writeJson(const std::vector<AlgorithmResult> &results, const std::string &path, std::string *error)
{
    return writeFile(path, error, [&](std::ostream &os) { writeJson(results, os); });
}

bool writeComparisonCsv(const std::vector<LabeledResult> &runs, const std::string &path, std::string *error)
{
    // union of (algorithm, feature) rows in order of first appearance
    std::vector<std::pair<std::string, std::string> > rows;
    for (const LabeledResult &lr : runs)
        for (const Feature &f : lr.result.features) {
            const std::pair<std::string, std::string> key(lr.result.algorithmId, f.key);
            bool found = false;
            for (const auto &k : rows)
                if (k == key) { found = true; break; }
            if (!found)
                rows.push_back(key);
        }

    return writeFile(path, error, [&](std::ostream &os) {
        os << "algorithm,feature";
        for (const LabeledResult &lr : runs)
            os << "," << csvField(lr.label);
        os << "\n";
        for (const auto &k : rows) {
            os << csvField(k.first) << "," << csvField(k.second);
            for (const LabeledResult &lr : runs) {
                const Feature *f = lr.result.algorithmId == k.first ? lr.result.feature(k.second) : nullptr;
                os << "," << (f ? formatValue(f->value) : "");
            }
            os << "\n";
        }
    });
}

bool writeFeatureMatrixCsv(const std::vector<LabeledResult> &runs, bool fullSummary, const std::string &path,
                           std::string *error)
{
    std::vector<std::string> keys;
    auto addKey = [&keys](const std::string &k) {
        for (const std::string &e : keys)
            if (e == k) return;
        keys.push_back(k);
    };
    for (const LabeledResult &lr : runs) {
        if (fullSummary)
            for (const Feature &f : lr.result.features) addKey(lr.result.algorithmId + "." + f.key);
        else
            for (const std::string &k : lr.result.featureVector) addKey(lr.result.algorithmId + "." + k);
    }

    return writeFile(path, error, [&](std::ostream &os) {
        os << "label,dataset,algorithm";
        for (const std::string &k : keys)
            os << "," << csvField(k);
        os << "\n";
        for (const LabeledResult &lr : runs) {
            os << csvField(lr.label) << "," << csvField(lr.result.dataset) << "," << csvField(lr.result.algorithmId);
            const std::string prefix = lr.result.algorithmId + ".";
            for (const std::string &k : keys) {
                const Feature *f = k.compare(0, prefix.size(), prefix) == 0 ? lr.result.feature(k.substr(prefix.size()))
                                                                            : nullptr;
                os << "," << (f ? formatValue(f->value) : "");
            }
            os << "\n";
        }
    });
}

} // namespace exporter
} // namespace turb
