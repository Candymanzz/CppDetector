#include "core/inspector.h"
#include "core/settings.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {

int checks = 0;

void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void settingsTests(const std::filesystem::path& dir) {
    cas::Settings s;
    require(s.shiftToleranceX == 3 && s.shiftToleranceY == 3, "default axes must both be 3");
    require(s.applyJson({{"shift_tolerance", 7}}).empty(), "legacy setting must load");
    require(s.shiftToleranceX == 7 && s.shiftToleranceY == 7, "legacy setting must populate both axes");
    require(s.applyJson({{"shift_tolerance_x", 2}}).empty(), "X-only update must load");
    require(s.shiftToleranceX == 2 && s.shiftToleranceY == 7, "X update must preserve Y");
    require(s.applyJson({{"shift_tolerance_y", 0}}).empty(), "zero Y must be accepted");
    require(s.shiftToleranceX == 2 && s.shiftToleranceY == 0, "zero Y must preserve X");
    require(s.applyJson({{"shift_tolerance", 5}, {"shift_tolerance_x", 1}}).empty(), "mixed update must load");
    require(s.shiftToleranceX == 1 && s.shiftToleranceY == 5, "explicit X must override legacy value");
    const auto before = s.toJson();
    for (const auto& invalid : {cas::json(-1), cas::json(31), cas::json("bad"), cas::json(nullptr)}) {
        require(!s.applyJson({{"shift_tolerance_x", 4}, {"shift_tolerance_y", invalid}}).empty(), "invalid axis must be rejected");
        require(s.toJson() == before, "invalid update must be atomic");
    }
    require(!before.contains("shift_tolerance"), "new serialization must use independent axes");
    cas::Settings roundTrip;
    require(roundTrip.applyJson(before).empty() && roundTrip.toJson() == before, "new settings must round-trip");

    cas::SettingsPatch::fromJson({{"shift_tolerance_y", 0}}).applyTo(s);
    require(s.shiftToleranceX == 1 && s.shiftToleranceY == 0, "request override must handle zero without changing X");
    cas::SettingsPatch::fromJson({{"shift_tolerance", 8}, {"shift_tolerance_y", 2}}).applyTo(s);
    require(s.shiftToleranceX == 8 && s.shiftToleranceY == 2, "request axis must override request legacy field");
    cas::SettingsPatch::fromJson({{"shift_tolerance_x", -2}, {"shift_tolerance_y", 30}}).applyTo(s);
    require(s.shiftToleranceX == 8 && s.shiftToleranceY == 30, "invalid request axis must not suppress valid other axis");

    const auto file = dir / "settings.json";
    { std::ofstream out(file); out << cas::json({{"camera", {{"shift_tolerance", 6}, {"threshold", 0.4}}}}).dump(); }
    {
        cas::SettingsStore store(file);
        const auto loaded = store.get("camera");
        require(loaded.shiftToleranceX == 6 && loaded.shiftToleranceY == 6, "old on-disk profile must load into both axes");
        cas::Settings saved;
        require(store.set("camera", {{"shift_tolerance_x", 2}}, saved).empty(), "migrated profile must save");
    }
    cas::SettingsStore restarted(file);
    const auto saved = restarted.get("camera");
    require(saved.shiftToleranceX == 2 && saved.shiftToleranceY == 6 && saved.threshold == 0.4,
            "restart must preserve both axes and other settings");
    std::ifstream in(file);
    const auto disk = cas::json::parse(in);
    require(!disk.at("camera").contains("shift_tolerance") && disk.at("camera").at("shift_tolerance_y") == 6,
            "saved profile must migrate to new keys");
}

cv::Mat fixture() {
    cv::Mat frame(320, 384, CV_8UC3);
    cv::RNG rng(20261026);
    rng.fill(frame, cv::RNG::UNIFORM, 30, 225);
    cv::putText(frame, "LABEL XY", {25, 100}, cv::FONT_HERSHEY_SIMPLEX, 1.3, {250, 250, 250}, 3);
    cv::rectangle(frame, {220, 170, 80, 70}, {10, 10, 10}, 5);
    cv::circle(frame, {90, 230}, 35, {240, 240, 240}, 4);
    return frame;
}

cv::Mat translated(const cv::Mat& image, double dx, double dy) {
    cv::Mat out;
    const cv::Mat matrix = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
    cv::warpAffine(image, out, matrix, image.size(),
                   cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

cas::InspectOutcome inspect(const cv::Mat& ref, const cv::Mat& cur, int x, int y, double scale = 1.0) {
    cas::Reference reference(ref.clone());
    cas::Settings s;
    s.shiftToleranceX = x;
    s.shiftToleranceY = y;
    s.inspectScale = scale;
    // Explicit limits for the legacy-envelope fixtures below.
    s.maxAlignmentShiftX = std::max(8.0, 4.0 * std::round(x * scale)) / scale;
    s.maxAlignmentShiftY = std::max(8.0, 4.0 * std::round(y * scale)) / scale;
    return cas::inspectFrame(reference, cur, s, true);
}

bool sameInspection(const cas::InspectOutcome& a, const cas::InspectOutcome& b) {
    return a.reject == b.reject && a.reason == b.reason && a.anomalyScore == b.anomalyScore
        && a.areaPercent == b.areaPercent && a.shift == b.shift && a.rawShift == b.rawShift
        && std::abs(a.shiftResponse - b.shiftResponse) < 1e-12 && a.zones == b.zones
        && cv::norm(a.diff, b.diff, cv::NORM_INF) == 0 && cv::norm(a.kept, b.kept, cv::NORM_INF) == 0;
}

void independentShiftTests(const std::filesystem::path& dir) {
    auto frame = fixture();
    cas::Reference reference(frame.clone());
    reference.setRoi({{0.15, 0.15}, {0.85, 0.15}, {0.85, 0.85}, {0.15, 0.85}});
    cas::Settings s;
    s.minBlobArea = 1; s.sensitivity = 0.8;
    s.shiftToleranceX = s.shiftToleranceY = 0;
    s.maxAlignmentShiftX = s.maxAlignmentShiftY = 20;
    for (double scale : {1.0, 0.5}) {
        s.inspectScale = scale;
        const auto shifted = translated(frame, 10, 0);
        auto out = cas::inspectFrame(reference, shifted, s, true);
        require(out.shiftLimit == cv::Point2d(20, 20) && std::abs(out.shift.x - 10) < 1,
                "large displacement must align even with zero comparison relaxation");
        s.shiftToleranceX = s.shiftToleranceY = 1;
        const auto good = cas::inspectFrame(reference, shifted, s, true);
        require(!good.reject, "a good translated label must pass with one-pixel contour relaxation");
        s.shiftToleranceX = s.shiftToleranceY = 0;
        s.maxAlignmentShiftX = 5;
        out = cas::inspectFrame(reference, shifted, s, true);
        require(out.reason == "shift_exceeded", "independent displacement limit must reject excessive motion");
        s.shiftToleranceX = 15;
        out = cas::inspectFrame(reference, shifted, s, true);
        require(out.reason == "shift_exceeded" && out.shiftLimit.x == 5,
                "comparison relaxation must not increase displacement limit");
        s.shiftToleranceX = 0; s.maxAlignmentShiftX = 20;
    }
    s.inspectScale = 1;
    cv::rectangle(frame, {100, 100, 100, 120}, {180, 180, 180}, cv::FILLED);
    cv::rectangle(frame, {140, 110, 8, 100}, {30, 30, 30}, cv::FILLED);
    cas::Reference printed(frame.clone());
    auto damaged = frame.clone();
    cv::rectangle(damaged, {148, 145, 3, 20}, {30, 30, 30}, cv::FILLED);
    const auto strict = cas::inspectFrame(printed, damaged, s, true);
    s.maxAlignmentShiftX = s.maxAlignmentShiftY = 60;
    const auto wider = cas::inspectFrame(printed, damaged, s, true);
    require(strict.anomalyScore > 0 && strict.anomalyScore == wider.anomalyScore,
            "raising the displacement limit must not suppress a real defect near print");
    const auto movedDefect = cas::inspectFrame(printed, translated(damaged, 5, -4), s, true);
    require(movedDefect.anomalyScore > 0 && cv::countNonZero(movedDefect.kept) > 0,
            "defect must survive alignment of a displaced label");
    s.shiftToleranceX = s.shiftToleranceY = 3;
    const auto relaxed = cas::inspectFrame(printed, damaged, s, true);
    require(relaxed.anomalyScore < strict.anomalyScore && relaxed.shiftLimit == wider.shiftLimit,
            "relaxation may suppress contour differences but must not change alignment limits");

    const auto file = dir / "migration.json";
    { std::ofstream out(file); out << cas::json({{"old", {{"shift_tolerance_x", 6},
            {"shift_tolerance_y", 2}, {"inspect_scale", 0.5}}}}).dump(); }
    cas::SettingsStore store(file);
    auto migrated = store.get("old");
    require(migrated.maxAlignmentShiftX == 24 && migrated.maxAlignmentShiftY == 16,
            "legacy profile migration must preserve actual scaled displacement limits");
    require(store.set("old", {{"shift_tolerance_x", 0}}, migrated).empty()
            && migrated.maxAlignmentShiftX == 24, "editing relaxation must preserve migrated limit");
    cas::SettingsStore restarted(file);
    require(restarted.get("old").maxAlignmentShiftX == 24, "independent limits must survive restart");
    cas::SettingsPatch::fromJson({{"max_alignment_shift_x", 0}}).applyTo(migrated);
    require(migrated.maxAlignmentShiftX == 0 && migrated.maxAlignmentShiftY == 16,
            "temporary zero limit must affect only its axis");
    const auto before = migrated.toJson();
    require(!migrated.applyJson({{"max_alignment_shift_y", -1}}).empty()
            && before == migrated.toJson(), "invalid limit update must be atomic");
    std::filesystem::remove(file);
}

void heatmapScoreTests() {
    auto frame = fixture();
    // A textured background stabilises alignment/normalisation; the flat patch holds tiny defects.
    cv::rectangle(frame, {120, 120, 80, 80}, {100, 100, 100}, cv::FILLED);
    cas::Reference reference(frame.clone());
    cas::Settings settings;
    settings.sensitivity = 0.8;
    settings.shiftToleranceX = settings.shiftToleranceY = 0;
    settings.minBlobArea = 1;
    const auto check = [&](const cv::Mat& current, int minimum) {
        settings.minBlobArea = minimum;
        const auto out = cas::inspectFrame(reference, current, settings, true);
        cv::Mat energy;
        out.diff.convertTo(energy, CV_8U, 128.0 / out.pixelThreshold);
        const cv::Mat candidates = energy > 63;
        if (minimum == 1)
            require(cv::norm(candidates, out.kept, cv::NORM_INF) == 0,
                    "every previously visible heatmap pixel must count when min area is one");
        const int pixels = cv::countNonZero(out.kept);
        require(std::abs(out.areaPercent - 100.0 * pixels / out.roiArea) < 1e-12,
                "area must count exact component pixels");
        const double normalizer = 0.2 + (0.001 - 0.2) * 0.6;
        require(std::abs(out.anomalyScore - std::min(1.0, out.areaPercent / normalizer)) < 1e-12,
                "score normalisation must remain unchanged");
        const auto heat = cas::buildHeatmap(out, {}, 0);
        require((cv::countNonZero(heat) > 0) == (pixels > 0), "heatmap and scored mask must agree");
        return pixels;
    };
    auto weak = frame.clone();
    weak.at<cv::Vec3b>(150, 150) = {180, 180, 180};
    require(check(weak, 1) == 1, "a weak single-pixel defect must contribute");
    require(check(weak, 2) == 0, "min area must remove defect from both score and heatmap");
    auto scratch = frame.clone();
    cv::line(scratch, {145, 150}, {164, 150}, {140, 140, 140}, 1);
    const int scratchPixels = check(scratch, 1);
    require(scratchPixels > 0, "thin scratch must survive without a 3x3 opening");
    require(check(scratch, scratchPixels) == scratchPixels, "pixel area cutoff must be inclusive");
    require(check(scratch, scratchPixels + 1) == 0, "pixel area cutoff must be exact");
    require(check(frame, 1) == 0, "identical frame must have no heatmap or score");
}

void sensitivityTests() {
    const auto frame = fixture();
    cas::Reference reference(frame.clone());
    const auto moved = translated(frame, 30, 0);
    auto damaged = frame.clone();
    cv::rectangle(damaged, {40, 40, 200, 200}, {255, 255, 255}, cv::FILLED);
    for (double threshold : {0.001, 0.6, 1.0}) {
        for (double scale : {1.0, 0.5}) {
            cas::Settings s;
            s.threshold = threshold;
            s.inspectScale = scale;
            for (const auto& current : {frame, moved, damaged}) {
                s.sensitivity = 0.0;
                const auto accepted = cas::inspectFrame(reference, current, s, true);
                require(!accepted.reject && accepted.anomalyScore == 0.0 && accepted.reason.empty(),
                        "zero sensitivity must accept clean, shifted and damaged frames");
                s.sensitivity = 1.0;
                const auto rejected = cas::inspectFrame(reference, current, s, true);
                require(rejected.reject && rejected.anomalyScore == 1.0,
                        "full sensitivity must reject even an identical reference at threshold 1");
            }
        }
    }
}

void alignmentCacheTests() {
    const auto frame = fixture();
    const auto moved = translated(frame, 6, -4);
    const cas::NormPolygon square{{40.0 / 383, 20.0 / 319}, {320.0 / 383, 20.0 / 319},
                                 {320.0 / 383, 300.0 / 319}, {40.0 / 383, 300.0 / 319}};
    for (bool squareRoi : {false, true}) for (double scale : {1.0, 0.5}) {
        cas::Reference reference(frame.clone());
        if (squareRoi) reference.setRoi(square);
        cas::Settings settings;
        settings.inspectScale = scale;
        const auto prepared = reference.prepared(scale);
        if (squareRoi && scale == 1.0)
            require(prepared->smallSize == cv::Size(256, 256), "regression fixture must exercise the unpadded DFT path");
        const auto originalSmall = prepared->refSmall.clone(), originalHann = prepared->hann.clone();
        const auto originalGray = prepared->refGray.clone(), originalBgr = reference.bgr().clone();
        const auto expected = cas::inspectFrame(reference, moved, settings, true);
        require(cv::norm(originalSmall, prepared->refSmall, cv::NORM_INF) == 0,
                "phase correlation must not modify the cached alignment thumbnail");
        require(!expected.reject && std::abs(expected.shift.x - 6) < 1.5 && std::abs(expected.shift.y + 4) < 1.5,
                "a small translation must align without adding a defect");
        for (int iteration = 0; iteration < 80; ++iteration)
            require(sameInspection(cas::inspectFrame(reference, moved, settings, true), expected),
                    "repeated identical inspections must preserve shift, confidence, maps and verdict");

        // All workers intentionally share ONE warmed reference/cache, not separate camera objects.
        std::vector<std::future<bool>> workers;
        for (int worker = 0; worker < 8; ++worker) workers.push_back(std::async(std::launch::async, [&] {
            for (int iteration = 0; iteration < 20; ++iteration)
                if (!sameInspection(cas::inspectFrame(reference, moved, settings, true), expected)) return false;
            return true;
        }));
        for (auto& worker : workers) require(worker.get(), "concurrent inspections must not corrupt shared cached data");
        require(reference.prepared(scale).get() == prepared.get(), "inspection must reuse the immutable prepared cache");
        require(cv::norm(originalSmall, prepared->refSmall, cv::NORM_INF) == 0, "shared alignment thumbnail must stay unchanged");
        require(cv::norm(originalHann, prepared->hann, cv::NORM_INF) == 0, "Hann window must stay unchanged");
        require(cv::norm(originalGray, prepared->refGray, cv::NORM_INF) == 0, "prepared reference image must stay unchanged");
        require(cv::norm(originalBgr, reference.bgr(), cv::NORM_INF) == 0, "original reference image must stay unchanged");

        cas::Reference coldReference(frame.clone());
        if (squareRoi) coldReference.setRoi(square);
        workers.clear();
        for (int worker = 0; worker < 8; ++worker) workers.push_back(std::async(std::launch::async, [&] {
            return sameInspection(cas::inspectFrame(coldReference, moved, settings, true), expected);
        }));
        for (auto& worker : workers) require(worker.get(), "concurrent first inspections must also be stable");
        require(cv::norm(originalSmall, coldReference.prepared(scale)->refSmall, cv::NORM_INF) == 0,
                "concurrent preparation must not publish a mutated thumbnail");
    }
}

void inspectionTests() {
    const auto ref = fixture();
    const auto horizontal = translated(ref, 10, 0);
    const auto vertical = translated(ref, 0, 10);
    auto permittedX = inspect(ref, horizontal, 3, 0);
    auto deniedX = inspect(ref, horizontal, 0, 3);
    auto permittedY = inspect(ref, vertical, 0, 3);
    auto deniedY = inspect(ref, vertical, 3, 0);
    require(permittedX.shiftResponse >= 0.3 && std::abs(permittedX.shift.x - 10) < 1, "X translation must be detected");
    require(permittedX.reason != "shift_exceeded" && permittedX.shiftLimit == cv::Point2d(12, 8), "X must use its own compensation limit");
    require(deniedX.reject && deniedX.reason == "shift_exceeded" && deniedX.shift == cv::Point2d(0, 0), "small X limit must reject excessive horizontal shift");
    require(permittedY.shiftResponse >= 0.3 && std::abs(permittedY.shift.y - 10) < 1, "Y translation must be detected");
    require(permittedY.reason != "shift_exceeded" && permittedY.shiftLimit == cv::Point2d(8, 12), "Y must use its own compensation limit");
    require(deniedY.reject && deniedY.reason == "shift_exceeded", "small Y limit must reject excessive vertical shift");
    require(permittedX.envelopeKernel == cv::Size(7, 1) && permittedY.envelopeKernel == cv::Size(1, 7), "envelope width/height must follow X/Y");
    for (const auto delta : {cv::Point(-10, 0), cv::Point(0, -10), cv::Point(10, -10)}) {
        const auto shifted = translated(ref, delta.x, delta.y);
        const auto allowed = inspect(ref, shifted, 3, 3);
        require(allowed.shiftResponse >= 0.3 && allowed.reason != "shift_exceeded"
                && std::abs(allowed.shift.x - delta.x) < 1 && std::abs(allowed.shift.y - delta.y) < 1,
                "negative and combined translations must be compensated");
        const auto denied = inspect(ref, shifted, delta.x ? 0 : 3, delta.y ? 0 : 3);
        require(denied.reject && denied.reason == "shift_exceeded", "axis limits must apply to both signs");
    }

    const auto noTolerance = inspect(ref, ref, 0, 0);
    require(noTolerance.envelopeKernel == cv::Size(1, 1) && noTolerance.shiftLimit == cv::Point2d(8, 8), "zero keeps old positioning minimum but disables local envelope expansion");
    const auto scaled = inspect(ref, ref, 6, 2, 0.5);
    require(scaled.envelopeKernel == cv::Size(7, 3) && scaled.shiftLimit == cv::Point2d(24, 16), "downscale must be applied per axis and limits reported in original pixels");

    auto localShift = ref.clone();
    const cv::Rect patch(110, 120, 90, 100);
    translated(ref, 3, 0)(patch).copyTo(localShift(patch));
    const auto tolerantX = inspect(ref, localShift, 3, 0);
    const auto tolerantY = inspect(ref, localShift, 0, 3);
    require(cv::sum(tolerantX.diff)[0] < cv::sum(tolerantY.diff)[0], "horizontal envelope must better tolerate a local horizontal shift");
    auto localVertical = ref.clone();
    translated(ref, 0, 3)(patch).copyTo(localVertical(patch));
    require(cv::sum(inspect(ref, localVertical, 0, 3).diff)[0] < cv::sum(inspect(ref, localVertical, 3, 0).diff)[0],
            "vertical envelope must better tolerate a local vertical shift");
    for (const auto& outcome : {tolerantX, tolerantY, noTolerance, scaled}) {
        cv::Mat gray, blurred, lo, hi, expected, refMin, refMax;
        cv::cvtColor(ref, gray, cv::COLOR_BGR2GRAY);
        if (outcome.scale != 1.0) cv::resize(gray, gray, outcome.workSize, 0, 0, cv::INTER_AREA);
        cv::GaussianBlur(gray, blurred, {5, 5}, 0);
        const auto kernel = cv::getStructuringElement(cv::MORPH_RECT, outcome.envelopeKernel);
        cv::erode(blurred, refMin, kernel);
        cv::dilate(blurred, refMax, kernel);
        cv::subtract(refMin, outcome.aligned, lo);
        cv::subtract(outcome.aligned, refMax, hi);
        cv::max(lo, hi, expected);
        require(cv::norm(expected, outcome.diff, cv::NORM_INF) == 0, "difference map must match the rectangular X/Y envelope");
    }

    cas::Settings oldSettings, newSettings;
    oldSettings.applyJson({{"shift_tolerance", 4}});
    newSettings.applyJson({{"shift_tolerance_x", 4}, {"shift_tolerance_y", 4}});
    cas::Reference oldReference(ref.clone()), newReference(ref.clone());
    const auto oldOutcome = cas::inspectFrame(oldReference, localShift, oldSettings, true);
    const auto newOutcome = cas::inspectFrame(newReference, localShift, newSettings, true);
    require(cv::norm(oldOutcome.diff, newOutcome.diff, cv::NORM_INF) == 0 && oldOutcome.reject == newOutcome.reject
            && oldOutcome.shift == newOutcome.shift, "legacy and equal-axis inputs must have identical results");
}

}

int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("shift-tolerance-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(dir);
        cv::setNumThreads(1);
        settingsTests(dir);
        inspectionTests();
        sensitivityTests();
        heatmapScoreTests();
        independentShiftTests(dir);
        alignmentCacheTests();
        std::filesystem::remove(dir / "settings.json");
        std::filesystem::remove(dir / "settings.json.tmp");
        std::filesystem::remove(dir);
        std::cout << "shift tolerance tests passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (test output: " << dir.string() << ")\n";
        return 1;
    }
}
