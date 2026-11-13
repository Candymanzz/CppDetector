#pragma once
#include "reference.h"
#include "settings.h"
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace cas {

constexpr double kUpperCornerSensitivityMultiplier = 1.1;

struct InspectOutcome {
    nlohmann::json learningDiagnostics = nlohmann::json::array();
    double learningMs = 0;
    double rawAnomalyScore = 0.0;
    std::vector<std::string> matchedCaseIds;
    double areaPercent = 0.0;        // anomalous pixels / ROI pixels * 100
    double anomalyScore = 0.0;       // whole-ROI score; upper corners use a lower contrast threshold
    double mainRoiScore = 0.0;       // unchanged base inspection, before the upper-corner sensitivity boost
    double upperCornerSensitivity = 0.0;
    double upperCornerExtraScore = 0.0; // final score minus mainRoiScore (after saturation)
    int upperCornerPixelThreshold = 0;
    int upperCornerArea = 0;         // selected ROI pixels at work resolution
    bool labelEnabled = false;
    double labelCoverage = 1, labelBoundarySupport = 0;
    cv::Mat inspectionMask;
    bool surfaceSkipped = false;
    NormPolygons exclusions; // exact mask configuration used by this inspection
    bool reject = false;
    std::string reason;              // "", "score_over_threshold", "shift_exceeded", "inspection_excluded"
    cv::Point2d shift;               // detected translation, original-frame pixels
    cv::Point2d rawShift;            // estimate before the limit/ignore rules (diagnostics)
    double shiftResponse = 0.0;      // accepted ECC correlation, otherwise coarse phase response
    double coarseShiftResponse = 0.0;
    bool shiftRefined = false;       // false when the coarse estimate was retained
    std::vector<cv::Rect> zones;     // anomaly zones, original-frame pixels
    double alignMs = 0, diffMs = 0, anomalyMs = 0, totalMs = 0;
    // diagnostics for tuning (logged, not part of the API response)
    double roiArea = 0.0;            // ROI pixels at work resolution
    double scale = 1.0;              // work scale (inspect_scale after clamping)
    int pixelThreshold = 0;          // diff level that counts as anomalous, derived from sensitivity
    cv::Size envelopeKernel{1, 1};   // shift-tolerance kernel width (X) and height (Y) at work resolution
    cv::Point2d shiftLimit;          // maximum compensated shift per axis, original-frame pixels
    double gain = 1.0;               // brightness normalisation gain
    double refMean = 0.0, curMean = 0.0;   // ROI mean brightness before normalisation
    double diffMean = 0.0;           // mean diff inside the ROI
    int diffMax = 0, diffP99 = 0;    // diff extremes inside the ROI
    int blobsTotal = 0, blobsKept = 0;
    double largestDroppedBlob = 0.0; // biggest blob below min_blob_area, work pixels (near-miss indicator)
    // seam straightness (filled by Service::run when the profile enables it)
    bool seamEnabled = false, seamFound = false, seamReject = false;
    double seamRatio = 0.0;          // max deviation / tolerance (> 1 = reject)
    double seamMaxDev = 0.0;         // px at the worst position
    double seamWorstT = 0.0;         // 0..1 along the seam
    double seamCoverage = 0.0;       // share of evaluated rows where the line was located
    double seamMs = 0.0;
    std::vector<double> keptBlobAreas;   // work pixels, same order as zones
    std::vector<int> keptBlobMaxDiff;    // peak diff per kept blob, same order as zones
    // visuals (only filled when requested)
    cv::Size workSize;
    cv::Rect region;
    cv::Mat diff;                    // CV_8U, region-sized
    cv::Mat kept;                    // CV_8U 0/255, region-sized (accepted anomaly blobs)
    cv::Mat aligned;                 // CV_8U, region-sized
    cv::Mat scoredEnergy;            // CV_8U, region-sized; includes the local visibility threshold
};

// Throws std::invalid_argument when the frame size differs from the reference size.
InspectOutcome inspectFrame(const Reference& ref, const cv::Mat& bgr, const Settings& s, bool keepVisuals);

// Gray-u8 heatmap: anomaly energy gated by the accepted blobs + a soft halo, ROI-masked,
// at most `maxWidth` wide (0 = work resolution).
cv::Mat buildHeatmap(const InspectOutcome& out, const NormPolygon& roi, int maxWidth);

// Full-size (work resolution) gray images for the visuals endpoint.
cv::Mat fullDiff(const InspectOutcome& out);
cv::Mat fullMask(const InspectOutcome& out);
cv::Mat fullAligned(const InspectOutcome& out);

}
