#include "decision_log.h"
#include <deque>
#include <fstream>

namespace cd {

DecisionLog::DecisionLog(std::filesystem::path dir) : dir_(std::move(dir)), file_(dir_ / "decisions.jsonl") {
    std::filesystem::create_directories(dir_ / "rejects");
}

void DecisionLog::append(const nlohmann::json& rec) {
    std::lock_guard<std::mutex> lk(m_);
    std::ofstream(file_, std::ios::app) << rec.dump() << "\n";
}

std::vector<nlohmann::json> DecisionLog::tail(size_t n) const {
    std::lock_guard<std::mutex> lk(m_);
    std::deque<std::string> lines;
    std::ifstream in(file_);
    for (std::string l; std::getline(in, l);) {
        lines.push_back(std::move(l));
        if (lines.size() > n) lines.pop_front();
    }
    std::vector<nlohmann::json> out;
    for (auto& l : lines) {
        auto j = nlohmann::json::parse(l, nullptr, false);
        if (!j.is_discarded()) out.push_back(std::move(j));
    }
    return out;
}

}
