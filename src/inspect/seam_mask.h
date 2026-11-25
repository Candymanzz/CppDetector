#pragma once
#include "seam.h"
namespace cd {
SeamResult measureMaskedSeam(const cv::Mat& bgr, const SeamParams& params, const cv::Mat& excluded = {});
// Ridge mask: independently follow both reference-supported sides. Step keeps the single-edge path.
SeamResult inspectMaskedSeam(const cv::Mat& current, const cv::Mat& reference,
                             const SeamParams& params, cv::Point2d pose = {}, const cv::Mat& excluded = {});
}
