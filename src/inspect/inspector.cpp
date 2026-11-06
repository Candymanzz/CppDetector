#include "inspector.h"
#include "../production/core/seam.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>

namespace cd {

void Inspector::setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams& params) {
    if (bgr.empty()) { reference_.reset(); return; }
    reference_ = std::make_shared<cas::Reference>(bgr.clone());
    cas::NormPolygon normalised;
    for (const auto& p : roi)
        normalised.emplace_back(p.x / double(std::max(1, bgr.cols - 1)),
                                p.y / double(std::max(1, bgr.rows - 1)));
    reference_->setRoi(std::move(normalised));
    std::vector<cas::NormPolygon> exclusions;
    for (const auto& polygon : params.exclusions) {
        cas::NormPolygon points;
        for (const auto& q : polygon) points.emplace_back(q.x / double(std::max(1, bgr.cols - 1)), q.y / double(std::max(1, bgr.rows - 1)));
        exclusions.push_back(std::move(points));
    }
    reference_->setExclusions(std::move(exclusions));
}

InspectResult Inspector::inspect(const cv::Mat& bgr, const InspectParams& p, const SeamParams* seam) const {
    InspectResult r;
    r.exclusions = p.exclusions;
    if (!hasReference()) { r.reason = "no_reference"; return r; }
    if (bgr.empty() || bgr.size() != reference_->size()) { r.reason = "size_mismatch"; return r; }

    auto& s = r.settings;
    s = p.nativeSettings().forFrame(bgr.size());
    if (seam && seam->pts.size() >= 2) {
        s.seam.enabled = seam->enabled;
        s.seam.p0x = seam->pts.front().x / bgr.cols; s.seam.p0y = seam->pts.front().y / bgr.rows;
        s.seam.p1x = seam->pts.back().x / bgr.cols; s.seam.p1y = seam->pts.back().y / bgr.rows;
        s.seam.band = seam->bandHalf; s.seam.tolMid = seam->tolMid;
        s.seam.tolEdge = seam->tolEdge; s.seam.edgeFrac = seam->edgeFrac;
    }
    auto& out = r.native;
    out = cas::inspectFrame(*reference_, bgr, s, true);
    r.learningDiagnostics = out.learningDiagnostics;
    r.learningMs = out.learningMs;

    // Same seam calculation and decision merge as cppAnalisSurface Service::run (20909141).
    if (s.sensitivity > 0.0 && s.seam.enabled && s.seam.hasLine() && out.reason != "size_mismatch") {
        cv::Mat excluded;
        if (!p.exclusions.empty()) {
            excluded = cv::Mat::zeros(bgr.size(), CV_8U);
            for (const auto& polygon : p.exclusions) {
                std::vector<cv::Point> shifted;
                for (const auto& q : polygon) shifted.emplace_back(cvRound(q.x + out.shift.x), cvRound(q.y + out.shift.y));
                cv::fillPoly(excluded, std::vector<std::vector<cv::Point>>{shifted}, 255);
            }
        }
        const auto sr = cas::measureSeam(bgr, s.seam, excluded);
        out.seamEnabled = true; out.seamFound = sr.found; out.seamReject = sr.reject;
        out.seamRatio = sr.ratio; out.seamMaxDev = sr.maxDev; out.seamWorstT = sr.worstT;
        out.seamCoverage = sr.coverage; out.seamMs = sr.ms; out.totalMs += sr.ms;
        if (sr.reject) {
            out.reject = true;
            if (out.reason.empty() || out.reason == "inspection_excluded") out.reason = "seam_deviation";
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
    // Remove decorative heatmap blur from holes as well as scored pixels.
    for (const auto& polygon : p.exclusions) {
        std::vector<cv::Point> points;
        for (const auto& q : polygon) points.emplace_back(cvRound(q.x * double(r.heatmap.cols - 1) / std::max(1, bgr.cols - 1)), cvRound(q.y * double(r.heatmap.rows - 1) / std::max(1, bgr.rows - 1)));
        cv::fillPoly(r.heatmap, std::vector<std::vector<cv::Point>>{points}, 0);
    }
    r.heatmapMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - visualStart).count();
    return r;
}
}
