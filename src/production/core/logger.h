#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <mutex>
#include <string>

namespace cas {

// File logger. Two daily-rotated files in the log directory:
//   cas-YYYY-MM-DD.log          human-readable events (start, uploads, ROI/settings changes, warnings, errors)
//   inspections-YYYY-MM-DD.jsonl one JSON object per inspection with all numbers needed to tune the analysis
// Files older than `keepDays` are removed at start. All methods are thread-safe and never throw.
class Logger {
public:
    static Logger& instance();

    void init(const std::filesystem::path& dir, int keepDays = 14);
    const std::filesystem::path& dir() const { return dir_; }

    void info(const std::string& msg) { write("INFO", msg); }
    void warn(const std::string& msg) { write("WARN", msg); }
    void error(const std::string& msg) { write("ERROR", msg); }

    void inspection(const nlohmann::json& record);

private:
    void write(const char* level, const std::string& msg);
    void rotate(std::ofstream& f, std::string& day, const char* prefix, const char* ext, const std::string& today);
    void cleanup(int keepDays);

    std::mutex m_;
    std::filesystem::path dir_;
    bool ready_ = false;
    std::ofstream textFile_, jsonFile_;
    std::string textDay_, jsonDay_;
    std::uintmax_t jsonBytes_ = 0;   // size of today's inspections file (daily cap protects the disk)
    bool capWarned_ = false;
};

// Log directory in the application root: the nearest parent of the executable that holds CMakeLists.txt
// (build/Release/x.exe -> project root); falls back to <exe dir>/logs.
std::filesystem::path defaultLogDir(const char* argv0);

}
