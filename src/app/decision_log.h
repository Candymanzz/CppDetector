#pragma once
#include <nlohmann/json.hpp>
#include <filesystem>
#include <mutex>
#include <vector>

namespace cd {

// Append-only JSONL log of every inspection decision (one line per frame).
class DecisionLog {
public:
    explicit DecisionLog(std::filesystem::path dir);
    void append(const nlohmann::json& rec);
    std::vector<nlohmann::json> tail(size_t n) const;
    std::filesystem::path rejectsDir() const { return dir_ / "rejects"; }

private:
    std::filesystem::path dir_, file_;
    mutable std::mutex m_;
};

}
