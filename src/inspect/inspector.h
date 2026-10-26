#pragma once
#include "../production/core/inspector.h"
#include "seam.h"
#include <memory>
#include <string>
#include <vector>

namespace cd {
inline constexpr const char* kInspectionEngine = "position-x-y";
inline constexpr const char* kEngineSourceCommit = "885a79ed66460440c7b22b6b5430425cd9d1849e";

// The local UI uses percentages for threshold/score. Everything else is native production units.
struct InspectParams {
    double sensitivity = 0.5;
    double rejectThreshold = 25.0; // native Settings::threshold * 100, (0..100]
    int shiftToleranceX = 3;        // original-frame px
    int shiftToleranceY = 3;        // original-frame px
    double downscale = 1.0;         // native inspect_scale
    int minBlobArea = 20;           // original-frame area; native core applies scale squared
};

struct InspectResult {
    double score = 0.0;             // native anomaly_score * 100, NOT anomalous area percent
    bool reject = false;
    std::string reason;
    cv::Point2d shift, shiftLimit;   // original-frame px
    cv::Size envelopeKernel{1, 1};
    std::vector<cv::Rect> zones;    // original-frame coordinates
    cv::Mat heatmap;                // native buildHeatmap energy (0..255), <= 512 px wide
    int diffThreshold = 0;
    double ms = 0.0, heatmapMs = 0.0;
    cas::InspectOutcome native;     // unchanged native calculations, also used by parity tests
    cas::Settings settings;
    SeamResult seam;               // native two-endpoint seam result adapted only for the local UI
};

class Inspector {
public:
    void setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams& p);
    InspectResult inspect(const cv::Mat& bgr, const InspectParams& p, const SeamParams* seam = nullptr) const;
    bool hasReference() const { return reference_ && !reference_->bgr().empty(); }
private:
    std::shared_ptr<cas::Reference> reference_;
};
}
