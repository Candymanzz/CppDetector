#pragma once
#include <opencv2/core.hpp>
#include <vector>

namespace cd {
// Proposes an ROI polygon (original image coordinates) for the main object, grown from the image centre (GrabCut).
// Cheap one-shot setup helper (not in the hot path); the human is expected to refine it.
std::vector<cv::Point> autoRoi(const cv::Mat& bgr, int maxVertices = 14);
}
