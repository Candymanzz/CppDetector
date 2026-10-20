// CppDetector: reference-based frame inspection with a web test UI.
// Usage: cppdetector [--frames DIR] [--port N] [--web DIR]
#include "app/app.h"
#include "web/routes.h"
#include <iostream>
#include <string>

#ifndef CD_SOURCE_DIR
#define CD_SOURCE_DIR "."
#endif

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    fs::path frames = fs::path(CD_SOURCE_DIR) / "frame" / "frame";
    fs::path webDir = fs::path(CD_SOURCE_DIR) / "web";
    int port = 8080;
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        if (k == "--frames") frames = argv[i + 1];
        else if (k == "--port") port = std::stoi(argv[i + 1]);
        else if (k == "--web") webDir = argv[i + 1];
    }

    fs::path data = fs::path(CD_SOURCE_DIR);
    cd::App app(frames, data / "config" / "config.json", data / "logs");

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
