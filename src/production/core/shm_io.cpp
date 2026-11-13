#include "shm_io.h"
#include <opencv2/imgcodecs.hpp>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace cas {

namespace fs = std::filesystem;

#ifdef _WIN32
constexpr bool kWindows = true;
#else
constexpr bool kWindows = false;
#endif

fs::path shmDirectory() {
    if (kWindows) {
        const char* local = std::getenv("LOCALAPPDATA");
        if (!local || !*local) local = std::getenv("TEMP");
        return fs::path(local && *local ? local : ".") / "iml_shm";
    }
    return fs::path("/dev/shm");
}

namespace {

bool insideDirectory(const fs::path& root, const fs::path& candidate) {
    std::error_code ec;
    fs::path r = fs::weakly_canonical(root, ec);
    fs::path c = fs::weakly_canonical(candidate, ec);
    if (ec) return false;
    auto fold = [](fs::path p) {
        std::string s = p.generic_string();
        if (kWindows) for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    };
    auto rit = r.begin(), cit = c.begin();
    for (; rit != r.end(); ++rit, ++cit)
        if (cit == c.end() || fold(*rit) != fold(*cit)) return false;
    return true;
}

}

fs::path resolveShmPath(const std::string& shmName) {
    std::string name = shmName;
    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());
    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
    if (name.empty()) throw std::invalid_argument("shm_name must not be empty");

    const fs::path dir = shmDirectory();
    if (kWindows) {
        fs::path candidate(name);
        if (candidate.is_absolute()) {
            std::error_code ec;
            if (fs::is_regular_file(candidate, ec)) return candidate;
            // the camera worker may report logs/*.bin while the data lives in iml_shm
            std::string stem = candidate.stem().string();
            if (candidate.extension() == ".bin" && fs::is_regular_file(dir / stem, ec)) return dir / stem;
            if (stem.rfind("iml_cam_", 0) == 0 && fs::is_regular_file(dir / stem, ec)) return dir / stem;
            return candidate;
        }
        std::string base = name;
        while (!base.empty() && (base.front() == '/' || base.front() == '\\')) base.erase(base.begin());
        for (char& ch : base) if (ch == '/' || ch == '\\') ch = '_';
        return dir / base;
    }

    fs::path path = name.rfind("/dev/shm/", 0) == 0 ? fs::path(name) : dir / fs::path(name).relative_path();
    if (!insideDirectory(dir, path)) throw std::invalid_argument("shm_name must resolve inside /dev/shm");
    return path;
}

cv::Mat readBgrFrame(const std::string& shmName, int width, int height, int stride, long long offset) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("width and height must be positive");
    if (offset < 0) throw std::invalid_argument("shm_offset must be >= 0");
    const long long rowBytes = 3LL * width;
    const long long rowStride = stride > 0 ? stride : rowBytes;
    if (rowStride < rowBytes) throw std::invalid_argument("stride must be at least width * 3");
    const long long needEnd = offset + (height - 1) * rowStride + rowBytes;

    fs::path path = resolveShmPath(shmName);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) throw std::runtime_error("shared memory file not found: " + path.string());
    const long long size = static_cast<long long>(fs::file_size(path, ec));
    if (needEnd > size) {
        throw std::runtime_error("shared memory payload is too small: need end offset " + std::to_string(needEnd) +
                                 ", file has " + std::to_string(size) + " bytes");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open shared memory file: " + path.string());

    cv::Mat frame(height, width, CV_8UC3);
    if (rowStride == rowBytes) {
        in.seekg(offset);
        in.read(reinterpret_cast<char*>(frame.data), rowBytes * height);
        if (!in) throw std::runtime_error("short read from shared memory file");
    } else {
        for (int y = 0; y < height; ++y) {
            in.seekg(offset + y * rowStride);
            in.read(reinterpret_cast<char*>(frame.ptr(y)), rowBytes);
            if (!in) throw std::runtime_error("short read from shared memory file");
        }
    }
    return frame;
}

ImageInfo writeU8Image(const std::string& outputPath, const cv::Mat& image) {
    if (image.empty() || image.depth() != CV_8U || image.channels() > 3)
        throw std::invalid_argument("visual output must be a non-empty uint8 image");
    cv::Mat img = image.isContinuous() ? image : image.clone();

    fs::path path = resolveShmPath(outputPath);
    if (!insideDirectory(shmDirectory(), path))
        throw std::invalid_argument("visual output path must resolve inside " + shmDirectory().string());
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot open visual output file: " + path.string());
    out.write(reinterpret_cast<const char*>(img.data), static_cast<std::streamsize>(img.total() * img.elemSize()));
    out.flush();
    if (!out) throw std::runtime_error("cannot write visual output file: " + path.string());

    ImageInfo info;
    info.path = path.string();
    info.width = img.cols;
    info.height = img.rows;
    info.channels = img.channels();
    info.stride = img.cols * img.channels();
    return info;
}

cv::Mat readImageFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return {};
    std::vector<uchar> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return {};
    return cv::imdecode(bytes, cv::IMREAD_COLOR);
}

}
