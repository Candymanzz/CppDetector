## Local learning hook (2026-11-02)

Local Inspector optionally supplies LearnedComparison to native inspectFrame. It updates the difference map
before native component filtering/scoring; Service and no-hook callers keep the original matcher.
Storage and locking stay unchanged. See [local-learning-v2.md](local-learning-v2.md).

## Update 2026-11-02

Native core/service now use snapshot 52aa31ea. App owns native Service for learning persistence and attaches
its camera/reference-scoped cases to the local Inspector. Per-camera locks protect the untouched latest source
image and version used by learning API requests. New routes/UI and data paths: [learning-port.md](learning-port.md).

# Architecture

FrameSource (frame/camera_N/*) -> BatchScheduler (every period_ms: next frame of each of 10 cameras)
 -> ThreadPool -> Inspector[camera] (reference + ROI + params) -> DecisionLogger (JSONL) + WebServer (HTTP/JSON + static UI)

Modules (src/): source, pipeline, inspect, reference (references, ROI, auto-ROI), config, log, web.
Status: CppDetector (test bed): inspect/, app/, web/ + UI. The production module now lives in defect-detector-v3/cppAnalisSurface (see integration-in-main-application.md).

Endpoints: GET /api/testfiles, /api/state, /api/status, /api/frame, /api/overlay, /api/log, /api/reject_image;
POST /api/camera, /api/reference_testfile, /api/inspect_testfile, /api/global, /api/run, /api/reference, /api/reference_upload, /api/inspect, /api/inspect_upload, /api/inspect_all, /api/auto_roi.
Emulated stream: App::loop -> tick() every period_ms, one frame per camera (cursor cycles through frames except the reference), std::async per camera.
