#pragma once
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace cas {

using json = nlohmann::json;

// Local score calibration: anomaly_score = min(1, flagged-area-percent / 5 * 3).
// With the default threshold 0.25 a frame is rejected when ~0.417% of the ROI area is anomalous.
constexpr double kScoreFullScalePercent = 5.0;
constexpr double kScoreMultiplier = 3.0;

// Label seam straightness check (a thin bright line along the label edge). Points are normalised 0..1 of the
// frame, p0 -> p1 follow the seam (approximate: `band` px of search margin absorb the pose of the product).
struct SeamSettings {
    bool enabled = false;
    double p0x = 0, p0y = 0, p1x = 0, p1y = 0;
    double band = 30;         // px, search half-width around the p0-p1 line
    double tolMid = 1.0;      // px, allowed deviation of the seam from a smooth line (middle part)
    double tolEdge = 5.0;     // px, allowed deviation near both ends of the seam
    double edgeFrac = 0.12;   // share of the seam length treated as an "end" on each side
    double minContrast = 8;   // minimum gray-level contrast for a measured seam row

    bool hasLine() const { return std::abs(p1x - p0x) + std::abs(p1y - p0y) > 1e-6; }
    json toJson() const;
    // Applies the fields present in `j` (partial update); returns an error text or an empty string.
    std::string applyJson(const json& j);
};

// The real, user-facing inspection settings (per analysis profile = camera).
struct Settings {
    double sensitivity = 0.5;     // 0..1, higher = stricter (lower pixel-difference threshold)
    double threshold = 0.25;      // (0..1], reject when anomaly_score >= threshold
    double shiftToleranceX = 3;   // original-frame px; retain fractions from physical calibration
    double shiftToleranceY = 3;   // original-frame px; round only at the final analysis resolution
    std::optional<double> alignmentLimitX, alignmentLimitY; // calibrated hard limits in original-frame px
    int minBlobArea = 20;         // px of the original frame, smaller anomalies are noise
    double inspectScale = 1.0;    // (0..1], frame is downscaled by this factor before analysis
    SeamSettings seam;            // seam straightness check

    json toJson() const;
    // Applies the fields present in `j`; returns an error text or an empty string.
    std::string applyJson(const json& j);
};

// Per-request overrides (never persisted).
struct SettingsPatch {
    std::optional<double> sensitivity, threshold, inspectScale;
    std::optional<double> shiftToleranceX, shiftToleranceY;
    std::optional<int> minBlobArea;
    json seam;                                       // partial seam settings (null = none)
    static SettingsPatch fromJson(const json& j);   // ignores missing, wrong-typed or out-of-range fields
    void applyTo(Settings& s) const;
};

// Persistent profile -> Settings map (JSON file). Re-reads the file when it changed on disk,
// so several processes sharing one data dir stay consistent.
class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path file);
    Settings get(const std::string& profile);
    bool has(const std::string& profile);
    std::string set(const std::string& profile, const json& body, Settings& out);   // error text or ""
    void reset(const std::string& profile);

private:
    void reloadIfStale();
    void save();
    std::filesystem::path file_;
    std::filesystem::file_time_type stamp_{};
    bool haveStamp_ = false;
    std::map<std::string, Settings> map_;
    std::mutex m_;
};

}
