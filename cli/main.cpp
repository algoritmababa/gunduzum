// Command-line front end: measure recordings into the same results table as the GUI,
// or analyze whether the measurements change consistently along the categories.

#include "Measurement.h"
#include "ResultTable.h"
#include "TrendAnalysis.h"

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
                 "  turbulence_measure --analyze <file.csv> [--order LOW,MEDIUM,HIGH]\n"
                 "                     [--unit recording|window] [--alpha 0.05] [--out trend.csv]\n";
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

static int analyze(const std::string &path, const TrendOptions &opt, const std::string &outPath)
{
    Table t;
    std::string err;
    if (!loadTable(path, t, &err)) {
        std::cerr << err << "\n";
        return 1;
    }
    const TrendReport rep = analyzeTrends(t, opt);
    if (!rep.ok) {
        std::cerr << rep.error << "\n";
        return 1;
    }
    std::printf("%d %s:", rep.units, opt.perRecording ? "recordings" : "windows");
    for (size_t k = 0; k < rep.categories.size(); ++k)
        std::printf(" %s=%d", rep.categories[k].c_str(), rep.unitsPerCategory[k]);
    std::printf("\n");
    for (const std::string &n : rep.notes)
        std::printf("note: %s\n", n.c_str());
    std::printf("%-20s", "measurement");
    for (const std::string &c : rep.categories)
        std::printf("  %-12s", c.c_str());
    std::printf("  %7s %9s %7s  %s\n", "rho", "p(Holm)", "d", "verdict");
    for (const FeatureTrend &f : rep.features) {
        std::printf("%-20s", f.feature.c_str());
        for (size_t k = 0; k < rep.categories.size(); ++k)
            std::printf("  %-12.4g", f.mean[k]);
        std::printf("  %7.3f %9.3g %7.2f  %s\n", f.rho, f.pHolm, f.effectSize, verdictName(f.verdict));
    }
    if (!outPath.empty() && !writeTrendCsv(rep, outPath, &err)) {
        std::cerr << err << "\n";
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    Recording rec;
    MeasureSettings settings;
    std::string results, analyzePath, outPath;
    TrendOptions trend;

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
        else if (a == "--analyze")
            analyzePath = v;
        else if (a == "--order")
            trend.categoryOrder = split(v);
        else if (a == "--unit")
            trend.perRecording = v != "window";
        else if (a == "--alpha")
            trend.alpha = std::atof(v.c_str());
        else if (a == "--out")
            outPath = v;
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

    if (!analyzePath.empty())
        return analyze(analyzePath, trend, outPath);
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
