#include "logger.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace cas {

namespace fs = std::filesystem;

namespace {

std::tm localTm(std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string dayString(std::time_t t) {
    const std::tm tm = localTm(t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d");
    return os.str();
}

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::tm tm = localTm(system_clock::to_time_t(now));
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
       << '.' << std::setw(3) << std::setfill('0') << ms;
    return os.str();
}

}

Logger& Logger::instance() {
    static Logger l;
    return l;
}

void Logger::init(const fs::path& dir, int keepDays) {
    std::lock_guard<std::mutex> lk(m_);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        std::cerr << "[log] cannot create " << dir.string() << ": " << ec.message() << std::endl;
        return;
    }
    dir_ = dir;
    ready_ = true;
    cleanup(keepDays);
}

void Logger::cleanup(int keepDays) {
    std::error_code ec;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(24 * std::max(1, keepDays));
    for (auto& e : fs::directory_iterator(dir_, ec)) {
        const auto ext = e.path().extension().string();
        const auto name = e.path().filename().string();
        if ((ext != ".log" && ext != ".jsonl") || (name.rfind("cas-", 0) != 0 && name.rfind("inspections-", 0) != 0)) continue;
        std::error_code e2;
        if (e.last_write_time(e2) < limit && !e2) fs::remove(e.path(), e2);
    }
}

void Logger::rotate(std::ofstream& f, std::string& day, const char* prefix, const char* ext, const std::string& today) {
    if (f.is_open() && day == today) return;
    f.close();
    f.clear();
    f.open(dir_ / (std::string(prefix) + today + ext), std::ios::app);
    day = today;
}

void Logger::write(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lk(m_);
    if (!ready_) return;
    try {
        rotate(textFile_, textDay_, "cas-", ".log", dayString(std::time(nullptr)));
        textFile_ << timestamp() << ' ' << level << ' ' << msg << '\n';
        textFile_.flush();
    } catch (...) {}
}

void Logger::inspection(const nlohmann::json& record) {
    std::lock_guard<std::mutex> lk(m_);
    if (!ready_) return;
    try {
        const std::string today = dayString(std::time(nullptr));
        if (jsonDay_ != today) {
            rotate(jsonFile_, jsonDay_, "inspections-", ".jsonl", today);
            std::error_code ec;
            const auto sz = fs::file_size(dir_ / ("inspections-" + today + ".jsonl"), ec);
            jsonBytes_ = ec ? 0 : sz;
            capWarned_ = false;
        }
        constexpr std::uintmax_t kDailyCap = 500ull * 1024 * 1024;
        if (jsonBytes_ >= kDailyCap) {
            if (!capWarned_) {
                capWarned_ = true;
                rotate(textFile_, textDay_, "cas-", ".log", today);
                textFile_ << timestamp() << " WARN inspections log reached the daily cap, further records are dropped today\n";
                textFile_.flush();
            }
            return;
        }
        nlohmann::json j = record;
        j["ts"] = timestamp();
        const std::string line = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        jsonFile_ << line << '\n';
        jsonFile_.flush();
        jsonBytes_ += line.size() + 1;
    } catch (...) {}
}

fs::path defaultLogDir(const char* argv0) {
    std::error_code ec;
    fs::path exe = fs::weakly_canonical(fs::u8path(argv0 ? argv0 : ""), ec);
    if (ec || exe.empty()) return fs::current_path(ec) / "logs";
    for (fs::path p = exe.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        if (fs::exists(p / "CMakeLists.txt", ec)) return p / "logs";
    }
    return exe.parent_path() / "logs";
}

}
