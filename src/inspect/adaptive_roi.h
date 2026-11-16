#pragma once
#include <opencv2/core.hpp>
#include <array>
#include <string>
#include <vector>

namespace cd {
struct AdaptiveRoiOptions {
    bool enabled = false;
    double maxMotion = 96;       // original image pixels, independent of comparison tolerance
    double minCoverage = .70;    // fraction of the reference inspection area
};

struct AdaptiveRoiResult {
    bool valid = false;
    std::string reason;
    int features = 0, tracked = 0, inliers = 0;
    double coverage = 0, residual = 0, ms = 0;
    // Normalised reference -> current: X'=a0+a1*X+a2*Y+a3*X^2; Y'=b0+b1*X+b2*Y.
    // Seven global coefficients, never an independently deformable field per defect.
    std::array<double, 7> model{0,1,0,0,0,0,1};
    cv::Size size;
    cv::Mat aligned, validReference, checkedCurrent, mapX, mapY, inverseX, inverseY;
    std::vector<cv::Point2f> referencePoints, currentPoints;
    cv::Point2f toCurrent(cv::Point2f p) const;
    cv::Mat toCurrentImage(const cv::Mat& referenceImage, int interpolation) const;
};

// Immutable after construction; every inspection estimates directly from the primary reference.
class AdaptiveRoiReference {
public:
    AdaptiveRoiReference(const cv::Mat& bgr, const cv::Mat& inspectionMask);
    AdaptiveRoiResult locate(const cv::Mat& current, const AdaptiveRoiOptions& options) const;
    const cv::Mat& mask() const { return mask_; }
private:
    cv::Size size_, trackingSize_;
    cv::Mat gray_, mask_, coarse_, window_;
    std::vector<cv::Mat> pyramid_;
    std::vector<cv::Point2f> points_;
};
}
