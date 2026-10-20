#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace cd {
// Console benchmark: all 10 cameras in parallel over all frames. Usage: cppdetector --bench [--verbose]
int runBench(const std::filesystem::path& framesDir, const std::vector<std::string>& args);
}
