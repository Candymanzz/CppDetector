#include "inspector.h"
#include "production/core/inspector.h"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace cd {
namespace {
cas::Settings settings(const InspectParams& p) {
    auto s = cas::Settings{}.withMetricDefaults(p.cameraId);
    s.sensitivity = std::clamp(p.sensitivity, 0.0, 1.0);
    s.threshold = p.rejectThreshold / 100.0;
    s.shiftToleranceX = s.shiftToleranceY = p.shiftTolerance;
    s.inspectScale = p.downscale;
    s.minBlobArea = p.minBlobArea;
    s.horizontalShiftMm = p.horizontalShiftMm;
    s.verticalShiftMm = p.verticalShiftMm;
    return s;
}
}
void Inspector::setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams&) {
    if (bgr.empty()) throw std::invalid_argument("empty reference");
    auto next = std::make_shared<cas::Reference>(bgr.clone());
    cas::NormPolygon normalized;
    for (const auto& point : roi)
        normalized.emplace_back(double(point.x) / std::max(1, bgr.cols - 1),
                                double(point.y) / std::max(1, bgr.rows - 1));
    next->setRoi(std::move(normalized));
    reference_ = std::move(next);
    refSize_ = bgr.size();
}
InspectResult Inspector::inspect(const cv::Mat& bgr, const InspectParams& p) const {
    const auto started = std::chrono::steady_clock::now();
    InspectResult result;
    if (!hasReference()) { result.reason = "no_reference"; return result; }
    if (bgr.size() != refSize_) { result.reason = "size_mismatch"; return result; }
    const auto s = settings(p);
    const auto outcome = cas::inspectFrame(*reference_, bgr, s, true);
    result.score = outcome.anomalyScore * 100.0;
    result.reject = outcome.reject;
    result.reason = outcome.reason;
    result.shift = outcome.rawShift;
    result.appliedShift = outcome.shift;
    const auto units = s.mmPerPixel(bgr.size());
    result.shiftMm = {result.shift.x * units.x, result.shift.y * units.y};
    result.shiftResponse = outcome.shiftResponse;
    result.shiftRefined = outcome.shiftRefined;
    result.zones = outcome.zones;
    result.diffThreshold = outcome.pixelThreshold;
    result.heatmap = cas::fullDiff(outcome);
    result.heatmap.setTo(0, cas::fullMask(outcome) == 0);
    result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return result;
}
}
