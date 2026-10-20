#include "auto_roi.h"
#include <opencv2/imgproc.hpp>

namespace cd {

std::vector<cv::Point> autoRoi(const cv::Mat& bgr, int maxVertices) {
    const double scale = 320.0 / std::max(bgr.cols, 1);
    cv::Mat g;
    cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
    cv::resize(g, g, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::GaussianBlur(g, g, cv::Size(7, 7), 0);

    cv::Mat bin;
    cv::threshold(g, bin, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    // cut thin bridges to neighbouring bright objects, then fill holes
    cv::morphologyEx(bin, bin, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9)));
    cv::morphologyEx(bin, bin, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(15, 15)));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return {};

    // prefer large blobs that are close to the image centre
    const cv::Point2d centre(g.cols / 2.0, g.rows / 2.0);
    int best = -1;
    double bestScore = 0;
    for (size_t i = 0; i < contours.size(); ++i) {
        double area = cv::contourArea(contours[i]);
        cv::Moments m = cv::moments(contours[i]);
        if (m.m00 <= 0) continue;
        cv::Point2d c(m.m10 / m.m00, m.m01 / m.m00);
        double d = cv::norm(c - centre) / cv::norm(centre);
        double s = area * (1.0 - 0.5 * d);
        if (s > bestScore) { bestScore = s; best = (int)i; }
    }
    if (best < 0) return {};

    std::vector<cv::Point> hull, poly;
    cv::convexHull(contours[best], hull);
    double eps = 0.005 * cv::arcLength(hull, true);
    do {
        cv::approxPolyDP(hull, poly, eps, true);
        eps *= 1.3;
    } while ((int)poly.size() > maxVertices);

    for (auto& p : poly) { p.x = cvRound(p.x / scale); p.y = cvRound(p.y / scale); }
    return poly;
}

} // namespace cd
