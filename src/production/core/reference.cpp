#include "reference.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace cas {

namespace {

constexpr int kShiftEstimateMaxSide = 256;   // shift is estimated on a thumbnail of the ROI box

cv::Mat upperCornerMask(const cv::Mat& roi) {
    std::vector<int> top(roi.cols, roi.rows), bottom(roi.cols, -1);
    const cv::Rect bounds = cv::boundingRect(roi);
    for (int y = 0; y < roi.rows; ++y) {
        const auto* row = roi.ptr<uchar>(y);
        for (int x = 0; x < roi.cols; ++x) if (row[x]) {
            top[x] = std::min(top[x], y); bottom[x] = y;
        }
    }
    cv::Mat mask = cv::Mat::zeros(roi.size(), CV_8U);
    for (int y = 0; y < roi.rows; ++y) {
        const auto* row = roi.ptr<uchar>(y);
        auto* selected = mask.ptr<uchar>(y);
        for (int x = bounds.x; x < bounds.x + bounds.width; ++x) if (row[x]) {
            const bool side = x - bounds.x < bounds.width * kUpperCornerWidthFraction
                || bounds.x + bounds.width - 1 - x < bounds.width * kUpperCornerWidthFraction;
            // Follow the actual upper ROI edge at each column, including tilted labels.
            if (side && (y - top[x]) * 7 < bottom[x] - top[x] + 1)
                selected[x] = 255;
        }
    }
    return mask;
}

cv::Mat toBlurredGray(const cv::Mat& bgr, double scale) {
    cv::Mat g;
    if (bgr.channels() == 1) g = bgr;
    else cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
    if (scale != 1.0) cv::resize(g, g, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::GaussianBlur(g, g, cv::Size(5, 5), 0);
    return g;
}

}

Reference::Reference(cv::Mat bgr) : bgr_(std::move(bgr)) {}

std::string Reference::fingerprint() const {
    uint64_t hash = 14695981039346656037ull;
    for (int y = 0; y < bgr_.rows; ++y) for (size_t x = 0; x < bgr_.cols * bgr_.elemSize(); ++x) {
        hash ^= bgr_.ptr<uchar>(y)[x]; hash *= 1099511628211ull;
    }
    return std::to_string(bgr_.cols) + "x" + std::to_string(bgr_.rows) + "-" + std::to_string(hash);
}
void Reference::setLearned(std::vector<LearnedRegion> regions) {
    std::lock_guard<std::mutex> lk(m_); learned_ = std::move(regions);
}
std::vector<LearnedRegion> Reference::learned() const {
    std::lock_guard<std::mutex> lk(m_); return learned_;
}

bool Reference::setRoi(NormPolygon pts) {
    std::lock_guard<std::mutex> lk(m_);
    if (pts.size() == roi_.size()) {
        bool same = true;
        for (size_t i = 0; i < pts.size() && same; ++i) same = pts[i] == roi_[i];
        if (same) return false;
    }
    roi_ = std::move(pts);
    cache_.clear();
    return true;
}

NormPolygon Reference::roi() const {
    std::lock_guard<std::mutex> lk(m_);
    return roi_;
}

void Reference::setExclusions(std::vector<NormPolygon> polygons) {
    std::lock_guard<std::mutex> lk(m_);
    if (exclusions_ == polygons) return;
    exclusions_ = std::move(polygons); cache_.clear();
}

std::shared_ptr<const Prepared> Reference::prepared(double scale) const {
    const int key = static_cast<int>(std::lround(scale * 1000.0));
    NormPolygon roi;
    std::vector<NormPolygon> exclusions;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        roi = roi_; exclusions = exclusions_;
    }
    auto built = build(key / 1000.0, roi, exclusions);
    std::lock_guard<std::mutex> lk(m_);
    // the ROI may have changed while building: only cache when it is still the same
    if (roi == roi_ && exclusions == exclusions_) cache_[key] = built;
    return built;
}

std::shared_ptr<const Prepared> Reference::build(double scale, const NormPolygon& roi, const std::vector<NormPolygon>& exclusions) const {
    auto p = std::make_shared<Prepared>();
    p->scale = scale;
    p->refGray = toBlurredGray(bgr_, scale);
    p->workSize = p->refGray.size();

    p->roiMask = cv::Mat::zeros(p->workSize, CV_8U);
    cv::Rect box(0, 0, p->workSize.width, p->workSize.height);
    if (roi.size() >= 3) {
        std::vector<cv::Point> pts;
        pts.reserve(roi.size());
        for (auto& q : roi)
            pts.emplace_back(cvRound(q.x * (p->workSize.width - 1)), cvRound(q.y * (p->workSize.height - 1)));
        cv::fillPoly(p->roiMask, std::vector<std::vector<cv::Point>>{pts}, 255);
        cv::Rect bb = cv::boundingRect(pts);
        int pad = std::max(16, static_cast<int>(0.04 * std::max(bb.width, bb.height)));
        cv::Rect padded(bb.x - pad, bb.y - pad, bb.width + 2 * pad, bb.height + 2 * pad);
        padded &= cv::Rect(0, 0, p->workSize.width, p->workSize.height);
        if (padded.width >= 32 && padded.height >= 32) box = padded;
    } else {
        p->roiMask.setTo(255);
    }
    // Compute corners before subtracting holes: an exclusion must not move the upper edge.
    p->region = box;
    p->upperCornerMask = roi.size() >= 3 ? upperCornerMask(p->roiMask(box))
                                       : cv::Mat::zeros(box.size(), CV_8U);
    cv::Mat excluded = cv::Mat::zeros(p->workSize, CV_8U);
    for (const auto& polygon : exclusions) {
        std::vector<cv::Point> pts;
        for (const auto& q : polygon) pts.emplace_back(cvRound(q.x * (p->workSize.width - 1)), cvRound(q.y * (p->workSize.height - 1)));
        if (pts.size() >= 3) cv::fillPoly(excluded, std::vector<std::vector<cv::Point>>{pts}, 255);
    }
    p->roiMask.setTo(0, excluded);
    p->upperCornerMask.setTo(0, excluded(box));
    p->roiArea = cv::countNonZero(p->roiMask);
    p->upperCornerArea = cv::countNonZero(p->upperCornerMask);

    const double f = std::min(1.0, static_cast<double>(kShiftEstimateMaxSide) / std::max(box.width, box.height));
    p->smallSize = cv::Size(std::max(16, cvRound(box.width * f)), std::max(16, cvRound(box.height * f)));
    cv::Mat small;
    cv::resize(p->refGray(box), small, p->smallSize, 0, 0, cv::INTER_AREA);
    small.convertTo(p->refSmall, CV_32F);
    cv::createHanningWindow(p->hann, p->smallSize, CV_32F);
    if (!exclusions.empty()) {
        cv::Mat smallExcluded;
        cv::resize(excluded(box), smallExcluded, p->smallSize, 0, 0, cv::INTER_AREA);
        p->hann.setTo(0, smallExcluded > 0);
    }
    return p;
}

}
