# Changelog

- 2026-10-20: PLAN.md written; skeleton (CMakeLists, vcpkg.json, CMakePresets.json, src/main.cpp); KB created; vcpkg cloned to D:\vcpkg.
- 2026-10-20: vcpkg deps built (OpenCV 4.14.0, httplib 0.62, nlohmann 3.12). Added src/inspect/inspector.{h,cpp} and a console runner in src/main.cpp (args: root sens thr [defect]).
- 2026-10-20: web test UI (grid of 10 cameras, per-camera setup with ROI polygon editor, auto-ROI, reference pick/upload, manual/uploaded/all-frames inspection, decision log view), JSON API, JSONL decision log + reject overlays, config persistence, emulated stream. Measured: batch 250-310 ms wall for 10 cameras incl. overlay and logging.
