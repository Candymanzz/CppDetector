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
        if (!b[k].is_number() || b[k] < lo || b[k] > hi) { err = std::string("bad seam value for ") + k; return false; }
        dst = b[k].get<double>();
        return true;
    };
    auto parsePt = [&](const json& a, cv::Point2d& dst) {
        if (!a.is_array() || a.size() != 2 || !a[0].is_number() || !a[1].is_number()) { err = "bad seam point"; return false; }
        double x = a[0].get<double>(), y = a[1].get<double>();
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
        if (!a.is_array() || a.size() > 64) { err = "seam pts must be an array of up to 64 points"; return false; }
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
        if (cj.contains("seam")) applySeam(cj["seam"], cv::Size(), c.seam, err);
        if (cj.contains("edge")) applySeam(cj["edge"], cv::Size(), c.edge, err);
        else if (c.seam.mode == SeamParams::Step) {      // older config: the single zone was used for the edge
            c.edge = c.seam;
            c.seam = SeamParams();
        }
        c.seam.mode = SeamParams::Ridge;
        c.edge.mode = SeamParams::Step;
        std::string ref = cj.value("reference", "");
        if (ref == "upload") {
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
        SeamParams seam = c.seam, edge = c.edge;
        if (b.contains("seam") && !applySeam(b["seam"], c.ref.size(), seam, err)) return false;
        if (b.contains("edge") && !applySeam(b["edge"], c.ref.size(), edge, err)) return false;
        seam.mode = SeamParams::Ridge;
        edge.mode = SeamParams::Step;
        c.p = p;
        c.roi = roi;
        c.seam = seam;
        c.edge = edge;
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
    InspectResult r = c.insp.inspect(img, c.p);
    SeamResult sr = measureSeam(img, c.seam);
    SeamResult er = measureSeam(img, c.edge);
    // each geometry zone adds its own reject reason; an existing reason of the main inspection stays the primary one
    const bool refOk = r.reason != "no_reference" && r.reason != "size_mismatch";
    const bool seamReject = sr.reject && refOk, edgeReject = er.reject && refOk;
    const bool reject = r.reject || seamReject || edgeReject;
    const std::string reason = !r.reason.empty() ? r.reason : seamReject ? "seam_deviation" : edgeReject ? "edge_deviation" : "";

    json j;
    j["cam"] = id;
    j["frame"] = name;
    j["source"] = source;
    j["defect_injected"] = defect;
    j["score"] = std::round(r.score * 1000) / 1000;
    j["reject"] = reject;
    j["verdict"] = reject ? "REJECT" : (r.reason == "no_reference" || r.reason == "size_mismatch" ? "NO_REFERENCE" : "OK");
    j["reason"] = reason;
    auto zoneJson = [](const SeamResult& z) {
        return json{{"found", z.found}, {"reject", z.reject}, {"ratio", std::round(z.ratio * 100) / 100},
                    {"max_dev", std::round(z.maxDev * 100) / 100}, {"worst_t", std::round(z.worstT * 1000) / 1000},
                    {"coverage", std::round(z.coverage * 100) / 100}, {"gap", std::round(z.longestGap * 1000) / 1000},
                    {"noise", std::round(z.noise * 100) / 100}, {"gap_reject", z.gapReject},
                    {"ms", std::round(z.ms * 100) / 100}};
    };
    if (c.seam.enabled) j["seam"] = zoneJson(sr);
    if (c.edge.enabled) j["edge"] = zoneJson(er);
    j["threshold"] = c.p.rejectThreshold;
    j["sensitivity"] = c.p.sensitivity;
    j["shift"] = {std::round(r.shift.x * 100) / 100, std::round(r.shift.y * 100) / 100};
    j["zones"] = json::array();
    for (auto& z : r.zones) j["zones"].push_back({z.x, z.y, z.width, z.height});
    j["ms"] = std::round((r.ms + sr.ms + er.ms) * 100) / 100;
    j["ts"] = nowMs();

    if (keepOverlay) {
        c.overlay = renderOverlay(img, r, c.roi, c.seam, sr, c.edge, er);
        j["ver"] = ++c.ver;
        const fs::path dbg = log_.rejectsDir().parent_path() / "seam";
        if (c.seam.enabled) writeSeamDebug(dbg, id, name, "seam", source, img, c.seam, sr, c.overlay, j);
        if (c.edge.enabled) writeSeamDebug(dbg, id, name, "edge", source, img, c.edge, er, c.overlay, j);
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
