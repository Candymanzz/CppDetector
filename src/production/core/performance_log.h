#pragma once
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace cas {

// Independent from the synchronous legacy logger: diagnostics never do disk IO on HTTP threads.
class PerformanceLog {
public:
    static PerformanceLog& instance();
    PerformanceLog() = default;
    ~PerformanceLog();
    PerformanceLog(const PerformanceLog&) = delete;
    PerformanceLog& operator=(const PerformanceLog&) = delete;
    void init(const std::filesystem::path& directory, size_t capacity = 8192);
    void write(nlohmann::json record) noexcept;
    void close() noexcept;
    uint64_t dropped() const { return dropped_.load(); }
    bool enabled() const { return enabled_.load(); }
private:
    void run() noexcept;
    std::filesystem::path directory_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<nlohmann::json> queue_;
    size_t capacity_ = 8192;
    bool running_ = false;
    std::atomic<uint64_t> dropped_{0};
    std::atomic<bool> enabled_{false};
    std::thread worker_;
};

}
