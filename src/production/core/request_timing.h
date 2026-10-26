#pragma once
#include "core/performance_log.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <string>

namespace cas {

struct RequestTiming {
    using Clock = std::chrono::steady_clock;
    bool active = false;
    Clock::time_point started{}, responseReady{};
    nlohmann::json context = nlohmann::json::object();
    nlohmann::json stages = nlohmann::json::object();
    void add(const char* name, double ms) noexcept {
        try { if (active) stages[name] = stages.value(name, 0.0) + ms; } catch (...) {}
    }
    static double elapsed(Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    void finish(int status, const char* boundary, bool completionKnown = true) noexcept {
        if (!active) return;
        try {
        auto record = context;
        record["event"] = "http_request";
        record["status_code"] = status;
        record["boundary"] = boundary;
        record["request_ms"] = completionKnown ? nlohmann::json(elapsed(started)) : nlohmann::json(nullptr);
        record["ms"] = stages;
        double accounted = 0;
        // Only disjoint top-level intervals, never algorithm_ms plus its children.
        for (const char* key : {"request_parse_ms", "wait_ms", "read_ms", "reference_check_ms", "service_setup_ms",
                 "algorithm_ms", "seam_ms", "visuals_ms", "legacy_inspection_logging_ms", "legacy_slow_logging_ms",
                 "response_build_ms", "response_serialize_ms", "response_write_ms", "test_frame_load_ms"}) {
            accounted += stages.value(key, 0.0);
        }
        record["accounted_ms"] = accounted;
        record["unaccounted_ms"] = completionKnown
            ? nlohmann::json(std::max(0.0, record["request_ms"].get<double>() - accounted)) : nlohmann::json(nullptr);
        PerformanceLog::instance().write(std::move(record));
        } catch (...) {}
        active = false;
    }
};

inline thread_local RequestTiming requestTiming;

class TimingPhase {
public:
    explicit TimingPhase(const char* name) : name_(name), start_(RequestTiming::Clock::now()) {}
    ~TimingPhase() { requestTiming.add(name_, RequestTiming::elapsed(start_)); }
private:
    const char* name_;
    RequestTiming::Clock::time_point start_;
};

}
