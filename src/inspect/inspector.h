#pragma once
#include "../production/core/inspector.h"
#include "seam.h"
#include "learning_matcher.h"
#include <memory>
#include <string>
#include <vector>

namespace cd {
inline constexpr const char* kInspectionEngine = "cpp-detector-eefefce4";
inline constexpr const char* kEngineSourceCommit = "eefefce455ea93a2184c8d4e9178a0f1eb45847a";

// The local UI uses percentages for threshold/score. Everything else is native production units.
struct InspectParams : cas::Settings {
    double rejectThreshold = 60.0; // native threshold * 100
    double downscale = 1.0;        // native inspect_scale
    LearningOptions learning;
    std::vector<std::vector<cv::Point>> exclusions; // camera/reference pixel coordinates

    cas::Settings nativeSettings() const {
        cas::Settings s = *this;
        s.threshold = rejectThreshold / 100.0;
        s.inspectScale = downscale;
        s.learningRobustEnabled = learning.robust;
        s.learningShadowEnabled = learning.shadows;
        s.learningSearchRadius = learning.searchRadius;
        return s;
    }
    void applyNativeSettings(const cas::Settings& s) {
        static_cast<cas::Settings&>(*this) = s;
        rejectThreshold = s.threshold * 100.0;
        downscale = s.inspectScale;
        learning = {s.learningRobustEnabled, s.learningShadowEnabled, s.learningSearchRadius};
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
    nlohmann::json learningDiagnostics = nlohmann::json::array();
    double learningMs = 0;
    cas::InspectOutcome native;     // unchanged native calculations, also used by parity tests
    cas::Settings settings;
    std::vector<std::vector<cv::Point>> exclusions;
    SeamResult seam;               // native two-endpoint seam result adapted only for the local UI
};

class Inspector {
public:
    void setReference(const cv::Mat& bgr, const std::vector<cv::Point>& roi, const InspectParams& p);
    InspectResult inspect(const cv::Mat& bgr, const InspectParams& p, const SeamParams* seam = nullptr) const;
    void setLearned(std::vector<cas::LearnedRegion> regions) { if (reference_) reference_->setLearned(std::move(regions)); }
    bool hasReference() const { return reference_ && !reference_->bgr().empty(); }
private:
    std::shared_ptr<cas::Reference> reference_;
};
}
