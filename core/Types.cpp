#include "Types.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace turb {

const char *algorithmGroupName(AlgorithmGroup g)
{
    switch (g) {
    case AlgorithmGroup::Intensity: return "Intensity Features";
    case AlgorithmGroup::QualityStructure: return "Image Quality / Structure Features";
    case AlgorithmGroup::MotionGeometric: return "Motion / Geometric Features";
    }
    return "";
}

ParamSpec ParamSpec::makeInt(const std::string &key, const std::string &label, int def, int min, int max,
                             const std::string &tooltip)
{
    ParamSpec s;
    s.key = key;
    s.label = label;
    s.tooltip = tooltip;
    s.type = Int;
    s.defaultValue = def;
    s.minValue = min;
    s.maxValue = max;
    s.step = 1;
    s.decimals = 0;
    return s;
}

ParamSpec ParamSpec::makeDouble(const std::string &key, const std::string &label, double def, double min,
                                double max, double step, int decimals, const std::string &tooltip)
{
    ParamSpec s;
    s.key = key;
    s.label = label;
    s.tooltip = tooltip;
    s.type = Double;
    s.defaultValue = def;
    s.minValue = min;
    s.maxValue = max;
    s.step = step;
    s.decimals = decimals;
    return s;
}

ParamSpec ParamSpec::makeBool(const std::string &key, const std::string &label, bool def,
                              const std::string &tooltip)
{
    ParamSpec s;
    s.key = key;
    s.label = label;
    s.tooltip = tooltip;
    s.type = Bool;
    s.tunable = true;
    s.defaultValue = def ? 1.0 : 0.0;
    s.minValue = 0;
    s.maxValue = 1;
    s.decimals = 0;
    return s;
}

ParamSpec ParamSpec::makeChoice(const std::string &key, const std::string &label,
                                const std::vector<std::string> &choices, int def, const std::string &tooltip)
{
    ParamSpec s;
    s.key = key;
    s.label = label;
    s.tooltip = tooltip;
    s.type = Choice;
    s.tunable = true;
    s.choices = choices;
    s.defaultValue = def;
    s.minValue = 0;
    s.maxValue = choices.empty() ? 0 : double(choices.size() - 1);
    s.decimals = 0;
    return s;
}

double ParamSet::get(const std::string &key, double fallback) const
{
    std::map<std::string, double>::const_iterator it = m_values.find(key);
    return it == m_values.end() ? fallback : it->second;
}

int ParamSet::getInt(const std::string &key, int fallback) const
{
    return int(std::lround(get(key, fallback)));
}

bool ParamSet::getBool(const std::string &key, bool fallback) const
{
    return get(key, fallback ? 1.0 : 0.0) != 0.0;
}

ParamSet ParamSet::withDefaults(const std::vector<ParamSpec> &specs, const ParamSet &given)
{
    ParamSet out;
    for (const ParamSpec &s : specs) {
        double v = given.get(s.key, s.defaultValue);
        if (!std::isfinite(v))
            v = s.defaultValue;
        v = std::min(std::max(v, s.minValue), s.maxValue);
        if (s.type != ParamSpec::Double)
            v = std::round(v);
        out.set(s.key, v);
    }
    return out;
}

const Feature *AlgorithmResult::feature(const std::string &key) const
{
    for (const Feature &f : features)
        if (f.key == key)
            return &f;
    return nullptr;
}

std::vector<double> AlgorithmResult::featureVectorValues() const
{
    std::vector<double> v;
    v.reserve(featureVector.size());
    for (const std::string &k : featureVector) {
        const Feature *f = feature(k);
        v.push_back(f ? f->value : std::numeric_limits<double>::quiet_NaN());
    }
    return v;
}

} // namespace turb
