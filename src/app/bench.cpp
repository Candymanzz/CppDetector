#include "bench.h"
#include "../inspect/inspector.h"
#include <opencv2/core/utility.hpp>
#include <opencv2/imgcodecs.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <numeric>
#include <thread>

namespace cd {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Scenario {
    std::string name;
    std::vector<cv::Mat> refs;                       // per camera
    std::vector<std::vector<fs::path>> frames;       // per camera, frames to inspect (batch k = frames[c][k])
    std::vector<std::vector<cv::Point>> rois;
    InspectParams params;
};

struct Stats { double total = 0, avg = 0, mn = 0, mx = 0, p95 = 0, frameAvg = 0, frameMax = 0; int batches = 0, rejects = 0, frames = 0; };

// One pass over all batches: every batch = one frame per camera, processed in parallel.
Stats runPass(const Scenario& s, bool includeDecode, std::vector<Inspector>& insp, bool verbose) {
    const int N = (int)s.refs.size();
    size_t batches = SIZE_MAX;
    for (auto& f : s.frames) batches = std::min(batches, f.size());

    // preloaded images for the "no decode" variant
    std::vector<std::vector<cv::Mat>> pre(N);
    if (!includeDecode)
        for (int c = 0; c < N; ++c)
            for (size_t k = 0; k < batches; ++k) pre[c].push_back(cv::imread(s.frames[c][k].string()));

    Stats st;
    std::vector<double> walls, frameMs;
    auto tAll = Clock::now();
    for (size_t k = 0; k < batches; ++k) {
        auto t0 = Clock::now();
        std::vector<std::future<std::pair<double, bool>>> fut;
        for (int c = 0; c < N; ++c)
            fut.push_back(std::async(std::launch::async, [&, c, k]() -> std::pair<double, bool> {
                auto tf = Clock::now();
                cv::Mat img = includeDecode ? cv::imread(s.frames[c][k].string()) : pre[c][k];
                InspectResult r = insp[c].inspect(img, s.params);
                return {msSince(tf), r.reject};
            }));
        for (auto& f : fut) {
            auto [ms, rej] = f.get();
            frameMs.push_back(ms);
            st.rejects += rej;
        }
        walls.push_back(msSince(t0));
        if (verbose) std::cout << "  batch " << k + 1 << ": " << (int)walls.back() << " ms\n";
    }
    st.total = msSince(tAll);
    st.batches = (int)batches;
    st.frames = (int)frameMs.size();
    st.avg = std::accumulate(walls.begin(), walls.end(), 0.0) / walls.size();
    st.mn = *std::min_element(walls.begin(), walls.end());
    st.mx = *std::max_element(walls.begin(), walls.end());
    auto sorted = walls;
    std::sort(sorted.begin(), sorted.end());
    st.p95 = sorted[std::min(sorted.size() - 1, (size_t)(sorted.size() * 0.95))];
    st.frameAvg = std::accumulate(frameMs.begin(), frameMs.end(), 0.0) / frameMs.size();
    st.frameMax = *std::max_element(frameMs.begin(), frameMs.end());
    return st;
}

void report(const Scenario& s, bool verbose) {
    const int N = (int)s.refs.size();
    std::vector<Inspector> insp(N);
    for (int c = 0; c < N; ++c) insp[c].setReference(s.refs[c], s.rois[c], s.params);
    std::cout << "\n=== " << s.name << " (downscale " << s.params.downscale << ") ===\n";
    runPass(s, true, insp, false);   // warm-up (allocations, file cache)
    for (bool decode : {false, true}) {
        Stats st = runPass(s, decode, insp, verbose);
        std::cout << (decode ? "with JPEG decode   " : "inspect only       ")
                  << ": " << st.batches << " batches x " << N << " cameras = " << st.frames << " frames in "
                  << (int)st.total << " ms | batch avg " << (int)st.avg << " / p95 " << (int)st.p95 << " / max "
                  << (int)st.mx << " ms (budget 500) | per-frame avg " << (int)st.frameAvg << " ms, max "
                  << (int)st.frameMax << " ms | rejects " << st.rejects << "\n";
    }
}

} // namespace

int runBench(const fs::path& framesDir, const std::vector<std::string>& args) {
    bool verbose = std::find(args.begin(), args.end(), "--verbose") != args.end();
    cv::setNumThreads(1);   // we parallelise across cameras; OpenCV-internal threads would oversubscribe the CPU
    std::cout << "hardware threads: " << std::thread::hardware_concurrency() << "\n";

    // Scenario A: the camera dataset, per camera reference = earliest frame, inspect the rest.
    const bool datasetOnly = std::find(args.begin(), args.end(), "--dataset-only") != args.end();
    for (double ds : {1.0, 0.7, 0.5}) {
        Scenario s;
        s.name = "camera dataset " + framesDir.filename().string();
        s.params.sensitivity = 0.5;
        s.params.downscale = ds;
        for (int c = 0; c < 10; ++c) {
            std::vector<fs::path> list;
            for (auto& d : fs::directory_iterator(framesDir / ("camera_" + std::to_string(c))))
                if (d.is_directory() && fs::exists(d.path() / "frame.jpg")) list.push_back(d.path() / "frame.jpg");
            std::sort(list.begin(), list.end());
            if (list.size() < 2) { std::cerr << "camera " << c << ": not enough frames\n"; return 1; }
            s.refs.push_back(cv::imread(list[0].string()));
            list.erase(list.begin());
            s.frames.push_back(list);
            s.rois.push_back({});
        }
        report(s, verbose);
    }

    // Scenario B: 10 virtual cameras of the large label shots (reference good1, bucket-body ROI).
    fs::path testDir = framesDir.parent_path() / "test-pr-ph";
    if (!datasetOnly && fs::exists(testDir / "good1.jpg")) {
        for (double ds : {0.4, 0.5, 0.6}) {
            Scenario s;
            s.name = "test-pr-ph 2448x2048 x10 cameras";
            s.params.sensitivity = 1.0;
            s.params.rejectThreshold = 0.1;
            s.params.downscale = ds;
            cv::Mat ref = cv::imread((testDir / "good1.jpg").string());
            std::vector<cv::Point> roi = {{612, 526}, {1903, 526}, {1927, 1346}, {1836, 1578}, {1224, 1713}, {697, 1615}, {612, 1346}};
            std::vector<fs::path> files;
            for (auto n : {"good2", "bad1", "bad2", "bad21"}) files.push_back(testDir / (std::string(n) + ".jpg"));
            for (int c = 0; c < 10; ++c) { s.refs.push_back(ref); s.rois.push_back(roi); s.frames.push_back(files); }
            report(s, verbose);
        }
    }
    return 0;
}

}
