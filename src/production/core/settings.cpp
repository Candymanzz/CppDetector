#include "settings.h"
#include <fstream>

namespace cas {

json SeamSettings::toJson() const {
    return {{"enabled", enabled}, {"p0", {p0x, p0y}}, {"p1", {p1x, p1y}}, {"band", band},
            {"tol_mid", tolMid}, {"tol_edge", tolEdge}, {"edge_frac", edgeFrac}, {"min_contrast", minContrast}};
}

json Settings::toJson() const {
    json j = {{"sensitivity", sensitivity}, {"threshold", threshold},
            {"shift_tolerance_x", shiftToleranceX}, {"shift_tolerance_y", shiftToleranceY},
            {"min_blob_area", minBlobArea}, {"inspect_scale", inspectScale}, {"seam", seam.toJson()}};
    if (alignmentLimitX) j["alignment_limit_x"] = *alignmentLimitX;
    if (alignmentLimitY) j["alignment_limit_y"] = *alignmentLimitY;
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
        !readNumber(j, "tol_edge", 0.1, 50, false, t.tolEdge, err) || !readNumber(j, "edge_frac", 0, 0.5, false, t.edgeFrac, err) ||
        !readNumber(j, "min_contrast", 1, 100, false, t.minContrast, err))
        return "seam." + err;
    *this = t;
    return "";
}

std::string Settings::applyJson(const json& j) {
    if (!j.is_object()) return "settings must be a JSON object";
    Settings tmp = *this;
    std::string err;
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
        tmp.shiftToleranceX = tmp.shiftToleranceY = v;
    }
    v = tmp.shiftToleranceX;
    if (!readNumber(j, "shift_tolerance_x", 0, 30, false, v, err)) return err;
    tmp.shiftToleranceX = v;
    v = tmp.shiftToleranceY;
    if (!readNumber(j, "shift_tolerance_y", 0, 30, false, v, err)) return err;
    tmp.shiftToleranceY = v;
    for (auto item : {std::make_pair("alignment_limit_x",&tmp.alignmentLimitX),
                      std::make_pair("alignment_limit_y",&tmp.alignmentLimitY)}) {
        if (!j.contains(item.first)) continue;
        if (j[item.first].is_null()) item.second->reset();
        else {
            double limit=0;
            if (!readNumber(j,item.first,0,100000,false,limit,err)) return err;
            *item.second=limit;
        }
    }
    v = tmp.minBlobArea;
    if (!readNumber(j, "min_blob_area", 1, 100000, false, v, err)) return err;
    tmp.minBlobArea = static_cast<int>(v);
    v = tmp.inspectScale;
    if (!readNumber(j, "inspect_scale", 0.1, 1, false, v, err)) return err;
    tmp.inspectScale = v;
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
    take("min_blob_area", p.minBlobArea);
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
    if (minBlobArea) s.minBlobArea = *minBlobArea;
    if (seam.is_object()) s.seam.applyJson(seam);
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
        if (s.applyJson(it.value()).empty()) map_[it.key()] = s;
    }
}

void SettingsStore::save() {
    json j = json::object();
    for (auto& [k, v] : map_) j[k] = v.toJson();
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

Settings SettingsStore::get(const std::string& profile) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    auto it = map_.find(profile);
    return it == map_.end() ? Settings{} : it->second;
}

bool SettingsStore::has(const std::string& profile) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    return map_.count(profile) > 0;
}

std::string SettingsStore::set(const std::string& profile, const json& body, Settings& out) {
    std::lock_guard<std::mutex> lk(m_);
    reloadIfStale();
    Settings s = map_.count(profile) ? map_[profile] : Settings{};
    std::string err = s.applyJson(body);
    if (!err.empty()) return err;
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
