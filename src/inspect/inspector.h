#pragma once
#include "../production/core/inspector.h"
#include "seam.h"
#include "calibration.h"
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
    MetricCalibration calibration;
    cv::Point2d estimatedMmPerPixel{0.25,0.33}; // provisional scale at 1224x1024; empirical tuning later
    std::optional<double> horizontalShiftMm, verticalShiftMm;
    double downscale = 1.0;         // native inspect_scale
    int minBlobArea = 20;           // original-frame area; native core applies scale squared
    cv::Point2d mmPerPixel(cv::Size size) const {
        if (size.empty()) size={1224,1024};
        if (calibration.ready()) return calibration.mmPerPixel(size);
        return {estimatedMmPerPixel.x*1224.0/size.width,estimatedMmPerPixel.y*1024.0/size.height};
    }
    cv::Point2d tolerancePixels(cv::Size size) const {
        cv::Point2d pixels(shiftToleranceX,shiftToleranceY);
        const auto units = mmPerPixel(size);
        if (horizontalShiftMm) pixels.x = *horizontalShiftMm / units.x;
        if (verticalShiftMm) pixels.y = *verticalShiftMm / units.y;
        return pixels;
    }
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
    cas::InspectOutcome native;     // core diagnostics including local score calibration, used by parity tests
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
