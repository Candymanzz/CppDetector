#include "performance_log.h"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace cas {
namespace {
std::string utcTime(long long micros, bool dayOnly = false) {
    const auto seconds = static_cast<std::time_t>(micros / 1000000);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, dayOnly ? "%Y-%m-%d" : "%Y-%m-%dT%H:%M:%S");
    if (!dayOnly) out << '.' << std::setw(6) << std::setfill('0') << micros % 1000000 << 'Z';
    return out.str();
}
long long epochMicros() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}

PerformanceLog& PerformanceLog::instance() { static PerformanceLog log; return log; }
PerformanceLog::~PerformanceLog() { close(); }

void PerformanceLog::init(const std::filesystem::path& directory, size_t capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    directory_ = directory;
    capacity_ = std::max<size_t>(1, capacity);
    running_ = true;
    enabled_ = true;
    try { worker_ = std::thread([this] { run(); }); }
    catch (...) { running_ = false; enabled_ = false; throw; }
}

void PerformanceLog::write(nlohmann::json record) noexcept {
    try {
        record["epoch_us"] = epochMicros();
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        if (queue_.size() >= capacity_) { ++dropped_; return; }
        queue_.push_back(std::move(record));
        ready_.notify_one();
    } catch (...) { ++dropped_; }
}

void PerformanceLog::close() noexcept {
    enabled_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    ready_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void PerformanceLog::run() noexcept {
    try {
        const auto pid = static_cast<long long>(
#ifdef _WIN32
            _getpid()
#else
            getpid()
#endif
        );
        std::filesystem::create_directories(directory_);
        std::ofstream out;
        std::string day;
        uint64_t bytes = 0, reportedDrops = 0;
        bool capWarned = false;
        auto lastFlush = std::chrono::steady_clock::now();
        for (;;) {
            std::deque<nlohmann::json> batch;
            bool stopping;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait_for(lock, std::chrono::milliseconds(200), [this] { return !running_ || !queue_.empty(); });
                batch.swap(queue_);
                stopping = !running_;
            }
            for (auto& row : batch) {
                const auto micros = row.value("epoch_us", epochMicros());
                const auto today = utcTime(micros, true);
                if (day != today) {
                    out.close(); out.clear();
                    const auto path = directory_ / ("performance-detector-" + today + "-" + std::to_string(pid) + ".jsonl");
                    std::error_code ec;
                    bytes = std::filesystem::exists(path, ec) ? std::filesystem::file_size(path, ec) : 0;
                    out.open(path, std::ios::app);
                    if (!out) throw std::runtime_error("cannot open performance log " + path.string());
                    day = today; capWarned = false;
                }
                if (bytes >= 512ull * 1024 * 1024) {
                    ++dropped_;
                    if (!capWarned) { std::cerr << "[performance] daily cap reached; records dropped\n"; capWarned = true; }
                    continue;
                }
                row["schema"] = 1; row["component"] = "cpp_detector";
                row["pid"] = pid; row["ts_utc"] = utcTime(micros);
                const auto lost = dropped_.load();
                if (lost != reportedDrops) row["dropped_records_since_last_write"] = lost - reportedDrops;
                reportedDrops = lost;
                const auto line = row.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                out << line << '\n'; bytes += line.size() + 1;
                if (!out) throw std::runtime_error("performance log write failed");
            }
            if (stopping || std::chrono::steady_clock::now() - lastFlush >= std::chrono::milliseconds(200)) {
                if (out.is_open()) out.flush();
                lastFlush = std::chrono::steady_clock::now();
            }
            if (stopping) break;
        }
        out.close();
        if (dropped_) std::cerr << "[performance] total dropped=" << dropped_.load() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "[performance] writer failed: " << e.what() << '\n';
        std::lock_guard<std::mutex> lock(mutex_);
        dropped_ += queue_.size(); queue_.clear(); running_ = false;
        enabled_ = false;
    }
}
}
