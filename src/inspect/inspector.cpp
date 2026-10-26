#include "inspector.h"
#include "../production/core/seam.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>

namespace cd {

void Inspector::setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams&) {
    if (bgr.empty()) { reference_.reset(); return; }
    reference_ = std::make_shared<cas::Reference>(bgr.clone());
    cas::NormPolygon normalised;
    for (const auto& p : roi)
        normalised.emplace_back(p.x / double(std::max(1, bgr.cols - 1)),
                                p.y / double(std::max(1, bgr.rows - 1)));
    reference_->setRoi(std::move(normalised));
}

InspectResult Inspector::inspect(const cv::Mat& bgr, const InspectParams& p, const SeamParams* seam) const {
    InspectResult r;
    if (!hasReference()) { r.reason = "no_reference"; return r; }
    if (bgr.empty() || bgr.size() != reference_->size()) { r.reason = "size_mismatch"; return r; }

    auto& s = r.settings;
    s.sensitivity = p.sensitivity;
    s.threshold = p.rejectThreshold / 100.0;
    s.shiftToleranceX = p.shiftToleranceX; s.shiftToleranceY = p.shiftToleranceY;
    s.inspectScale = p.downscale; s.minBlobArea = p.minBlobArea;
    if (seam && seam->pts.size() >= 2) {
        s.seam.enabled = seam->enabled;
        s.seam.p0x = seam->pts.front().x / bgr.cols; s.seam.p0y = seam->pts.front().y / bgr.rows;
        s.seam.p1x = seam->pts.back().x / bgr.cols; s.seam.p1y = seam->pts.back().y / bgr.rows;
        s.seam.band = seam->bandHalf; s.seam.tolMid = seam->tolMid;
        s.seam.tolEdge = seam->tolEdge; s.seam.edgeFrac = seam->edgeFrac;
    }
    auto& out = r.native;
    out = cas::inspectFrame(*reference_, bgr, s, true);

    // Same seam calculation and decision merge as cppAnalisSurface Service::applySeam (885a79ed).
    if (s.seam.enabled && s.seam.hasLine() && out.reason != "size_mismatch") {
        const auto sr = cas::measureSeam(bgr, s.seam);
        out.seamEnabled = true; out.seamFound = sr.found; out.seamReject = sr.reject;
        out.seamRatio = sr.ratio; out.seamMaxDev = sr.maxDev; out.seamWorstT = sr.worstT;
        out.seamCoverage = sr.coverage; out.seamMs = sr.ms; out.totalMs += sr.ms;
        if (sr.reject) {
            out.reject = true;
            if (out.reason.empty()) out.reason = "seam_deviation";
            if (!sr.badPts.empty()) {
                cv::Rect bb = cv::boundingRect(sr.badPts);
                bb.x -= 12; bb.y -= 12; bb.width += 24; bb.height += 24;
                bb &= cv::Rect(0, 0, bgr.cols, bgr.rows);
                if (bb.area() > 0) out.zones.push_back(bb);
            }
        }
        auto& ui = r.seam;
        ui.enabled = true; ui.found = sr.found; ui.reject = sr.reject;
        ui.coverage = sr.coverage; ui.ratio = sr.ratio; ui.maxDev = sr.maxDev;
        ui.worstT = sr.worstT; ui.centerPts = sr.centerPts; ui.badPts = sr.badPts; ui.ms = sr.ms;
        ui.path = {cv::Point2f(seam->pts.front()), cv::Point2f(seam->pts.back())};
        cv::Point2f direction = ui.path.back() - ui.path.front();
        const float length = std::max(1e-6f, float(cv::norm(direction)));
        const cv::Point2f normal(-direction.y / length, direction.x / length);
        ui.pathNormal = {normal, normal};
    }

    r.score = out.anomalyScore * 100.0; r.reject = out.reject; r.reason = out.reason;
    r.shift = out.shift; r.shiftLimit = out.shiftLimit; r.envelopeKernel = out.envelopeKernel;
    r.zones = out.zones; r.diffThreshold = out.pixelThreshold; r.ms = out.totalMs;
    const auto visualStart = std::chrono::steady_clock::now();
    r.heatmap = cas::buildHeatmap(out, reference_->roi(), 512);
    r.heatmapMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - visualStart).count();
    return r;
}
}
