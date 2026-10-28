#pragma once
#include <opencv2/core.hpp>
#include <cmath>

namespace cd {

// Image coordinates are fractions of width/height, so calibration follows a digital resize.
// This is a local axis calibration at the measured surface, not a camera/lens or 3-D calibration.
struct CalibrationAxis {
    cv::Point2d p0, p1;
    double lengthMm = 0;
    bool valid(bool horizontal) const {
        const double span = horizontal ? std::abs(p1.x-p0.x) : std::abs(p1.y-p0.y);
        return lengthMm > 0 && span > 1e-9;
    }
};

struct MetricCalibration {
    CalibrationAxis horizontal, vertical;
    bool ready() const { return horizontal.valid(true) && vertical.valid(false); }
    cv::Point2d mmPerPixel(cv::Size size) const {
        return {horizontal.lengthMm / (std::abs(horizontal.p1.x-horizontal.p0.x)*size.width),
                vertical.lengthMm / (std::abs(vertical.p1.y-vertical.p0.y)*size.height)};
    }
};

}
