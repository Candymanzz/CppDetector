#include "inspector.h"
#include "request_timing.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace cas {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

constexpr double kMinShiftResponse = 0.05;   // below this the shift estimate is not trusted
// A shift beyond the limit rejects the frame only when the estimate is confident: on flat / overexposed
// surfaces phase correlation returns a large shift with a response of ~0.05-0.1 that is just noise.
constexpr double kRejectShiftResponse = 0.3;

}

InspectOutcome inspectFrame(const Reference& ref, const cv::Mat& bgr, const Settings& s, bool keepVisuals) {
    TimingPhase algorithmTiming("algorithm_ms");
    const auto tStart = Clock::now();
    if (bgr.empty() || bgr.size() != ref.size()) {
        throw std::invalid_argument("frame size " + std::to_string(bgr.cols) + "x" + std::to_string(bgr.rows) +
                                    " differs from the reference size " + std::to_string(ref.size().width) + "x" +
                                    std::to_string(ref.size().height));
    }
    InspectOutcome out;
    auto prep = ref.prepared(s.inspectScale);
    out.workSize = prep->workSize;
    out.region = prep->region;
    const double scale = prep->scale;
    const cv::Rect region = prep->region;

    // ---- current frame at work resolution ----
    cv::Mat cur;
    if (bgr.channels() == 1) cur = bgr.clone();
    else cv::cvtColor(bgr, cur, cv::COLOR_BGR2GRAY);
    if (scale != 1.0) cv::resize(cur, cur, prep->workSize, 0, 0, cv::INTER_AREA);
    cv::GaussianBlur(cur, cur, cv::Size(5, 5), 0);

    // ---- cheap translation estimate on a thumbnail of the ROI box ----
    requestTiming.add("prepare_ms", msSince(tStart));
    const auto tAlign = Clock::now();
    // A larger envelope already covers the entire region; bound allocation for metric inputs.
    const int tolWorkX = static_cast<int>(std::lround(std::clamp(s.shiftToleranceX * scale,0.0,double(region.width))));
    const int tolWorkY = static_cast<int>(std::lround(std::clamp(s.shiftToleranceY * scale,0.0,double(region.height))));
    cv::Mat small, smallF;
    cv::resize(cur(region), small, prep->smallSize, 0, 0, cv::INTER_AREA);
    small.convertTo(smallF, CV_32F);
    // Match the cached reference's even DFT canvas without resizing the content.
    cv::copyMakeBorder(smallF, smallF, 0, prep->refSmall.rows - smallF.rows,
                      0, prep->refSmall.cols - smallF.cols, cv::BORDER_CONSTANT, 0);
    double response = 0;
    // OpenCV may apply the Hann window in-place when no DFT padding is needed.
    // Never pass the shared immutable cache as its writable working buffer.
    cv::Mat referenceSmall = prep->refSmall.clone();
    cv::Point2d sh = cv::phaseCorrelate(referenceSmall, smallF, prep->hann, &response);
    // Padding adds no spatial scale: retain the original, unpadded thumbnail size.
    sh.x *= static_cast<double>(region.width) / prep->smallSize.width;
    sh.y *= static_cast<double>(region.height) / prep->smallSize.height;
    out.shiftResponse = response;
    if (response < kMinShiftResponse) sh = cv::Point2d(0, 0);
    out.rawShift = cv::Point2d(sh.x / scale, sh.y / scale);

    const double maxShiftX = s.alignmentLimitX ? *s.alignmentLimitX * scale : std::max(8.0, 4.0 * tolWorkX);
    const double maxShiftY = s.alignmentLimitY ? *s.alignmentLimitY * scale : std::max(8.0, 4.0 * tolWorkY);
    out.shiftLimit = cv::Point2d(maxShiftX / scale, maxShiftY / scale);
    if (std::abs(sh.x) > maxShiftX + 1e-6 || std::abs(sh.y) > maxShiftY + 1e-6) {
        if (response >= kRejectShiftResponse) {
            out.reject = true;
            out.reason = "shift_exceeded";
        }
        sh = cv::Point2d(0, 0);   // a shift this large cannot be compensated reliably
    }
    out.shift = cv::Point2d(sh.x / scale, sh.y / scale);

    cv::Mat curR = cur(region);
    if (std::abs(sh.x) >= 0.15 || std::abs(sh.y) >= 0.15) {
        cv::Mat m = (cv::Mat_<double>(2, 3) << 1, 0, -sh.x, 0, 1, -sh.y);
        cv::Mat warped;
        cv::warpAffine(curR, warped, m, curR.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
        curR = warped;
    }
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
    const int thr = cvRound(80.0 - 75.0 * std::clamp(s.sensitivity, 0.0, 1.0));
    cv::Mat bin;
    cv::threshold(diff, bin, thr, 255, cv::THRESH_BINARY);
    cv::morphologyEx(bin, bin, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));

    // Count actual connected pixels. Filling external contours turns rings into solid
    // discs and can count clean holes (including pixels outside a concave ROI) as defects.
    cv::bitwise_and(bin, maskR, bin);
    cv::Mat labels, stats, centroids;
    const int components = cv::connectedComponentsWithStats(bin, labels, stats, centroids, 8, CV_32S);
    const double minArea = std::max(1.0, s.minBlobArea * scale * scale);
    cv::Mat kept = cv::Mat::zeros(bin.size(), CV_8U);
    out.pixelThreshold = thr;
    out.blobsTotal = components - 1;
    for (int i = 1; i < components; ++i) {
        const double area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < minArea) {
            out.largestDroppedBlob = std::max(out.largestDroppedBlob, area);
            continue;
        }
        const cv::Rect bb(stats.at<int>(i, cv::CC_STAT_LEFT), stats.at<int>(i, cv::CC_STAT_TOP),
                          stats.at<int>(i, cv::CC_STAT_WIDTH), stats.at<int>(i, cv::CC_STAT_HEIGHT));
        {
            const cv::Mat blobMask = labels(bb) == i;
            kept(bb).setTo(255, blobMask);
            double mx = 0;
            cv::minMaxLoc(diff(bb), nullptr, &mx, nullptr, nullptr, blobMask);
            out.keptBlobMaxDiff.push_back(static_cast<int>(mx));
            out.keptBlobAreas.push_back(area);
        }
        out.zones.emplace_back(cvRound((bb.x + region.x) / scale), cvRound((bb.y + region.y) / scale),
                               cvRound(bb.width / scale), cvRound(bb.height / scale));
    }
    out.blobsKept = static_cast<int>(out.zones.size());
    const int flagged = cv::countNonZero(kept);
    out.areaPercent = prep->roiArea > 0 ? 100.0 * flagged / prep->roiArea : 0.0;
    out.anomalyScore = std::min(1.0, (out.areaPercent / kScoreFullScalePercent) * kScoreMultiplier);
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
    // anomaly energy: diff normalised to the detection threshold (threshold -> mid grey, 2x threshold -> white),
    // values below half of the threshold are dropped so that noise does not paint the whole object. Unlike a
    // blob-gated map this also shows sub-threshold hints, so a near-miss is visible and a clean frame is empty.
    const double thr = std::max(1, out.pixelThreshold);
    cv::Mat core;
    out.diff.convertTo(core, CV_8U, 128.0 / thr);
    cv::threshold(core, core, 63, 0, cv::THRESH_TOZERO);
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
    return heat;
}

cv::Mat fullDiff(const InspectOutcome& out) { return placeInCanvas(out, out.diff); }
cv::Mat fullMask(const InspectOutcome& out) { return placeInCanvas(out, out.kept); }
cv::Mat fullAligned(const InspectOutcome& out) { return placeInCanvas(out, out.aligned); }

}
