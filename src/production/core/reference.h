#pragma once
#include <opencv2/core.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace cas {

using NormPolygon = std::vector<cv::Point2d>;   // ROI points in 0..1 of the frame

// Reference data prepared for one analysis scale and the current ROI (immutable, shared between threads).
struct Prepared {
    double scale = 1.0;
    cv::Size workSize;        // frame size at this scale
    cv::Mat refGray;          // CV_8U blurred gray of the reference at work size
    cv::Mat roiMask;          // CV_8U 0/255 at work size (full frame when no ROI)
    int roiArea = 0;          // pixels in roiMask
    cv::Rect region;          // ROI bounding box + padding (analysis is limited to it)
    cv::Size smallSize;       // size used for the cheap shift estimation
    cv::Mat refSmall;         // CV_32F reference region resized to smallSize
    cv::Mat hann;             // CV_32F Hanning window of smallSize
};

// One reference frame of one camera-scoped product plus its ROI.
class Reference {
public:
    explicit Reference(cv::Mat bgr);

    const cv::Mat& bgr() const { return bgr_; }
    cv::Size size() const { return bgr_.size(); }

    // Empty polygon = whole frame. Returns true when the ROI actually changed (cache is dropped then).
    bool setRoi(NormPolygon pts);
    NormPolygon roi() const;

    std::shared_ptr<const Prepared> prepared(double scale) const;

private:
    std::shared_ptr<const Prepared> build(double scale, const NormPolygon& roi) const;

    cv::Mat bgr_;
    mutable std::mutex m_;
    NormPolygon roi_;
    mutable std::map<int, std::shared_ptr<const Prepared>> cache_;
};

}
