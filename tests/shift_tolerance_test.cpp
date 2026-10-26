#include "app/app.h"
#include "inspect/inspector.h"
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

cv::Mat fixture() {
    cv::Mat frame(320, 384, CV_8UC3);
    cv::RNG rng(20261026);
    rng.fill(frame, cv::RNG::UNIFORM, 30, 225);
    cv::putText(frame, "LABEL XY", {25, 100}, cv::FONT_HERSHEY_SIMPLEX, 1.3, {250, 250, 250}, 3);
    cv::rectangle(frame, {220, 170, 80, 70}, {10, 10, 10}, 5);
    return frame;
}

cv::Mat translated(const cv::Mat& frame, int dx, int dy) {
    cv::Mat result;
    const cv::Mat matrix = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
    cv::warpAffine(frame, result, matrix, frame.size(), cv::INTER_LINEAR, cv::BORDER_REPLICATE);
    return result;
}

cd::InspectResult inspect(const cv::Mat& reference, const cv::Mat& frame, int x, int y, double downscale = 1) {
    cd::InspectParams p;
    p.shiftToleranceX = x; p.shiftToleranceY = y; p.downscale = downscale;
    cd::Inspector inspector;
    inspector.setReference(reference, {}, p);
    return inspector.inspect(frame, p);
}

void inspectorTests() {
    const auto ref = fixture();
    for (const auto delta : {cv::Point(10, 0), cv::Point(-10, 0), cv::Point(0, 10), cv::Point(0, -10), cv::Point(10, -10)}) {
        const auto frame = translated(ref, delta.x, delta.y);
        auto allowed = inspect(ref, frame, 3, 3);
        require(std::abs(allowed.shift.x - delta.x) < 1 && std::abs(allowed.shift.y - delta.y) < 1,
                "translation must be detected in both directions");
        require(allowed.reason != "shift_exceeded", "allowed shift must not be rejected by alignment");
        auto denied = inspect(ref, frame, delta.x ? 0 : 3, delta.y ? 0 : 3);
        require(denied.reject && denied.reason == "shift_exceeded", "each axis must use its own shift limit");
    }
    const auto x = inspect(ref, translated(ref, 10, 0), 3, 0);
    const auto y = inspect(ref, translated(ref, 0, 10), 0, 3);
    require(x.shiftLimit == cv::Point2d(12, 8) && x.envelopeKernel == cv::Size(7, 1), "X controls width and horizontal limit");
    require(y.shiftLimit == cv::Point2d(8, 12) && y.envelopeKernel == cv::Size(1, 7), "Y controls height and vertical limit");
    const auto zero = inspect(ref, ref, 0, 0);
    require(zero.envelopeKernel == cv::Size(1, 1) && zero.shiftLimit == cv::Point2d(8, 8),
            "zero preserves the old positioning minimum and removes comparison expansion");
    const auto scaled = inspect(ref, translated(ref, 20, 0), 3, 0, 0.5);
    require(scaled.shiftLimit == cv::Point2d(12, 8) && scaled.envelopeKernel == cv::Size(7, 1)
            && scaled.reason != "shift_exceeded", "downscale must preserve this project's working-pixel semantics");

    const cv::Rect patch(110, 120, 90, 100);
    auto horizontal = ref.clone(), vertical = ref.clone();
    translated(ref, 3, 0)(patch).copyTo(horizontal(patch));
    translated(ref, 0, 3)(patch).copyTo(vertical(patch));
    require(cv::sum(inspect(ref, horizontal, 3, 0).heatmap)[0] < cv::sum(inspect(ref, horizontal, 0, 3).heatmap)[0],
            "horizontal envelope must better tolerate a local horizontal movement");
    require(cv::sum(inspect(ref, vertical, 0, 3).heatmap)[0] < cv::sum(inspect(ref, vertical, 3, 0).heatmap)[0],
            "vertical envelope must better tolerate a local vertical movement");
}

void settingsTests(const std::filesystem::path& dir) {
    const auto config = dir / "config" / "test.json";
    std::filesystem::create_directories(config.parent_path());
    {
        std::ofstream out(config);
        out << cd::json({{"cameras", {{{"shift_tolerance", 6}, {"sensitivity", 0.8}}, {{"shift_tolerance", 4}}}}}).dump();
    }
    {
        cd::App app(dir / "frames", config, dir / "logs");
        auto c = app.state()["cameras_info"][0];
        require(c["shift_tolerance_x"] == 6 && c["shift_tolerance_y"] == 6, "legacy config must populate both axes");
        require(!c.contains("shift_tolerance"), "state must expose only canonical axis fields");
        std::string error;
        require(app.setCamera(0, {{"shift_tolerance_x", 2}}, error), "X-only update must succeed");
        c = app.state()["cameras_info"][0];
        require(c["shift_tolerance_x"] == 2 && c["shift_tolerance_y"] == 6, "X update must preserve Y");
        require(app.setCamera(0, {{"shift_tolerance_y", 0}}, error), "zero Y must be accepted");
        c = app.state()["cameras_info"][0];
        require(c["shift_tolerance_x"] == 2 && c["shift_tolerance_y"] == 0, "zero Y must preserve X");
        require(app.setCamera(0, {{"shift_tolerance", 5}, {"shift_tolerance_x", 1}}, error), "mixed legacy/axis update must succeed");
        c = app.state()["cameras_info"][0];
        require(c["shift_tolerance_x"] == 1 && c["shift_tolerance_y"] == 5, "explicit axis must override legacy field");
        const auto before = c;
        for (const auto& bad : {cd::json(-1), cd::json(31), cd::json("bad"), cd::json(nullptr)}) {
            error.clear();
            require(!app.setCamera(0, {{"shift_tolerance_x", 3}, {"shift_tolerance_y", bad}}, error), "invalid axis must be rejected");
            require(app.state()["cameras_info"][0] == before, "invalid settings must not partially modify the camera");
        }
        const auto other = app.state()["cameras_info"][1];
        require(other["shift_tolerance_x"] == 4 && other["shift_tolerance_y"] == 4, "another camera's settings must remain unchanged");
    }
    {
        cd::App restarted(dir / "frames", config, dir / "logs");
        const auto c = restarted.state()["cameras_info"][0];
        require(c["shift_tolerance_x"] == 1 && c["shift_tolerance_y"] == 5 && c["sensitivity"] == 0.8,
                "restart must preserve unequal axes and other parameters");
        std::ifstream in(config);
        const auto disk = cd::json::parse(in);
        require(!disk["cameras"][0].contains("shift_tolerance") && disk["cameras"][0]["shift_tolerance_y"] == 5,
                "saved config must migrate to independent fields");
    }
}
}

int main() {
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("cppdetector-xy-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        cv::setNumThreads(1);
        fs::create_directories(dir);
        inspectorTests();
        settingsTests(dir);
        fs::remove(dir / "config" / "test.json");
        fs::remove(dir / "config");
        fs::remove(dir / "logs" / "rejects");
        fs::remove(dir / "logs");
        fs::remove(dir);
        std::cout << "shift tolerance tests passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (test data: " << dir.string() << ")\n";
        return 1;
    }
}
