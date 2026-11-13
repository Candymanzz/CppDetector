# Inspection exclusions — 2026-11-06

Draw polygon zones in the camera setup to skip inspection inside them. Exclusions
are persistent per-camera holes in ROI and also apply to the seam.
[Behavior, reasons, checks and limits](exclusion-zones-2026-11-06.md).

# Automatic learning — 2026-11-04

The current UI uses one «Дообучить» button and automatically encloses all nonzero
raw heatmap pixels with 10 source pixels of padding, matching the main application.
The inspected version pins the source crop. Manual selection notes below are historical.
[Implementation, compatibility and checks](automatic-learning-2026-11-04.md).

# Current update — 2026-11-04

Native core/service are now pinned to main-app 20909141. The production robust matcher
replaces the local callback hook; metrics, upper corners and sensitivity-zero seam
behavior follow production. Existing data paths remain compatible. Read
[the authoritative sync report](production-sync-2026-11-04.md) for current behavior,
validation and the pre-existing missing reference names. Older sections below are history.

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
