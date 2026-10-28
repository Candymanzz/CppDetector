#include "app/app.h"
#include "inspect/inspector.h"
#include "production/core/seam.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

void identicalDownscaled() {
    cv::Mat image(321, 383, CV_8UC3);
    cv::RNG(20261028).fill(image, cv::RNG::UNIFORM, 0, 255);
    cd::Inspector inspector;
    cd::InspectParams p; p.sensitivity = 1; p.shiftToleranceX = p.shiftToleranceY = 0; p.minBlobArea = 1;
    inspector.setReference(image, {}, p);
    for (double scale : {1.0, 0.73, 0.7, 0.45, 0.333, 0.2}) {
        p.downscale = scale;
        const auto r = inspector.inspect(image, p);
        std::cout << "  identical scale=" << scale << " diffMax=" << r.native.diffMax << " score=" << r.score << '\n';
        require(r.native.diffMax == 0 && r.score == 0 && !r.reject,
                "identical images must stay identical after downscaling rounded image dimensions");
    }
}

void identicalRoiAlignment() {
    cv::Mat image(321, 383, CV_8UC3);
    cv::RNG(28).fill(image, cv::RNG::UNIFORM, 20, 235);
    cd::InspectParams p;
    p.horizontalShiftMm = p.verticalShiftMm = 0;
    int cases = 0;
    for (int width : {59, 79, 91, 101, 123, 137, 173, 191, 233, 257}) {
        for (bool vertical : {false, true}) {
            const std::vector<cv::Point> roi = vertical
                ? std::vector<cv::Point>{{30,30},{200,30},{200,30+width},{30,30+width}}
                : std::vector<cv::Point>{{30,30},{30+width,30},{30+width,200},{30,200}};
            cd::Inspector inspector;
            inspector.setReference(image, roi, p);
            for (double scale : {1.0, 0.7, 0.5, 0.2}) {
                p.downscale = scale;
                for (int repeat = 0; repeat < 2; ++repeat) {
                    const auto r = inspector.inspect(image, p);
                    if (r.reject || r.score != 0 || cv::norm(r.native.rawShift) > 1e-5) {
                        std::cerr << "  self ROI width=" << width << " vertical=" << vertical << " scale=" << scale
                                  << " raw=" << r.native.rawShift << " score=" << r.score << " reason=" << r.reason << '\n';
                        throw std::runtime_error("identical ROI images must have zero measured shift and pass even with zero tolerance");
                    }
                    ++cases;
                }
            }
        }
    }
    std::cout << "  identical ROI comparisons=" << cases << '\n';
}

void knownRoiTranslations() {
    cv::Mat texture(80, 80, CV_8UC3), image;
    cv::RNG(20261028).fill(texture, cv::RNG::UNIFORM, 25, 230);
    cv::resize(texture, image, {400,400}, 0, 0, cv::INTER_CUBIC);
    int cases = 0;
    for (bool vertical : {false, true}) {
        cas::Reference reference(image.clone());
        reference.setRoi(vertical ? cas::NormPolygon{{.25,.25},{.675,.25},{.675,.45},{.25,.45}}
                                  : cas::NormPolygon{{.25,.25},{.45,.25},{.45,.675},{.25,.675}});
        cas::Settings s;
        s.alignmentLimitX = s.alignmentLimitY = 100;
        for (double scale : {1.0, 0.7, 0.5, 0.2}) {
            s.inspectScale = scale;
            for (double shift : {-5.0, -1.0, -0.5, 0.5, 1.0, 5.0}) {
                // Express the motion in analysis pixels, then verify the returned original-pixel units.
                const cv::Point2d expected = vertical ? cv::Point2d(0,shift / scale) : cv::Point2d(shift / scale,0);
                const cv::Mat transform = (cv::Mat_<double>(2,3) << 1,0,expected.x,0,1,expected.y);
                cv::Mat moved;
                cv::warpAffine(image, moved, transform, image.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
                const auto r = cas::inspectFrame(reference, moved, s, true);
                const double error = cv::norm(r.rawShift - expected);
                // Phase-centroid interpolation is approximate; allow half an analysis pixel.
                if (r.shiftResponse < 0.3 || error > 0.5 / scale) {
                    std::cerr << "  translation scale=" << scale << " expected=" << expected << " actual=" << r.rawShift
                              << " response=" << r.shiftResponse << " error=" << error << '\n';
                    throw std::runtime_error("alignment padding must retain real signed translations and original-pixel coordinate conversion");
                }
                ++cases;
            }
        }
    }
    std::cout << "  known signed/subpixel translations=" << cases << '\n';
}

void ringDefect() {
    cv::Mat image(320, 384, CV_8UC3, cv::Scalar::all(130));
    cv::Mat frame = image.clone();
    cv::circle(frame, {192, 160}, 55, cv::Scalar::all(20), 8);
    cas::Reference ref(image);
    cas::Settings s; s.sensitivity = 1; s.shiftToleranceX = s.shiftToleranceY = 0; s.minBlobArea = 1;
    auto r = cas::inspectFrame(ref, frame, s, true);
    cv::Mat expected;
    cv::threshold(r.diff, expected, r.pixelThreshold, 255, cv::THRESH_BINARY);
    cv::morphologyEx(expected, expected, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, {3, 3}));
    std::cout << "  ring flagged=" << cv::countNonZero(r.kept) << " actual=" << cv::countNonZero(expected) << '\n';
    require(cv::norm(r.kept, expected, cv::NORM_INF) == 0,
            "a ring-shaped defect must not count its clean interior as anomalous area");
    s.minBlobArea = cv::countNonZero(expected) + 100;
    r = cas::inspectFrame(ref, frame, s, true);
    require(r.blobsKept == 0 && r.anomalyScore == 0,
            "minimum blob area must use detected pixels instead of the area enclosed by the outer contour");
}

cv::Mat seamImage(int contrast = 170) {
    cv::Mat image(320, 384, CV_8UC3, cv::Scalar::all(80));
    cv::line(image, {180, 35}, {180, 285}, cv::Scalar::all(80 + contrast), 4);
    return image;
}
cd::SeamParams seamParams() {
    cd::SeamParams p; p.enabled = true; p.pts = {{180, 40}, {180, 280}}; p.bandHalf = 20;
    return p;
}
void missingSeam() {
    const auto ref = seamImage();
    cd::InspectParams p; p.rejectThreshold = 100;
    auto seam = seamParams();
    cd::Inspector inspector; inspector.setReference(ref, {}, p);
    auto clean = inspector.inspect(ref, p, &seam);
    require(clean.seam.found && !clean.seam.reject, "a straight visible seam must pass");
    const auto r = inspector.inspect(cv::Mat(ref.size(), ref.type(), cv::Scalar::all(80)), p, &seam);
    std::cout << "  missing seam found=" << r.seam.found << " reject=" << r.reject << " reason=" << r.reason << '\n';
    require(r.native.seamEnabled && !r.seam.found && r.seam.reject && r.reject,
            "an enabled seam inspection must not pass when no seam can be measured");
    require(r.reason == "seam_not_found" || r.reason == "score_over_threshold" || r.reason == "shift_exceeded",
            "a failed seam measurement needs an explicit inspection reason");
    seam.enabled = false;
    const auto off = inspector.inspect(ref, p, &seam);
    require(!off.native.seamEnabled && !off.reject, "disabled seam must not affect the verdict");
}
void seamContrast() {
    const auto ref = seamImage(15);
    cd::InspectParams p;
    cd::Inspector inspector; inspector.setReference(ref, {}, p);
    auto seam = seamParams(); seam.minContrast = 5;
    const auto low = inspector.inspect(ref, p, &seam);
    seam.minContrast = 30;
    const auto high = inspector.inspect(ref, p, &seam);
    std::cout << "  contrast found low=" << low.seam.found << " high=" << high.seam.found << '\n';
    require(low.seam.found && !high.seam.found, "the configured seam contrast must reach the measurement algorithm");
}

void seamGeometry() {
    cd::InspectParams p; p.rejectThreshold = 100;
    cd::Inspector inspector; inspector.setReference(seamImage(), {}, p);
    auto seam = seamParams();
    auto curved = cv::Mat(320, 384, CV_8UC3, cv::Scalar::all(80));
    std::vector<cv::Point> points;
    for (int y = 35; y <= 285; ++y) points.emplace_back(180 + cvRound(4 * std::pow((y - 160) / 125.0, 2)), y);
    cv::polylines(curved, points, false, cv::Scalar::all(250), 4);
    const auto smooth = inspector.inspect(curved, p, &seam);
    require(smooth.seam.found && !smooth.seam.reject, "smooth cylindrical curvature must not count as a broken seam");
    auto bent = seamImage();
    cv::rectangle(bent, {174, 133, 25, 40}, cv::Scalar::all(80), cv::FILLED);
    cv::polylines(bent, std::vector<cv::Point>{{180, 132}, {187, 145}, {187, 160}, {180, 173}}, false, cv::Scalar::all(250), 4);
    const auto defect = inspector.inspect(bent, p, &seam);
    std::cout << "  seam bump score=" << defect.score << " ratio=" << defect.seam.ratio << " reason=" << defect.reason << '\n';
    require(defect.seam.found && defect.seam.reject && defect.reject && defect.reason == "seam_deviation",
            "a local seam bend must reject independently of the surface score");
    seam.tolMid = seam.tolEdge = 20;
    require(!inspector.inspect(bent, p, &seam).seam.reject, "raising seam tolerance must affect the seam verdict");
    seam = seamParams(); seam.pts = {{180, 40}};
    require(inspector.inspect(seamImage(), p, &seam).reason == "seam_not_found",
            "an invalid line from a direct caller must not skip an enabled inspection");
    seam.pts = {{1e9, 40}, {1e9, 280}};
    require(inspector.inspect(seamImage(), p, &seam).reason == "seam_not_found",
            "out-of-image seam geometry must fail before allocating a sampling band");
}

void grayscaleCache() {
    cv::Mat gray(321, 383, CV_8U);
    cv::RNG(28).fill(gray, cv::RNG::UNIFORM, 0, 255);
    const auto before = gray.clone();
    cas::Reference ref(gray);
    cas::Settings s; s.shiftToleranceX = s.shiftToleranceY = 0;
    for (double scale : {1.0, 0.73, 1.0}) {
        s.inspectScale = scale;
        require(cas::inspectFrame(ref, gray, s, true).diffMax == 0, "identical grayscale frames must pass at every scale");
        require(cv::norm(gray, before, cv::NORM_INF) == 0, "reference preparation must not blur the caller's grayscale image in place");
    }
}

void apiGeometry(const std::filesystem::path& dir) {
    cd::App app(dir / "frames", dir / "config" / "test.json", dir / "logs");
    std::string err;
    require(app.setReferenceFromImage(0, seamImage(), err), "cannot create test reference");
    const auto before = app.state()["cameras_info"][0];
    require(!app.setCamera(0, {{"seam", {{"enabled", true}, {"pts", {{180, 40}, {180, 50}}}}}}, err),
            "the API must reject enabled seam lines shorter than the algorithm can measure");
    require(!app.setCamera(0, {{"roi", {{10, 10}, {20, 20}, {30, 30}}}}, err),
            "a zero-area ROI must be rejected instead of silently returning a clean score");
    require(app.state()["cameras_info"][0]["roi"] == before["roi"], "invalid geometry must not mutate camera state");
    require(app.setCamera(0, {{"seam", {{"enabled", true}, {"pts", {{180, 40}, {180, 280}}}}}}, err),
            "a valid seam must be accepted");
    require(app.setReferenceFromImage(0, cv::Mat(160, 192, CV_8UC3, cv::Scalar::all(80)), err), "cannot resize reference");
    const auto resized = app.state()["cameras_info"][0];
    require(resized["seam"]["enabled"] == false && resized["seam"]["pts"].empty(),
            "replacing the reference at a different resolution must invalidate old pixel seam coordinates");
}
}

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    cv::setNumThreads(1);
    const auto dir = fs::temp_directory_path() / ("inspection-regression-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    int failures = 0, cases = 0;
    auto run = [&](const char* name, const std::function<void()>& test) {
        if (argc > 1 && std::string(argv[1]) != name) return;
        ++cases;
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failures; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    run("identical_downscaled", identicalDownscaled);
    run("identical_roi_alignment", identicalRoiAlignment);
    run("known_roi_translations", knownRoiTranslations);
    run("ring_defect", ringDefect);
    run("missing_seam", missingSeam);
    run("seam_contrast", seamContrast);
    run("seam_geometry", seamGeometry);
    run("grayscale_cache", grayscaleCache);
    run("api_geometry", [&] { apiGeometry(dir); });
    const auto resolved = fs::weakly_canonical(dir);
    if (resolved.parent_path() == fs::weakly_canonical(fs::temp_directory_path()) &&
        resolved.filename().string().rfind("inspection-regression-", 0) == 0) fs::remove_all(resolved);
    std::cout << cases << " cases, " << failures << " failures\n";
    return failures ? 1 : 0;
}
