#pragma once
#include "core/inspector.h"
#include "core/reference.h"
#include "core/settings.h"
#include "core/shm_io.h"
#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>

namespace cas {

// Raised when an inspection is requested for a product without a reference (HTTP 400, code no_reference).
struct NoReference : std::runtime_error {
    explicit NoReference(const std::string& product) : std::runtime_error("Reference is not set for " + product) {}
};

// A frame described by shared memory (what the orchestrator sends).
struct ShmFrame {
    std::string name;
    int width = 0, height = 0, stride = 0;
    long long offset = 0;
};

struct VisualOutputs {
    std::string heatmapPath, alignedPath, diffPath, maskPath;
    int heatmapMaxWidth = 0;
    bool any() const { return !heatmapPath.empty() || !alignedPath.empty() || !diffPath.empty() || !maskPath.empty(); }
};

struct InspectJob {
    std::string product;                 // camera-scoped product type
    std::string profile;                 // analysis profile (settings key); defaults to product
    SettingsPatch patch;                 // per-request overrides
    std::optional<NormPolygon> roi;      // ROI re-sent with the request (re-applied before inspecting)
    std::optional<NormPolygon> seamLine; // seam line [start, end] of the reference (overrides settings.seam.p0/p1)
};

struct Inspected {
    InspectOutcome out;
    Settings settings;
    NormPolygon roi;
    cv::Size frameSize;
    double heatmapMs = 0.0;
    double waitMs = 0.0;   // time spent waiting for a free worker slot (CPU saturation indicator)
    double readMs = 0.0;   // shared-memory frame read
};

class Service {
public:
    Service(std::filesystem::path dataDir, int workers);

    const std::string& detectorId() const { return detectorId_; }
    int workers() const { return workers_; }
    SettingsStore& settings() { return settings_; }

    void setReference(const std::string& product, cv::Mat bgr);
    std::shared_ptr<Reference> reference(const std::string& product);
    // Needs a reference (like the Python service). Validates the polygon (>=3 points, 0..1).
    void setRoi(const std::string& product, NormPolygon pts);
    std::optional<NormPolygon> roi(const std::string& product);
    nlohmann::json clearContext();

    nlohmann::json addLearnedRegion(const std::string& product, cv::Mat crop, cv::Rect bbox,
        cv::Size frameSize, const std::string& source);
    nlohmann::json learnedCases(const std::string& product);
    cv::Mat learnedImage(const std::string& id);
    bool deleteLearned(const std::string& id);

    // Reads the frame from shared memory and inspects it (bounded by the worker limit).
    Inspected inspectShm(const InspectJob& job, const ShmFrame& frame, bool keepVisuals);
    // Inspects an already decoded frame (multipart upload / test frame).
    Inspected inspectMat(const InspectJob& job, const cv::Mat& frame, bool keepVisuals);
    // JPEG from disk, cached by (cache_key, path); resized to the reference size when needed.
    cv::Mat loadTestFrame(const std::string& cacheKey, const std::string& path, const std::string& imageUrl,
                          const std::string& product);

    // Builds the heatmap of an inspection result and writes the requested images. Returns the descriptors.
    struct Written { std::optional<ImageInfo> heatmap, aligned, diff, mask; double heatmapMs = 0; };
    Written writeVisuals(const Inspected& r, const VisualOutputs& outputs);

private:
    class Slot;   // RAII concurrency slot
    Inspected run(const InspectJob& job, const cv::Mat& frame, bool keepVisuals);
    static NormPolygon validated(NormPolygon pts);

    void saveLearned(); // learningM_ held
    void attachLearned(const std::string& product, const std::shared_ptr<Reference>& ref); // learningM_ held
    std::filesystem::path learningDir_;
    std::mutex learningM_;
    nlohmann::json learnedMetadata_ = nlohmann::json::array();
    std::map<std::string, cv::Mat> learnedImages_;
    std::string detectorId_;
    int workers_;
    SettingsStore settings_;

    std::mutex regM_;
    std::map<std::string, std::shared_ptr<Reference>> refs_;

    std::mutex limitM_;
    std::condition_variable limitCv_;
    int freeSlots_;

    std::mutex testM_;
    std::map<std::tuple<std::string, std::string, std::string>, cv::Mat> testCache_;
};

}
