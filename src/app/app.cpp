#include "app.h"
#include "../inspect/auto_roi.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>

namespace cd {

using Clock = std::chrono::steady_clock;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

static json paramsToJson(const InspectParams& p) {
    return {{"sensitivity", p.sensitivity}, {"reject_threshold", p.rejectThreshold},
            {"shift_tolerance", p.shiftTolerance}, {"downscale", p.downscale}, {"min_blob_area", p.minBlobArea}};
}

static json roiToJson(const std::vector<cv::Point>& roi) {
    json a = json::array();
    for (auto& p : roi) a.push_back({p.x, p.y});
    return a;
}

// Validates and applies the fields present in `b`.
static bool applyParams(const json& b, InspectParams& p, std::string& err) {
    auto num = [&](const char* k, double lo, double hi, auto& dst) {
        if (!b.contains(k)) return true;
        if (!b[k].is_number() || b[k] < lo || b[k] > hi) { err = std::string("bad value for ") + k; return false; }
        dst = static_cast<std::remove_reference_t<decltype(dst)>>(b[k].get<double>());
        return true;
    };
    return num("sensitivity", 0, 1, p.sensitivity) && num("reject_threshold", 0, 100, p.rejectThreshold) &&
           num("shift_tolerance", 0, 30, p.shiftTolerance) && num("downscale", 0.1, 1, p.downscale) &&
           num("min_blob_area", 1, 100000, p.minBlobArea);
}

static bool parseRoi(const json& a, std::vector<cv::Point>& out, std::string& err) {
    out.clear();
    if (!a.is_array()) { err = "roi must be an array"; return false; }
    for (auto& pt : a) {
        if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number()) { err = "bad roi point"; return false; }
        out.emplace_back(cvRound(pt[0].get<double>()), cvRound(pt[1].get<double>()));
    }
    if (!out.empty() && out.size() < 3) { err = "roi needs at least 3 points"; out.clear(); return false; }
    return true;
}

App::App(fs::path sourceDir, fs::path configPath, fs::path logDir)
    : source_(std::move(sourceDir)), cfgPath_(std::move(configPath)), log_(std::move(logDir)) {
    for (int i = 0; i < kCameras; ++i) {
        auto c = std::make_unique<Cam>();
        c->dir = source_ / ("camera_" + std::to_string(i));
        std::error_code ec;
        for (auto& d : fs::directory_iterator(c->dir, ec))
            if (d.is_directory() && fs::exists(d.path() / "frame.jpg")) c->frames.push_back(d.path().filename().string());
        std::sort(c->frames.begin(), c->frames.end());
        if (!c->frames.empty()) {            // default reference = earliest frame (KB D3)
            c->refName = c->frames[0];
            c->ref = cv::imread((c->dir / c->refName / "frame.jpg").string());
        }
        cams_[i] = std::move(c);
    }
    load();
    for (auto& c : cams_) rebuild(*c);
    worker_ = std::thread(&App::loop, this);
}

App::~App() {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
}

void App::rebuild(Cam& c) {
    if (!c.ref.empty()) c.insp.setReference(c.ref, c.roi, c.p);
}

void App::load() {
    std::ifstream in(cfgPath_);
    if (!in) return;
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) return;
    std::string err;
    if (j.contains("period_ms") && j["period_ms"].is_number()) periodMs_ = std::clamp(j["period_ms"].get<int>(), 100, 60000);
    if (j.contains("defect") && j["defect"].is_boolean()) defect_ = j["defect"].get<bool>();
    if (!j.contains("cameras") || !j["cameras"].is_array()) return;
    for (int i = 0; i < kCameras && i < (int)j["cameras"].size(); ++i) {
        const json& cj = j["cameras"][i];
        Cam& c = *cams_[i];
        applyParams(cj, c.p, err);
        if (cj.contains("roi")) parseRoi(cj["roi"], c.roi, err);
        std::string ref = cj.value("reference", "");
        if (ref == "upload") {
            cv::Mat m = cv::imread((cfgPath_.parent_path() / ("ref_cam" + std::to_string(i) + ".png")).string());
            if (!m.empty()) { c.ref = m; c.refName = "upload"; }
        } else if (!ref.empty() && std::find(c.frames.begin(), c.frames.end(), ref) != c.frames.end()) {
            c.refName = ref;
            c.ref = cv::imread((c.dir / ref / "frame.jpg").string());
        }
    }
}

void App::save() {
    std::lock_guard<std::mutex> lk(saveM_);
    json j;
    j["source_dir"] = source_.string();
    j["period_ms"] = periodMs_.load();
    j["defect"] = defect_.load();
    j["cameras"] = json::array();
    for (auto& c : cams_) {
        std::lock_guard<std::mutex> cl(c->m);
        json cj = paramsToJson(c->p);
        cj["reference"] = c->refName;
        cj["roi"] = roiToJson(c->roi);
        j["cameras"].push_back(cj);
    }
    fs::create_directories(cfgPath_.parent_path());
    std::ofstream(cfgPath_) << j.dump(2);
}

json App::state() {
    json j = status();
    j["source_dir"] = source_.string();
    j["cameras_info"] = json::array();
    for (int i = 0; i < kCameras; ++i) {
        Cam& c = *cams_[i];
        std::lock_guard<std::mutex> lk(c.m);
        json cj = paramsToJson(c.p);
        cj["id"] = i;
        cj["frames"] = c.frames;
        cj["reference"] = c.refName;
        cj["roi"] = roiToJson(c.roi);
        cj["width"] = c.ref.cols;
        cj["height"] = c.ref.rows;
        j["cameras_info"].push_back(cj);
    }
    return j;
}

json App::status() {
    json j;
    j["running"] = running_.load();
    j["period_ms"] = periodMs_.load();
    j["defect"] = defect_.load();
    j["batch"] = batch_.load();
    j["batch_ms"] = batchMs_.load();
    j["batch_max_frame_ms"] = batchMaxFrameMs_.load();
    j["budget_ms"] = 500;
    j["last"] = json::array();
    for (int i = 0; i < kCameras; ++i) {
        std::lock_guard<std::mutex> lk(cams_[i]->m);
        j["last"].push_back(cams_[i]->last.is_null() ? json::object() : cams_[i]->last);
    }
    return j;
}

bool App::setCamera(int cam, const json& b, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return false; }
    Cam& c = *cams_[cam];
    {
        std::lock_guard<std::mutex> lk(c.m);
        InspectParams p = c.p;
        std::vector<cv::Point> roi = c.roi;
        if (!applyParams(b, p, err)) return false;
        if (b.contains("roi") && !parseRoi(b["roi"], roi, err)) return false;
        for (auto& pt : roi)
            if (c.ref.empty() || pt.x < 0 || pt.y < 0 || pt.x >= c.ref.cols || pt.y >= c.ref.rows) {
                err = "roi point outside image";
                return false;
            }
        c.p = p;
        c.roi = roi;
        rebuild(c);
    }
    save();
    return true;
}

bool App::setGlobal(const json& b, std::string& err) {
    if (b.contains("period_ms")) {
        if (!b["period_ms"].is_number() || b["period_ms"] < 100 || b["period_ms"] > 60000) {
            err = "period_ms must be 100..60000";
            return false;
        }
        periodMs_ = b["period_ms"].get<int>();
    }
    if (b.contains("defect") && b["defect"].is_boolean()) defect_ = b["defect"].get<bool>();
    save();
    return true;
}

bool App::readFrame(Cam& c, const std::string& frame, cv::Mat& out) {
    // only names from our own listing are accepted, which also blocks path tricks
    if (std::find(c.frames.begin(), c.frames.end(), frame) == c.frames.end()) return false;
    out = cv::imread((c.dir / frame / "frame.jpg").string());
    return !out.empty();
}

bool App::setReferenceFromFrame(int cam, const std::string& frame, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return false; }
    Cam& c = *cams_[cam];
    {
        std::lock_guard<std::mutex> lk(c.m);
        cv::Mat m;
        if (!readFrame(c, frame, m)) { err = "unknown frame"; return false; }
        if (!c.ref.empty() && m.size() != c.ref.size()) c.roi.clear();
        c.ref = m;
        c.refName = frame;
        rebuild(c);
    }
    save();
    return true;
}

bool App::setReferenceFromImage(int cam, const cv::Mat& img, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return false; }
    if (img.empty()) { err = "cannot decode image"; return false; }
    Cam& c = *cams_[cam];
    {
        std::lock_guard<std::mutex> lk(c.m);
        if (!c.ref.empty() && img.size() != c.ref.size()) c.roi.clear();
        c.ref = img;
        c.refName = "upload";
        fs::create_directories(cfgPath_.parent_path());
        cv::imwrite((cfgPath_.parent_path() / ("ref_cam" + std::to_string(cam) + ".png")).string(), img);
        rebuild(c);
    }
    save();
    return true;
}

// Synthetic defect so rejection can be tested on clean frames: dark spot + scratch near the ROI centre.
static void drawDefect(cv::Mat& img, const std::vector<cv::Point>& roi) {
    cv::Point c(img.cols / 2, img.rows / 2);
    if (!roi.empty()) { cv::Rect bb = cv::boundingRect(roi); c = {bb.x + bb.width / 2, bb.y + bb.height / 2}; }
    int r = std::max(8, img.cols / 25);
    cv::ellipse(img, c, cv::Size(r, r * 2 / 3), 20, 0, 360, cv::Scalar(40, 40, 160), cv::FILLED);
    cv::line(img, c + cv::Point(-4 * r, -3 * r), c + cv::Point(-r, -r), cv::Scalar(30, 30, 30), std::max(2, r / 12));
}

static std::vector<uchar> renderOverlay(const cv::Mat& img, const InspectResult& r, const std::vector<cv::Point>& roi) {
    cv::Mat vis = img.clone();
    if (!r.heatmap.empty()) {
        cv::Mat hm, heat;
        cv::resize(r.heatmap, hm, img.size());
        // normalise to the detection threshold: thr -> mid colour, 2*thr -> red; below thr/2 not shown
        const double thr = std::max(1, r.diffThreshold);
        cv::Mat boosted;
        hm.convertTo(boosted, CV_8U, 128.0 / thr);
        cv::applyColorMap(boosted, heat, cv::COLORMAP_JET);
        cv::Mat mask = hm > thr * 0.5;
        cv::Mat blended;
        cv::addWeighted(vis, 0.45, heat, 0.55, 0, blended);
        blended.copyTo(vis, mask);
    }
    if (roi.size() >= 3) cv::polylines(vis, roi, true, cv::Scalar(0, 255, 255), 2);
    for (auto& z : r.zones) cv::rectangle(vis, z, cv::Scalar(0, 0, 255), 2);
    std::vector<uchar> buf;
    cv::imencode(".jpg", vis, buf, {cv::IMWRITE_JPEG_QUALITY, 80});
    return buf;
}

json App::inspectLocked(Cam& c, int id, const cv::Mat& imgIn, const std::string& name,
                        const std::string& source, bool defect, bool keepOverlay) {
    cv::Mat img = imgIn;
    if (defect) { img = imgIn.clone(); drawDefect(img, c.roi); }
    InspectResult r = c.insp.inspect(img, c.p);

    json j;
    j["cam"] = id;
    j["frame"] = name;
    j["source"] = source;
    j["defect_injected"] = defect;
    j["score"] = std::round(r.score * 1000) / 1000;
    j["reject"] = r.reject;
    j["verdict"] = r.reject ? "REJECT" : (r.reason == "no_reference" ? "NO_REFERENCE" : "OK");
    j["reason"] = r.reason;
    j["threshold"] = c.p.rejectThreshold;
    j["sensitivity"] = c.p.sensitivity;
    j["shift"] = {std::round(r.shift.x * 100) / 100, std::round(r.shift.y * 100) / 100};
    j["zones"] = json::array();
    for (auto& z : r.zones) j["zones"].push_back({z.x, z.y, z.width, z.height});
    j["ms"] = std::round(r.ms * 100) / 100;
    j["ts"] = nowMs();

    if (keepOverlay) {
        c.overlay = renderOverlay(img, r, c.roi);
        j["ver"] = ++c.ver;
    }
    c.last = j;

    // reject artifacts: always for manual runs, throttled for the emulated stream
    json rec = j;
    if (r.reject && keepOverlay) {
        int64_t t = nowMs();
        if (source != "emulation" || t - c.lastRejectSaveMs > 5000) {
            c.lastRejectSaveMs = t;
            fs::path p = log_.rejectsDir() / ("cam" + std::to_string(id) + "_" + std::to_string(t) + "_" + name + ".jpg");
            std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(c.overlay.data()), c.overlay.size());
            rec["overlay"] = p.filename().string();
        }
    }
    log_.append(rec);
    return j;
}

json App::inspectFrame(int cam, const std::string& frame, bool defect, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return {}; }
    Cam& c = *cams_[cam];
    std::lock_guard<std::mutex> lk(c.m);
    cv::Mat img;
    if (!readFrame(c, frame, img)) { err = "unknown frame"; return {}; }
    return inspectLocked(c, cam, img, frame, "manual", defect);
}

json App::inspectUpload(int cam, const cv::Mat& img, const std::string& name, bool defect, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return {}; }
    if (img.empty()) { err = "cannot decode image"; return {}; }
    Cam& c = *cams_[cam];
    std::lock_guard<std::mutex> lk(c.m);
    if (!c.ref.empty() && img.size() != c.ref.size()) { err = "image size differs from the reference"; return {}; }
    return inspectLocked(c, cam, img, name, "upload", defect);
}

json App::inspectAll(int cam, bool defect, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return {}; }
    Cam& c = *cams_[cam];
    std::lock_guard<std::mutex> lk(c.m);
    json out = json::array();
    for (auto& f : c.frames) {
        cv::Mat img;
        if (f == c.refName || !readFrame(c, f, img)) continue;
        json j = inspectLocked(c, cam, img, f, "manual_all", defect, false);
        out.push_back({{"frame", f}, {"score", j["score"]}, {"verdict", j["verdict"]}, {"reason", j["reason"]}, {"ms", j["ms"]}});
    }
    return out;
}

bool App::autoRoi(int cam, const std::string& frame, std::vector<cv::Point>& out, std::string& err) {
    if (!validCam(cam)) { err = "bad camera"; return false; }
    Cam& c = *cams_[cam];
    std::lock_guard<std::mutex> lk(c.m);
    cv::Mat img = c.ref;
    if (!frame.empty() && !readFrame(c, frame, img)) { err = "unknown frame"; return false; }
    if (img.empty()) { err = "no image"; return false; }
    out = cd::autoRoi(img);
    if (out.empty()) { err = "auto ROI found nothing"; return false; }
    return true;
}

bool App::frameJpeg(int cam, const std::string& frame, std::vector<uchar>& out) {
    if (!validCam(cam)) return false;
    Cam& c = *cams_[cam];
    std::lock_guard<std::mutex> lk(c.m);
    if (frame.empty()) return !c.ref.empty() && cv::imencode(".jpg", c.ref, out, {cv::IMWRITE_JPEG_QUALITY, 90});
    if (std::find(c.frames.begin(), c.frames.end(), frame) == c.frames.end()) return false;
    std::ifstream in(c.dir / frame / "frame.jpg", std::ios::binary);
    out.assign(std::istreambuf_iterator<char>(in), {});
    return !out.empty();
}

bool App::overlayJpeg(int cam, std::vector<uchar>& out) {
    if (!validCam(cam)) return false;
    std::lock_guard<std::mutex> lk(cams_[cam]->m);
    out = cams_[cam]->overlay;
    return !out.empty();
}

// ---- emulated stream: every period one frame of every camera, processed in parallel ----

void App::tick() {
    auto t0 = Clock::now();
    const bool defect = defect_;
    std::vector<std::future<double>> fut;
    for (int i = 0; i < kCameras; ++i)
        fut.push_back(std::async(std::launch::async, [this, i, defect]() -> double {
            Cam& c = *cams_[i];
            std::lock_guard<std::mutex> lk(c.m);
            if (c.frames.empty() || c.ref.empty()) return 0;
            std::string name;
            for (size_t tries = 0; tries < c.frames.size(); ++tries) {
                name = c.frames[c.cursor++ % c.frames.size()];
                if (name != c.refName) break;
            }
            cv::Mat img;
            if (!readFrame(c, name, img)) return 0;
            return inspectLocked(c, i, img, name, "emulation", defect)["ms"].get<double>();
        }));
    double maxMs = 0;
    for (auto& f : fut) maxMs = std::max(maxMs, f.get());
    batchMs_ = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    batchMaxFrameMs_ = maxMs;
    ++batch_;
}

void App::loop() {
    while (!quit_) {
        if (!running_) { std::this_thread::sleep_for(std::chrono::milliseconds(50)); continue; }
        auto t0 = Clock::now();
        tick();
        while (!quit_ && running_ &&
               std::chrono::duration<double, std::milli>(Clock::now() - t0).count() < periodMs_)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

}
