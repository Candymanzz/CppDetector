#pragma once
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace cas {

inline cv::Size inspectionWorkSize(cv::Size source, double scale) {
    return {std::max(1, cvRound(source.width * scale)),
            std::max(1, cvRound(source.height * scale))};
}

inline cv::Size shiftDftSize(cv::Size thumbnail) {
    // phaseCorrelate's peak centre has a half-pixel offset for odd DFT sizes.
    // An even optimal size keeps identical thumbnails centred at zero shift.
    return {2 * cv::getOptimalDFTSize((thumbnail.width + 1) / 2),
            2 * cv::getOptimalDFTSize((thumbnail.height + 1) / 2)};
}

inline void padShiftInput(cv::Mat& image, cv::Size dftSize) {
    if (image.size() != dftSize)
        cv::copyMakeBorder(image, image, 0, dftSize.height - image.rows,
            0, dftSize.width - image.cols, cv::BORDER_CONSTANT, cv::Scalar::all(0));
}

// Reference and current frame must use the same integer size and interpolation.
// Passing fx/fy in one path and a rounded size in the other changes the pixels.
inline cv::Mat prepareInspectionGray(const cv::Mat& frame, cv::Size workSize) {
    cv::Mat gray;
    if (frame.channels() == 1) gray = frame.clone();
    else cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    if (gray.size() != workSize)
        cv::resize(gray, gray, workSize, 0, 0, cv::INTER_AREA);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0);
    return gray;
}

}
