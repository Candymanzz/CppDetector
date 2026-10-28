# Changelog

- 2026-10-28: audited surface inspection and seam handling (D24). Fixed inconsistent fractional-scale resizing, counted connected anomaly pixels without filling clean holes, applied configured seam contrast, and rejected failed enabled seam measurements with seam_not_found. Added geometry validation and invalidation after reference resolution changes. Serialized camera setting writes, flushed changes before inspection, and disabled the stored seam after deleting an endpoint. Added regression coverage and checked actual browser interactions and frames36.

- 2026-10-27: deferred calibration at the user's request: removed its UI and point-picking workflow, enabled mm controls immediately with provisional per-camera scales, preserved digital-scale conversion and added estimated-scale/save validation.

- 2026-10-27: added per-camera local metric calibration and horizontal/vertical displacement controls in mm. Normalized measurement spans follow digital image resizing; fractional pixel conversion is rounded once at analysis scale. Calibrated alignment uses the configured physical limit instead of the legacy fourfold margin. Added physical result/log diagnostics, calibration UI, migration/scale/limit/restart tests and documented the limits of oblique curved-surface measurements.

- 2026-10-27: applied the user-requested local score multiplier x3 before saturation and threshold comparison. Added score_multiplier to inspection results/logs and regression coverage for clean frames, proportional scores, saturation and rejection; recorded the local calibration as D21.

- 2026-10-26: ported the reference-cache correction from defect-detector-v3 cpp-detector (bfd9b54a): clone the alignment thumbnail before phaseCorrelate so its in-place Hann window cannot mutate shared reference data. Added repeated/concurrent warm/cold cache regressions and changed square-ROI adapter parity from cache diagnostics to immutable-cache/stable-result assertions.

- 2026-10-26: superseded the X/Y-only port with a full local mirror of position-x-y core 885a79ed. Native score, ROI-thumbnail alignment/confidence, original-pixel settings, min-area scaling, seam and heatmap now run behind the existing UI; edge no longer affects verdicts. Added separate native profiles/logs, preserved old scenes/configs, native defaults button and parity tests.

- 2026-10-26: ported the independent X/Y shift tolerance from defect-detector-v3 position-x-y (885a79ed): per-axis alignment limits and rectangular comparison envelope, two web sliders, backward-compatible legacy config loading, per-axis result/log diagnostics and CTest coverage. CppDetector's existing score/downscale/geometry behavior is preserved.

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
- 2026-10-21: seam geometry check added (D18): seam.{h,cpp}, per-camera `seam` settings/API/UI card, overlay (cyan band, green centre, red bad points), reason `seam_deviation`; camera 4 of frames36 preconfigured.
- 2026-10-21: rebuild-and-run.{ps1,cmd} / run.{ps1,cmd} + scripts/common.ps1 (copied from cppAnalisSurface): auto-install VS Build Tools, CMake, git, vcpkg, libraries; run opens the UI on frames36.
- 2026-10-22: seam check generalised to a polyline (D19): control points + Catmull-Rom axis, modes ridge (seam) / step (label edge), polarity, local reference fit, skip segments, max_gap; "Взять из точек ROI" in the UI; legacy p0/p1 configs still load.
- 2026-10-22: geometry check split into two independent zones per camera, "Стык" (seam) and "Кромка" (edge); old step-mode seam config migrates to edge; per-zone deviation graph in the overlay and per-zone debug sets in logs/seam.
