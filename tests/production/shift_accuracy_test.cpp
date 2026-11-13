#include "core/inspector.h"
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
cv::Mat fixture() {
    cv::Mat image(1024, 1224, CV_8UC3);
    cv::RNG random(20261105);
    random.fill(image, cv::RNG::UNIFORM, 30, 225);
    cv::putText(image, "TRANSLATION ONLY", {140, 480}, cv::FONT_HERSHEY_SIMPLEX, 2, {240,240,240}, 5);
    cv::rectangle(image, {700,200,200,200}, {20,20,20}, 6);
    return image;
}
cv::Mat translated(const cv::Mat& image, cv::Point2d shift) {
    const cv::Mat warp = (cv::Mat_<double>(2,3) << 1,0,shift.x,0,1,shift.y);
    cv::Mat result;
    cv::warpAffine(image, result, warp, image.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return result;
}
void accuracyAndScale() {
    const auto image = fixture();
    cas::Reference reference(image.clone());
    reference.setRoi({{.08,.12},{.91,.10},{.90,.86},{.12,.88}});
    const auto cache = reference.prepared(1)->alignment;
    const auto fine = cache->fineGray.clone(), mask = cache->fineMask.clone();
    for (const auto wanted : {cv::Point2d(3.125,-2.75), cv::Point2d(-12.5,6.25),
                              cv::Point2d(.25,0), cv::Point2d(0,-.25)}) {
        const auto moved = translated(image, wanted);
        cv::Point2d baseline;
        for (double scale : {1., .73, .5, .21, .2, .1}) {
            cas::Settings s;
            s.inspectScale = scale;
            s.maxAlignmentShiftX = s.maxAlignmentShiftY = 32;
            const auto out = cas::inspectFrame(reference, moved, s, false);
            require(out.shiftRefined, "textured translated frame must use ECC refinement");
            require(cv::norm(out.rawShift - wanted) < .15, "subpixel translation error must be below 0.15 native px");
            if (scale == 1) baseline = out.rawShift;
            require(cv::norm(out.rawShift - baseline) < 1e-8, "measurement must be independent of inspect_scale");
            require(reference.prepared(scale)->alignment == cache, "scales must share the native positioning cache");
        }
    }
    require(cv::norm(fine, cache->fineGray, cv::NORM_INF) == 0
        && cv::norm(mask, cache->fineMask, cv::NORM_INF) == 0, "ECC must preserve the shared template and mask");
    reference.setRoi({{.2,.2},{.8,.2},{.8,.8},{.2,.8}});
    const auto changed = reference.prepared(.2)->alignment;
    require(changed != cache && changed->region != cache->region, "ROI change must rebuild native positioning data");
    require(cv::norm(fine, cache->fineGray, cv::NORM_INF) == 0, "old ROI snapshots must remain immutable");
}
void zeroAndLimits() {
    const auto image = fixture();
    cas::Reference reference(image.clone());
    for (double scale : {.1,.2,.21,.5,.73,1.}) {
        auto s = cas::Settings{}.withMetricDefaults(0);
        s.inspectScale = scale;
        s.horizontalShiftMm = s.verticalShiftMm = 0;
        const auto same = cas::inspectFrame(reference, image, s, true);
        require(!same.reject && same.rawShift == cv::Point2d(0,0) && same.anomalyScore == 0,
            "zero limit must not reject numerical residue of identical images");
        const auto moved = translated(image, {2,0});
        require(cas::inspectFrame(reference, moved, s, false).reason == "shift_exceeded",
            "zero limit must still reject a true translation");
        s.horizontalShiftMm = s.verticalShiftMm = .5;
        for (int relaxation : {0,3,15}) {
            s.shiftToleranceX = s.shiftToleranceY = relaxation;
            require(cas::inspectFrame(reference, translated(image, {1,0}), s, false).reason != "shift_exceeded",
                "translation below the metric limit must be allowed");
            require(cas::inspectFrame(reference, translated(image, {3,0}), s, false).reason == "shift_exceeded",
                "local relaxation must not override the metric displacement limit");
        }
    }
}
void fallback() {
    cas::Settings s;
    const cv::Mat flat(320,384,CV_8UC3,cv::Scalar::all(128));
    cas::Reference blank(flat.clone());
    const auto blankOut = cas::inspectFrame(blank, flat, s, false);
    require(!blankOut.shiftRefined && !blankOut.reject && blankOut.anomalyScore == 0,
        "flat surfaces must skip ECC and remain stable");
    const auto image = fixture();
    cv::Mat unrelated(image.size(), image.type());
    cv::RNG random(753);
    random.fill(unrelated, cv::RNG::UNIFORM, 0, 255);
    cas::Reference reference(image.clone());
    const auto poor = cas::inspectFrame(reference, unrelated, s, false);
    require(!poor.shiftRefined && std::isfinite(poor.rawShift.x) && std::isfinite(poor.rawShift.y),
        "poorly matching frames must keep a finite coarse fallback");
    require(poor.shiftResponse == poor.coarseShiftResponse, "fallback must keep the coarse confidence");
}
}
int main() {
    try {
        cv::setNumThreads(1);
        accuracyAndScale(); zeroAndLimits(); fallback();
        std::cout << "native phase256/ECC512 positioning passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
