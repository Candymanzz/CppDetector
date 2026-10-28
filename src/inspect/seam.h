#pragma once
#include <opencv2/core.hpp>
#include <vector>

namespace cd {

// UI/profile container. The active Inspector uses the native two-endpoint bright-seam check:
// enabled, pts.front/back, bandHalf, tolMid/tolEdge, edgeFrac and minContrast.
// Other fields below belong to the historical polyline/step detector (seam.cpp), which is not linked.
// The seam is measured for smoothness in the current frame, independently of the surface reference.
struct SeamParams {
    enum Mode { Ridge = 0, Step = 1 };
    bool enabled = false;
    int mode = Ridge;
    std::vector<cv::Point2d> pts;    // control points in original px, >= 2 (first = start, last = end)
    int polarity = 0;                // Step only: 0 = any, +1 = brighter on the +normal side, -1 = darker
    int fitDeg = 0;                  // Ridge: degree of the smooth reference curve, 0 = auto (2)
    double smoothWin = 60;           // Step: px of the line over which its local reference is fitted (> defect width x2)
    double bandHalf = 30;            // px, search half-width around the line (pose variation)
    double tolMid = 1.0;             // px, allowed deviation from the smooth line in the middle part
    double tolEdge = 5.0;            // px, allowed deviation near both ends of the line
    double edgeFrac = 0.12;          // fraction of the line length treated as "end" on each side
    double endIgnore = 0.03;         // fraction at both ends that is not evaluated (line end artefacts)
    double minContrast = 8;          // gray levels: weaker line/step than this = "not found" for that row
    std::vector<int> skip;           // segments (between pts[i] and pts[i+1]) excluded from the check, e.g. a printed
                                     // stripe that crosses the label edge on every bucket
    double skipMargin = 15;          // px added at both ends of a skipped segment (bucket pose varies)
    double maxGap = 1.0;             // fraction of the line that may be continuously "not found" (1 = off)
};

struct SeamResult {
    bool enabled = false;
    bool found = false;              // line located in >= 50% of the evaluated rows
    bool reject = false;
    bool gapReject = false;          // rejected because of a too long stretch with no line found
    double coverage = 0;             // share of evaluated rows where the line was located
    double noise = 0;                // px, 95th percentile of |deviation| over the evaluated rows (how rough the edge is)
    double longestGap = 0;           // longest run without a line, fraction of the evaluated length
    double ratio = 0;                // max |deviation| / tolerance at that position (> 1 = reject)
    double maxDev = 0;               // px, deviation at the worst position
    double worstT = 0;               // 0..1 position along the line of the worst deviation
    std::vector<cv::Point2f> path;       // search axis (spline), original px, for overlay
    std::vector<cv::Point2f> pathNormal; // unit normal at each path point
    std::vector<cv::Point2f> centerPts;  // located line centre (original px), for overlay
    std::vector<cv::Point2f> badPts;     // edge points over tolerance (original px), for overlay
    // per axis sample (1 px steps), for the debug log
    std::vector<cv::Point2f> rowPos;     // axis position
    std::vector<float> rowDev;           // signed deviation of the worst edge, NaN = not evaluated
    std::vector<char> rowState;          // 0 evaluated, 1 skipped segment, 2 line end ignored, 3 no edge found
    std::vector<float> rowTol;           // tolerance that applies at the sample
    std::vector<cv::Point2f> rowEdge;    // measured edge position (valid where rowState == 0)
    std::vector<cv::Point2f> rowRef;     // the smooth reference line at the same sample
    double ms = 0;
};

SeamResult measureSeam(const cv::Mat& bgr, const SeamParams& p);

} // namespace cd
