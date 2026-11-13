#pragma once
#include "calibration.h"
#include "reference.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace cas {

using json = nlohmann::json;

// Score scale at default sensitivity 0.5: anomaly_score = min(1, flagged-area-percent / kScoreFullScalePercent).
// 0..0.2% of the ROI area is stretched over 0..100% of the score; 0.2% and more saturate at 1.0.
// With the default threshold 0.6 a frame is rejected when ~0.12% of the ROI area is anomalous.
constexpr double kScoreFullScalePercent = 0.2;

// Label seam straightness check (a thin bright line along the label edge). Points are normalised 0..1 of the
// frame, p0 -> p1 follow the seam (approximate: `band` px of search margin absorb the pose of the product).
struct SeamSettings {
    bool enabled = false;
    double p0x = 0, p0y = 0, p1x = 0, p1y = 0;
    double band = 30;         // px, search half-width around the p0-p1 line
    double tolMid = 1.0;      // px, allowed deviation of the seam from a smooth line (middle part)
    double tolEdge = 5.0;     // px, allowed deviation near both ends of the seam
    double edgeFrac = 0.12;   // share of the seam length treated as an "end" on each side

    bool hasLine() const { return std::abs(p1x - p0x) + std::abs(p1y - p0y) > 1e-6; }
    json toJson() const;
    // Applies the fields present in `j` (partial update); returns an error text or an empty string.
    std::string applyJson(const json& j);
};

// The real, user-facing inspection settings (per analysis profile = camera).
struct Settings {
    double sensitivity = 0.5;     // 0..1, higher = stricter; forced endpoints apply to a nonempty inspection ROI
    double threshold = 0.6;     // (0..1], reject when anomaly_score >= threshold
    int shiftToleranceX = 3;      // original-frame px, horizontal comparison relaxation only
    int shiftToleranceY = 3;      // original-frame px, vertical comparison relaxation only
    double maxAlignmentShiftX = 12.0; // maximum global translation, original-frame pixels
    double maxAlignmentShiftY = 12.0;
    std::optional<double> horizontalShiftMm, verticalShiftMm;
    cv::Point2d estimatedMmPerPixel{0.25, 0.33}; // provisional at 1224x1024, as in ppc-reserv
    bool estimatedScaleExplicit = false;
    MetricCalibration calibration;
    int minBlobArea = 20;         // px of the original frame, smaller anomalies are noise
    double inspectScale = 1.0;    // (0..1], frame is downscaled by this factor before analysis
    bool learningRobustEnabled = true;
    bool learningShadowEnabled = true;
    int learningSearchRadius = 24; // original pixels, independent from global alignment
    NormPolygons exclusions; // normalized polygon holes, persisted per analysis profile
    SeamSettings seam;            // seam straightness check

    Settings withMetricDefaults(int cameraId = -1) const;
    Settings forFrame(cv::Size size) const;
    cv::Point2d mmPerPixel(cv::Size size) const;
    json toJson() const;
    // Applies the fields present in `j`; returns an error text or an empty string.
    std::string applyJson(const json& j);
};

// Per-request overrides (never persisted).
struct SettingsPatch {
    std::optional<double> sensitivity, threshold, inspectScale;
    std::optional<int> shiftToleranceX, shiftToleranceY, minBlobArea;
    std::optional<double> maxAlignmentShiftX, maxAlignmentShiftY;
    json metric;
    std::optional<bool> learningRobustEnabled, learningShadowEnabled;
    std::optional<int> learningSearchRadius;
    std::optional<NormPolygons> exclusions;
    json seam;                                       // partial seam settings (null = none)
    // Invalid legacy controls are ignored; invalid explicit exclusions throw std::invalid_argument.
    static SettingsPatch fromJson(const json& j);
    void applyTo(Settings& s) const;
};

// Persistent profile -> Settings map (JSON file). Re-reads external changes.
// A save replaces the whole file atomically; separate processes' updates are not serialised.
class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path file);
    Settings get(const std::string& profile, int cameraId = -1);
    bool has(const std::string& profile);
    std::string set(const std::string& profile, const json& body, Settings& out, int cameraId = -1);
    void reset(const std::string& profile);

private:
    void reloadIfStale();
    void save(const std::map<std::string, Settings>& next);
    std::filesystem::path file_;
    std::filesystem::file_time_type stamp_{};
    bool haveStamp_ = false;
    std::map<std::string, Settings> map_;
    std::mutex m_;
};

}
