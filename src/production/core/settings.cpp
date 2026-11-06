#include "settings.h"
#include <fstream>
#include <algorithm>

namespace cas {

namespace {
json axisJson(const CalibrationAxis& axis) {
    return {{"p0", {axis.p0.x, axis.p0.y}}, {"p1", {axis.p1.x, axis.p1.y}}, {"length_mm", axis.lengthMm}};
}
}

cv::Point2d Settings::mmPerPixel(cv::Size size) const {
    if (size.empty()) size = {1224, 1024};
    if (calibration.ready()) return calibration.mmPerPixel(size);
    return {estimatedMmPerPixel.x * 1224.0 / size.width, estimatedMmPerPixel.y * 1024.0 / size.height};
}

Settings Settings::withMetricDefaults(int cameraId) const {
    Settings s = *this;
    static const cv::Point2d estimates[] = {{.25,.33},{.28,.32},{.31,.31},{.25,.32},{.27,.34},
                                          {.27,.34},{.25,.33},{.22,.23},{.22,.32},{.28,.33}};
    if (!s.estimatedScaleExplicit && cameraId >= 0 && cameraId < 10) s.estimatedMmPerPixel = estimates[cameraId];
    const auto units = s.mmPerPixel({1224, 1024});
    // Migrate the existing global limits, independently of local comparison relaxation.
    if (!s.horizontalShiftMm) {
        s.horizontalShiftMm = std::min(10.0, s.maxAlignmentShiftX * units.x);
        s.maxAlignmentShiftX = *s.horizontalShiftMm / units.x;
    }
    if (!s.verticalShiftMm) {
        s.verticalShiftMm = std::min(10.0, s.maxAlignmentShiftY * units.y);
        s.maxAlignmentShiftY = *s.verticalShiftMm / units.y;
    }
    return s;
}

Settings Settings::forFrame(cv::Size size) const {
    Settings s = *this;
    const auto units = mmPerPixel(size);
    if (horizontalShiftMm) s.maxAlignmentShiftX = *horizontalShiftMm / units.x;
    if (verticalShiftMm) s.maxAlignmentShiftY = *verticalShiftMm / units.y;
    return s;
}

json SeamSettings::toJson() const {
    return {{"enabled", enabled}, {"p0", {p0x, p0y}}, {"p1", {p1x, p1y}}, {"band", band},
            {"tol_mid", tolMid}, {"tol_edge", tolEdge}, {"edge_frac", edgeFrac}};
}

json Settings::toJson() const {
    json j = {{"sensitivity", sensitivity}, {"threshold", threshold},
            {"shift_tolerance_x", shiftToleranceX}, {"shift_tolerance_y", shiftToleranceY},
            {"max_alignment_shift_x", maxAlignmentShiftX}, {"max_alignment_shift_y", maxAlignmentShiftY},
            {"learning_robust_enabled", learningRobustEnabled}, {"learning_shadow_enabled", learningShadowEnabled},
            {"learning_search_radius", learningSearchRadius}, {"min_blob_area", minBlobArea}, {"inspect_scale", inspectScale}, {"seam", seam.toJson()}};
    j["horizontal_shift_tolerance_mm"] = horizontalShiftMm ? json(*horizontalShiftMm) : json(nullptr);
    j["vertical_shift_tolerance_mm"] = verticalShiftMm ? json(*verticalShiftMm) : json(nullptr);
    j["estimated_mm_per_pixel"] = {estimatedMmPerPixel.x, estimatedMmPerPixel.y};
    j["metric_reference_size"] = {1224, 1024};
    j["metric_scale_source"] = calibration.ready() ? "calibrated" : "estimated";
    j["calibration"] = {{"horizontal", axisJson(calibration.horizontal)}, {"vertical", axisJson(calibration.vertical)}};
    return j;
}

namespace {

// Reads j[key] into `out` when present; validates the range.
bool readNumber(const json& j, const char* key, double lo, double hi, bool loExclusive, double& out, std::string& err) {
    if (!j.contains(key)) return true;
    const json& v = j[key];
    if (!v.is_number()) { err = std::string(key) + " must be a number"; return false; }
    double d = v.get<double>();
    if (!std::isfinite(d) || d > hi || d < lo || (loExclusive && d == lo)) {
        err = std::string(key) + " out of range";
        return false;
    }
    out = d;
    return true;
}


// Reads a normalised point [x, y] (0..1) into x/y when present.
bool readPoint(const json& j, const char* key, double& x, double& y, std::string& err) {
    if (!j.contains(key)) return true;
    const json& a = j[key];
    if (!a.is_array() || a.size() != 2 || !a[0].is_number() || !a[1].is_number()) {
        err = std::string(key) + " must be [x, y]";
        return false;
    }
    double px = a[0].get<double>(), py = a[1].get<double>();
    if (!std::isfinite(px) || !std::isfinite(py) || px < 0 || px > 1 || py < 0 || py > 1) { err = std::string(key) + " out of range"; return false; }
    x = px;
    y = py;
    return true;
}

}

namespace {
bool readCalibrationAxis(const json& j, CalibrationAxis& axis, bool horizontal, std::string& err) {
    if (!j.is_object()) { err = "calibration axis must be an object"; return false; }
    CalibrationAxis next = axis;
    if (!readPoint(j, "p0", next.p0.x, next.p0.y, err) || !readPoint(j, "p1", next.p1.x, next.p1.y, err) ||
        !readNumber(j, "length_mm", 0, 100000, false, next.lengthMm, err)) return false;
    if (next.lengthMm > 0 && (next.lengthMm < .001 || !next.valid(horizontal))) {
        err = "calibration measurement needs a positive axis span and length"; return false;
    }
    axis = next;
    return true;
}
}

std::string SeamSettings::applyJson(const json& j) {
    if (!j.is_object()) return "seam must be a JSON object";
    SeamSettings t = *this;
    std::string err;
    if (j.contains("enabled")) {
        if (!j["enabled"].is_boolean()) return "seam.enabled must be a boolean";
        t.enabled = j["enabled"].get<bool>();
    }
    if (!readPoint(j, "p0", t.p0x, t.p0y, err) || !readPoint(j, "p1", t.p1x, t.p1y, err)) return "seam." + err;
    if (!readNumber(j, "band", 5, 200, false, t.band, err) || !readNumber(j, "tol_mid", 0.1, 50, false, t.tolMid, err) ||
        !readNumber(j, "tol_edge", 0.1, 50, false, t.tolEdge, err) || !readNumber(j, "edge_frac", 0, 0.5, false, t.edgeFrac, err))
        return "seam." + err;
    *this = t;
    return "";
}

std::string Settings::applyJson(const json& j) {
    if (!j.is_object()) return "settings must be a JSON object";
    Settings tmp = *this;
    std::string err;
    if (j.contains("max_alignment_shift_x")) tmp.horizontalShiftMm.reset();
    if (j.contains("max_alignment_shift_y")) tmp.verticalShiftMm.reset();
    double v = tmp.sensitivity;
    if (!readNumber(j, "sensitivity", 0, 1, false, v, err)) return err;
    tmp.sensitivity = v;
    v = tmp.threshold;
    if (!readNumber(j, "threshold", 0, 1, true, v, err)) return err;
    tmp.threshold = v;
    // Old profiles/clients set both axes. Explicit axis values take precedence.
    if (j.contains("shift_tolerance")) {
        v = tmp.shiftToleranceX;
        if (!readNumber(j, "shift_tolerance", 0, 30, false, v, err)) return err;
        tmp.shiftToleranceX = tmp.shiftToleranceY = static_cast<int>(v);
    }
    v = tmp.shiftToleranceX;
    if (!readNumber(j, "shift_tolerance_x", 0, 30, false, v, err)) return err;
    tmp.shiftToleranceX = static_cast<int>(v);
    v = tmp.shiftToleranceY;
    if (!readNumber(j, "shift_tolerance_y", 0, 30, false, v, err)) return err;
    tmp.shiftToleranceY = static_cast<int>(v);
    if (!readNumber(j, "max_alignment_shift_x", 0, 160, false, tmp.maxAlignmentShiftX, err) ||
        !readNumber(j, "max_alignment_shift_y", 0, 160, false, tmp.maxAlignmentShiftY, err)) return err;
    for (auto field : {std::make_pair("horizontal_shift_tolerance_mm", &tmp.horizontalShiftMm),
                       std::make_pair("vertical_shift_tolerance_mm", &tmp.verticalShiftMm)}) {
        if (!j.contains(field.first)) continue;
        if (j[field.first].is_null()) field.second->reset();
        else {
            double value = 0;
            if (!readNumber(j, field.first, 0, 10, false, value, err)) return err;
            *field.second = value;
        }
    }
    if (j.contains("estimated_mm_per_pixel")) {
        const auto& units = j["estimated_mm_per_pixel"];
        if (!units.is_array() || units.size() != 2 || !units[0].is_number() || !units[1].is_number())
            return "estimated_mm_per_pixel must contain two positive scales";
        const double x = units[0].get<double>(), y = units[1].get<double>();
        if (!std::isfinite(x) || !std::isfinite(y) || x < .00001 || y < .00001 || x > 100 || y > 100)
            return "estimated_mm_per_pixel out of range";
        tmp.estimatedMmPerPixel = {x, y};
        tmp.estimatedScaleExplicit = true;
    }
    if (j.contains("calibration")) {
        const auto& c = j["calibration"];
        if (c.is_null()) tmp.calibration = {};
        else {
            if (!c.is_object()) return "calibration must be an object";
            if (c.contains("horizontal") && !readCalibrationAxis(c["horizontal"], tmp.calibration.horizontal, true, err)) return err;
            if (c.contains("vertical") && !readCalibrationAxis(c["vertical"], tmp.calibration.vertical, false, err)) return err;
        }
    }
    v = tmp.minBlobArea;
    if (!readNumber(j, "min_blob_area", 1, 100000, false, v, err)) return err;
    tmp.minBlobArea = static_cast<int>(v);
    v = tmp.inspectScale;
    if (!readNumber(j, "inspect_scale", 0.1, 1, false, v, err)) return err;
    tmp.inspectScale = v;
    for (auto field : {"learning_robust_enabled", "learning_shadow_enabled"}) {
        if (j.contains(field) && !j[field].is_boolean()) return std::string(field)+" must be a boolean";
    }
    if(j.contains("learning_robust_enabled")) tmp.learningRobustEnabled=j["learning_robust_enabled"].get<bool>();
    if(j.contains("learning_shadow_enabled")) tmp.learningShadowEnabled=j["learning_shadow_enabled"].get<bool>();
    v=tmp.learningSearchRadius;
    if(j.contains("learning_search_radius") && !j["learning_search_radius"].is_number_integer()) return "learning_search_radius must be an integer";
    if(!readNumber(j,"learning_search_radius",0,64,false,v,err)) return err;
    tmp.learningSearchRadius=static_cast<int>(v);
    if (j.contains("seam")) {
        err = tmp.seam.applyJson(j["seam"]);
        if (!err.empty()) return err;
    }
    *this = tmp;
    return "";
}

SettingsPatch SettingsPatch::fromJson(const json& j) {
    SettingsPatch p;
    if (!j.is_object()) return p;
    Settings probe;   // range validation is shared with applyJson; invalid values are ignored
    auto take = [&](const char* key, auto& dst) {
        if (!j.contains(key) || !j[key].is_number()) return;
        json one;
        one[key] = j[key];
        if (probe.applyJson(one).empty()) dst = static_cast<std::decay_t<decltype(*dst)>>(j[key].get<double>());
    };
    take("sensitivity", p.sensitivity);
    take("threshold", p.threshold);
    take("inspect_scale", p.inspectScale);
    take("shift_tolerance", p.shiftToleranceX);
    p.shiftToleranceY = p.shiftToleranceX;
    take("shift_tolerance_x", p.shiftToleranceX);
    take("shift_tolerance_y", p.shiftToleranceY);
    take("max_alignment_shift_x", p.maxAlignmentShiftX);
    take("max_alignment_shift_y", p.maxAlignmentShiftY);
    take("min_blob_area", p.minBlobArea);
    p.metric = json::object();
    for (const char* key : {"horizontal_shift_tolerance_mm", "vertical_shift_tolerance_mm", "estimated_mm_per_pixel", "calibration"}) {
        if (!j.contains(key)) continue;
        Settings candidate;
        if (candidate.applyJson({{key, j[key]}}).empty()) p.metric[key] = j[key];
    }
    take("learning_search_radius", p.learningSearchRadius);
    if(j.contains("learning_robust_enabled") && j["learning_robust_enabled"].is_boolean()) p.learningRobustEnabled=j["learning_robust_enabled"].get<bool>();
    if(j.contains("learning_shadow_enabled") && j["learning_shadow_enabled"].is_boolean()) p.learningShadowEnabled=j["learning_shadow_enabled"].get<bool>();
    if (j.contains("seam") && j["seam"].is_object()) {
        SeamSettings probeSeam;
        if (probeSeam.applyJson(j["seam"]).empty()) p.seam = j["seam"];   // invalid seam override is ignored as a whole
    }
    return p;
}

void SettingsPatch::applyTo(Settings& s) const {
    if (sensitivity) s.sensitivity = *sensitivity;
    if (threshold) s.threshold = *threshold;
    if (inspectScale) s.inspectScale = *inspectScale;
    if (shiftToleranceX) s.shiftToleranceX = *shiftToleranceX;
    if (shiftToleranceY) s.shiftToleranceY = *shiftToleranceY;
    if (maxAlignmentShiftX) { s.maxAlignmentShiftX = *maxAlignmentShiftX; s.horizontalShiftMm.reset(); }
    if (maxAlignmentShiftY) { s.maxAlignmentShiftY = *maxAlignmentShiftY; s.verticalShiftMm.reset(); }
    if (minBlobArea) s.minBlobArea = *minBlobArea;
    if(learningRobustEnabled) s.learningRobustEnabled=*learningRobustEnabled;
    if(learningShadowEnabled) s.learningShadowEnabled=*learningShadowEnabled;
    if(learningSearchRadius) s.learningSearchRadius=*learningSearchRadius;
    if (seam.is_object()) s.seam.applyJson(seam);
    if (metric.is_object()) s.applyJson(metric);
}

SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) { reloadIfStale(); }

void SettingsStore::reloadIfStale() {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(file_, ec);
    if (ec) return;
    if (haveStamp_ && t == stamp_) return;
    std::ifstream in(file_);
    json j = json::parse(in, nullptr, false);
    stamp_ = t;
    haveStamp_ = true;
    if (!j.is_object()) return;
    map_.clear();
    for (auto it = j.begin(); it != j.end(); ++it) {
        Settings s;
        if (s.applyJson(it.value()).empty()) {
            // Migrate old stored profiles once on load; partial edits never couple the controls again.
            const double scale = std::round(s.inspectScale * 1000.0) / 1000.0;
            if (!it.value().contains("max_alignment_shift_x"))
                s.maxAlignmentShiftX = std::max(8.0, 4.0 * std::round(s.shiftToleranceX * scale)) / scale;
            if (!it.value().contains("max_alignment_shift_y"))
                s.maxAlignmentShiftY = std::max(8.0, 4.0 * std::round(s.shiftToleranceY * scale)) / scale;
            map_[it.key()] = s;
        }
    }
}

void SettingsStore::save() {
    json j = json::object();
    for (auto& [k, v] : map_) {
        j[k] = v.toJson();
        if (!v.estimatedScaleExplicit) j[k].erase("estimated_mm_per_pixel");
    }
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    auto tmp = file_;
    tmp += ".tmp";
    { std::ofstream(tmp) << j.dump(2); }
    std::filesystem::rename(tmp, file_, ec);
    if (ec) { std::ofstream(file_) << j.dump(2); }
    stamp_ = std::filesystem::last_write_time(file_, ec);
    haveStamp_ = !ec;
}

Settings SettingsStore::get(const std::string& profile, int cameraId) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    auto it = map_.find(profile);
    return (it == map_.end() ? Settings{} : it->second).withMetricDefaults(cameraId);
}

bool SettingsStore::has(const std::string& profile) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    return map_.count(profile) > 0;
}

std::string SettingsStore::set(const std::string& profile, const json& body, Settings& out, int cameraId) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    Settings s = (map_.count(profile) ? map_[profile] : Settings{}).withMetricDefaults(cameraId);
    std::string err = s.applyJson(body);
    if (!err.empty()) return err;
    s = s.withMetricDefaults(cameraId);
    map_[profile] = s;
    save();
    out = s;
    return "";
}

void SettingsStore::reset(const std::string& profile) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    if (map_.erase(profile)) save();
}

}
