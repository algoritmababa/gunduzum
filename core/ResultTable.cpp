#include "ResultTable.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
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
                                  "end_frame", "frames", "roi_x", "roi_y", "roi_w", "roi_h", "preprocess", "measured_at"};
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

// Inserts the 'preprocess' column ("none") into a results file of the previous version.
static bool upgradeResults(const std::string &csvPath, size_t at, std::string *error)
{
    std::vector<std::string> lines;
    {
        std::ifstream in(csvPath.c_str());
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (line.empty())
                continue;
            std::vector<std::string> fields = parseCsvLine(line);
            if (fields.size() >= at)
                fields.insert(fields.begin() + long(at), first ? "preprocess" : "none");
            first = false;
            std::string joined;
            for (size_t i = 0; i < fields.size(); ++i)
                joined += (i ? "," : "") + csvEscape(fields[i]);
            lines.push_back(joined);
        }
    }
    const std::string tmp = csvPath + ".upgrade.tmp";
    {
        std::ofstream o(tmp.c_str(), std::ios::out | std::ios::trunc);
        for (const std::string &l : lines)
            o << l << "\n";
        o.flush();
        if (!o) {
            if (error) *error = "Cannot write " + tmp;
            return false;
        }
    }
    std::remove(csvPath.c_str()); // rename() does not replace an existing file on Windows
    if (std::rename(tmp.c_str(), csvPath.c_str()) != 0) {
        if (error) *error = "Cannot replace " + csvPath + " (the upgraded copy is " + tmp + ")";
        return false;
    }
    return true;
}

bool appendResults(const std::string &csvPath, const Recording &rec, const MeasureOutput &out, std::string *error)
{
    const std::vector<std::string> cols = resultColumns();
    const size_t ppAt = size_t(std::find(cols.begin(), cols.end(), "preprocess") - cols.begin());
    bool needHeader = true;
    {
        std::ifstream in(csvPath.c_str());
        std::string first;
        if (in && std::getline(in, first) && !first.empty()) {
            std::vector<std::string> old = parseCsvLine(first);
            std::vector<std::string> previous = cols;
            previous.erase(previous.begin() + long(ppAt));
            if (old == previous) {
                in.close();
                if (!upgradeResults(csvPath, ppAt, error))
                    return false;
                old = cols;
            }
            if (old != cols) {
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
          << "," << out.roi.x << "," << out.roi.y << "," << out.roi.width << "," << out.roi.height << ","
          << csvEscape(out.preprocess.empty() ? std::string("none") : out.preprocess) << "," << stamp;
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

std::vector<std::string> preprocessVariants(const Table &table)
{
    std::vector<std::string> v;
    const int c = table.column("preprocess");
    if (c < 0)
        return std::vector<std::string>(1, "none");
    for (const std::vector<std::string> &r : table.rows) {
        const std::string x = size_t(c) < r.size() && !r[size_t(c)].empty() ? r[size_t(c)] : std::string("none");
        if (std::find(v.begin(), v.end(), x) == v.end())
            v.push_back(x);
    }
    return v;
}

Table filterPreprocess(const Table &table, const std::string &variant)
{
    const int c = table.column("preprocess");
    if (c < 0 && variant == "none")
        return table;
    Table t;
    t.header = table.header;
    if (c < 0)
        return t;
    for (const std::vector<std::string> &r : table.rows) {
        const std::string x = size_t(c) < r.size() && !r[size_t(c)].empty() ? r[size_t(c)] : std::string("none");
        if (x == variant)
            t.rows.push_back(r);
    }
    return t;
}

} // namespace turb
