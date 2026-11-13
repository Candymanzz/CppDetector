#include "inspector.h"
#include "exclusion_zones.h"
#include "learning_matcher.h"
#include "preprocessing.h"
#include "request_timing.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <stdexcept>

namespace cas {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// Shared visibility cutoff: keep the existing heatmap contrast and detection sensitivity.
cv::Mat anomalyEnergy(const cv::Mat& diff, int pixelThreshold) {
    cv::Mat energy;
    diff.convertTo(energy, CV_8U, 128.0 / std::max(1, pixelThreshold));
    cv::threshold(energy, energy, 63, 0, cv::THRESH_TOZERO);
    return energy;
}

double scoreNormalizer(double sensitivity) {
    return sensitivity <= 0.5
        ? 100.0 + (kScoreFullScalePercent - 100.0) * (2.0 * sensitivity)
        : kScoreFullScalePercent + (0.001 - kScoreFullScalePercent) * (2.0 * sensitivity - 1.0);
}

constexpr double kMinShiftResponse = 0.05;   // below this the shift estimate is not trusted
// A shift beyond the limit rejects the frame only when the estimate is confident: on flat / overexposed
// surfaces phase correlation returns a large shift with a response of ~0.05-0.1 that is just noise.
constexpr double kRejectShiftResponse = 0.3;
constexpr double kShiftNumericalEpsilon = 1e-4; // original pixels; only floating-point residue

struct ShiftEstimate {
    cv::Point2d pixels;
    double response = 0;
    double coarseResponse = 0;
    bool refined = false;
};

ShiftEstimate estimateShift(const AlignmentPrepared& a, const cv::Mat& nativeGray) {
    ShiftEstimate result;
    const auto current = nativeGray(a.region);
    cv::Mat small, smallFloat;
    cv::resize(current, small, a.smallSize, 0, 0, cv::INTER_AREA);
    small.convertTo(smallFloat, CV_32F);
    padShiftInput(smallFloat, a.refSmall.size());
    // phaseCorrelate can window its inputs in place; never mutate the shared cache.
    auto referenceFloat = a.refSmall.clone();
    const auto coarse = cv::phaseCorrelate(referenceFloat, smallFloat, a.hann, &result.coarseResponse);
    result.response = result.coarseResponse;
    if (!std::isfinite(coarse.x) || !std::isfinite(coarse.y) || !std::isfinite(result.response)) {
        result.response = result.coarseResponse = 0;
        return result;
    }
    if (result.response < kMinShiftResponse) return result;
    result.pixels = {coarse.x * a.coarseToNative.x, coarse.y * a.coarseToNative.y};

    if (a.canRefine) {
        cv::Mat fine;
        cv::resize(current, fine, a.fineGray.size(), 0, 0, cv::INTER_AREA);
        cv::Mat warp = (cv::Mat_<float>(2, 3) << 1, 0, float(result.pixels.x / a.fineToNative.x),
                                                  0, 1, float(result.pixels.y / a.fineToNative.y));
        try {
            const double confidence = cv::findTransformECC(a.fineGray, fine, warp, cv::MOTION_TRANSLATION,
                cv::TermCriteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 30, 1e-6), a.fineMask, 5);
            const cv::Point2d refined(warp.at<float>(0, 2) * a.fineToNative.x,
                                     warp.at<float>(1, 2) * a.fineToNative.y);
            if (std::isfinite(confidence) && confidence >= .8 && std::isfinite(refined.x)
                    && std::isfinite(refined.y) && cv::norm(refined - result.pixels) <= 8.0) {
                result.pixels = refined;
                result.response = confidence;
                result.refined = true;
            }
        } catch (const cv::Exception&) {
            // Non-convergence, low texture or poor overlap: retain the coarse estimate and confidence.
        }
    }
    if (std::abs(result.pixels.x) < kShiftNumericalEpsilon) result.pixels.x = 0;
    if (std::abs(result.pixels.y) < kShiftNumericalEpsilon) result.pixels.y = 0;
    return result;
}

}

InspectOutcome inspectFrame(const Reference& ref, const cv::Mat& bgr, const Settings& configured, bool keepVisuals) {
    TimingPhase algorithmTiming("algorithm_ms");
    const auto tStart = Clock::now();
    if (bgr.empty() || bgr.size() != ref.size()) {
        throw std::invalid_argument("frame size " + std::to_string(bgr.cols) + "x" + std::to_string(bgr.rows) +
                                    " differs from the reference size " + std::to_string(ref.size().width) + "x" +
                                    std::to_string(ref.size().height));
    }
    InspectOutcome out;
    const Settings s = configured.forFrame(bgr.size());
    auto prep = ref.prepared(s.inspectScale, s.exclusions);
    out.exclusions = s.exclusions;
    out.workSize = prep->workSize;
    out.region = prep->region;
    const double scale = prep->scale;
    const cv::Rect region = prep->region;

    if (prep->roiArea == 0) {
        out.scale = scale; out.reason = "inspection_excluded"; out.surfaceSkipped = true;
        out.diff = cv::Mat::zeros(region.size(), CV_8U);
        out.kept = out.diff.clone(); out.scoredEnergy = out.diff.clone();
        out.totalMs = msSince(tStart); return out;
    }

    // Positioning uses its own native-resolution image. Prepare the comparison
    // image only after compensating the source pixels, before resize and blur.
    const cv::Mat nativeGray = prepareInspectionGray(bgr, bgr.size());

    // ---- native-resolution positioning, independent of the defect inspection scale ----
    requestTiming.add("prepare_ms", msSince(tStart));
    const auto tAlign = Clock::now();
    const int tolWorkX = std::max(0, static_cast<int>(std::lround(s.shiftToleranceX * scale)));
    const int tolWorkY = std::max(0, static_cast<int>(std::lround(s.shiftToleranceY * scale)));
    const auto estimate = estimateShift(*prep->alignment, nativeGray);
    out.shiftResponse = estimate.response;
    out.coarseShiftResponse = estimate.coarseResponse;
    out.shiftRefined = estimate.refined;
    out.rawShift = estimate.pixels;
    auto applied = estimate.pixels;
    out.shiftLimit = {s.maxAlignmentShiftX, s.maxAlignmentShiftY};
    if (std::abs(applied.x) > s.maxAlignmentShiftX || std::abs(applied.y) > s.maxAlignmentShiftY) {
        if (estimate.response >= kRejectShiftResponse) {
            out.reject = true;
            out.reason = "shift_exceeded";
        }
        applied = cv::Point2d(0, 0);   // a shift this large cannot be compensated reliably
    }
    out.shift = applied;
    // Rounded work dimensions may have slightly different X/Y ratios on odd-size frames.
    const cv::Point2d sh(applied.x * prep->workSize.width / bgr.cols,
                         applied.y * prep->workSize.height / bgr.rows);

    cv::Mat cur;
    if (std::abs(sh.x) >= 0.15 || std::abs(sh.y) >= 0.15) {
        // Warp the full source: pixels outside the ROI are still valid context.
        // Blurring before this warp can carry a newly exposed bright strip into
        // the comparison even when the translation itself is compensated.
        cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, -applied.x, 0, 1, -applied.y);
        cv::Mat warped;
        cv::warpAffine(bgr, warped, m, bgr.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        cur = prepareInspectionGray(warped, prep->workSize);
    } else {
        cur = prep->workSize == bgr.size() ? nativeGray : prepareInspectionGray(bgr, prep->workSize);
    }
    const cv::Mat curR = cur(region);
    out.alignMs = msSince(tAlign);
    requestTiming.add("align_ms", out.alignMs);

    // ---- difference map with brightness normalisation and shift-tolerant envelope ----
    const auto tDiff = Clock::now();
    cv::Mat refR = prep->refGray(region);
    cv::Mat maskR = prep->roiMask(region);
    cv::Scalar mr, sr, mc, sc;
    cv::meanStdDev(refR, mr, sr, maskR);
    cv::meanStdDev(curR, mc, sc, maskR);
    const double gain = sc[0] > 1.0 ? std::clamp(sr[0] / sc[0], 0.5, 2.0) : 1.0;
    cv::Mat aligned;
    curR.convertTo(aligned, CV_8U, gain, mr[0] - mc[0] * gain);

    const cv::Size kernelSize(2 * tolWorkX + 1, 2 * tolWorkY + 1);
    cv::Mat diff;
    if (kernelSize.width > 1 || kernelSize.height > 1) {
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, kernelSize);
        cv::Mat refMin, refMax, lo, hi;
        cv::erode(refR, refMin, kernel);
        cv::dilate(refR, refMax, kernel);
        cv::subtract(refMin, aligned, lo);
        cv::subtract(aligned, refMax, hi);
        cv::max(lo, hi, diff);
    } else {
        cv::absdiff(aligned, refR, diff);
    }
    diff.setTo(0, ~maskR);
    out.scale = scale;
    out.roiArea = prep->roiArea;
    out.envelopeKernel = kernelSize;
    out.gain = gain;
    out.refMean = mr[0];
    out.curMean = mc[0];
    {
        cv::Scalar md = cv::mean(diff, maskR);
        out.diffMean = md[0];
        double mx = 0;
        cv::minMaxLoc(diff, nullptr, &mx, nullptr, nullptr, maskR);
        out.diffMax = static_cast<int>(mx);
        const int histSize = 256;
        const float range[] = {0, 256};
        const float* ranges[] = {range};
        const int ch = 0;
        cv::Mat hist;
        cv::calcHist(&diff, 1, &ch, maskR, hist, 1, &histSize, ranges);
        const double target = 0.99 * cv::sum(hist)[0];
        double acc = 0;
        for (int i = 0; i < histSize; ++i) {
            acc += hist.at<float>(i);
            if (acc >= target) { out.diffP99 = i; break; }
        }
    }
    out.diffMs = msSince(tDiff);
    requestTiming.add("diff_ms", out.diffMs);

    // ---- sensitivity -> pixel threshold, blobs, score ----
    const auto tAnomaly = Clock::now();
    const double sensitivity = std::clamp(s.sensitivity, 0.0, 1.0);
    const int thr = cvRound(80.0 - 75.0 * sensitivity);
    const auto learnedRegions = ref.learned();
    const int learningThreshold = prep->upperCornerArea > 0
        ? cvRound(80.0 - 75.0 * std::min(1.0, sensitivity * kUpperCornerSensitivityMultiplier)) : thr;
    int originalFlagged = 0;
    if (!learnedRegions.empty()) {
        // Raw score uses the same union and component filtering as the final score, including corners.
        cv::Mat originalKept = cv::Mat::zeros(diff.size(), CV_8U);
        auto collect = [&](int threshold, const cv::Mat& mask) {
            cv::Mat binary = anomalyEnergy(diff, threshold) > 0;
            if (!mask.empty()) binary.setTo(0, mask == 0);
            cv::Mat labels, stats, centroids;
            const int count = cv::connectedComponentsWithStats(binary, labels, stats, centroids, 8, CV_32S);
            for (int label = 1; label < count; ++label)
                if (stats.at<int>(label, cv::CC_STAT_AREA) >= std::max(1.0, s.minBlobArea * scale * scale))
                    originalKept.setTo(255, labels == label);
        };
        collect(thr, {});
        if (sensitivity > 0.0 && sensitivity < 1.0 && prep->upperCornerArea > 0)
            collect(learningThreshold, prep->upperCornerMask);
        originalFlagged = cv::countNonZero(originalKept);
    }
    if (s.learningRobustEnabled && !learnedRegions.empty()) {
        const auto started=Clock::now();
        compareLearned(ref,*prep,curR,diff,region,scale,s,out,
            {s.learningRobustEnabled,s.learningShadowEnabled,s.learningSearchRadius},out.learningDiagnostics);
        out.learningMs=msSince(started);
        requestTiming.add("learning_ms",out.learningMs);
    } else for (const auto& learned : learnedRegions) {
        cv::Rect box(cvRound(learned.bbox.x * scale), cvRound(learned.bbox.y * scale),
                     std::max(1, cvRound(learned.bbox.width * scale)), std::max(1, cvRound(learned.bbox.height * scale)));
        const cv::Rect overlap = box & region;
        if (overlap.empty()) continue;
        const cv::Rect local(overlap.x - region.x, overlap.y - region.y, overlap.width, overlap.height);
        if (!cv::countNonZero(anomalyEnergy(diff(local), learningThreshold))) continue;
        cv::Mat gray;
        cv::cvtColor(learned.bgr, gray, cv::COLOR_BGR2GRAY);
        cv::resize(gray, gray, box.size(), 0, 0, cv::INTER_AREA);
        cv::GaussianBlur(gray, gray, {5, 5}, 0);
        const cv::Rect patch(overlap.x - box.x, overlap.y - box.y, overlap.width, overlap.height);
        cv::Mat valid(gray.size(), CV_8U, cv::Scalar(255));
        // The saved crop is from the real frame, while curR has been aligned to the primary reference.
        // Estimate the remaining crop translation independently, within the configured alignment limits.
        if (overlap.width >= 16 && overlap.height >= 16) {
            cv::Mat learnedF, currentF, hann;
            gray(patch).convertTo(learnedF, CV_32F);
            curR(local).convertTo(currentF, CV_32F);
            cv::createHanningWindow(hann, overlap.size(), CV_32F);
            double confidence = 0;
            const auto offset = cv::phaseCorrelate(learnedF, currentF, hann, &confidence);
            if (confidence >= 0.3 && std::abs(offset.x) <= s.maxAlignmentShiftX * scale &&
                std::abs(offset.y) <= s.maxAlignmentShiftY * scale) {
                const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1, 0, offset.x, 0, 1, offset.y);
                cv::warpAffine(gray, gray, transform, gray.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
                cv::warpAffine(valid, valid, transform, valid.size(), cv::INTER_NEAREST, cv::BORDER_CONSTANT);
            }
        }
        cv::Mat lo, hi, lower, upper, alternate;
        const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, kernelSize);
        cv::erode(gray, lower, kernel); cv::dilate(gray, upper, kernel);
        cv::subtract(lower(patch), curR(local), lo);
        cv::subtract(curR(local), upper(patch), hi);
        cv::max(lo, hi, alternate);
        alternate.setTo(255, valid(patch) == 0);
        // Both comparisons must agree that a pixel is anomalous for it to remain flagged.
        const int before = cv::countNonZero(anomalyEnergy(diff(local), learningThreshold));
        cv::Mat target = diff(local);
        cv::min(target, alternate, target);
        if (cv::countNonZero(anomalyEnergy(diff(local), learningThreshold)) < before)
            out.matchedCaseIds.push_back(learned.id);
    }
    cv::Mat energy = anomalyEnergy(diff, thr);
    cv::Mat labels, stats, centroids;
    const int components = cv::connectedComponentsWithStats(energy > 0, labels, stats, centroids, 8, CV_32S);
    const double minArea = std::max(1.0, s.minBlobArea * scale * scale);
    cv::Mat kept = cv::Mat::zeros(diff.size(), CV_8U);
    out.pixelThreshold = thr;
    out.blobsTotal = components - 1;
    for (int label = 1; label < components; ++label) {
        // Count real pixels: contour area loses single pixels/thin scratches and fills holes.
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);
        if (area < minArea) {
            out.largestDroppedBlob = std::max(out.largestDroppedBlob, static_cast<double>(area));
            continue;
        }
        const cv::Rect bb(stats.at<int>(label, cv::CC_STAT_LEFT), stats.at<int>(label, cv::CC_STAT_TOP),
                          stats.at<int>(label, cv::CC_STAT_WIDTH), stats.at<int>(label, cv::CC_STAT_HEIGHT));
        const cv::Mat blobMask = labels(bb) == label;
        kept(bb).setTo(255, blobMask);
        double mx = 0;
        cv::minMaxLoc(diff(bb), nullptr, &mx, nullptr, nullptr, blobMask);
        out.keptBlobMaxDiff.push_back(static_cast<int>(mx));
        out.keptBlobAreas.push_back(area);
        out.zones.emplace_back(cvRound((bb.x + region.x) / scale), cvRound((bb.y + region.y) / scale),
                               cvRound(bb.width / scale), cvRound(bb.height / scale));
    }
    if (sensitivity == 0.0) {
        kept.setTo(0);
        out.zones.clear();
        out.keptBlobAreas.clear();
        out.keptBlobMaxDiff.clear();
    }
    out.blobsKept = static_cast<int>(out.zones.size());
    int flagged = cv::countNonZero(kept);
    out.areaPercent = prep->roiArea > 0 ? 100.0 * flagged / prep->roiArea : 0.0;
    // Piecewise linear scale preserves the existing score at the default sensitivity.
    const double n = scoreNormalizer(sensitivity);
    out.anomalyScore = std::min(1.0, out.areaPercent / n);
    out.mainRoiScore = out.anomalyScore;

    // Only local sensitivity changes: alignment, blur, comparison relaxation and minimum
    // component area remain those configured for the entire ROI. No extra local-density score.
    out.upperCornerSensitivity = std::min(1.0, sensitivity * kUpperCornerSensitivityMultiplier);
    out.upperCornerPixelThreshold = cvRound(80.0 - 75.0 * out.upperCornerSensitivity);
    out.upperCornerArea = prep->upperCornerArea;
    if (sensitivity > 0.0 && sensitivity < 1.0 && prep->upperCornerArea > 0) {
        cv::Mat cornerEnergy = anomalyEnergy(diff, out.upperCornerPixelThreshold);
        cornerEnergy.setTo(0, prep->upperCornerMask == 0);
        cv::Mat cornerLabels, cornerStats, cornerCentroids;
        const int cornerComponents = cv::connectedComponentsWithStats(cornerEnergy > 0,
            cornerLabels, cornerStats, cornerCentroids, 8, CV_32S);
        bool addedPixels = false;
        for (int label = 1; label < cornerComponents; ++label) {
            if (cornerStats.at<int>(label, cv::CC_STAT_AREA) < minArea) continue;
            const cv::Rect bb(cornerStats.at<int>(label, cv::CC_STAT_LEFT),
                cornerStats.at<int>(label, cv::CC_STAT_TOP), cornerStats.at<int>(label, cv::CC_STAT_WIDTH),
                cornerStats.at<int>(label, cv::CC_STAT_HEIGHT));
            const cv::Mat blob = cornerLabels(bb) == label;
            cv::Mat extra;
            cv::bitwise_and(blob, kept(bb) == 0, extra);
            addedPixels = addedPixels || cv::countNonZero(extra) > 0;
            kept(bb).setTo(255, blob);
        }
        flagged = cv::countNonZero(kept);
        out.areaPercent = prep->roiArea > 0 ? 100.0 * flagged / prep->roiArea : 0.0;
        // Keep n tied to the global setting. A local sensitivity crossing 0.5 must not
        // introduce the sharp score-scale change independently of the rest of the ROI.
        out.anomalyScore = std::max(out.mainRoiScore, std::min(1.0, out.areaPercent / n));
        out.upperCornerExtraScore = out.anomalyScore - out.mainRoiScore;
        cornerEnergy.copyTo(energy, prep->upperCornerMask);
        if (addedPixels) {
            // Rebuild components from the union: overlapping detections count exactly once.
            out.zones.clear(); out.keptBlobAreas.clear(); out.keptBlobMaxDiff.clear();
            const int count = cv::connectedComponentsWithStats(kept, labels, stats, centroids, 8, CV_32S);
            out.blobsTotal = std::max(out.blobsTotal, count - 1);
            for (int label = 1; label < count; ++label) {
                const cv::Rect bb(stats.at<int>(label, cv::CC_STAT_LEFT), stats.at<int>(label, cv::CC_STAT_TOP),
                    stats.at<int>(label, cv::CC_STAT_WIDTH), stats.at<int>(label, cv::CC_STAT_HEIGHT));
                const cv::Mat blob = labels(bb) == label;
                double peak = 0;
                cv::minMaxLoc(diff(bb), nullptr, &peak, nullptr, nullptr, blob);
                out.keptBlobAreas.push_back(stats.at<int>(label, cv::CC_STAT_AREA));
                out.keptBlobMaxDiff.push_back(static_cast<int>(peak));
                out.zones.emplace_back(cvRound((bb.x + region.x) / scale), cvRound((bb.y + region.y) / scale),
                    cvRound(bb.width / scale), cvRound(bb.height / scale));
            }
            out.blobsKept = static_cast<int>(out.zones.size());
        }
    }
    if (learnedRegions.empty()) originalFlagged = flagged;
    out.rawAnomalyScore = prep->roiArea > 0 ? std::min(1.0, 100.0 * originalFlagged / prep->roiArea / n) : 0.0;
    if (sensitivity == 0.0) out.rawAnomalyScore = 0.0;
    if (sensitivity == 1.0) out.rawAnomalyScore = 1.0;
    // Division alone cannot guarantee the endpoints (e.g. zero anomalous pixels).
    if (sensitivity == 0.0) {
        out.anomalyScore = 0.0;
        out.mainRoiScore = 0.0;
        out.reject = false;
        out.reason.clear();
    } else if (sensitivity == 1.0) {
        out.anomalyScore = 1.0;
        out.mainRoiScore = 1.0;
        out.reject = true;
        out.reason = "score_over_threshold";
    }
    if (!out.reject && out.anomalyScore >= s.threshold) {
        out.reject = true;
        out.reason = "score_over_threshold";
    }
    out.anomalyMs = msSince(tAnomaly);
    requestTiming.add("anomaly_ms", out.anomalyMs);

    if (keepVisuals) {
        out.diff = diff;
        out.kept = kept;
        out.aligned = aligned;
        energy.setTo(0, kept == 0);
        out.scoredEnergy = energy;
    }
    out.totalMs = msSince(tStart);
    return out;
}

namespace {

cv::Mat placeInCanvas(const InspectOutcome& out, const cv::Mat& regionImg) {
    cv::Mat canvas = cv::Mat::zeros(out.workSize, CV_8U);
    if (!regionImg.empty()) regionImg.copyTo(canvas(out.region));
    return canvas;
}

}

cv::Mat buildHeatmap(const InspectOutcome& out, const NormPolygon& roi, int maxWidth) {
    // Only scored components are visible; the halo below is decorative and adds no scored area.
    cv::Mat core = out.scoredEnergy.empty() ? anomalyEnergy(out.diff, out.pixelThreshold) : out.scoredEnergy.clone();
    core.setTo(0, out.kept == 0);
    cv::Mat heat = placeInCanvas(out, core);

    // downscale first (the halo is a visual effect, so it is cheaper to blur the small image)
    int haloKernel = 11;
    if (maxWidth > 0 && heat.cols > maxWidth) {
        const double f = static_cast<double>(maxWidth) / heat.cols;
        cv::resize(heat, heat, cv::Size(maxWidth, std::max(1, cvRound(heat.rows * f))), 0, 0, cv::INTER_AREA);
        haloKernel = std::max(3, (cvRound(11 * f) | 1));
    }
    cv::Mat halo;
    cv::GaussianBlur(heat, halo, cv::Size(haloKernel, haloKernel), 0);
    halo.convertTo(halo, CV_8U, 0.65);
    heat = cv::max(heat, halo);
    if (roi.size() >= 3) {   // re-apply the ROI after resizing: INTER_AREA blends border pixels
        std::vector<cv::Point> pts;
        for (auto& q : roi) pts.emplace_back(cvRound(q.x * (heat.cols - 1)), cvRound(q.y * (heat.rows - 1)));
        cv::Mat mask = cv::Mat::zeros(heat.size(), CV_8U);
        cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{pts}, 255);
        cv::Mat masked;
        heat.copyTo(masked, mask);
        heat = masked;
    }
    if (!out.exclusions.empty()) heat.setTo(0, exclusionMask(out.exclusions, heat.size()));
    return heat;
}

cv::Mat fullDiff(const InspectOutcome& out) { return placeInCanvas(out, out.diff); }
cv::Mat fullMask(const InspectOutcome& out) { return placeInCanvas(out, out.kept); }
cv::Mat fullAligned(const InspectOutcome& out) { return placeInCanvas(out, out.aligned); }

}
