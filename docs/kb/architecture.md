# Architecture

FrameSource (frame/camera_N/*) -> BatchScheduler (every period_ms: next frame of each of 10 cameras)
 -> ThreadPool -> Inspector[camera] (reference + ROI + params) -> DecisionLogger (JSONL) + WebServer (HTTP/JSON + static UI)

Modules (src/): source, pipeline, inspect, reference (references, ROI, auto-ROI), config, log, web.
Status: only skeleton exists (src/main.cpp prints OpenCV version).
