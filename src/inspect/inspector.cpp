#include "inspector.h"
#include "../production/core/seam.h"
#include "seam_mask.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>

namespace cd {

void Inspector::setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams& params) {
    seamReference_.reset();seamReferenceReason_="seam_reference_required";
    adaptiveReference_.reset();
    if (bgr.empty()) { reference_.reset(); return; }
    reference_ = std::make_shared<cas::Reference>(bgr.clone());
    cas::NormPolygon normalised;
    for (const auto& p : roi)
        normalised.emplace_back(p.x / double(std::max(1, bgr.cols - 1)),
                                p.y / double(std::max(1, bgr.rows - 1)));
    reference_->setRoi(std::move(normalised));
    cas::NormPolygon label;
    if (params.labelTrackingEnabled)
        for (const auto& q : params.labelOutline) label.emplace_back(q.x/double(std::max(1,bgr.cols-1)),q.y/double(std::max(1,bgr.rows-1)));
    reference_->setLabelOutline(std::move(label));
    // Exclusions are passed with Settings per inspection, preserving immutable cache variants.
    if (params.adaptiveRoi.enabled) {
        auto settings = params.nativeSettings();
        settings.exclusions.clear();
        for (const auto& polygon : params.exclusions) {
            cas::NormPolygon points;
            for (const auto& q : polygon)
                points.emplace_back(q.x / double(std::max(1, bgr.cols - 1)),
                                    q.y / double(std::max(1, bgr.rows - 1)));
            settings.exclusions.push_back(std::move(points));
        }
        adaptiveReference_ = std::make_shared<AdaptiveRoiReference>(bgr,
            reference_->prepared(1, settings.exclusions)->roiMask);
    }
}

void Inspector::configureSeamReference(const SeamParams& params) {
    seamReference_.reset();seamReferenceReason_="seam_reference_required";
    if(!params.referenceSearch||!hasReference()||params.referenceSignature.empty())return;
    const auto pixels=reference_->fingerprint();
    if(params.referenceSignature!=seamReferenceSignature(pixels,params)){seamReferenceReason_="seam_reference_changed";return;}
    const auto prepared=prepareSeamReference(reference_->bgr(),params,pixels);
    if(prepared->ready){seamReference_=prepared;seamReferenceReason_.clear();}
    else seamReferenceReason_=prepared->reason;
}

bool Inspector::confirmSeamReference(SeamParams& params,std::string& error) {
    if(!hasReference()){error="no_reference";return false;}
    const auto prepared=prepareSeamReference(reference_->bgr(),params,reference_->fingerprint());
    if(!prepared->ready){error=prepared->reason;return false;}
    params.referenceSearch=true;params.maskSearch=false;params.referenceSignature=prepared->signature;
    seamReference_=prepared;seamReferenceReason_.clear();return true;
}

nlohmann::json Inspector::seamReferenceInfo() const {
    nlohmann::json result={{"ready",bool(seamReference_)},{"reason",seamReferenceReason_},{"coverage",seamReference_?seamReference_->coverage:0},{"path",nlohmann::json::array()}};
    if(seamReference_){
        result["signature"]=seamReference_->signature;
        result["endpoints"]={{seamReference_->origin.x,seamReference_->origin.y},{seamReference_->origin.x+seamReference_->u.x*seamReference_->length,seamReference_->origin.y+seamReference_->u.y*seamReference_->length}};
        for(int i=0;i<seamReference_->rows;i+=3){const auto p=seamReference_->origin+seamReference_->u*i+seamReference_->normal*seamReference_->axis[i];result["path"].push_back({p.x,p.y});}
        result["normal"]={seamReference_->normal.x,seamReference_->normal.y};
    }
    return result;
}

InspectResult Inspector::inspect(const cv::Mat& bgr, const InspectParams& p, const SeamParams* seam) const {
    InspectResult r;
    r.exclusions = p.exclusions;
    if (!hasReference()) { r.reason = "no_reference"; return r; }
    if (bgr.empty() || bgr.size() != reference_->size()) { r.reason = "size_mismatch"; return r; }

    auto& s = r.settings;
    s = p.nativeSettings().forFrame(bgr.size());
    s.exclusions.clear();
    for (const auto& polygon : p.exclusions) {
        cas::NormPolygon points;
        for (const auto& q : polygon) points.emplace_back(q.x / double(std::max(1, bgr.cols - 1)), q.y / double(std::max(1, bgr.rows - 1)));
        s.exclusions.push_back(std::move(points));
    }
    if (seam && seam->pts.size() >= 2) {
        s.seam.enabled = seam->enabled;
        s.seam.p0x = seam->pts.front().x / bgr.cols; s.seam.p0y = seam->pts.front().y / bgr.rows;
        s.seam.p1x = seam->pts.back().x / bgr.cols; s.seam.p1y = seam->pts.back().y / bgr.rows;
        s.seam.band = seam->bandHalf; s.seam.tolMid = seam->tolMid;
        s.seam.tolEdge = seam->tolEdge; s.seam.edgeFrac = seam->edgeFrac;
    }
    auto& out = r.native;
    cas::InspectionGeometry geometry;
    cv::Mat inspectionImage=bgr;
    if (p.adaptiveRoi.enabled && reference_->prepared(1, s.exclusions)->roiArea>0) {
        auto features=adaptiveReference_;
        if(!features)features=std::make_shared<AdaptiveRoiReference>(reference_->bgr(),reference_->prepared(1, s.exclusions)->roiMask);
        r.adaptive=features->locate(bgr,p.adaptiveRoi);
        if(!r.adaptive.valid) {
            r.controlComplete=false;r.reject=s.sensitivity>0;r.reason=r.adaptive.reason;r.ms=r.adaptive.ms;
            out.reject=r.reject;out.reason=r.reason;out.totalMs=r.ms;
            return r;
        }
        inspectionImage=r.adaptive.aligned;
        geometry.validReference=r.adaptive.validReference;geometry.prealigned=true;geometry.allowLearned=false;
    }
    out = cas::inspectFrame(*reference_, inspectionImage, s, true,r.adaptive.valid?&geometry:nullptr);
    if(r.adaptive.valid) {
        const auto nominal=reference_->prepared(s.inspectScale, s.exclusions);
        r.adaptive.coverage=nominal->roiArea?double(out.roiArea)/nominal->roiArea:0;
        cv::Mat checked;cv::resize(out.comparisonMask,checked,bgr.size(),0,0,cv::INTER_NEAREST);
        r.adaptive.checkedCurrent=r.adaptive.toCurrentImage(checked,cv::INTER_NEAREST);
        if(r.adaptive.coverage<p.adaptiveRoi.minCoverage) {
            r.controlComplete=false;out.reject=s.sensitivity>0;out.reason="adaptive_low_coverage";
        }
        const cv::Point2f centre(float(bgr.cols)*.5f,float(bgr.rows)*.5f);
        const auto shift=r.adaptive.toCurrent(centre)-centre;
        out.rawShift=out.shift=cv::Point2d(shift);
        out.shiftLimit={p.adaptiveRoi.maxMotion,p.adaptiveRoi.maxMotion};
        out.alignMs+=r.adaptive.ms;out.totalMs+=r.adaptive.ms;
    }
    r.learningDiagnostics = out.learningDiagnostics;
    r.learningMs = out.learningMs;

    // Same seam calculation and decision merge as cppAnalisSurface Service::run (20909141).
    if (s.sensitivity > 0.0 && ((s.seam.enabled && s.seam.hasLine())||(seam&&seam->enabled&&(seam->referenceSearch||seam->maskSearch))) && out.reason != "size_mismatch") {
        cv::Mat excluded;
        if(r.adaptive.valid)excluded=~r.adaptive.validReference;
        if (!p.exclusions.empty()) {
            if(excluded.empty())excluded = cv::Mat::zeros(bgr.size(), CV_8U);
            for (const auto& polygon : p.exclusions) {
                std::vector<cv::Point> shifted;
                for (const auto& q : polygon) shifted.emplace_back(cvRound(q.x + (r.adaptive.valid?0:out.shift.x)), cvRound(q.y + (r.adaptive.valid?0:out.shift.y)));
                cv::fillPoly(excluded, std::vector<std::vector<cv::Point>>{shifted}, 255);
            }
        }
        auto& ui = r.seam;
        if(seam->referenceSearch) {
            ui=inspectReferenceSeam(inspectionImage,seamReference_.get(),*seam,r.adaptive.valid?cv::Point2d{}:out.shift,excluded);
            if(!seamReference_)ui.maskReason=seamReferenceReason_.empty()?"seam_reference_required":seamReferenceReason_;
        }
        else if(seam->maskSearch) ui=inspectMaskedSeam(inspectionImage,reference_->bgr(),*seam,r.adaptive.valid?cv::Point2d{}:out.shift,excluded);
        else {
            const auto native = cas::measureSeam(inspectionImage, s.seam, excluded);
            ui.enabled=true;ui.found=native.found;ui.reject=native.reject;ui.coverage=native.coverage;
            ui.ratio=native.ratio;ui.maxDev=native.maxDev;ui.worstT=native.worstT;
            ui.centerPts=native.centerPts;ui.badPts=native.badPts;ui.ms=native.ms;
            ui.rowPos=native.rowPos;ui.rowEdge=native.rowEdge;ui.rowRef=native.rowRef;
            ui.rowDev=native.rowDev;ui.rowTol=native.rowTol;ui.rowState=native.rowState;
            ui.path={cv::Point2f(seam->pts.front()),cv::Point2f(seam->pts.back())};
            const auto direction=ui.path.back()-ui.path.front();const float length=std::max(1e-6f,float(cv::norm(direction)));
            const cv::Point2f normal(-direction.y/length,direction.x/length);ui.pathNormal={normal,normal};
        }
        const auto& sr=ui;
        out.seamEnabled = true; out.seamFound = sr.found; out.seamReject = sr.reject;
        out.seamRatio = sr.ratio; out.seamMaxDev = sr.maxDev; out.seamWorstT = sr.worstT;
        out.seamCoverage = sr.coverage; out.seamMs = sr.ms; out.totalMs += sr.ms;
        if (sr.reject) {
            out.reject = true;
            if (out.reason.empty() || out.reason == "inspection_excluded") out.reason = sr.maskReason.empty()?"seam_deviation":sr.maskReason;
            if (!sr.badPts.empty()) {
                cv::Rect bb = cv::boundingRect(sr.badPts);
                bb.x -= 12; bb.y -= 12; bb.width += 24; bb.height += 24;
                bb &= cv::Rect(0, 0, bgr.cols, bgr.rows);
                if (bb.area() > 0) out.zones.push_back(bb);
            }
        }
        if(sr.maskSearch&&!sr.controlComplete){r.controlComplete=false;out.reject=true;out.reason=sr.maskReason;}
        if(r.adaptive.valid && (!sr.found||sr.coverage<.60)) {
            r.controlComplete=false;out.reject=true;out.reason="adaptive_seam_unavailable";
        }
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
    if(r.adaptive.valid) {
        cv::Mat mask;cv::resize(r.adaptive.validReference,mask,r.heatmap.size(),0,0,cv::INTER_NEAREST);
        r.heatmap.setTo(0,mask==0);
        r.heatmap=r.adaptive.toCurrentImage(r.heatmap,cv::INTER_LINEAR);
        r.heatmap.setTo(0,r.adaptive.checkedCurrent==0);
        const double factor=std::min(1.,512./r.heatmap.cols);
        if(factor<1)cv::resize(r.heatmap,r.heatmap,{512,std::max(1,cvRound(r.heatmap.rows*factor))},0,0,cv::INTER_AREA);
        for(auto& zone:r.zones) {
            std::vector<cv::Point2f> points;
            for(int y=0;y<=2;++y)for(int x=0;x<=2;++x)
                points.push_back(r.adaptive.toCurrent({float(zone.x+zone.width*x/2),float(zone.y+zone.height*y/2)}));
            zone=cv::boundingRect(points)&cv::Rect(0,0,bgr.cols,bgr.rows);
        }
        auto transform=[&](std::vector<cv::Point2f>& points){for(auto& point:points)point=r.adaptive.toCurrent(point);};
        transform(r.seam.path);transform(r.seam.centerPts);transform(r.seam.badPts);
        transform(r.seam.rowPos);transform(r.seam.rowEdge);transform(r.seam.rowRef);
        for(auto& side:r.seam.sides){transform(side.edge);transform(side.reference);}
        if(r.seam.path.size()>=2) {
            const auto d=r.seam.path.back()-r.seam.path.front();const float length=std::max(1e-6f,float(cv::norm(d)));
            r.seam.pathNormal.assign(r.seam.path.size(),{-d.y/length,d.x/length});
        }
        for(auto& polygon:r.exclusions)for(auto& q:polygon)q=r.adaptive.toCurrent(cv::Point2f(q));
    }
    r.heatmapMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - visualStart).count();
    return r;
}
}
