#pragma once
#include "../production/core/inspector.h"
#include <nlohmann/json.hpp>

namespace cd {
struct LearningOptions {
    bool robust = true;
    bool shadows = true;
    int searchRadius = 24; // original pixels; independent of camera alignment
};

// Local experiment hook. Native service keeps its original matcher when no hook is supplied.
void compareLearned(const cas::Reference& reference, const cv::Mat& current,
                    cv::Mat& diff, cv::Rect region, double scale, const cas::Settings& settings,
                    cas::InspectOutcome& outcome, const LearningOptions& options,
                    nlohmann::json& diagnostics);
}
