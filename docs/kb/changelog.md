# Changelog

- 2026-10-20: PLAN.md written; skeleton (CMakeLists, vcpkg.json, CMakePresets.json, src/main.cpp); KB created; vcpkg cloned to D:\vcpkg.
- 2026-10-20: vcpkg deps built (OpenCV 4.14.0, httplib 0.62, nlohmann 3.12). Added src/inspect/inspector.{h,cpp} and a console runner in src/main.cpp (args: root sens thr [defect]).
- 2026-10-20: web test UI (grid of 10 cameras, per-camera setup with ROI polygon editor, auto-ROI, reference pick/upload, manual/uploaded/all-frames inspection, decision log view), JSON API, JSONL decision log + reject overlays, config persistence, emulated stream. Measured: batch 250-310 ms wall for 10 cameras incl. overlay and logging.
- 2026-10-20: test-pr-ph support (API: /api/testfiles, /api/reference_testfile, /api/inspect_testfile; UI controls); size_mismatch guard in Inspector. Camera 0 currently configured with good1 as reference (config/config.json, config/ref_cam0.png).
- 2026-10-20: added `--bench` console benchmark (src/app/bench.cpp); results in decisions.md D9.
- 2026-10-20: default dataset = frame/frames36; settings per dataset (config_<name>.json); --bench accepts --frames and --dataset-only; bench runs downscale 1.0/0.7/0.5.
- 2026-10-20: reviewed defect-detector-v3 (branch core-dev-p) for integration; created integration-in-main-application.md; no integration code yet.
- 2026-10-20: integration implemented in defect-detector-v3 (cppAnalisSurface service, Java CppAnalisSurfaceHttpSupervisor + DetectorBackend + settings proxy, front-end DetectorSettingsPanel, config examples, README). See integration-in-main-application.md. CppDetector also got: per-dataset config files, default dataset frames36, size_mismatch guard, --bench.
- 2026-10-20: cppAnalisSurface got rebuild-and-run / run scripts with dependency auto-install (integration doc 5.3a).
- 2026-10-20: main app root launch scripts adapted for the cpp detector (no Python needed with backend: cpp).
- 2026-10-20: python_parallelism corrected to 10 (was 1 and serialised the cameras), see D16.
- 2026-10-21: cppAnalisSurface file logging (logs/, JSONL per inspection + event log + minute summaries), see D17.
