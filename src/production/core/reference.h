#pragma once
#include <opencv2/core.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>

namespace cas {

using NormPolygon = std::vector<cv::Point2d>; // points in 0..1 of the reference frame
using NormPolygons = std::vector<NormPolygon>;

constexpr double kUpperCornerWidthFraction = 0.15;
constexpr double kUpperCornerHeightFraction = 1.0 / 7.0;

struct LearnedRegion {
    std::string id;
    cv::Rect bbox;
    cv::Mat bgr;
};

// Native-frame positioning cache, shared by all inspection scales for one ROI.
struct AlignmentPrepared {
    NormPolygons exclusions; // immutable identity shared only by inspections with the same zones
    cv::Rect region;          // original-frame coordinates
    cv::Size smallSize;       // coarse phase-correlation thumbnail, at most 256 px
    cv::Mat refSmall, hann;   // CV_32F, padded to the same even DFT size
    cv::Mat fineGray;         // CV_8U, ECC template, at most 512 px
    cv::Mat fineMask;         // eroded ROI, excluding the sampling border
    cv::Point2d coarseToNative, fineToNative;
    bool canRefine = false;
};

// Reference data prepared for one analysis scale and the current ROI (immutable, shared between threads).
struct Prepared {
    NormPolygons exclusions; // immutable per-inspection mask cache identity
    double scale = 1.0;
    cv::Size workSize;        // frame size at this scale
    cv::Mat refGray;          // CV_8U blurred gray of the reference at work size
    cv::Mat roiMask;          // CV_8U 0/255 at work size (full frame when no ROI)
    bool labelEnabled = false;
    int roiArea = 0;          // pixels in roiMask
    cv::Rect region;          // ROI bounding box + padding (analysis is limited to it)
    cv::Mat upperCornerMask;  // CV_8U, region-sized; top 1/7, outer 15% on each side of explicit ROI
    int upperCornerArea = 0;
    std::shared_ptr<const AlignmentPrepared> alignment;
};

// One reference frame of one camera-scoped product plus its ROI.
class Reference {
public:
    explicit Reference(cv::Mat bgr);

    std::string fingerprint() const;
    void setLearned(std::vector<LearnedRegion> regions);
    std::vector<LearnedRegion> learned() const;
    const cv::Mat& bgr() const { return bgr_; }
    cv::Size size() const { return bgr_.size(); }

    // Empty polygon = whole frame. Returns true when the ROI actually changed (cache is dropped then).
    bool setRoi(NormPolygon pts);
    NormPolygon roi() const;
    void setLabelOutline(NormPolygon pts);
    NormPolygon labelOutline() const;

    std::shared_ptr<const Prepared> prepared(double scale, const NormPolygons& exclusions = {}) const;

private:
    std::shared_ptr<Prepared> build(double scale, const NormPolygon& roi, const NormPolygons& exclusions, const NormPolygon& label) const;

    cv::Mat bgr_;
    mutable std::mutex m_;
    NormPolygon roi_, label_;
    std::vector<LearnedRegion> learned_;
    mutable std::map<int, std::shared_ptr<const Prepared>> cache_;
    mutable std::shared_ptr<const AlignmentPrepared> alignment_;
};

}
