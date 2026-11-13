#pragma once
#include "inspector.h"
#include <nlohmann/json.hpp>

namespace cas {
struct LearningOptions {
    bool robust = true;
    bool shadows = true;
    int searchRadius = 24; // original pixels; independent of camera alignment
};

// Use the inspection's immutable preparation, including its exclusions and ROI.
// Do not look it up again: a default mask variant can replace the shared cache.
void compareLearned(const cas::Reference& reference, const cas::Prepared& prepared, const cv::Mat& current,
                    cv::Mat& diff, cv::Rect region, double scale, const cas::Settings& settings,
                    cas::InspectOutcome& outcome, const LearningOptions& options,
                    nlohmann::json& diagnostics);
}
