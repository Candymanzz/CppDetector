#include "reference.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace cas {

namespace {

constexpr int kShiftEstimateMaxSide = 256;   // shift is estimated on a thumbnail of the ROI box

int evenOptimalDftSize(int size) {
    int optimal = cv::getOptimalDFTSize(size);
    while (optimal % 2 != 0) optimal = cv::getOptimalDFTSize(optimal + 1);
    return optimal;
}

cv::Mat toBlurredGray(const cv::Mat& bgr, double scale) {
    cv::Mat g;
    if (bgr.channels() == 1) g = bgr.clone();
    else cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
    // Use the same rounded destination size for reference and current frame. Passing fx/fy
    // for one and dsize for the other samples different coordinates at fractional scales.
    if (scale != 1.0) cv::resize(g, g, cv::Size(std::max(1, cvRound(g.cols * scale)),
                                              std::max(1, cvRound(g.rows * scale))), 0, 0, cv::INTER_AREA);
    cv::GaussianBlur(g, g, cv::Size(5, 5), 0);
    return g;
}

}

Reference::Reference(cv::Mat bgr) : bgr_(std::move(bgr)) {}

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

std::shared_ptr<const Prepared> Reference::prepared(double scale) const {
    const int key = static_cast<int>(std::lround(scale * 1000.0));
    NormPolygon roi;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = cache_.find(key);
        if (it != cache_.end()) return it->second;
        roi = roi_;
    }
    auto built = build(key / 1000.0, roi);
    std::lock_guard<std::mutex> lk(m_);
    // the ROI may have changed while building: only cache when it is still the same
    if (roi == roi_) cache_[key] = built;
    return built;
}

std::shared_ptr<const Prepared> Reference::build(double scale, const NormPolygon& roi) const {
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
    p->roiArea = cv::countNonZero(p->roiMask);
    p->region = box;

    const double f = std::min(1.0, static_cast<double>(kShiftEstimateMaxSide) / std::max(box.width, box.height));
    p->smallSize = cv::Size(std::max(16, cvRound(box.width * f)), std::max(16, cvRound(box.height * f)));
    cv::Mat small;
    cv::resize(p->refGray(box), small, p->smallSize, 0, 0, cv::INTER_AREA);
    small.convertTo(p->refSmall, CV_32F);
    cv::createHanningWindow(p->hann, p->smallSize, CV_32F);

    // phaseCorrelate measures its peak relative to width/2, height/2. An odd DFT
    // dimension introduces a half-pixel zero offset even for identical images.
    // Pad pixels and their existing window, preserving thumbnail geometry/scale.
    const cv::Size phaseSize(evenOptimalDftSize(p->smallSize.width), evenOptimalDftSize(p->smallSize.height));
    cv::copyMakeBorder(p->refSmall, p->refSmall, 0, phaseSize.height - p->smallSize.height,
                      0, phaseSize.width - p->smallSize.width, cv::BORDER_CONSTANT, 0);
    cv::copyMakeBorder(p->hann, p->hann, 0, phaseSize.height - p->smallSize.height,
                      0, phaseSize.width - p->smallSize.width, cv::BORDER_CONSTANT, 0);
    return p;
}

}
