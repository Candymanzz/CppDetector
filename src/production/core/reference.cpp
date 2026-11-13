#include "reference.h"
#include "exclusion_zones.h"
#include "preprocessing.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

namespace cas {

namespace {

constexpr int kShiftEstimateMaxSide = 256;   // shift is estimated on a thumbnail of the ROI box
constexpr int kShiftRefineMaxSide = 512;

cv::Size thumbnailSize(cv::Size source, int maxSide) {
    const double factor = std::min(1.0, static_cast<double>(maxSide) / std::max(source.width, source.height));
    return {std::max(16, cvRound(source.width * factor)), std::max(16, cvRound(source.height * factor))};
}

std::shared_ptr<const AlignmentPrepared> buildAlignment(const Prepared& native) {
    auto a = std::make_shared<AlignmentPrepared>();
    a->region = native.region;
    a->exclusions = native.exclusions;
    const auto gray = native.refGray(a->region);
    a->smallSize = thumbnailSize(gray.size(), kShiftEstimateMaxSide);
    cv::Mat small;
    cv::resize(gray, small, a->smallSize, 0, 0, cv::INTER_AREA);
    small.convertTo(a->refSmall, CV_32F);
    cv::createHanningWindow(a->hann, a->smallSize, CV_32F);
    if (!native.exclusions.empty()) {
        const auto excluded = exclusionMask(native.exclusions, native.workSize);
        cv::Mat smallExcluded;
        cv::resize(excluded(a->region), smallExcluded, a->smallSize, 0, 0, cv::INTER_AREA);
        a->hann.setTo(0, smallExcluded > 0);
    }
    if (native.labelEnabled) {
        cv::Mat interior, smallMask;
        cv::erode(native.roiMask(a->region),interior,cv::getStructuringElement(cv::MORPH_RECT,{9,9}));
        cv::resize(interior,smallMask,a->smallSize,0,0,cv::INTER_AREA);
        a->hann.setTo(0,smallMask<255);
    }
    const auto dftSize = shiftDftSize(a->smallSize);
    padShiftInput(a->refSmall, dftSize);
    padShiftInput(a->hann, dftSize);
    a->coarseToNative = {double(gray.cols) / a->smallSize.width, double(gray.rows) / a->smallSize.height};

    const auto fineSize = thumbnailSize(gray.size(), kShiftRefineMaxSide);
    cv::resize(gray, a->fineGray, fineSize, 0, 0, cv::INTER_AREA);
    cv::resize(native.roiMask(a->region), a->fineMask, fineSize, 0, 0, cv::INTER_NEAREST);
    cv::erode(a->fineMask, a->fineMask, cv::getStructuringElement(cv::MORPH_RECT, {9, 9}));
    a->fineMask.rowRange(0, 6).setTo(0);
    a->fineMask.rowRange(fineSize.height - 6, fineSize.height).setTo(0);
    a->fineMask.colRange(0, 6).setTo(0);
    a->fineMask.colRange(fineSize.width - 6, fineSize.width).setTo(0);
    a->fineToNative = {double(gray.cols) / fineSize.width, double(gray.rows) / fineSize.height};
    cv::Scalar mean, deviation;
    if (cv::countNonZero(a->fineMask) >= 64) {
        cv::meanStdDev(a->fineGray, mean, deviation, a->fineMask);
        a->canRefine = deviation[0] > 1.0;
    }
    return a;
}

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
    alignment_.reset();
    return true;
}

void Reference::setLabelOutline(NormPolygon pts) {
    std::lock_guard<std::mutex> lk(m_);
    if (label_ == pts) return;
    label_ = std::move(pts); cache_.clear(); alignment_.reset();
}
NormPolygon Reference::labelOutline() const { std::lock_guard<std::mutex> lk(m_); return label_; }

NormPolygon Reference::roi() const {
    std::lock_guard<std::mutex> lk(m_);
    return roi_;
}

std::shared_ptr<const Prepared> Reference::prepared(double scale, const NormPolygons& exclusions) const {
    const int key = static_cast<int>(std::lround(scale * 1000.0));
    NormPolygon roi, label;
    std::shared_ptr<const AlignmentPrepared> alignment;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second->exclusions == exclusions) return it->second;
        roi = roi_; label = label_;
        if (alignment_ && alignment_->exclusions == exclusions) alignment = alignment_;
    }
    // Build the positioning data once from native pixels, never from the inspection thumbnail.
    // Keep the same ROI snapshot for positioning and comparison, even if it changes during a build.
    std::shared_ptr<Prepared> native;
    if (!alignment) {
        native = build(1.0, roi, exclusions, label);
        alignment = buildAlignment(*native);
    }
    auto built = key == 1000 && native ? native : build(key / 1000.0, roi, exclusions, label);
    built->alignment = alignment;
    std::lock_guard<std::mutex> lk(m_);
    // the ROI may have changed while building: only cache when it is still the same
    if (roi == roi_ && label == label_) {
        if (!alignment_ || alignment_->exclusions != exclusions) alignment_ = alignment;
        built->alignment = alignment_;
        cache_[key] = built;
    }
    return built;
}

std::shared_ptr<Prepared> Reference::build(double scale, const NormPolygon& roi, const NormPolygons& exclusions, const NormPolygon& label) const {
    auto p = std::make_shared<Prepared>();
    p->scale = scale;
    p->exclusions = exclusions;
    p->workSize = inspectionWorkSize(bgr_.size(), scale);
    p->refGray = prepareInspectionGray(bgr_, p->workSize);

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
    p->labelEnabled = label.size() >= 3;
    if (p->labelEnabled) {
        std::vector<cv::Point> points;
        for (const auto& q : label) points.emplace_back(cvRound(q.x*(p->workSize.width-1)),cvRound(q.y*(p->workSize.height-1)));
        cv::Mat mask = cv::Mat::zeros(p->workSize, CV_8U);
        cv::fillPoly(mask,std::vector<std::vector<cv::Point>>{points},255);
        p->roiMask &= mask;
    }
    p->region = box;
    p->upperCornerMask = roi.size() >= 3 ? upperCornerMask(p->roiMask(box))
                                       : cv::Mat::zeros(box.size(), CV_8U);
    // Keep the original upper-edge geometry, then subtract holes from both masks.
    if (!exclusions.empty()) {
        const auto excluded = exclusionMask(exclusions, p->workSize);
        p->roiMask.setTo(0, excluded);
        p->upperCornerMask.setTo(0, excluded(box));
    }
    p->roiArea = cv::countNonZero(p->roiMask);
    p->upperCornerArea = cv::countNonZero(p->upperCornerMask);

    return p;
}

}
