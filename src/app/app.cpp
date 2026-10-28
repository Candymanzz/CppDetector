#include "app.h"
#include "../inspect/auto_roi.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <future>
#include <iomanip>
#include <sstream>

namespace cd {

using Clock = std::chrono::steady_clock;

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

static json calibrationAxisJson(const CalibrationAxis& a) {
    return {{"p0",{a.p0.x,a.p0.y}},{"p1",{a.p1.x,a.p1.y}},{"length_mm",a.lengthMm}};
}

static json paramsToJson(const InspectParams& p) {
    json j = {{"sensitivity", p.sensitivity}, {"reject_threshold", p.rejectThreshold},
            {"shift_tolerance_x", p.shiftToleranceX}, {"shift_tolerance_y", p.shiftToleranceY},
            {"downscale", p.downscale}, {"min_blob_area", p.minBlobArea}};
    j["calibration"] = {{"horizontal",calibrationAxisJson(p.calibration.horizontal)},
                         {"vertical",calibrationAxisJson(p.calibration.vertical)}};
    j["horizontal_shift_tolerance_mm"] = p.horizontalShiftMm ? json(*p.horizontalShiftMm) : json(nullptr);
    j["vertical_shift_tolerance_mm"] = p.verticalShiftMm ? json(*p.verticalShiftMm) : json(nullptr);
    j["estimated_mm_per_pixel"] = {p.estimatedMmPerPixel.x,p.estimatedMmPerPixel.y};
    j["metric_scale_source"] = p.calibration.ready() ? "calibrated" : "estimated";
    return j;
}

static void initialiseMetricTolerances(InspectParams& p, cv::Size size) {
    const auto units = p.mmPerPixel(size);
    if (!p.horizontalShiftMm) p.horizontalShiftMm = p.shiftToleranceX*units.x;
    if (!p.verticalShiftMm) p.verticalShiftMm = p.shiftToleranceY*units.y;
}

static bool applyCalibrationAxis(const json& j, CalibrationAxis& a, bool horizontal, std::string& err) {
    if (!j.is_object()) {err="calibration axis must be an object";return false;}
    CalibrationAxis next = a;
    for (auto item : {std::make_pair("p0",&next.p0),std::make_pair("p1",&next.p1)}) {
        if (!j.contains(item.first)) continue;
        const auto& point = j[item.first];
        if (!point.is_array() || point.size()!=2 || !point[0].is_number() || !point[1].is_number()) {
            err="calibration points must be normalized [x,y]";return false;
        }
        const double x=point[0].get<double>(),y=point[1].get<double>();
        if (!std::isfinite(x)||!std::isfinite(y)||x<0||x>1||y<0||y>1) {err="calibration point outside image";return false;}
        *item.second={x,y};
    }
    if (j.contains("length_mm")) {
        if (!j["length_mm"].is_number()) {err="calibration length must be a number";return false;}
        next.lengthMm=j["length_mm"].get<double>();
        if (!std::isfinite(next.lengthMm)||next.lengthMm<0||next.lengthMm>100000||
            (next.lengthMm>0 && next.lengthMm<0.001)) {err="invalid calibration length";return false;}
    }
    if (next.lengthMm>0 && !next.valid(horizontal)) {err="calibration measurement must have nonzero span along its axis";return false;}
    a=next;return true;
}

static json roiToJson(const std::vector<cv::Point>& roi) {
    json a = json::array();
    for (auto& p : roi) a.push_back({p.x, p.y});
    return a;
}

// Validates and applies the fields present in `b`.
static bool applyParams(const json& b, InspectParams& p, std::string& err) {
    if (b.contains("reject_threshold") && b["reject_threshold"].is_number() && b["reject_threshold"] <= 0) {
        err = "reject_threshold must be greater than zero (native threshold = percent / 100)";
        return false;
    }
    auto num = [&](const char* k, double lo, double hi, auto& dst) {
        if (!b.contains(k)) return true;
        if (!b[k].is_number() || !std::isfinite(b[k].get<double>()) || b[k] < lo || b[k] > hi) { err = std::string("bad value for ") + k; return false; }
        dst = static_cast<std::remove_reference_t<decltype(dst)>>(b[k].get<double>());
        return true;
    };
    // Old configs/clients set both axes; explicit X/Y fields override the shared value.
    if (b.contains("shift_tolerance")) {
        int legacy = p.shiftToleranceX;
        if (!num("shift_tolerance", 0, 30, legacy)) return false;
        p.shiftToleranceX = p.shiftToleranceY = legacy;
    }
    if (b.contains("shift_tolerance") || b.contains("shift_tolerance_x")) p.horizontalShiftMm.reset();
    if (b.contains("shift_tolerance") || b.contains("shift_tolerance_y")) p.verticalShiftMm.reset();
    if (b.contains("calibration")) {
        const auto& c=b["calibration"];
        if (c.is_null()) {p.calibration={};p.horizontalShiftMm.reset();p.verticalShiftMm.reset();}
        else {
            if (!c.is_object()) {err="calibration must be an object";return false;}
            if (c.contains("horizontal") && !applyCalibrationAxis(c["horizontal"],p.calibration.horizontal,true,err)) return false;
            if (c.contains("vertical") && !applyCalibrationAxis(c["vertical"],p.calibration.vertical,false,err)) return false;
        }
        if (!p.calibration.ready()) {p.horizontalShiftMm.reset();p.verticalShiftMm.reset();}
    }
    for (auto item : {std::make_pair("horizontal_shift_tolerance_mm",&p.horizontalShiftMm),
                      std::make_pair("vertical_shift_tolerance_mm",&p.verticalShiftMm)}) {
        if (!b.contains(item.first) || b[item.first].is_null()) continue;
        double value=0;
        if (!num(item.first,0,30,value) || !std::isfinite(value)) {err="invalid tolerance in millimeters";return false;}
        *item.second=value;
    }
    if (b.contains("estimated_mm_per_pixel")) {
        const auto& units=b["estimated_mm_per_pixel"];
        if (!units.is_array()||units.size()!=2||!units[0].is_number()||!units[1].is_number()) {
            err="estimated_mm_per_pixel must contain two positive scales";return false;
        }
        const double x=units[0].get<double>(),y=units[1].get<double>();
        if (!std::isfinite(x)||!std::isfinite(y)||x<0.00001||y<0.00001||x>100||y>100) {
            err="invalid estimated millimeter scale";return false;
        }
        p.estimatedMmPerPixel={x,y};
    }
    return num("sensitivity", 0, 1, p.sensitivity) && num("reject_threshold", 0, 100, p.rejectThreshold) &&
           num("shift_tolerance_x", 0, 30, p.shiftToleranceX) && num("shift_tolerance_y", 0, 30, p.shiftToleranceY) &&
           num("downscale", 0.1, 1, p.downscale) &&
           num("min_blob_area", 1, 100000, p.minBlobArea);
}

static bool parseRoi(const json& a, std::vector<cv::Point>& out, std::string& err) {
    std::vector<cv::Point> next;
    if (!a.is_array() || a.size() > 1024) { err = "roi must be an array of at most 1024 points"; return false; }
    for (auto& pt : a) {
        if (!pt.is_array() || pt.size() != 2 || !pt[0].is_number() || !pt[1].is_number()) { err = "bad roi point"; return false; }
        const double x = pt[0].get<double>(), y = pt[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000) {
            err = "invalid roi coordinate"; return false;
        }
        next.emplace_back(cvRound(x), cvRound(y));
    }
    if (!next.empty() && (next.size() < 3 || std::abs(cv::contourArea(next)) < 0.5)) {
        err = "roi needs at least 3 points enclosing a nonzero area"; return false;
    }
    out = std::move(next);
    return true;
}

static json seamToJson(const SeamParams& s) {
    json pts = json::array();
    for (auto& q : s.pts) pts.push_back({cvRound(q.x), cvRound(q.y)});
    return {{"enabled", s.enabled}, {"mode", s.mode == SeamParams::Step ? "step" : "ridge"}, {"pts", pts}, {"skip", s.skip},
            {"polarity", s.polarity}, {"fit_deg", s.fitDeg}, {"smooth_win", s.smoothWin}, {"band", s.bandHalf}, {"tol_mid", s.tolMid},
            {"tol_edge", s.tolEdge}, {"edge_frac", s.edgeFrac}, {"min_contrast", s.minContrast}, {"max_gap", s.maxGap}};
}

// Validates and applies the fields present in `b` (partial update). Points are in original image px.
// Legacy configs carry the two line ends as "p0"/"p1"; they are read as a two-point "pts".
static bool applySeam(const json& b, const cv::Size& imgSize, SeamParams& s, std::string& err) {
    if (!b.is_object()) { err = "seam must be an object"; return false; }
    auto num = [&](const char* k, double lo, double hi, double& dst) {
        if (!b.contains(k)) return true;
        if (!b[k].is_number() || !std::isfinite(b[k].get<double>()) || b[k] < lo || b[k] > hi) { err = std::string("bad seam value for ") + k; return false; }
        dst = b[k].get<double>();
        return true;
    };
    auto parsePt = [&](const json& a, cv::Point2d& dst) {
        if (!a.is_array() || a.size() != 2 || !a[0].is_number() || !a[1].is_number()) { err = "bad seam point"; return false; }
        double x = a[0].get<double>(), y = a[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1000000 || y > 1000000) {
            err = "invalid seam coordinate"; return false;
        }
        if (imgSize.width > 0 && (x < 0 || y < 0 || x >= imgSize.width || y >= imgSize.height)) { err = "seam point outside image"; return false; }
        dst = {x, y};
        return true;
    };
    if (b.contains("enabled")) {
        if (!b["enabled"].is_boolean()) { err = "bad seam value for enabled"; return false; }
        s.enabled = b["enabled"].get<bool>();
    }
    if (b.contains("mode")) {
        if (!b["mode"].is_string() || (b["mode"] != "ridge" && b["mode"] != "step")) { err = "bad seam value for mode"; return false; }
        s.mode = b["mode"] == "step" ? SeamParams::Step : SeamParams::Ridge;
    }
    if (b.contains("pts")) {
        const json& a = b["pts"];
        if (!a.is_array() || a.size() > 2) { err = "position-x-y seam uses two endpoints"; return false; }
        std::vector<cv::Point2d> pts(a.size());
        for (size_t i = 0; i < a.size(); ++i)
            if (!parsePt(a[i], pts[i])) return false;
        s.pts = std::move(pts);
        s.skip.clear();                                // segment numbers refer to the old points
    } else if (b.contains("p0") && b.contains("p1")) {
        cv::Point2d p0, p1;
        if (!parsePt(b["p0"], p0) || !parsePt(b["p1"], p1)) return false;
        s.pts = (p0 == p1) ? std::vector<cv::Point2d>{} : std::vector<cv::Point2d>{p0, p1};
        s.skip.clear();
    }
    if (b.contains("skip")) {
        const json& a = b["skip"];
        if (!a.is_array() || a.size() > 64) { err = "seam skip must be an array of segment numbers"; return false; }
        std::vector<int> skip;
        for (auto& v : a) {
            if (!v.is_number_integer() || v < 0 || v > 62) { err = "bad seam skip segment"; return false; }
            skip.push_back(v.get<int>());
        }
        s.skip = std::move(skip);
    }
    double polarity = s.polarity, fitDeg = s.fitDeg;
    if (!num("polarity", -1, 1, polarity) || !num("fit_deg", 0, 6, fitDeg)) return false;
    s.polarity = int(polarity);
    s.fitDeg = int(fitDeg);
    return num("band", 5, 200, s.bandHalf) && num("tol_mid", 0.1, 50, s.tolMid) && num("tol_edge", 0.1, 50, s.tolEdge) &&
           num("edge_frac", 0, 0.5, s.edgeFrac) && num("smooth_win", 15, 400, s.smoothWin) && num("min_contrast", 1, 100, s.minContrast) && num("max_gap", 0, 1, s.maxGap);
}

App::App(fs::path sourceDir, fs::path configPath, fs::path logDir, fs::path legacyConfigPath)
    : source_(std::move(sourceDir)), cfgPath_(std::move(configPath)), legacyCfgPath_(std::move(legacyConfigPath)),
      log_(std::move(logDir)) {
    // Provisional estimates from the 1224x1024 bucket views; not a measured calibration.
    const cv::Point2d estimatedScales[kCameras] = {
        {0.25,0.33},{0.28,0.32},{0.31,0.31},{0.25,0.32},{0.27,0.34},
        {0.27,0.34},{0.25,0.33},{0.22,0.23},{0.22,0.32},{0.28,0.33}};
    for (int i = 0; i < kCameras; ++i) {
        auto c = std::make_unique<Cam>();
        c->p.estimatedMmPerPixel=estimatedScales[i];
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
    if (importedLegacy_) save(); // new profile only; the old config and uploaded references stay intact
    worker_ = std::thread(&App::loop, this);
}

App::~App() {
    quit_ = true;
    if (worker_.joinable()) worker_.join();
}

void App::rebuild(Cam& c) {
    initialiseMetricTolerances(c.p,c.ref.size());
    if (!c.ref.empty()) c.insp.setReference(c.ref, c.roi, c.p);
}

void App::load() {
    fs::path readPath = cfgPath_;
    if (!fs::exists(cfgPath_) && !legacyCfgPath_.empty() && fs::exists(legacyCfgPath_)) {
        readPath = legacyCfgPath_;
        importedLegacy_ = true;
    }
    std::ifstream in(readPath);
    if (!in) return;
    json j = json::parse(in, nullptr, false);
    if (j.is_discarded()) return;
    std::string err;
    if (j.contains("period_ms") && j["period_ms"].is_number()) periodMs_ = std::clamp(j["period_ms"].get<int>(), 100, 60000);
    if (!importedLegacy_ && j.contains("defect") && j["defect"].is_boolean()) defect_ = j["defect"].get<bool>();
    if (!j.contains("cameras") || !j["cameras"].is_array()) return;
    for (int i = 0; i < kCameras && i < (int)j["cameras"].size(); ++i) {
        const json& cj = j["cameras"][i];
        Cam& c = *cams_[i];
        // A clean native baseline, not a reinterpretation of the old amplified score / working-px settings.
        if (!importedLegacy_) {
            InspectParams parsed = c.p;
            if (applyParams(cj, parsed, err)) c.p = parsed;
        }
        if (cj.contains("roi")) parseRoi(cj["roi"], c.roi, err);
        if (cj.contains("seam")) {
            json seam = cj["seam"];
            if (seam.contains("pts") && seam["pts"].is_array() && seam["pts"].size() > 2)
                seam["pts"] = {seam["pts"].front(), seam["pts"].back()};
            applySeam(seam, cv::Size(), c.seam, err);
        }
        if (c.seam.mode == SeamParams::Step) c.seam = SeamParams();
        c.seam.skip.clear();
        c.seam.mode = SeamParams::Ridge;
        c.edge.enabled = false; // there is no edge check in the source production detector
        c.edge.mode = SeamParams::Step;
        std::string ref = cj.value("reference", "");
        if (ref == "upload") {
            if (!fs::exists(refImagePath(i)) && !legacyCfgPath_.empty()) {
                const auto oldImage = legacyCfgPath_.parent_path() /
                    ("ref_" + legacyCfgPath_.stem().string() + "_cam" + std::to_string(i) + ".png");
                if (fs::exists(oldImage)) {
                    fs::create_directories(cfgPath_.parent_path());
                    fs::copy_file(oldImage, refImagePath(i), fs::copy_options::skip_existing);
                }
            }
            cv::Mat m = cv::imread(refImagePath(i).string());
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
    j["inspection_engine"] = kInspectionEngine;
    j["engine_source_commit"] = kEngineSourceCommit;
    j["period_ms"] = periodMs_.load();
    j["defect"] = defect_.load();
    j["cameras"] = json::array();
    for (auto& c : cams_) {
        std::lock_guard<std::mutex> cl(c->m);
        json cj = paramsToJson(c->p);
        cj["reference"] = c->refName;
        cj["roi"] = roiToJson(c->roi);
        cj["seam"] = seamToJson(c->seam);
        cj["edge"] = seamToJson(c->edge);
        j["cameras"].push_back(cj);
    }
    fs::create_directories(cfgPath_.parent_path());
    std::ofstream(cfgPath_) << j.dump(2);
}

json App::state() {
    json j = status();
    j["config_path"] = cfgPath_.string();
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
        cj["seam"] = seamToJson(c.seam);
        cj["edge"] = seamToJson(c.edge);
        cj["width"] = c.ref.cols;
        cj["height"] = c.ref.rows;
        cj["calibration_ready"] = c.p.calibration.ready();
        const auto units=c.p.mmPerPixel(c.ref.size());
        cj["mm_per_pixel"]={units.x,units.y};
        j["cameras_info"].push_back(cj);
    }
    return j;
}

json App::status() {
    json j;
    j["inspection_engine"] = kInspectionEngine;
    j["engine_source_commit"] = kEngineSourceCommit;
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
    if (b.contains("edge")) { err = "edge check is not part of position-x-y"; return false; }
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
        SeamParams seam = c.seam;
        if (b.contains("seam") && !applySeam(b["seam"], c.ref.size(), seam, err)) return false;
        if (seam.enabled && (c.ref.empty() || seam.pts.size() != 2 ||
            cv::norm(seam.pts[1] - seam.pts[0]) < 50)) {
            err = "enabled seam needs two endpoints at least 50 pixels apart"; return false;
        }
        seam.mode = SeamParams::Ridge;
        c.p = p;
        c.roi = roi;
        c.seam = seam;
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
        if (!c.ref.empty() && m.size() != c.ref.size()) { c.roi.clear(); c.seam = SeamParams(); }
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
        if (!c.ref.empty() && img.size() != c.ref.size()) { c.roi.clear(); c.seam = SeamParams(); }
        c.ref = img;
        c.refName = "upload";
        fs::create_directories(cfgPath_.parent_path());
        cv::imwrite(refImagePath(cam).string(), img);
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

// Geometry line detail: the reference line (white), the measured edge (green, red where over tolerance), rows with
// no edge found (magenta) and skipped segments (orange) on the image, and a deviation graph over the bottom strip.
static void drawSeamDetail(cv::Mat& vis, const SeamParams& sp, const SeamResult& sr, int slot, const char* name) {
    const int N = int(sr.rowPos.size());
    if (N < 2) return;
    const cv::Scalar white(255, 255, 255), green(0, 230, 0), red(0, 0, 255), magenta(255, 0, 255), orange(0, 150, 255);
    auto over = [&](int i) { return std::fabs(sr.rowDev[i]) > sr.rowTol[i]; };

    // runs of evaluated rows -> polylines
    auto runs = [&](auto&& draw) {
        std::vector<cv::Point> ref, edge;
        for (int i = 0; i <= N; ++i) {
            if (i < N && sr.rowState[i] == 0) {
                ref.emplace_back(sr.rowRef[i]);
                edge.emplace_back(sr.rowEdge[i]);
            } else if (!ref.empty()) {
                draw(ref, edge);
                ref.clear();
                edge.clear();
            }
        }
    };
    runs([&](const std::vector<cv::Point>& ref, const std::vector<cv::Point>& edge) {
        if (ref.size() > 1) cv::polylines(vis, ref, false, white, 1, cv::LINE_AA);
        if (edge.size() > 1) cv::polylines(vis, edge, false, green, 1, cv::LINE_AA);
    });
    for (int i = 0; i < N; ++i) {
        if (sr.rowState[i] == 3 && i % 3 == 0) cv::circle(vis, sr.rowPos[i], 1, magenta, -1);
        else if (sr.rowState[i] == 1) cv::circle(vis, sr.rowPos[i], 1, orange, -1);
        else if (sr.rowState[i] == 0 && over(i)) cv::circle(vis, sr.rowEdge[i], 2, red, -1);
    }

    // deviation graph
    const int H = std::min(150, vis.rows / 3), M = 8;
    cv::Mat panel = vis(cv::Rect(0, vis.rows - H * (slot + 1), vis.cols, H));
    cv::Mat dark(panel.size(), panel.type(), cv::Scalar(20, 20, 20));
    cv::addWeighted(panel, 0.3, dark, 0.7, 0, panel);
    const double tolMax = std::max(sp.tolMid, sp.tolEdge);
    const double range = std::max(3.0 * tolMax, 3.0);          // px shown above and below zero
    const double mid = H / 2.0, scale = (H / 2.0 - 16) / range;
    auto X = [&](int i) { return M + (vis.cols - 2 * M) * double(i) / (N - 1); };
    auto Y = [&](double d) { return mid - std::clamp(d, -range, range) * scale; };
    cv::line(panel, {M, int(mid)}, {vis.cols - M, int(mid)}, cv::Scalar(120, 120, 120), 1);
    std::vector<cv::Point> up, lo;
    int worst = -1;
    for (int i = 0; i < N; ++i) {
        up.emplace_back(int(X(i)), int(Y(sr.rowTol[i])));
        lo.emplace_back(int(X(i)), int(Y(-sr.rowTol[i])));
        if (sr.rowState[i] == 0 && (worst < 0 || std::fabs(sr.rowDev[i]) > std::fabs(sr.rowDev[worst]))) worst = i;
    }
    cv::polylines(panel, up, false, cv::Scalar(0, 215, 255), 1);
    cv::polylines(panel, lo, false, cv::Scalar(0, 215, 255), 1);
    for (int i = 0; i < N; ++i) {
        const int x = int(X(i));
        if (sr.rowState[i] == 1) cv::line(panel, {x, 0}, {x, 5}, orange, 1);
        else if (sr.rowState[i] == 3) cv::line(panel, {x, H - 6}, {x, H - 1}, magenta, 1);
        else if (sr.rowState[i] == 0) cv::circle(panel, {x, int(Y(sr.rowDev[i]))}, 1, over(i) ? red : green, -1);
    }
    char txt[160];
    std::snprintf(txt, sizeof txt, "%s: deviation, px (yellow = tolerance, scale +-%.1f)   magenta = not found, orange = skipped", name, range);
    cv::putText(panel, txt, {M, 12}, cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(220, 220, 220), 1, cv::LINE_AA);
    if (worst >= 0) {
        std::snprintf(txt, sizeof txt, "max %.2f px", std::fabs(sr.rowDev[worst]));
        cv::line(panel, {int(X(worst)), 14}, {int(X(worst)), H - 8}, cv::Scalar(255, 255, 255), 1);
        cv::putText(panel, txt, {std::min(int(X(worst)) + 4, vis.cols - 90), 26}, cv::FONT_HERSHEY_SIMPLEX, 0.45, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }
}

static std::vector<uchar> renderOverlay(const cv::Mat& img, const InspectResult& r, const std::vector<cv::Point>& roi,
                                        const SeamParams& sp, const SeamResult& sr, const SeamParams& ep,
                                        const SeamResult& er) {
    cv::Mat vis = img.clone();
    if (!r.heatmap.empty()) {
        cv::Mat hm, heat;
        cv::resize(r.heatmap, hm, img.size());
        // Native buildHeatmap already normalises energy and adds the same halo as position-x-y.
        cv::applyColorMap(hm, heat, cv::COLORMAP_JET);
        cv::Mat mask = hm > 0;
        cv::Mat blended;
        cv::addWeighted(vis, 0.45, heat, 0.55, 0, blended);
        blended.copyTo(vis, mask);
    }
    if (roi.size() >= 3) cv::polylines(vis, roi, true, cv::Scalar(0, 255, 255), 2);
    for (auto& z : r.zones) cv::rectangle(vis, z, cv::Scalar(0, 0, 255), 2);
    int slot = 0;
    auto zone = [&](const SeamParams& zp, const SeamResult& zr, const char* name, const cv::Scalar& col) {
        if (!zp.enabled || zr.path.size() < 2) return;
        // search band and axis, then the measured line and the deviation graph
        std::vector<cv::Point> axis, sideA, sideB;
        for (size_t i = 0; i < zr.path.size(); ++i) {
            cv::Point2f n = zr.pathNormal[i] * float(zp.bandHalf);
            axis.emplace_back(zr.path[i]);
            sideA.emplace_back(zr.path[i] + n);
            sideB.emplace_back(zr.path[i] - n);
        }
        cv::polylines(vis, axis, false, col, 1);
        cv::polylines(vis, sideA, false, col, 1);
        cv::polylines(vis, sideB, false, col, 1);
        cv::line(vis, sideA.front(), sideB.front(), col, 1);
        cv::line(vis, sideA.back(), sideB.back(), col, 1);
        std::vector<cv::Point> centres;
        for (const auto& p : zr.centerPts) centres.emplace_back(cvRound(p.x), cvRound(p.y));
        if (centres.size() > 1) cv::polylines(vis, centres, false, cv::Scalar(0, 230, 0), 1);
        for (const auto& p : zr.badPts) cv::circle(vis, p, 2, cv::Scalar(0, 0, 255), -1);
        drawSeamDetail(vis, zp, zr, slot++, name);
    };
    zone(sp, sr, "seam", cv::Scalar(255, 255, 0));
    zone(ep, er, "edge", cv::Scalar(255, 128, 200));
    std::vector<uchar> buf;
    cv::imencode(".jpg", vis, buf, {cv::IMWRITE_JPEG_QUALITY, 80});
    return buf;
}

// One debug set per manual inspection with the geometry line on: <date_time>_camN_<frame>.{txt,png,jpg} in logs/seam
// (parameters, result, per-row deviation table, the exact input image and the overlay). Only the newest kKeep sets stay.
static void writeSeamDebug(const fs::path& dir, int id, const std::string& frame, const char* zone, const std::string& source,
                           const cv::Mat& img, const SeamParams& sp, const SeamResult& sr,
                           const std::vector<uchar>& overlay, const json& verdict) {
    constexpr size_t kKeep = 60;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const int64_t ms = nowMs();
    std::time_t tt = std::time_t(ms / 1000);
    std::tm tm{};
    localtime_s(&tm, &tt);
    std::ostringstream stem;
    stem << std::put_time(&tm, "%Y-%m-%d_%H-%M-%S") << "_" << std::setw(3) << std::setfill('0') << ms % 1000 << "_cam" << id << "_" << frame << "_" << zone;
    const fs::path base = dir / stem.str();

    std::ofstream t(base.string() + ".txt");
    t << "zone " << zone << ", camera " << id << ", frame " << frame << ", source " << source << "\n";
    t << "verdict: " << verdict.value("verdict", "") << ", reason: " << verdict.value("reason", "") << "\n";
    t << "params: " << seamToJson(sp).dump() << "\n";
    t << "result: found=" << sr.found << " reject=" << sr.reject << " gap_reject=" << sr.gapReject << " coverage=" << sr.coverage
      << " longest_gap=" << sr.longestGap << " ratio=" << sr.ratio << " max_dev=" << sr.maxDev << " worst_t=" << sr.worstT
      << " ms=" << sr.ms << " bad_points=" << sr.badPts.size() << "\n";
    if (sr.rowPos.empty()) t << "line was not built (disabled, fewer than 2 points, shorter than 50 px or outside the image)\n";
    t << "state: 0 evaluated, 1 skipped segment, 2 line end ignored, 3 no edge found\n";
    t << "row x y state dev_px\n";
    for (size_t i = 0; i < sr.rowPos.size(); ++i) {
        t << i << " " << std::fixed << std::setprecision(1) << sr.rowPos[i].x << " " << sr.rowPos[i].y << " " << int(sr.rowState[i]) << " ";
        if (std::isnan(sr.rowDev[i])) t << "-"; else t << std::setprecision(2) << sr.rowDev[i];
        t << "\n";
    }
    t.close();
    cv::imwrite(base.string() + ".png", img);
    std::ofstream(base.string() + ".jpg", std::ios::binary).write(reinterpret_cast<const char*>(overlay.data()), overlay.size());

    std::vector<fs::path> sets;
    for (auto& e : fs::directory_iterator(dir, ec))
        if (e.path().extension() == ".txt") sets.push_back(e.path());
    std::sort(sets.begin(), sets.end());
    for (size_t i = 0; i + kKeep < sets.size(); ++i)
        for (const char* ext : {".txt", ".png", ".jpg"}) fs::remove(fs::path(sets[i]).replace_extension(ext), ec);
}

json App::inspectLocked(Cam& c, int id, const cv::Mat& imgIn, const std::string& name,
                        const std::string& source, bool defect, bool keepOverlay) {
    cv::Mat img = imgIn;
    if (defect) { img = imgIn.clone(); drawDefect(img, c.roi); }
    InspectResult r = c.insp.inspect(img, c.p, &c.seam);
    const SeamResult& sr = r.seam;
    const SeamResult er; // retained only for the overlay signature; never measured or used for verdicts
    const bool reject = r.reject;
    const std::string& reason = r.reason;

    json j;
    j["cam"] = id;
    j["frame"] = name;
    j["source"] = source;
    j["defect_injected"] = defect;
    j["inspection_engine"] = kInspectionEngine;
    j["engine_source_commit"] = kEngineSourceCommit;
    j["native_settings"] = r.settings.toJson();
    j["score_multiplier"] = cas::kScoreMultiplier;
    j["anomaly_score"] = r.native.anomalyScore;
    j["area_percent"] = r.native.areaPercent;
    j["score"] = std::round(r.score * 1000) / 1000;
    j["reject"] = reject;
    j["verdict"] = reject ? "REJECT" : (r.reason == "no_reference" || r.reason == "size_mismatch" ? "NO_REFERENCE" : "OK");
    j["reason"] = reason;
    auto zoneJson = [](const SeamResult& z) {
        return json{{"found", z.found}, {"reject", z.reject}, {"ratio", std::round(z.ratio * 100) / 100},
                    {"max_dev", std::round(z.maxDev * 100) / 100}, {"worst_t", std::round(z.worstT * 1000) / 1000},
                    {"coverage", std::round(z.coverage * 100) / 100},
                    {"ms", std::round(z.ms * 100) / 100}};
    };
    if (r.native.seamEnabled) j["seam"] = zoneJson(sr);
    j["threshold"] = c.p.rejectThreshold;
    j["sensitivity"] = c.p.sensitivity;
    j["shift"] = {std::round(r.shift.x * 100) / 100, std::round(r.shift.y * 100) / 100};
    j["shift_raw"] = {r.native.rawShift.x, r.native.rawShift.y};
    j["shift_response"] = r.native.shiftResponse;
    j["shift_units"] = "original_px";
    j["shift_mm"] = nullptr;
    j["shift_raw_mm"] = nullptr;
    j["shift_limit_mm"] = nullptr;
    j["effective_shift_tolerance_mm"] = nullptr;
    j["calibration_ready"] = c.p.calibration.ready();
    j["metric_scale_source"] = c.p.calibration.ready() ? "calibrated" : "estimated";
    if (!c.ref.empty()) {
        const auto units=c.p.mmPerPixel(c.ref.size());
        j["mm_per_pixel"]={units.x,units.y};
        j["shift_mm"]={r.shift.x*units.x,r.shift.y*units.y};
        j["shift_raw_mm"]={r.native.rawShift.x*units.x,r.native.rawShift.y*units.y};
        j["shift_limit_mm"]={r.shiftLimit.x*units.x,r.shiftLimit.y*units.y};
        j["horizontal_shift_tolerance_mm"]=c.p.horizontalShiftMm ? json(*c.p.horizontalShiftMm) : json(nullptr);
        j["vertical_shift_tolerance_mm"]=c.p.verticalShiftMm ? json(*c.p.verticalShiftMm) : json(nullptr);
        if (r.native.scale>0) {
            j["effective_shift_tolerance_mm"]={(r.envelopeKernel.width-1)/2*units.x/r.native.scale,
                                               (r.envelopeKernel.height-1)/2*units.y/r.native.scale};
            j["analysis_pixel_mm"]={units.x/r.native.scale,units.y/r.native.scale};
        }
    }
    j["shift_tolerance_x"] = r.settings.shiftToleranceX;
    j["shift_tolerance_y"] = r.settings.shiftToleranceY;
    j["shift_limit"] = {r.shiftLimit.x, r.shiftLimit.y};
    j["envelope_kernel"] = {r.envelopeKernel.width, r.envelopeKernel.height};
    j["downscale"] = c.p.downscale;
    j["zones"] = json::array();
    for (auto& z : r.zones) j["zones"].push_back({z.x, z.y, z.width, z.height});
    j["ms"] = std::round(r.ms * 100) / 100;
    j["align_ms"] = r.native.alignMs;
    j["diff_ms"] = r.native.diffMs;
    j["heatmap_ms"] = r.heatmapMs;
    j["ts"] = nowMs();

    if (keepOverlay) {
        c.overlay = renderOverlay(img, r, c.roi, c.seam, sr, c.edge, er);
        j["ver"] = ++c.ver;
        // Legacy polyline/edge debug files do not describe the native seam calculation; keep JSON diagnostics.
    }
    c.last = j;

    // reject artifacts: always for manual runs, throttled for the emulated stream
    json rec = j;
    if (reject && keepOverlay) {
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

namespace cd {

// Extra test images (e.g. real labelled shots) live next to the camera folders.
std::vector<std::string> App::testFiles() const {
    std::vector<std::string> v;
    std::error_code ec;
    for (auto& d : fs::directory_iterator(source_.parent_path() / "test-pr-ph", ec)) {
        std::string ext = d.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (d.is_regular_file() && (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp"))
            v.push_back(d.path().filename().string());
    }
    std::sort(v.begin(), v.end());
    return v;
}

bool App::readTestFile(const std::string& name, cv::Mat& out) const {
    auto list = testFiles();
    if (std::find(list.begin(), list.end(), name) == list.end()) return false;   // listing only: no path tricks
    out = cv::imread((source_.parent_path() / "test-pr-ph" / name).string());
    return !out.empty();
}

}
