#include "ResultTable.h"

#include "Stats.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

namespace turb {

int Table::column(const std::string &name) const
{
    for (size_t i = 0; i < header.size(); ++i)
        if (header[i] == name)
            return int(i);
    return -1;
}

std::vector<std::string> resultColumns()
{
    std::vector<std::string> c = {"recording", "category", "note", "source", "window", "start_frame",
                                  "end_frame", "frames", "roi_x", "roi_y", "roi_w", "roi_h", "measured_at"};
    for (const std::string &f : featureNames())
        c.push_back(f);
    return c;
}

std::string csvEscape(const std::string &s)
{
    if (s.find_first_of(",\"\n\r") == std::string::npos)
        return s;
    std::string out = "\"";
    for (char ch : s) {
        if (ch == '"')
            out += '"';
        out += (ch == '\n' || ch == '\r') ? ' ' : ch;
    }
    return out + "\"";
}

std::vector<std::string> parseCsvLine(const std::string &line)
{
    std::vector<std::string> fields;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    fields.push_back(cur);
    return fields;
}

static std::string number(double v)
{
    if (!std::isfinite(v))
        return "NaN";
    std::ostringstream os;
    os.precision(10);
    os << v;
    return os.str();
}

bool appendResults(const std::string &csvPath, const Recording &rec, const MeasureOutput &out, std::string *error)
{
    const std::vector<std::string> cols = resultColumns();
    bool needHeader = true;
    {
        std::ifstream in(csvPath.c_str());
        std::string first;
        if (in && std::getline(in, first) && !first.empty()) {
            if (parseCsvLine(first) != cols) {
                if (error)
                    *error = "The results file has different columns (created by another version?). "
                             "Choose a new results file.";
                return false;
            }
            needHeader = false;
        }
    }
    std::ofstream f(csvPath.c_str(), std::ios::out | std::ios::app);
    if (!f) {
        if (error) *error = "Cannot write " + csvPath;
        return false;
    }
    if (needHeader) {
        for (size_t i = 0; i < cols.size(); ++i)
            f << (i ? "," : "") << cols[i];
        f << "\n";
    }
    char stamp[32];
    const std::time_t t = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    for (const WindowResult &w : out.windows) {
        f << csvEscape(out.recordingId) << "," << csvEscape(rec.category) << "," << csvEscape(rec.note) << ","
          << csvEscape(rec.path) << "," << w.window << "," << w.startFrame << "," << w.endFrame << "," << w.frames
          << "," << out.roi.x << "," << out.roi.y << "," << out.roi.width << "," << out.roi.height << "," << stamp;
        for (const auto &kv : w.features)
            f << "," << number(kv.second);
        f << "\n";
    }
    f.flush();
    if (!f) {
        if (error) *error = "Write error: " + csvPath;
        return false;
    }
    return true;
}

bool loadTable(const std::string &csvPath, Table &table, std::string *error)
{
    table = Table();
    std::ifstream in(csvPath.c_str());
    if (!in) {
        if (error) *error = "Cannot read " + csvPath;
        return false;
    }
    std::string line;
    if (!std::getline(in, line))
        return true; // empty file
    table.header = parseCsvLine(line);
    while (std::getline(in, line))
        if (!line.empty())
            table.rows.push_back(parseCsvLine(line));
    return true;
}

std::vector<std::string> categoriesIn(const Table &table, const std::vector<std::string> &preferredOrder)
{
    const int cc = table.column("category");
    std::set<std::string> present;
    if (cc >= 0)
        for (const std::vector<std::string> &r : table.rows)
            if (size_t(cc) < r.size() && !r[size_t(cc)].empty())
                present.insert(r[size_t(cc)]);
    std::vector<std::string> out;
    for (const std::string &c : preferredOrder)
        if (present.erase(c))
            out.push_back(c);
    out.insert(out.end(), present.begin(), present.end());
    return out;
}

std::vector<CategoryStats> categorySummary(const Table &table, const std::vector<std::string> &categoryOrder)
{
    std::vector<CategoryStats> result;
    const int cc = table.column("category");
    if (cc < 0)
        return result;
    for (const std::string &feature : featureNames()) {
        const int fc = table.column(feature);
        if (fc < 0)
            continue;
        CategoryStats st;
        st.feature = feature;
        for (const std::string &cat : categoryOrder) {
            std::vector<double> v;
            for (const std::vector<std::string> &r : table.rows)
                if (size_t(std::max(cc, fc)) < r.size() && r[size_t(cc)] == cat)
                    v.push_back(std::strtod(r[size_t(fc)].c_str(), nullptr)); // "NaN" parses as NaN
            const Summary s = summarize(v);
            st.mean.push_back(s.mean);
            st.std.push_back(s.std);
            st.count.push_back(s.count);
        }

        // neighbouring categories that have data
        std::vector<size_t> idx;
        for (size_t i = 0; i < st.count.size(); ++i)
            if (st.count[i] > 0)
                idx.push_back(i);
        if (idx.size() >= 2) {
            bool up = true, down = true, clear = true;
            for (size_t k = 1; k < idx.size(); ++k) {
                const size_t a = idx[k - 1], b = idx[k];
                up = up && st.mean[b] > st.mean[a];
                down = down && st.mean[b] < st.mean[a];
                clear = clear && std::fabs(st.mean[b] - st.mean[a]) > st.std[a] + st.std[b];
            }
            st.trend = up ? "increasing" : (down ? "decreasing" : "mixed");
            st.separation = clear ? "clear" : "overlapping";
        }
        result.push_back(st);
    }
    return result;
}

} // namespace turb
