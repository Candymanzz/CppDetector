#include "service/service.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool near(double a, double b) { return std::abs(a-b) < 1e-8; }

void settingsAndPersistence(const std::filesystem::path& dir) {
    auto s = cas::Settings{}.withMetricDefaults(4);
    require(near(*s.horizontalShiftMm, 3.24) && near(*s.verticalShiftMm, 4.08), "migrate existing 12px global limits using camera 4 scale");
    require(s.applyJson({{"horizontal_shift_tolerance_mm", .5}, {"vertical_shift_tolerance_mm", 4}}).empty(), "fractional metric update");
    const auto before = s.toJson();
    for (const auto& bad : {cas::json(-1), cas::json(30.01), cas::json("bad"), cas::json(std::numeric_limits<double>::infinity())}) {
        require(!s.applyJson({{"horizontal_shift_tolerance_mm", 2}, {"vertical_shift_tolerance_mm", bad}}).empty(), "reject invalid mm");
        require(s.toJson() == before, "metric update is atomic");
    }
    require(!s.applyJson({{"estimated_mm_per_pixel", {0, .34}}}).empty(), "reject zero scale");
    cas::SettingsPatch::fromJson({{"shift_tolerance_x", 0}, {"shift_tolerance_y", 0}}).applyTo(s);
    require(s.horizontalShiftMm == .5 && s.verticalShiftMm == 4, "comparison relaxation does not reset metric limits");
    cas::SettingsPatch::fromJson({{"max_alignment_shift_x", 5}}).applyTo(s);
    require(!s.horizontalShiftMm && s.verticalShiftMm == 4 && s.maxAlignmentShiftX == 5, "legacy pixel override resets its metric axis only");
    cas::SettingsPatch::fromJson({{"horizontal_shift_tolerance_mm", 0}}).applyTo(s);
    require(s.horizontalShiftMm == 0, "zero metric override is preserved");

    const auto file = dir / "settings.json";
    { std::ofstream out(file); out << cas::json{{"legacy", {{"max_alignment_shift_x", 8}, {"max_alignment_shift_y", 10}, {"threshold", .4}}},
                                               {"untouched", {{"shift_tolerance", 30}, {"inspect_scale", .1}}}}; }
    {
        cas::SettingsStore store(file);
        require(near(*store.get("legacy", 4).horizontalShiftMm, 2.16), "migrate stored global limits, not local tolerance");
        cas::Settings saved;
        require(store.set("legacy", {{"horizontal_shift_tolerance_mm", .5}, {"vertical_shift_tolerance_mm", 4}}, saved, 4).empty(), "save camera metric settings");
        require(saved.threshold == .4, "migration preserves the anomaly threshold");
        require(store.set("other", {{"sensitivity", .7}}, saved, 1).empty(), "save another profile");
        require(near(store.get("untouched", 4).estimatedMmPerPixel.x, .27), "another save must not pin generic camera scale");
    }
    cas::SettingsStore restarted(file);
    const auto saved = restarted.get("legacy", 4);
    require(saved.horizontalShiftMm == .5 && saved.verticalShiftMm == 4 && saved.threshold == .4, "metric values survive restart");
    require(*restarted.get("untouched", 4).verticalShiftMm <= 30, "large legacy limits migrate into the mm range");
}

cv::Mat fixture() {
    cv::Mat image(512, 612, CV_8UC3);
    cv::RNG rng(20261102);
    rng.fill(image, cv::RNG::UNIFORM, 30, 225);
    cv::putText(image, "METRIC LABEL", {120, 240}, cv::FONT_HERSHEY_SIMPLEX, 1.2, {250,250,250}, 3);
    return image;
}
cv::Mat translated(const cv::Mat& image, double dx, double dy) {
    cv::Mat out;
    const cv::Mat matrix = (cv::Mat_<double>(2,3) << 1,0,dx,0,1,dy);
    cv::warpAffine(image, out, matrix, image.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return out;
}

void inspection(const std::filesystem::path& dir) {
    const auto image = fixture();
    cas::Settings metric = cas::Settings{}.withMetricDefaults(0);
    metric.horizontalShiftMm = 2; metric.verticalShiftMm = 1.5;
    metric.shiftToleranceX = metric.shiftToleranceY = 1;
    cas::Service service(dir / "service", 1);
    cas::InspectJob job; job.product = "bucket#cam=4"; job.profile = "arbitrary-profile";
    service.setReference(job.product, image.clone());
    auto result = service.inspectMat(job, image, true);
    require(near(result.settings.estimatedMmPerPixel.x, .27), "camera scale is independent of profile name");
    job.cameraId = 7;
    result = service.inspectMat(job, image, true);
    require(near(result.settings.estimatedMmPerPixel.x, .22), "explicit camera identity selects its scale");

    for (int resize : {1, 2}) {
        cv::Mat frame;
        cv::resize(image, frame, {image.cols*resize, image.rows*resize});
        cas::Reference reference(frame.clone());
        for (double scale : {1., .7, .5, .2}) {
            metric.inspectScale = scale;
            const auto out = cas::inspectFrame(reference, frame, metric, true);
            const auto units = metric.mmPerPixel(frame.size());
            require(near(out.shiftLimit.x*units.x, 2) && near(out.shiftLimit.y*units.y, 1.5), "mm limits follow frame resolution and analysis scale");
            auto pixels = metric.forFrame(frame.size());
            pixels.horizontalShiftMm.reset(); pixels.verticalShiftMm.reset();
            const auto baseline = cas::inspectFrame(reference, frame, pixels, true);
            require(out.anomalyScore == baseline.anomalyScore && out.areaPercent == baseline.areaPercent &&
                    cv::norm(out.diff, baseline.diff, cv::NORM_INF) == 0, "metric conversion preserves current anomaly and heatmap calculation");
            metric.horizontalShiftMm = 4;
            const auto wider = cas::inspectFrame(reference, frame, metric, true);
            require(wider.envelopeKernel == out.envelopeKernel, "global metric tolerance does not relax local defect comparison");
            metric.horizontalShiftMm = 2;
        }
    }
    cas::Reference reference(image.clone());
    reference.setRoi({{.15,.15},{.85,.15},{.85,.85},{.15,.85}});
    metric.horizontalShiftMm = 2; metric.verticalShiftMm = 2;
    for (double scale : {1., .5}) {
        metric.inspectScale = scale;
        require(cas::inspectFrame(reference, translated(image, 2, 0), metric, false).reason != "shift_exceeded", "horizontal motion below 2mm is allowed");
        require(cas::inspectFrame(reference, translated(image, 12, 0), metric, false).reason == "shift_exceeded", "horizontal motion above 2mm rejects");
        require(cas::inspectFrame(reference, translated(image, 0, 2), metric, false).reason != "shift_exceeded", "vertical motion below 2mm is allowed");
        require(cas::inspectFrame(reference, translated(image, 0, 12), metric, false).reason == "shift_exceeded", "vertical motion above 2mm rejects");
    }
}
}

int main() {
    cv::setNumThreads(1);
    const auto dir = std::filesystem::temp_directory_path() / ("metric-tolerances-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(dir);
        settingsAndPersistence(dir); inspection(dir);
        const auto resolved = std::filesystem::weakly_canonical(dir);
        if (resolved.parent_path() == std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()) &&
            resolved.filename().string().rfind("metric-tolerances-", 0) == 0) std::filesystem::remove_all(resolved);
        std::cout << "metric settings, migration, independent comparison and inspection passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (fixtures: " << dir.string() << ")\n"; return 1;
    }
}
