#pragma once
#include "reference.h"
#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <stdexcept>

namespace cas {

inline nlohmann::json exclusionsJson(const NormPolygons& polygons) {
    auto result = nlohmann::json::array();
    for (const auto& polygon : polygons) {
        auto points = nlohmann::json::array();
        for (const auto& p : polygon) points.push_back({p.x, p.y});
        result.push_back(std::move(points));
    }
    return result;
}

// Strict validation for both durable settings and temporary test overrides.
inline NormPolygons parseExclusions(const nlohmann::json& value) {
    if (!value.is_array() || value.size() > 32)
        throw std::invalid_argument("exclusions must be an array of up to 32 polygons");
    NormPolygons polygons;
    for (const auto& polygon : value) {
        if (!polygon.is_array() || polygon.size() < 3 || polygon.size() > 128)
            throw std::invalid_argument("exclusion polygon must contain 3..128 points");
        NormPolygon points;
        for (const auto& p : polygon) {
            if (!p.is_array() || p.size() != 2 || !p[0].is_number() || !p[1].is_number())
                throw std::invalid_argument("exclusion point must be [x, y]");
            const double x = p[0].get<double>(), y = p[1].get<double>();
            if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1)
                throw std::invalid_argument("exclusion coordinates must be finite numbers in 0..1");
            points.emplace_back(x, y);
        }
        double area = 0;
        for (size_t i = 0; i < points.size(); ++i) {
            const auto& a = points[i]; const auto& b = points[(i + 1) % points.size()];
            area += a.x * b.y - a.y * b.x;
        }
        if (std::abs(area) < 1e-12) throw std::invalid_argument("exclusion polygon has no area");
        polygons.push_back(std::move(points));
    }
    return polygons;
}

inline cv::Mat exclusionMask(const NormPolygons& polygons, cv::Size size, cv::Point2d offset = {}) {
    cv::Mat mask = cv::Mat::zeros(size, CV_8U);
    for (const auto& polygon : polygons) {
        std::vector<cv::Point> points;
        for (const auto& p : polygon)
            points.emplace_back(cvRound(p.x * (size.width - 1) + offset.x), cvRound(p.y * (size.height - 1) + offset.y));
        if (points.size() >= 3) cv::fillPoly(mask, std::vector<std::vector<cv::Point>>{points}, 255);
    }
    return mask;
}
}
