#include "core/inspector.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
void require(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

cv::Mat label(cv::Size size) {
    cv::Mat image(size, CV_8UC3, cv::Scalar(40, 90, 140));
    cv::RNG random(20261106);
    for (int i = 0; i < 120; ++i) {
        const int x = random.uniform(50, size.width - 70);
        const int y = random.uniform(50, size.height - 70);
        cv::rectangle(image, {x, y, random.uniform(8, 20), random.uniform(5, 15)},
            cv::Scalar::all(random.uniform(10, 240)), cv::FILLED);
    }
    cv::putText(image, "VERTICAL LABEL", {80, size.height / 2},
        cv::FONT_HERSHEY_SIMPLEX, 1, {230, 230, 230}, 2);
    // Uniform margins make the original boundary recoverable after translation.
    // The current frame exposes a contrasting white surface at its new edge.
    return image;
}

cv::Mat translated(const cv::Mat& image, double dy) {
    cv::Mat current;
    const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1, 0, 0, 0, 1, dy);
    cv::warpAffine(image, current, transform, image.size(), cv::INTER_LINEAR,
        cv::BORDER_CONSTANT, cv::Scalar::all(230));
    return current;
}

void checkExposedStrip() {
    for (const auto size : {cv::Size(640, 480), cv::Size(617, 459)}) {
        const auto referenceImage = label(size);
        for (bool bottomRoi : {false, true}) {
            cas::Reference reference(referenceImage.clone());
            if (bottomRoi) reference.setRoi({{.1,.1},{.9,.1},{.9,1.},{.1,1.}});
            for (double scale : {.2, .5, .73, 1.}) for (double dy : {-2., 2.}) {
                cas::Settings settings;
                settings.inspectScale = scale;
                settings.maxAlignmentShiftX = settings.maxAlignmentShiftY = 6;
                const auto current = translated(referenceImage, dy);
                const auto before = current.clone();
                const auto out = cas::inspectFrame(reference, current, settings, true);
                const std::string context = "size=" + std::to_string(size.width)
                    + " scale=" + std::to_string(scale) + " dy=" + std::to_string(dy)
                    + " roi=" + std::to_string(bottomRoi) + " score=" + std::to_string(out.anomalyScore);
                require(std::abs(out.rawShift.y - dy) < .15 && std::abs(out.rawShift.x) < .15,
                    "translation estimate changed: " + context);
                require(!out.reject && out.anomalyScore < .05,
                    "exposed white strip must not survive valid compensation: " + context);
                require(cv::norm(current, before, cv::NORM_INF) == 0,
                    "inspection modified caller's source image: " + context);

                auto defect = referenceImage.clone();
                cv::rectangle(defect, {size.width / 2, size.height - 36, 40, 28},
                    cv::Scalar::all(0), cv::FILLED);
                const auto bad = cas::inspectFrame(reference, translated(defect, dy), settings, false);
                require(bad.reject && bad.reason == "score_over_threshold",
                    "a real defect close to the bottom edge must remain detectable: " + context);

                settings.maxAlignmentShiftX = settings.maxAlignmentShiftY = 1;
                const auto exceeded = cas::inspectFrame(reference, current, settings, false);
                require(exceeded.reject && exceeded.reason == "shift_exceeded"
                    && cv::norm(exceeded.shift) == 0,
                    "above-limit translation must still reject without compensation: " + context);
            }
        }
    }
}
}

int main() {
    try {
        cv::setNumThreads(1);
        checkExposedStrip();
        std::cout << "alignment before preprocessing passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
