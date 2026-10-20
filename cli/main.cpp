// Headless command-line front end of the analysis engine (batch runs / scripting).

#include "AlgorithmRegistry.h"
#include "AnalysisEngine.h"
#include "ResultExporter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

using namespace turb;

static void usage()
{
    std::cout <<
        "Usage:\n"
        "  turbulence_cli --list\n"
        "  turbulence_cli --params <algorithm_id>\n"
        "  turbulence_cli --dataset <dir|video> --algorithm <id> [--algorithm <id> ...] [options]\n\n"
        "Options:\n"
        "  --roi x,y,w,h                  ROI (default: full frame)\n"
        "  --fps <hz>                     frame rate for timestamps (default: video rate / index)\n"
        "  --range start:end[:step]       frame range, end inclusive, -1 = last\n"
        "  --param key=value              algorithm parameter (applies to every selected algorithm)\n"
        "  --denoise none|gaussian|median|bilateral   --denoise-kernel <k>  --denoise-sigma <s>\n"
        "  --contrast none|histeq|clahe\n"
        "  --normalize none|fullscale|minmax|zscore\n"
        "  --registration none|translation|affine  --reg-reference first|previous|fixed  --reg-index <n>\n"
        "  --out <prefix>                 write <prefix>_<id>_frames.csv, <prefix>_<id>_summary.csv, <prefix>.json\n";
}

static int pick(const std::string &v, const char *const *names, int n, const char *what)
{
    for (int i = 0; i < n; ++i)
        if (v == names[i])
            return i;
    std::cerr << "Invalid value for " << what << ": " << v << "\n";
    std::exit(2);
}

static void printParams(const std::string &id)
{
    AlgorithmPtr a = AlgorithmRegistry::instance().create(id);
    if (!a) {
        std::cerr << "Unknown algorithm " << id << "\n";
        std::exit(2);
    }
    std::cout << a->name() << "\n" << a->description() << "\n\n";
    for (const ParamSpec &s : a->parameterSpecs()) {
        std::cout << "  " << s.key << " (" << s.label << ") default=" << s.defaultValue;
        if (s.type == ParamSpec::Choice) {
            std::cout << " choices:";
            for (size_t i = 0; i < s.choices.size(); ++i)
                std::cout << " " << i << "=" << s.choices[i];
        }
        std::cout << "\n";
    }
}

int main(int argc, char **argv)
{
    AnalysisConfig cfg;
    std::vector<std::string> algIds;
    ParamSet params;
    std::string outPrefix;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto val = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "Missing value for " << a << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--help" || a == "-h") {
            usage();
            return 0;
        } else if (a == "--list") {
            for (const AlgorithmRegistry::Entry &e : AlgorithmRegistry::instance().entries())
                std::cout << e.priority << ". " << e.id << "  (" << e.name << ", " << algorithmGroupName(e.group)
                          << ")\n";
            return 0;
        } else if (a == "--params") {
            printParams(val());
            return 0;
        } else if (a == "--dataset") {
            cfg.dataset.path = val();
        } else if (a == "--algorithm") {
            algIds.push_back(val());
        } else if (a == "--roi") {
            Roi &r = cfg.preprocess.roi;
            if (std::sscanf(val().c_str(), "%d,%d,%d,%d", &r.x, &r.y, &r.width, &r.height) != 4) {
                std::cerr << "--roi expects x,y,w,h\n";
                return 2;
            }
        } else if (a == "--fps") {
            cfg.dataset.frameRate = std::atof(val().c_str());
        } else if (a == "--range") {
            int s = 0, e = -1, st = 1;
            std::sscanf(val().c_str(), "%d:%d:%d", &s, &e, &st);
            cfg.dataset.startFrame = s;
            cfg.dataset.endFrame = e;
            cfg.dataset.frameStep = st;
        } else if (a == "--param") {
            const std::string kv = val();
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) {
                std::cerr << "--param expects key=value\n";
                return 2;
            }
            params.set(kv.substr(0, eq), std::atof(kv.substr(eq + 1).c_str()));
        } else if (a == "--denoise") {
            static const char *n[] = {"none", "gaussian", "median", "bilateral"};
            cfg.preprocess.denoise = PreprocessConfig::Denoise(pick(val(), n, 4, "--denoise"));
        } else if (a == "--denoise-kernel") {
            cfg.preprocess.denoiseKernel = std::atoi(val().c_str());
        } else if (a == "--denoise-sigma") {
            cfg.preprocess.denoiseSigma = std::atof(val().c_str());
        } else if (a == "--contrast") {
            static const char *n[] = {"none", "histeq", "clahe"};
            cfg.preprocess.contrast = PreprocessConfig::Contrast(pick(val(), n, 3, "--contrast"));
        } else if (a == "--normalize") {
            static const char *n[] = {"none", "fullscale", "minmax", "zscore"};
            cfg.preprocess.normalization = PreprocessConfig::Normalization(pick(val(), n, 4, "--normalize"));
        } else if (a == "--registration") {
            static const char *n[] = {"none", "translation", "affine"};
            cfg.registration.mode = RegistrationConfig::Mode(pick(val(), n, 3, "--registration"));
        } else if (a == "--reg-reference") {
            static const char *n[] = {"first", "previous", "fixed"};
            cfg.registration.reference = RegistrationConfig::Reference(pick(val(), n, 3, "--reg-reference"));
        } else if (a == "--reg-index") {
            cfg.registration.fixedFrameIndex = std::atoi(val().c_str());
        } else if (a == "--out") {
            outPrefix = val();
        } else {
            std::cerr << "Unknown option " << a << "\n";
            usage();
            return 2;
        }
    }

    if (cfg.dataset.path.empty() || algIds.empty()) {
        usage();
        return 2;
    }
    for (const std::string &id : algIds) {
        AlgorithmRun run;
        run.id = id;
        run.params = params;
        cfg.algorithms.push_back(run);
    }

    int lastPercent = -1;
    AnalysisEngine engine;
    const AnalysisOutput out = engine.run(
        cfg,
        [&lastPercent](const AnalysisProgress &p) {
            const int pct = p.total > 0 ? 100 * p.processed / p.total : 0;
            if (pct != lastPercent && pct % 10 == 0) {
                std::cerr << "\r" << p.status << "  " << pct << "%   " << std::flush;
                lastPercent = pct;
            }
        },
        nullptr);
    std::cerr << "\n";
    if (!out.ok) {
        std::cerr << out.error << "\n";
        return 1;
    }

    for (const AlgorithmResult &r : out.results) {
        std::cout << "== " << r.algorithmName << " (" << r.frameRows.size() << " frame rows, "
                  << out.elapsedSeconds << " s)\n";
        for (const Feature &f : r.features) {
            std::string key = f.key;
            key.resize(std::max<size_t>(key.size(), 34), ' ');
            std::cout << "  " << key << exporter::formatValue(f.value, 8) << (f.unit.empty() ? "" : " " + f.unit)
                      << "\n";
        }
        std::cout << "  feature_vector = [";
        const std::vector<double> v = r.featureVectorValues();
        for (size_t i = 0; i < v.size(); ++i)
            std::cout << (i ? ", " : "") << exporter::formatValue(v[i], 8);
        std::cout << "]\n";
        for (const std::string &n : r.notes)
            std::cout << "  note: " << n << "\n";

        if (!outPrefix.empty()) {
            std::string err;
            if (!exporter::writeFrameCsv(r, outPrefix + "_" + r.algorithmId + "_frames.csv", &err) ||
                !exporter::writeSummaryCsv(r, outPrefix + "_" + r.algorithmId + "_summary.csv", &err)) {
                std::cerr << err << "\n";
                return 1;
            }
        }
    }
    if (!outPrefix.empty()) {
        std::string err;
        if (!exporter::writeJson(out.results, outPrefix + ".json", &err)) {
            std::cerr << err << "\n";
            return 1;
        }
    }
    return 0;
}
