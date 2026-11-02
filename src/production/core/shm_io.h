#pragma once
#include <opencv2/core.hpp>
#include <filesystem>
#include <string>

namespace cas {

struct ImageInfo {
    std::string path;
    int width = 0, height = 0, stride = 0, channels = 1;
};

// Directory of the shared frames: Linux /dev/shm, Windows %LOCALAPPDATA%\iml_shm.
std::filesystem::path shmDirectory();

// Maps a logical shm name (as sent by the orchestrator) to a file. Throws std::invalid_argument.
std::filesystem::path resolveShmPath(const std::string& shmName);

// Reads a BGR8 frame (row stride in bytes, offset in bytes) and returns an owned copy.
// Throws std::invalid_argument for bad geometry and std::runtime_error when the file is missing/short.
cv::Mat readBgrFrame(const std::string& shmName, int width, int height, int stride, long long offset);

// Writes a raw uint8 image (rows contiguous) to `outputPath` (must resolve inside the shm directory).
ImageInfo writeU8Image(const std::string& outputPath, const cv::Mat& image);

// Reads a file as bytes and decodes it (UTF-8 safe paths on Windows).
cv::Mat readImageFile(const std::filesystem::path& file);

}
