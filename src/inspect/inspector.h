#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <memory>
namespace cas { class Reference; }
#include <vector>

namespace cd {

// The production engine calculates a sensitivity-dependent 0..1 score.
// This adapter displays that score in the test application's existing 0..100 units.

struct InspectParams {
    double sensitivity = 0.5;       // production 0..1; endpoints force accept/reject
    double rejectThreshold = 10.0;  // 0..100; production uses score >= threshold
    int shiftTolerance = 3;         // px, micro-shift tolerance
    double downscale = 1.0;         // 1.0 = original
    int minBlobArea = 20;           // original-frame pixels
    double horizontalShiftMm = 3.0, verticalShiftMm = 3.96;
    int cameraId = -1;             // selects the production provisional mm scale
};

struct InspectResult {
    double score = 0.0;             // 100 * production anomalyScore
    bool reject = false;
    std::string reason;             // "", "score_over_threshold", "shift_exceeded", "no_reference"
    cv::Point2d shift;              // raw translation, original-frame pixels
    cv::Point2d appliedShift, shiftMm;
    double shiftResponse = 0;
    bool shiftRefined = false;
    std::vector<cv::Rect> zones;    // bboxes of anomaly zones (original coords)
    cv::Mat heatmap;                // CV_8U diff map (working resolution)
    int diffThreshold = 0;          // diff level treated as anomalous (sensitivity mapped)
    double ms = 0.0;
};

class Inspector {
public:
    // roi: polygon in original-image coordinates; empty = whole frame
    void setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams& p);
    InspectResult inspect(const cv::Mat& bgr, const InspectParams& p) const;
    bool hasReference() const { return bool(reference_); }

private:
    std::shared_ptr<cas::Reference> reference_;
    cv::Size refSize_;

};

} // namespace cd
