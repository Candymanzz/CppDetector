#pragma once
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cd {
// Same bounds as main-app front-end/src/shared/learnedRegion.ts:
// all nonzero raw heatmap pixels, floor/ceil to source pixels, 10 px padding.
inline cv::Rect learnedRegionBounds(const cv::Mat& heatmap, cv::Size frame, int padding = 10) {
    if (frame.empty() || padding < 0 || (!heatmap.empty() && heatmap.type() != CV_8UC1))
        throw std::invalid_argument("Invalid heatmap or frame dimensions");
    if (heatmap.empty()) return {};
    const auto bounds = cv::boundingRect(heatmap);
    if (bounds.empty()) return {};
    const int x = std::max(0, int(std::floor(double(bounds.x) * frame.width / heatmap.cols)) - padding);
    const int y = std::max(0, int(std::floor(double(bounds.y) * frame.height / heatmap.rows)) - padding);
    const int right = std::min(frame.width, int(std::ceil(double(bounds.x + bounds.width) * frame.width / heatmap.cols)) + padding);
    const int bottom = std::min(frame.height, int(std::ceil(double(bounds.y + bounds.height) * frame.height / heatmap.rows)) + padding);
    return {x, y, right-x, bottom-y};
}
}
