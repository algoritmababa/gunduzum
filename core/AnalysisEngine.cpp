#include "AnalysisEngine.h"

#include "AlgorithmRegistry.h"
#include "FrameSource.h"
#include "Statistics.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>

namespace turb {

std::string currentDateTimeIso()
{
    const std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::localtime(&t));
    return buf;
}

static std::string formatNumber(double v)
{
    std::ostringstream os;
    os.precision(10);
    os << v;
    return os.str();
}

static std::string describeParams(const Algorithm &alg)
{
    std::ostringstream os;
    bool first = true;
    for (const ParamSpec &s : alg.parameterSpecs()) {
        if (!first)
            os << ", ";
        first = false;
        const double v = alg.parameters().get(s.key, s.defaultValue);
        os << s.key << "=";
        if (s.type == ParamSpec::Choice && v >= 0 && size_t(v) < s.choices.size())
            os << s.choices[size_t(v)];
        else if (s.type == ParamSpec::Bool)
            os << (v != 0.0 ? "true" : "false");
        else
            os << formatNumber(v);
    }
    return os.str();
}

bool AnalysisEngine::loadPreprocessedFrame(const AnalysisConfig &config, int index, cv::Mat &out, std::string *error)
{
    std::unique_ptr<FrameSource> src = FrameSource::create(config.dataset.path, error);
    if (!src)
        return false;
    cv::Mat raw;
    if (!src->readAt(index, raw)) {
        if (error) *error = "Cannot read frame " + std::to_string(index);
        return false;
    }
    try {
        Preprocessor pre(config.preprocess);
        pre.process(raw, out);
    } catch (const cv::Exception &e) {
        if (error) *error = e.what();
        return false;
    }
    return true;
}

AnalysisOutput AnalysisEngine::run(const AnalysisConfig &config, const ProgressCallback &progress,
                                   const std::atomic<bool> *cancel) const
{
    AnalysisOutput out;
    const auto t0 = std::chrono::steady_clock::now();
    const std::string startedAt = currentDateTimeIso();

    if (config.algorithms.empty()) {
        out.error = "No algorithm selected.";
        return out;
    }

    std::string err;
    std::unique_ptr<FrameSource> src = FrameSource::create(config.dataset.path, &err);
    if (!src) {
        out.error = err;
        return out;
    }
    src->setFrameRate(config.dataset.frameRate);

    const int count = src->frameCount();
    const int start = std::max(0, config.dataset.startFrame);
    const int end = config.dataset.endFrame < 0 ? count - 1 : std::min(count - 1, config.dataset.endFrame);
    const int step = std::max(1, config.dataset.frameStep);
    if (start > end) {
        out.error = "Empty frame range.";
        return out;
    }
    const int framesPerPass = (end - start) / step + 1;

    try {
        // --- instantiate algorithms ---------------------------------------------------
        std::vector<AlgorithmPtr> algs;
        int passCount = 1;
        for (const AlgorithmRun &run : config.algorithms) {
            AlgorithmPtr a = AlgorithmRegistry::instance().create(run.id);
            if (!a) {
                out.error = "Unknown algorithm: " + run.id;
                return out;
            }
            a->configure(run.params);
            passCount = std::max(passCount, a->passCount());
            algs.push_back(std::move(a));
        }

        Preprocessor pre(config.preprocess);
        Registrator reg(config.registration);

        cv::Mat raw, frame;
        if (!src->readAt(start, raw)) {
            out.error = "Cannot read first frame of the range.";
            return out;
        }
        const cv::Rect roiRect = Preprocessor::effectiveRoi(config.preprocess.roi, raw.size());
        if (roiRect.area() <= 0) {
            out.error = "ROI is outside the image.";
            return out;
        }

        AnalysisContext ctx;
        ctx.frameCount = framesPerPass;
        ctx.firstFrameIndex = start;
        ctx.frameStep = step;
        ctx.frameRate = src->effectiveFrameRate();
        ctx.frameSize = roiRect.size();
        ctx.roiOffset = roiRect.tl();
        ctx.sourceDepth = raw.depth();
        ctx.perFrameNormalization = config.preprocess.isPerFrameNormalization();
        ctx.registrationEnabled = reg.enabled();

        if (reg.enabled() && config.registration.reference == RegistrationConfig::FixedFrame) {
            cv::Mat refRaw, ref;
            if (!src->readAt(config.registration.fixedFrameIndex, refRaw)) {
                out.error = "Cannot read the registration reference frame.";
                return out;
            }
            pre.process(refRaw, ref);
            reg.setFixedReference(ref);
        }

        for (AlgorithmPtr &a : algs)
            a->begin(ctx);

        AnalysisProgress p;
        p.passCount = passCount;
        p.total = framesPerPass * passCount;
        const bool fastSeek = dynamic_cast<ImageSequenceSource *>(src.get()) != nullptr;
        int regFailures = 0;
        std::vector<double> regDx, regDy;

        for (int pass = 0; pass < passCount; ++pass) {
            for (AlgorithmPtr &a : algs)
                if (pass < a->passCount())
                    a->beginPass(pass);
            reg.reset();
            src->seek(start);
            p.pass = pass;
            std::ostringstream st;
            st << "Pass " << (pass + 1) << "/" << passCount;
            p.status = st.str();

            for (int idx = start; idx <= end; idx += step) {
                if (cancel && cancel->load()) {
                    out.cancelled = true;
                    out.error = "Cancelled by user.";
                    return out;
                }
                bool ok = true;
                if (fastSeek && step > 1) {
                    ok = src->readAt(idx, raw);
                } else {
                    // sequential decoding; for videos the skipped frames are decoded and dropped
                    // because container seeking is slow and often inexact
                    if (idx != start)
                        for (int k = 1; k < step && ok; ++k)
                            ok = src->next(raw, nullptr);
                    ok = ok && src->next(raw, nullptr);
                }
                if (!ok) {
                    if (idx == start) {
                        out.error = "Cannot read frame " + std::to_string(idx);
                        return out;
                    }
                    break; // video containers often over-report the frame count
                }

                pre.process(raw, frame);
                if (reg.enabled()) {
                    if (!reg.apply(frame))
                        ++regFailures;
                    if (pass == 0) {
                        regDx.push_back(reg.lastDx());
                        regDy.push_back(reg.lastDy());
                    }
                }

                FrameInfo info;
                info.index = idx;
                info.timestamp = src->timestamp(idx);
                for (AlgorithmPtr &a : algs)
                    if (pass < a->passCount())
                        a->processFrame(pass, frame, info);

                ++p.processed;
                p.frameIndex = idx;
                if (progress)
                    progress(p);
            }
        }

        out.elapsedSeconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        // --- results + metadata -----------------------------------------------------------
        const SeriesStats sdx = stats::summarize(regDx), sdy = stats::summarize(regDy);
        for (AlgorithmPtr &a : algs) {
            AlgorithmResult r = a->finish();
            r.dataset = config.dataset.path;
            r.roi.x = roiRect.x;
            r.roi.y = roiRect.y;
            r.roi.width = roiRect.width;
            r.roi.height = roiRect.height;
            for (const ParamSpec &s : a->parameterSpecs())
                r.parameters.push_back(std::make_pair(s.key, a->parameters().get(s.key, s.defaultValue)));

            std::ostringstream range;
            range << start << ".." << end << " step " << step;
            r.metadata = {
                {"dataset", config.dataset.path},
                {"source", src->description()},
                {"frame_range", range.str()},
                {"frames_per_pass", std::to_string(framesPerPass)},
                {"passes", std::to_string(a->passCount())},
                {"frame_rate_hz", formatNumber(ctx.frameRate)},
                {"roi", std::to_string(roiRect.x) + "," + std::to_string(roiRect.y) + "," +
                            std::to_string(roiRect.width) + "x" + std::to_string(roiRect.height)},
                {"preprocessing", config.preprocess.describe()},
                {"registration", config.registration.describe()},
                {"parameters", describeParams(*a)},
                {"started_at", startedAt},
                {"elapsed_seconds", formatNumber(out.elapsedSeconds)},
                {"opencv_version", CV_VERSION},
            };
            if (reg.enabled()) {
                r.metadata.push_back(std::make_pair(std::string("registration_failures"), std::to_string(regFailures)));
                r.metadata.push_back(std::make_pair(std::string("registration_shift_std_x"), formatNumber(sdx.stddev)));
                r.metadata.push_back(std::make_pair(std::string("registration_shift_std_y"), formatNumber(sdy.stddev)));
                r.notes.push_back("Registration is enabled: global motion inside the ROI is removed before the "
                                  "algorithm runs, which also removes turbulence-induced whole-ROI tilt.");
            }
            out.results.push_back(std::move(r));
        }
        out.ok = true;
    } catch (const cv::Exception &e) {
        out.error = std::string("OpenCV error: ") + e.what();
    } catch (const std::exception &e) {
        out.error = std::string("Error: ") + e.what();
    }
    return out;
}

} // namespace turb
