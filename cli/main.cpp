// Command-line front end: measure recordings into the same results table as the GUI,
// or print the category summary of a results table.

#include "Measurement.h"
#include "ResultTable.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace turb;

static void usage()
{
    std::cout << "Usage:\n"
                 "  turbulence_measure --input <folder|video> --results <file.csv>\n"
                 "                     [--category LOW|MEDIUM|HIGH|...] [--note text]\n"
                 "                     [--roi x,y,w,h] [--window frames] [--frames-csv file.csv]\n"
                 "  turbulence_measure --summary <file.csv> [--order LOW,MEDIUM,HIGH]\n";
}

static std::vector<std::string> split(const std::string &s)
{
    std::vector<std::string> out;
    size_t a = 0;
    for (size_t b; (b = s.find(',', a)) != std::string::npos; a = b + 1)
        out.push_back(s.substr(a, b - a));
    out.push_back(s.substr(a));
    return out;
}

static int summary(const std::string &path, const std::vector<std::string> &order)
{
    Table t;
    std::string err;
    if (!loadTable(path, t, &err)) {
        std::cerr << err << "\n";
        return 1;
    }
    const std::vector<std::string> cats = categoriesIn(t, order);
    std::printf("%-22s", "feature");
    for (const std::string &c : cats)
        std::printf("  %-26s", c.c_str());
    std::printf("  %-11s %s\n", "trend", "separation");
    for (const CategoryStats &s : categorySummary(t, cats)) {
        std::printf("%-22s", s.feature.c_str());
        for (size_t i = 0; i < cats.size(); ++i) {
            char buf[64];
            if (s.count[i] > 0)
                std::snprintf(buf, sizeof(buf), "%.4g +- %.3g (%d)", s.mean[i], s.std[i], s.count[i]);
            else
                std::snprintf(buf, sizeof(buf), "-");
            std::printf("  %-26s", buf);
        }
        std::printf("  %-11s %s\n", s.trend.c_str(), s.separation.c_str());
    }
    return 0;
}

int main(int argc, char **argv)
{
    Recording rec;
    MeasureSettings settings;
    std::string results, summaryPath;
    std::vector<std::string> order = {"LOW", "MEDIUM", "HIGH"};

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << a << "\n";
            return 2;
        }
        const std::string v = argv[++i];
        if (a == "--input")
            rec.path = v;
        else if (a == "--results")
            results = v;
        else if (a == "--category")
            rec.category = v;
        else if (a == "--note")
            rec.note = v;
        else if (a == "--window")
            settings.windowLength = std::atoi(v.c_str());
        else if (a == "--frames-csv")
            settings.frameCsvPath = v;
        else if (a == "--summary")
            summaryPath = v;
        else if (a == "--order")
            order = split(v);
        else if (a == "--roi") {
            if (std::sscanf(v.c_str(), "%d,%d,%d,%d", &rec.roi.x, &rec.roi.y, &rec.roi.width, &rec.roi.height) != 4) {
                std::cerr << "--roi expects x,y,w,h\n";
                return 2;
            }
        } else {
            std::cerr << "Unknown option " << a << "\n";
            usage();
            return 2;
        }
    }

    if (!summaryPath.empty())
        return summary(summaryPath, order);
    if (rec.path.empty() || results.empty()) {
        usage();
        return 2;
    }

    int lastPct = -1;
    const MeasureOutput out = measure(
        rec, settings,
        [&lastPct](int done, int total) {
            const int pct = total > 0 ? 100 * done / total : 0;
            if (pct / 10 != lastPct / 10) {
                std::cerr << "\r" << pct << "% " << std::flush;
                lastPct = pct;
            }
        },
        nullptr);
    std::cerr << "\n";
    if (!out.ok) {
        std::cerr << out.error << "\n";
        return 1;
    }
    std::string err;
    if (!appendResults(results, rec, out, &err)) {
        std::cerr << err << "\n";
        return 1;
    }
    std::cout << out.recordingId << ": " << out.windows.size() << " window(s) appended to " << results << "\n";
    return 0;
}
