#include "auto_roi.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace cd {

// Grows the ROI from the image centre: the centre is assumed to be on the product, the frame border on the background.
// GrabCut (colour models learned from those seeds) decides the rest, so bright lamps/background no longer win by area.
std::vector<cv::Point> autoRoi(const cv::Mat& bgr, int maxVertices) {
    if (bgr.empty()) return {};
    const double scale = 320.0 / std::max(bgr.cols, 1);
    cv::Mat small;
    cv::resize(bgr, small, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::GaussianBlur(small, small, cv::Size(5, 5), 0);
    const int w = small.cols, h = small.rows;
    const cv::Point centre(w / 2, h / 2);

    // seeds: border strip = background, small centre ellipse = foreground, central area = probably foreground
    cv::Mat mask(h, w, CV_8U, cv::Scalar(cv::GC_PR_BGD));
    const int bx = std::max(2, w / 25), by = std::max(2, h / 25);
    mask(cv::Rect(0, 0, w, by)).setTo(cv::GC_BGD);
    mask(cv::Rect(0, h - by, w, by)).setTo(cv::GC_BGD);
    mask(cv::Rect(0, 0, bx, h)).setTo(cv::GC_BGD);
    mask(cv::Rect(w - bx, 0, bx, h)).setTo(cv::GC_BGD);
    cv::ellipse(mask, centre, cv::Size(w * 7 / 20, h * 7 / 20), 0, 0, 360, cv::Scalar(cv::GC_PR_FGD), cv::FILLED);
    cv::ellipse(mask, centre, cv::Size(w * 3 / 25, h * 3 / 25), 0, 0, 360, cv::Scalar(cv::GC_FGD), cv::FILLED);

    cv::Mat bgModel, fgModel;
    try {
        cv::grabCut(small, mask, cv::Rect(), bgModel, fgModel, 4, cv::GC_INIT_WITH_MASK);
    } catch (const cv::Exception&) {
        return {};
    }
    cv::Mat fg = (mask == cv::GC_FGD) | (mask == cv::GC_PR_FGD);

    // cut thin bridges to neighbouring objects, close small gaps (text, glare), keep the blob that contains the centre
    cv::morphologyEx(fg, fg, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9)));
    cv::morphologyEx(fg, fg, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15, 15)));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fg, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    int best = -1;
    double bestArea = 0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = cv::contourArea(contours[i]);
        bool hasCentre = cv::pointPolygonTest(contours[i], cv::Point2f(centre), false) >= 0;
        if (hasCentre && area > bestArea) { bestArea = area; best = (int)i; }
    }
    if (best < 0) return {};

    // the product can fall apart into several blobs (light label parts vs dark body): merge the sizeable ones that reach
    // the central zone, the hull then covers them all
    std::vector<cv::Point> all = contours[best];
    for (size_t i = 0; i < contours.size(); ++i) {
        if ((int)i == best || cv::contourArea(contours[i]) < 0.05 * bestArea) continue;
        bool central = false;
        for (const auto& p : contours[i]) {
            double dx = (p.x - centre.x) / (w * 0.3), dy = (p.y - centre.y) / (h * 0.3);
            if (dx * dx + dy * dy <= 1.0) { central = true; break; }
        }
        if (central) all.insert(all.end(), contours[i].begin(), contours[i].end());
    }

    std::vector<cv::Point> hull, poly;
    cv::convexHull(all, hull);
    double eps = 0.005 * cv::arcLength(hull, true);
    do {
        cv::approxPolyDP(hull, poly, eps, true);
        eps *= 1.3;
    } while ((int)poly.size() > maxVertices);

    for (auto& p : poly) { p.x = cvRound(p.x / scale); p.y = cvRound(p.y / scale); }
    return poly;
}

} // namespace cd
