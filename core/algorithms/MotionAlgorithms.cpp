// Algorithms 5 (Phase Correlation), 6 (Centroid Jitter) and 7 (Optical Flow).

#include "Algorithms.h"

#include "../Registration.h"

#include <opencv2/video/tracking.hpp>

#include <algorithm>
#include <cmath>

namespace turb {

// =====================================================================================
// 5. Phase Correlation / Image Displacement
// =====================================================================================

std::string PhaseCorrelationDisplacement::description() const
{
    return "Sub-pixel apparent displacement (DX, DY) of each frame relative to a reference using "
           "cv::phaseCorrelate. D = sqrt(DX^2 + DY^2).";
}

std::vector<ParamSpec> PhaseCorrelationDisplacement::parameterSpecs() const
{
    return {
        ReferenceFrame::modeParam(ReferenceFrame::First),
        ReferenceFrame::fixedIndexParam(),
        ParamSpec::makeBool("hanning", "Hanning window", true,
                            "Apply a Hanning window before the FFT to suppress edge effects. The window is fixed "
                            "to the ROI, so for whole-ROI shifts it biases the sub-pixel estimate towards zero."),
        ParamSpec::makeDouble("min_response", "Minimum peak response", 0.0, 0.0, 1.0, 0.01, 3,
                              "Frames whose phase-correlation peak response is below this value are marked "
                              "invalid and excluded from the statistics (0 = keep all)."),
    };
}

int PhaseCorrelationDisplacement::passCount() const
{
    ReferenceFrame tmp;
    tmp.configure(ReferenceFrame::Mode(m_params.getInt("reference", 0)), 0);
    return tmp.passCount();
}

void PhaseCorrelationDisplacement::begin(const AnalysisContext &ctx)
{
    m_ref.configure(ReferenceFrame::Mode(m_params.getInt("reference", 0)), m_params.getInt("reference_index", 0));
    m_window.release();
    if (m_params.getBool("hanning", true))
        cv::createHanningWindow(m_window, ctx.frameSize, CV_32F);
    std::vector<double> *series[] = {&m_dx, &m_dy, &m_response, &m_time};
    for (std::vector<double> *v : series) {
        v->clear();
        v->reserve(size_t(ctx.frameCount));
    }
    m_index.clear();
    m_valid.clear();
}

void PhaseCorrelationDisplacement::beginPass(int pass)
{
    if (pass > 0)
        m_ref.finalizePreparation();
}

void PhaseCorrelationDisplacement::processFrame(int pass, const cv::Mat &frame, const FrameInfo &info)
{
    if (!m_ref.isMeasurementPass(pass)) {
        m_ref.accumulate(frame, info);
        return;
    }
    cv::Mat ref;
    if (!m_ref.reference(frame, info, ref) || m_ref.isReferenceFrame(info))
        return; // the reference frame itself carries no displacement information

    double response = 0.0;
    const cv::Point2d s = phaseCorrelateSafe(ref, frame, m_window, &response, m_bufA, m_bufB);
    m_ref.update(frame);

    m_index.push_back(info.index);
    m_time.push_back(info.timestamp);
    m_dx.push_back(s.x);
    m_dy.push_back(s.y);
    m_response.push_back(response);
    m_valid.push_back(response >= m_params.get("min_response", 0.0) ? 1 : 0);
}

AlgorithmResult PhaseCorrelationDisplacement::finish()
{
    AlgorithmResult r = makeResult();
    std::vector<double> dx, dy, mag;
    size_t nValid = 0;
    for (size_t i = 0; i < m_dx.size(); ++i) {
        if (!m_valid[i])
            continue;
        ++nValid;
        dx.push_back(m_dx[i]);
        dy.push_back(m_dy[i]);
        mag.push_back(std::sqrt(m_dx[i] * m_dx[i] + m_dy[i] * m_dy[i]));
    }
    const SeriesStats sx = stats::summarize(dx), sy = stats::summarize(dy), sm = stats::summarize(mag);
    const SeriesStats sr = stats::summarize(m_response);

    r.features = {
        {"mean_dx", "Mean DX", sx.mean, "px"},
        {"mean_dy", "Mean DY", sy.mean, "px"},
        {"std_dx", "Std DX", sx.stddev, "px"},
        {"std_dy", "Std DY", sy.stddev, "px"},
        {"rms_dx", "RMS DX", sx.rms, "px"},
        {"rms_dy", "RMS DY", sy.rms, "px"},
        {"mean_displacement", "Mean Displacement", sm.mean, "px"},
        {"std_displacement", "Std Displacement", sm.stddev, "px"},
        {"rms_displacement", "RMS Displacement", sm.rms, "px"},
        {"max_displacement", "Max Displacement", sm.max, "px"},
        {"p95_displacement", "P95 Displacement", sm.p95, "px"},
        {"mean_response", "Mean Peak Response", sr.mean, ""},
        {"min_response", "Min Peak Response", sr.min, ""},
        {"valid_ratio", "Valid Frame Ratio", m_dx.empty() ? stats::nan() : double(nValid) / double(m_dx.size()), ""},
    };
    r.featureVector = {"mean_dx", "mean_dy", "std_dx", "std_dy", "rms_dx", "rms_dy", "mean_displacement",
                       "std_displacement", "rms_displacement", "max_displacement", "p95_displacement"};

    r.frameColumns = {"frame", "timestamp", "dx", "dy", "magnitude", "response", "valid"};
    r.frameRows.reserve(m_dx.size());
    for (size_t i = 0; i < m_dx.size(); ++i)
        r.frameRows.push_back({double(m_index[i]), m_time[i], m_dx[i], m_dy[i],
                               std::sqrt(m_dx[i] * m_dx[i] + m_dy[i] * m_dy[i]), m_response[i],
                               double(m_valid[i])});
    r.notes.push_back("Reference: " + m_ref.describe());
    return r;
}

// =====================================================================================
// 6. Centroid Jitter
// =====================================================================================

namespace {
enum Segmentation { SegBinary = 0, SegOtsu, SegAdaptive };
enum Polarity { BrightTarget = 0, DarkTarget };
enum Morphology { MorphNone = 0, MorphOpen, MorphClose, MorphOpenClose };
enum Target { TargetLargestContour = 0, TargetAllForeground };
enum Weighting { WeightBinary = 0, WeightIntensity };
enum CentroidRef { CRefFirst = 0, CRefMean, CRefFixed };
} // namespace

std::string CentroidJitter::description() const
{
    return "Segments the target in every frame (threshold / Otsu / adaptive, optional morphology, contour "
           "selection), computes its centroid with cv::moments and the jitter J = |C(t) - C_ref|.";
}

std::vector<ParamSpec> CentroidJitter::parameterSpecs() const
{
    return {
        ParamSpec::makeChoice("segmentation", "Segmentation", {"Binary threshold", "Otsu threshold",
                                                              "Adaptive threshold"}, SegOtsu),
        ParamSpec::makeChoice("polarity", "Target polarity", {"Bright target", "Dark target"}, BrightTarget),
        ParamSpec::makeDouble("threshold", "Threshold (intensity units)", 128.0, -1e9, 1e9, 1.0, 4,
                              "Used by Binary threshold, in the units of the preprocessed frame."),
        ParamSpec::makeInt("adaptive_block", "Adaptive block size", 31, 3, 1001,
                           "Neighbourhood size of cv::adaptiveThreshold (forced odd)."),
        ParamSpec::makeDouble("adaptive_c", "Adaptive C", 5.0, -255.0, 255.0, 0.5, 2,
                              "Constant subtracted from the local mean (8-bit scale)."),
        ParamSpec::makeChoice("morphology", "Morphological filtering", {"None", "Open", "Close", "Open + Close"},
                              MorphOpen),
        ParamSpec::makeInt("morph_kernel", "Morphology kernel", 3, 1, 101, "Elliptical structuring element size."),
        ParamSpec::makeChoice("target", "Target selection", {"Largest contour", "All foreground pixels"},
                              TargetLargestContour),
        ParamSpec::makeDouble("min_area", "Minimum target area (px)", 10.0, 0.0, 1e9, 1.0, 1,
                              "Detections smaller than this are treated as invalid frames."),
        ParamSpec::makeChoice("weighting", "Centroid weighting", {"Binary (shape)", "Intensity weighted"},
                              WeightIntensity,
                              "Binary: centroid of the target mask (pixel-locked, ~0.5 px quantization). "
                              "Intensity weighted: weights max(I - threshold, 0), sub-pixel accurate."),
        ParamSpec::makeChoice("reference", "Reference centroid", {"First valid frame", "Mean centroid",
                                                                  "Fixed frame"}, CRefMean),
        ParamSpec::makeInt("reference_index", "Fixed reference frame #", 0, 0, 10000000,
                           "Dataset frame index used when Reference = Fixed frame (first valid frame at or "
                           "after this index)."),
    };
}

void CentroidJitter::begin(const AnalysisContext &ctx)
{
    m_ctx = ctx;
    std::vector<double> *series[] = {&m_cx, &m_cy, &m_area, &m_time};
    for (std::vector<double> *v : series) {
        v->clear();
        v->reserve(size_t(ctx.frameCount));
    }
    m_index.clear();
    m_valid.clear();
    const int k = std::max(1, m_params.getInt("morph_kernel", 3));
    m_kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
}

bool CentroidJitter::segment(const cv::Mat &frame, double &cx, double &cy, double &area)
{
    const int seg = m_params.getInt("segmentation", SegOtsu);
    const bool bright = m_params.getInt("polarity", BrightTarget) == BrightTarget;
    const int type = bright ? cv::THRESH_BINARY : cv::THRESH_BINARY_INV;

    // threshold level in frame units (used as the zero level of intensity weighting)
    double mn = 0.0, mx = 0.0;
    cv::minMaxLoc(frame, &mn, &mx);
    double level = bright ? mn : mx;
    if (seg == SegBinary) {
        level = m_params.get("threshold", 128.0);
        cv::threshold(frame, m_work, level, 255.0, type);
        m_work.convertTo(m_mask, CV_8U);
    } else {
        cv::normalize(frame, m_u8, 0, 255, cv::NORM_MINMAX, CV_8U);
        if (seg == SegOtsu) {
            const double t8 = cv::threshold(m_u8, m_mask, 0, 255, type | cv::THRESH_OTSU);
            level = mn + t8 * (mx - mn) / 255.0;
        } else {
            int block = m_params.getInt("adaptive_block", 31);
            if (block % 2 == 0)
                ++block;
            cv::adaptiveThreshold(m_u8, m_mask, 255, cv::ADAPTIVE_THRESH_MEAN_C, type, block,
                                  m_params.get("adaptive_c", 5.0));
        }
    }

    switch (m_params.getInt("morphology", MorphOpen)) {
    case MorphOpen: cv::morphologyEx(m_mask, m_mask, cv::MORPH_OPEN, m_kernel); break;
    case MorphClose: cv::morphologyEx(m_mask, m_mask, cv::MORPH_CLOSE, m_kernel); break;
    case MorphOpenClose:
        cv::morphologyEx(m_mask, m_mask, cv::MORPH_OPEN, m_kernel);
        cv::morphologyEx(m_mask, m_mask, cv::MORPH_CLOSE, m_kernel);
        break;
    default: break;
    }

    if (m_params.getInt("target", TargetLargestContour) == TargetLargestContour) {
        std::vector<std::vector<cv::Point> > contours;
        m_work = m_mask.clone(); // findContours modifies its input in OpenCV 3.x
        cv::findContours(m_work, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        int best = -1;
        double bestArea = -1.0;
        for (size_t i = 0; i < contours.size(); ++i) {
            const double a = std::fabs(cv::contourArea(contours[i]));
            if (a > bestArea) {
                bestArea = a;
                best = int(i);
            }
        }
        if (best < 0)
            return false;
        m_mask.setTo(0);
        cv::drawContours(m_mask, contours, best, cv::Scalar(255), cv::FILLED);
    }

    area = double(cv::countNonZero(m_mask));
    if (area <= 0.0 || area < m_params.get("min_area", 10.0))
        return false;

    cv::Moments m;
    if (m_params.getInt("weighting", WeightIntensity) == WeightIntensity) {
        // w = max(I - level, 0) (bright) inside a slightly dilated target mask: the weights go to
        // zero continuously at the threshold, which avoids the pixel locking of binary masks
        if (bright)
            cv::subtract(frame, cv::Scalar(level), m_weights);
        else
            cv::subtract(cv::Scalar(level), frame, m_weights);
        cv::max(m_weights, 0.0, m_weights);
        cv::dilate(m_mask, m_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5)));
        m_work = cv::Mat::zeros(frame.size(), CV_32F);
        m_weights.copyTo(m_work, m_mask);
        m = cv::moments(m_work, false);
    } else {
        m = cv::moments(m_mask, true);
    }
    if (m.m00 <= 0.0)
        return false;
    cx = m.m10 / m.m00 + m_ctx.roiOffset.x;
    cy = m.m01 / m.m00 + m_ctx.roiOffset.y;
    return true;
}

void CentroidJitter::processFrame(int, const cv::Mat &frame, const FrameInfo &info)
{
    double cx = stats::nan(), cy = stats::nan(), area = 0.0;
    const bool ok = segment(frame, cx, cy, area);
    m_index.push_back(info.index);
    m_time.push_back(info.timestamp);
    m_cx.push_back(ok ? cx : stats::nan());
    m_cy.push_back(ok ? cy : stats::nan());
    m_area.push_back(ok ? area : stats::nan());
    m_valid.push_back(ok ? 1 : 0);
}

AlgorithmResult CentroidJitter::finish()
{
    AlgorithmResult r = makeResult();
    const SeriesStats sx = stats::summarize(m_cx), sy = stats::summarize(m_cy), sa = stats::summarize(m_area);

    // reference centroid (all centroids are kept as two scalar series, so no extra pass is needed)
    double refX = stats::nan(), refY = stats::nan();
    int refFrame = -1;
    const int refMode = m_params.getInt("reference", CRefMean);
    if (refMode == CRefMean) {
        refX = sx.mean;
        refY = sy.mean;
    } else {
        const int minIndex = refMode == CRefFixed ? m_params.getInt("reference_index", 0) : 0;
        for (size_t i = 0; i < m_cx.size(); ++i) {
            if (m_valid[i] && m_index[i] >= minIndex) {
                refX = m_cx[i];
                refY = m_cy[i];
                refFrame = m_index[i];
                break;
            }
        }
    }

    std::vector<double> dX(m_cx.size(), stats::nan()), dY(m_cx.size(), stats::nan()), jit(m_cx.size(), stats::nan());
    std::vector<double> jitForStats, f2f;
    size_t nValid = 0;
    int prev = -1;
    for (size_t i = 0; i < m_cx.size(); ++i) {
        if (!m_valid[i])
            continue;
        ++nValid;
        dX[i] = m_cx[i] - refX;
        dY[i] = m_cy[i] - refY;
        jit[i] = std::sqrt(dX[i] * dX[i] + dY[i] * dY[i]);
        if (m_index[i] != refFrame)
            jitForStats.push_back(jit[i]);
        if (prev >= 0) {
            const double ex = m_cx[i] - m_cx[size_t(prev)], ey = m_cy[i] - m_cy[size_t(prev)];
            f2f.push_back(std::sqrt(ex * ex + ey * ey));
        }
        prev = int(i);
    }
    const SeriesStats sj = stats::summarize(jitForStats);
    const SeriesStats sf = stats::summarize(f2f);

    r.features = {
        {"mean_centroid_x", "Mean Centroid X", sx.mean, "px"},
        {"mean_centroid_y", "Mean Centroid Y", sy.mean, "px"},
        {"std_centroid_x", "Std Centroid X", sx.stddev, "px"},
        {"std_centroid_y", "Std Centroid Y", sy.stddev, "px"},
        {"mean_jitter", "Mean Jitter", sj.mean, "px"},
        {"std_jitter", "Std Jitter", sj.stddev, "px"},
        {"rms_jitter", "RMS Jitter", sj.rms, "px"},
        {"max_jitter", "Max Jitter", sj.max, "px"},
        {"p95_jitter", "P95 Jitter", sj.p95, "px"},
        {"mean_frame_to_frame_jitter", "Mean Frame-to-Frame Jitter", sf.mean, "px"},
        {"rms_frame_to_frame_jitter", "RMS Frame-to-Frame Jitter", sf.rms, "px"},
        {"mean_area", "Mean Target Area", sa.mean, "px^2"},
        {"std_area", "Std Target Area", sa.stddev, "px^2"},
        {"valid_detection_ratio", "Valid Detection Ratio",
         m_cx.empty() ? stats::nan() : double(nValid) / double(m_cx.size()), ""},
    };
    r.featureVector = {"mean_centroid_x", "mean_centroid_y", "std_centroid_x", "std_centroid_y", "mean_jitter",
                       "std_jitter", "rms_jitter", "max_jitter", "p95_jitter"};

    r.frameColumns = {"frame", "timestamp", "centroid_x", "centroid_y", "area", "delta_x", "delta_y", "jitter",
                      "valid"};
    for (size_t i = 0; i < m_cx.size(); ++i)
        r.frameRows.push_back({double(m_index[i]), m_time[i], m_cx[i], m_cy[i], m_area[i], dX[i], dY[i], jit[i],
                               double(m_valid[i])});

    r.notes.push_back("Centroids are in source-image coordinates (ROI offset added).");
    if (nValid == 0)
        r.notes.push_back("No valid target detection: check segmentation parameters / polarity.");
    else if (refMode != CRefMean && refFrame < 0)
        r.notes.push_back("No valid detection at or after the reference index.");
    return r;
}

// =====================================================================================
// 7. Optical Flow (sparse pyramidal Lucas-Kanade)
// =====================================================================================

namespace {
enum FlowReference { FlowPrevious = 0, FlowFirst };
}

std::string OpticalFlowLK::description() const
{
    return "Sparse optical flow: cv::goodFeaturesToTrack + cv::calcOpticalFlowPyrLK with optional "
           "forward-backward validation. Statistics over all valid flow vectors F = (Fx, Fy).";
}

std::vector<ParamSpec> OpticalFlowLK::parameterSpecs() const
{
    return {
        ParamSpec::makeChoice("reference", "Reference", {"Previous frame", "First frame"}, FlowPrevious,
                              "Previous: frame-to-frame flow, features re-detected every frame. First: features "
                              "detected once on the first frame and tracked from it to every frame."),
        ParamSpec::makeInt("max_corners", "Max feature points", 200, 1, 100000),
        ParamSpec::makeDouble("quality_level", "Feature quality level", 0.01, 0.0001, 1.0, 0.001, 4),
        ParamSpec::makeDouble("min_distance", "Min feature distance (px)", 5.0, 0.0, 1000.0, 1.0, 1),
        ParamSpec::makeInt("win_size", "LK window size", 21, 3, 201),
        ParamSpec::makeInt("max_level", "Pyramid levels", 3, 0, 8),
        ParamSpec::makeDouble("max_error", "Max LK error (0 = off)", 0.0, 0.0, 1e6, 1.0, 2),
        ParamSpec::makeBool("fb_check", "Forward-backward check", true,
                            "Track back from the current to the reference frame and reject vectors whose "
                            "round-trip error exceeds the threshold."),
        ParamSpec::makeDouble("fb_threshold", "Forward-backward threshold (px)", 0.5, 0.01, 100.0, 0.1, 2),
        ParamSpec::makeBool("remove_global_motion", "Remove per-frame mean motion", false,
                            "Subtract the mean flow vector of each frame so that only local deformation "
                            "remains (global tilt / jitter removed)."),
    };
}

void OpticalFlowLK::begin(const AnalysisContext &ctx)
{
    m_haveScale = false;
    m_refU8.release();
    m_refPts.clear();
    std::vector<double> *series[] = {&m_time, &m_meanMag, &m_meanFx, &m_meanFy, &m_maxMag, &m_tracked,
                                     &m_validCount};
    for (std::vector<double> *v : series) {
        v->clear();
        v->reserve(size_t(ctx.frameCount));
    }
    m_index.clear();
    m_mags.clear();
    m_sumFx = m_sumFy = m_sumFx2 = m_sumFy2 = 0.0;
    m_totalTracked = m_totalValid = 0;
}

void OpticalFlowLK::toU8(const cv::Mat &frame, cv::Mat &u8)
{
    // One fixed linear mapping (from the first frame) for all frames keeps brightness
    // constancy intact; a per-frame stretch would create artificial intensity changes.
    if (!m_haveScale) {
        double mn = 0.0, mx = 0.0;
        cv::minMaxLoc(frame, &mn, &mx);
        const double range = mx - mn > 1e-12 ? mx - mn : 1.0;
        m_alpha = 255.0 / range;
        m_beta = -mn * m_alpha;
        m_haveScale = true;
    }
    frame.convertTo(u8, CV_8U, m_alpha, m_beta);
}

void OpticalFlowLK::detect(const cv::Mat &u8, std::vector<cv::Point2f> &pts) const
{
    pts.clear();
    cv::goodFeaturesToTrack(u8, pts, m_params.getInt("max_corners", 200), m_params.get("quality_level", 0.01),
                            m_params.get("min_distance", 5.0));
}

void OpticalFlowLK::processFrame(int, const cv::Mat &frame, const FrameInfo &info)
{
    toU8(frame, m_curU8);
    const bool firstRef = m_params.getInt("reference", FlowPrevious) == FlowFirst;

    if (m_refU8.empty()) {
        m_curU8.copyTo(m_refU8);
        if (firstRef)
            detect(m_refU8, m_refPts);
        return;
    }
    if (!firstRef)
        detect(m_refU8, m_refPts);

    std::vector<cv::Point2f> next, back;
    std::vector<unsigned char> status, statusBack;
    std::vector<float> err, errBack;
    const int win = std::max(3, m_params.getInt("win_size", 21));
    const int levels = m_params.getInt("max_level", 3);
    const cv::TermCriteria crit(cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 30, 0.01);

    if (!m_refPts.empty()) {
        cv::calcOpticalFlowPyrLK(m_refU8, m_curU8, m_refPts, next, status, err, cv::Size(win, win), levels, crit);
        if (m_params.getBool("fb_check", true))
            cv::calcOpticalFlowPyrLK(m_curU8, m_refU8, next, back, statusBack, errBack, cv::Size(win, win), levels,
                                     crit);
    }

    const double maxErr = m_params.get("max_error", 0.0);
    const double fbThr = m_params.get("fb_threshold", 0.5);
    std::vector<cv::Point2f> flows;
    flows.reserve(m_refPts.size());
    for (size_t i = 0; i < m_refPts.size(); ++i) {
        if (!status[i])
            continue;
        if (maxErr > 0.0 && err[i] > maxErr)
            continue;
        if (!back.empty()) {
            if (!statusBack[i])
                continue;
            const cv::Point2f e = back[i] - m_refPts[i];
            if (std::sqrt(e.x * e.x + e.y * e.y) > fbThr)
                continue;
        }
        flows.push_back(next[i] - m_refPts[i]);
    }

    if (m_params.getBool("remove_global_motion", false) && !flows.empty()) {
        cv::Point2f mean(0.f, 0.f);
        for (const cv::Point2f &f : flows)
            mean += f;
        mean *= 1.0f / float(flows.size());
        for (cv::Point2f &f : flows)
            f -= mean;
    }

    double sumMag = 0.0, sumFx = 0.0, sumFy = 0.0, maxMag = 0.0;
    for (const cv::Point2f &f : flows) {
        const double mag = std::sqrt(double(f.x) * f.x + double(f.y) * f.y);
        sumMag += mag;
        sumFx += f.x;
        sumFy += f.y;
        maxMag = std::max(maxMag, mag);
        m_mags.push_back(float(mag));
        m_sumFx += f.x;
        m_sumFy += f.y;
        m_sumFx2 += double(f.x) * f.x;
        m_sumFy2 += double(f.y) * f.y;
    }
    const double n = double(flows.size());
    m_totalTracked += (long long)m_refPts.size();
    m_totalValid += (long long)flows.size();

    m_index.push_back(info.index);
    m_time.push_back(info.timestamp);
    m_tracked.push_back(double(m_refPts.size()));
    m_validCount.push_back(n);
    m_meanMag.push_back(n > 0 ? sumMag / n : stats::nan());
    m_meanFx.push_back(n > 0 ? sumFx / n : stats::nan());
    m_meanFy.push_back(n > 0 ? sumFy / n : stats::nan());
    m_maxMag.push_back(n > 0 ? maxMag : stats::nan());

    if (!firstRef)
        cv::swap(m_refU8, m_curU8);
}

AlgorithmResult OpticalFlowLK::finish()
{
    AlgorithmResult r = makeResult();
    const double n = double(m_mags.size());
    double meanMag = stats::nan(), varMag = stats::nan(), rmsMag = stats::nan(), maxMag = stats::nan(),
           p95 = stats::nan();
    double meanFx = stats::nan(), meanFy = stats::nan(), stdFx = stats::nan(), stdFy = stats::nan();
    if (n > 0) {
        double s = 0.0, s2 = 0.0, mx = 0.0;
        for (float m : m_mags) {
            s += m;
            s2 += double(m) * m;
            mx = std::max(mx, double(m));
        }
        meanMag = s / n;
        varMag = std::max(0.0, s2 / n - meanMag * meanMag);
        rmsMag = std::sqrt(s2 / n);
        maxMag = mx;
        p95 = stats::percentile(m_mags, 95.0);
        meanFx = m_sumFx / n;
        meanFy = m_sumFy / n;
        stdFx = std::sqrt(std::max(0.0, m_sumFx2 / n - meanFx * meanFx));
        stdFy = std::sqrt(std::max(0.0, m_sumFy2 / n - meanFy * meanFy));
    }
    const SeriesStats sPerFrame = stats::summarize(m_meanMag);
    const SeriesStats sTracked = stats::summarize(m_tracked);

    r.features = {
        {"mean_flow", "Mean Flow Magnitude", meanMag, "px"},
        {"flow_variance", "Flow Magnitude Variance", varMag, "px^2"},
        {"flow_std", "Flow Magnitude Std", std::sqrt(varMag), "px"},
        {"rms_flow", "RMS Flow", rmsMag, "px"},
        {"max_flow", "Max Flow", maxMag, "px"},
        {"p95_flow", "P95 Flow", p95, "px"},
        {"mean_flow_x", "Mean Flow X", meanFx, "px"},
        {"mean_flow_y", "Mean Flow Y", meanFy, "px"},
        {"std_flow_x", "Std Flow X", stdFx, "px"},
        {"std_flow_y", "Std Flow Y", stdFy, "px"},
        {"valid_vector_ratio", "Valid Vector Ratio",
         m_totalTracked > 0 ? double(m_totalValid) / double(m_totalTracked) : stats::nan(), ""},
        {"std_frame_mean_flow", "Std of Per-Frame Mean Flow", sPerFrame.stddev, "px"},
        {"mean_tracked_points", "Mean Tracked Points per Frame", sTracked.mean, ""},
        {"total_valid_vectors", "Total Valid Vectors", n, ""},
    };
    r.featureVector = {"mean_flow", "flow_variance", "flow_std", "rms_flow", "max_flow", "p95_flow",
                       "mean_flow_x", "mean_flow_y", "std_flow_x", "std_flow_y", "valid_vector_ratio"};

    r.frameColumns = {"frame", "timestamp", "tracked_points", "valid_vectors", "mean_flow", "mean_flow_x",
                      "mean_flow_y", "max_flow"};
    for (size_t i = 0; i < m_index.size(); ++i)
        r.frameRows.push_back({double(m_index[i]), m_time[i], m_tracked[i], m_validCount[i], m_meanMag[i],
                               m_meanFx[i], m_meanFy[i], m_maxMag[i]});

    if (m_totalTracked == 0)
        r.notes.push_back("No trackable feature points were found in the ROI (low texture?).");
    if (m_params.getBool("remove_global_motion", false))
        r.notes.push_back("Per-frame mean motion removed: features describe local deformation only.");
    return r;
}

} // namespace turb
