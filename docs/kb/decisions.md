# Decisions

## D1 - Build stack (2026-10-20)
Context: no C++ OpenCV on machine; Python OpenCV unusable; MSVC Build Tools 2022 present.
Decision: CMake + vcpkg manifest (vcpkg at D:\vcpkg), opencv4 minimal features (jpeg, png), cpp-httplib, nlohmann-json.
Why: one reproducible dependency flow, header-only web server keeps the app a single exe.
Consequences: first configure builds OpenCV (20-40 min); later builds are cached.

## D2 - Alignment strategy (2026-10-20)
Decision: only translation via cv::phaseCorrelate + a per-camera shift tolerance (px) applied in diff (min over neighborhood). No ORB/ECC.
Why: 500 ms budget for 10 frames; user said micro-shifts are minor and should be handled by a tolerance setting.
Consequences: rotations/large shifts are not corrected; flagged as reason `shift_exceeded`.

## D3 - Reference selection (2026-10-20)
Decision: reference per camera = earliest frame by folder name; can be re-picked from UI.
Why: user wants to emulate real inspection: first frame becomes reference, later frames are checked against it.

## D4 - First measurements (2026-10-20)
Console runner, 10 cameras in parallel via std::async, full-frame ROI, sens 0.5, includes JPEG decode: batch wall time 190-260 ms (budget 500).
Clean frames score ~0.00-0.05%. Synthetic defect (ellipse + line, ~0.5% of frame) scores 0.5-0.66% on all cameras and zones are localized correctly (also on overexposed cameras 6-9).
Known: cam 0 shows a tiny false zone on the bucket rim (sub-pixel/shift edge) -> an ROI polygon and/or higher shiftTolerance is the remedy, not code changes.
Open: sensitivity->threshold mapping (80..5) is a first guess, tune with real defects.

## D5 - Web UI and API (2026-10-20)
Decision: cpp-httplib server serves static files from <repo>/web (plain HTML/JS/CSS, no bundler) and a JSON API under /api. App (src/app) owns state; routes (src/web/routes.cpp) are thin.
Why: single exe, no Node toolchain, easy to iterate on the UI without rebuilding C++.
Consequences: the UI must be launched with cwd-independent paths (CD_SOURCE_DIR compile definition = repo root; override with --frames/--web/--port).
- Per-camera mutex in App::Cam guards reference/ROI/params/last result; the emulated stream and manual inspections serialize per camera, run in parallel across cameras.
- Config persisted to config/config.json (params, ROI, reference name; uploaded reference stored as config/ref_cam<N>.png). Every change autosaves.
- Frame names accepted by the API must exist in the camera's own listing (no path traversal). Reject images served only by plain file name from logs/rejects.
- Test aids: synthetic defect injection (global stream flag or per-run checkbox), "inspect all frames of camera", upload of an arbitrary frame/reference.

## D6 - Auto-ROI (2026-10-20)
Decision: blur -> Otsu -> open/close -> blob closest to centre and largest -> convex hull -> approxPolyDP (<=14 vertices). Only a proposal; applied then edited by hand.
Result: cam 0-5 get a plausible but too generous polygon (includes neighbouring bright objects at the left/bottom). Cam 6-9 (overexposed) get an almost full-frame polygon. Needs manual edit; improve only if the user asks.

## D7 - Heatmap rendering (2026-10-20)
Heatmap colours are normalised to the detection threshold (thr -> mid colour, 2*thr -> red); values below thr/2 are not painted. Otherwise JPEG/lighting noise paints the whole bucket.
