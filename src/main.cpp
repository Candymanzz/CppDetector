// CppDetector: reference-based frame inspection with a web test UI.
// Usage: cppdetector [--frames DIR] [--port N] [--web DIR] [--bench [--dataset-only] [--verbose]]
#include "app/app.h"
#include "app/bench.h"
#include "web/routes.h"
#include <iostream>
#include <string>

#ifndef CD_SOURCE_DIR
#define CD_SOURCE_DIR "."
#endif

int main(int argc, char** argv) {
    cv::setNumThreads(1); // identical to the production service: parallelism comes from cameras
    namespace fs = std::filesystem;
    fs::path frames = fs::path(CD_SOURCE_DIR) / "frame" / "frames36";
    fs::path webDir = fs::path(CD_SOURCE_DIR) / "web";
    int port = 8080;
    bool bench = false;
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "--bench") bench = true;
        else if (i + 1 < argc && k == "--frames") frames = argv[++i];
        else if (i + 1 < argc && k == "--port") port = std::stoi(argv[++i]);
        else if (i + 1 < argc && k == "--web") webDir = argv[++i];
    }
    if (bench) return cd::runBench(frames, std::vector<std::string>(argv + 1, argv + argc));

    fs::path data = fs::path(CD_SOURCE_DIR);
    // settings are kept per dataset (folder name), so different frame sets do not overwrite each other
    const auto stem = "config_" + frames.filename().string();
    cd::App app(frames, data / "config" / (stem + "_position-x-y.json"), data / "logs" / "position-x-y",
                data / "config" / (stem + ".json"));

    httplib::Server svr;
    svr.new_task_queue = [] { return new httplib::ThreadPool(8); };
    svr.set_payload_max_length(64 * 1024 * 1024);
    cd::registerRoutes(svr, app);
    if (!svr.set_mount_point("/", webDir.string())) {
        std::cerr << "web dir not found: " << webDir << "\n";
        return 1;
    }
    std::cout << "frames: " << frames << "\nUI: http://localhost:" << port << "/\n" << std::flush;
    if (!svr.listen("127.0.0.1", port)) {
        std::cerr << "cannot listen on port " << port << "\n";
        return 1;
    }
}
