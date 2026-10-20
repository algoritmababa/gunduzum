#ifndef TURB_TYPES_H
#define TURB_TYPES_H

#include <map>
#include <string>
#include <vector>

namespace turb {

// Region of interest in source-image pixel coordinates.
// width/height <= 0 means "full frame".
struct Roi {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isFullFrame() const { return width <= 0 || height <= 0; }
};

struct FrameInfo {
    int index = 0;          // index inside the dataset (not inside the processed range)
    double timestamp = 0.0; // seconds
};

enum class AlgorithmGroup {
    Intensity,
    QualityStructure,
    MotionGeometric
};

const char *algorithmGroupName(AlgorithmGroup g);

// Generic, GUI-independent parameter description. The GUI builds its editors
// from these specs; nothing algorithm-specific is hard-coded in the GUI.
struct ParamSpec {
    enum Type { Int, Double, Bool, Choice };

    std::string key;
    std::string label;
    std::string tooltip;
    Type type = Double;
    double defaultValue = 0.0;
    double minValue = 0.0;
    double maxValue = 1.0;
    double step = 1.0;
    int decimals = 3;
    std::vector<std::string> choices; // for Choice: value is the index

    static ParamSpec makeInt(const std::string &key, const std::string &label, int def, int min, int max,
                             const std::string &tooltip = std::string());
    static ParamSpec makeDouble(const std::string &key, const std::string &label, double def, double min,
                                double max, double step, int decimals, const std::string &tooltip = std::string());
    static ParamSpec makeBool(const std::string &key, const std::string &label, bool def,
                              const std::string &tooltip = std::string());
    static ParamSpec makeChoice(const std::string &key, const std::string &label,
                                const std::vector<std::string> &choices, int def,
                                const std::string &tooltip = std::string());
};

// Parameter values keyed by ParamSpec::key. Choice and Bool are stored as numbers.
class ParamSet {
public:
    void set(const std::string &key, double value) { m_values[key] = value; }
    bool has(const std::string &key) const { return m_values.count(key) != 0; }
    double get(const std::string &key, double fallback) const;
    int getInt(const std::string &key, int fallback) const;
    bool getBool(const std::string &key, bool fallback) const;
    const std::map<std::string, double> &values() const { return m_values; }

    // Fills missing keys with their defaults and clamps to [min, max].
    static ParamSet withDefaults(const std::vector<ParamSpec> &specs, const ParamSet &given);

private:
    std::map<std::string, double> m_values;
};

struct Feature {
    Feature() : value(0.0) {}
    Feature(const std::string &k, const std::string &l, double v, const std::string &u = std::string())
        : key(k), label(l), value(v), unit(u)
    {
    }

    std::string key;   // machine name, e.g. "rms_displacement"
    std::string label; // human readable, e.g. "RMS Displacement"
    double value;
    std::string unit;  // optional, e.g. "px"
};

// Output of one algorithm run on one dataset.
struct AlgorithmResult {
    std::string algorithmId;
    std::string algorithmName;
    AlgorithmGroup group = AlgorithmGroup::Intensity;

    std::vector<Feature> features;         // full summary, in display order
    std::vector<std::string> featureVector; // keys (subset of features) intended for a future classifier

    // Raw frame-level series. Columns always start with "frame", "timestamp".
    std::vector<std::string> frameColumns;
    std::vector<std::vector<double>> frameRows;

    // Run metadata (dataset, ROI, preprocessing, registration, parameters, ...),
    // filled by the engine. Ordered key/value pairs.
    std::vector<std::pair<std::string, std::string>> metadata;
    std::vector<std::pair<std::string, double>> parameters;
    Roi roi;
    std::string dataset;

    std::vector<std::string> notes; // warnings / diagnostics

    const Feature *feature(const std::string &key) const;
    std::vector<double> featureVectorValues() const;
};

} // namespace turb

#endif
