#pragma once
#include "settings.h"
#include <opencv2/core.hpp>
#include <vector>

namespace cas {

// Label seam = a thin bright line running along the label edge. We do not compare it with the
// reference (the bucket pose changes every shot); we measure how straight it is (CppDetector KB D18).
struct SeamParams {
    bool enabled = false;
    cv::Point2d p0{0, 0}, p1{0, 0};  // approximate line ends in original px (p0 = start, p1 = end)
    double bandHalf = 30;            // px, search half-width around the p0-p1 line (pose variation)
    double tolMid = 1.0;             // px, allowed deviation from the smooth line in the middle part
    double tolEdge = 5.0;            // px, allowed deviation near both ends of the seam
    double edgeFrac = 0.12;          // fraction of the seam length treated as "end" on each side
    double endIgnore = 0.03;         // fraction at both ends that is not evaluated (line end artefacts)
    double minContrast = 8;          // gray levels: weaker line than this = "not found" for that row
};

struct SeamResult {
    bool enabled = false;
    bool found = false;              // line located in >= 50% of the evaluated rows
    bool reject = false;
    double coverage = 0;             // share of evaluated rows where the line was located
    double ratio = 0;                // max |deviation| / tolerance at that position (> 1 = reject)
    double maxDev = 0;               // px, deviation at the worst position
    double worstT = 0;               // 0..1 position along the seam of the worst deviation
    std::vector<cv::Point2f> centerPts;  // located line centre (original px), for overlay
    std::vector<cv::Point2f> badPts;     // edge points over tolerance (original px), for overlay
    double ms = 0;
};

SeamResult measureSeam(const cv::Mat& bgr, const SeamParams& p);

// Profile settings (normalised points) -> pixel parameters of the given frame.
SeamResult measureSeam(const cv::Mat& bgr, const SeamSettings& s);

} // namespace cas
