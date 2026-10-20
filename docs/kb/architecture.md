# Architecture

FrameSource (frame/camera_N/*) -> BatchScheduler (every period_ms: next frame of each of 10 cameras)
 -> ThreadPool -> Inspector[camera] (reference + ROI + params) -> DecisionLogger (JSONL) + WebServer (HTTP/JSON + static UI)

Modules (src/): source, pipeline, inspect, reference (references, ROI, auto-ROI), config, log, web.
Status: src/app (App, DecisionLog), src/web (routes), src/inspect (Inspector, autoRoi), web/ (static UI) exist. main.cpp starts the HTTP server (default http://localhost:8080).

Endpoints: GET /api/testfiles, /api/state, /api/status, /api/frame, /api/overlay, /api/log, /api/reject_image;
POST /api/camera, /api/reference_testfile, /api/inspect_testfile, /api/global, /api/run, /api/reference, /api/reference_upload, /api/inspect, /api/inspect_upload, /api/inspect_all, /api/auto_roi.
Emulated stream: App::loop -> tick() every period_ms, one frame per camera (cursor cycles through frames except the reference), std::async per camera.
