#pragma once
#include "decision_log.h"
#include "../inspect/inspector.h"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>

namespace cd {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int kCameras = 10;

// Owns cameras (reference, ROI, params), the emulated frame stream and the decision log.
class App {
public:
    App(fs::path sourceDir, fs::path configPath, fs::path logDir);
    ~App();

    json state();    // full snapshot for the UI
    json status();   // light snapshot (polled)

    bool setCamera(int cam, const json& body, std::string& err);
    bool setGlobal(const json& body, std::string& err);
    bool setReferenceFromFrame(int cam, const std::string& frame, std::string& err);
    bool setReferenceFromImage(int cam, const cv::Mat& img, std::string& err);
    json inspectFrame(int cam, const std::string& frame, bool defect, std::string& err);
    json inspectUpload(int cam, const cv::Mat& img, const std::string& name, bool defect, std::string& err);
    json inspectAll(int cam, bool defect, std::string& err);
    bool autoRoi(int cam, const std::string& frame, std::vector<cv::Point>& out, std::string& err);
    std::vector<std::string> testFiles() const;                  // images in <frames>/../test-pr-ph
    bool readTestFile(const std::string& name, cv::Mat& out) const;
    void setRunning(bool on) { running_ = on; }

    bool frameJpeg(int cam, const std::string& frame, std::vector<uchar>& out);   // frame="" -> reference
    bool overlayJpeg(int cam, std::vector<uchar>& out);
    DecisionLog& log() { return log_; }

private:
    struct Cam {
        std::mutex m;
        fs::path dir;
        std::vector<std::string> frames;
        std::string refName;          // frame folder name or "upload"
        cv::Mat ref;
        std::vector<cv::Point> roi;
        InspectParams p;
        Inspector insp;
        json last;
        std::vector<uchar> overlay;
        uint64_t ver = 0;
        size_t cursor = 0;
        int64_t lastRejectSaveMs = 0;
    };

    fs::path refImagePath(int cam) const { return cfgPath_.parent_path() / ("ref_" + cfgPath_.stem().string() + "_cam" + std::to_string(cam) + ".png"); }
    bool validCam(int cam) const { return cam >= 0 && cam < kCameras; }
    bool readFrame(Cam& c, const std::string& frame, cv::Mat& out);
    json inspectLocked(Cam& c, int id, const cv::Mat& img, const std::string& name,
                       const std::string& source, bool defect, bool keepOverlay = true);
    void rebuild(Cam& c);
    void save();
    void load();
    void tick();
    void loop();

    fs::path source_, cfgPath_;
    DecisionLog log_;
    std::unique_ptr<Cam> cams_[kCameras];
    std::mutex saveM_;

    std::atomic<bool> running_{false}, quit_{false};
    std::atomic<int> periodMs_{1000};
    std::atomic<bool> defect_{false};
    std::atomic<uint64_t> batch_{0};
    std::atomic<double> batchMs_{0}, batchMaxFrameMs_{0};
    std::thread worker_;
};

}
