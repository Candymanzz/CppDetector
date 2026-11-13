# Production detector sync — 2026-11-04

Source: D:/Pj/defect-detector-v3, branch boobs+pasha, commit
2090914128af2822c5a94720a89555c02238642b. Destination baseline: CppDetector,
branch merg, commit 0f3c211. Both working trees were initially clean.

## Scope and reasons

All files under cppAnalisSurface/src/core and src/service are imported byte-for-byte
into src/production. This includes the metric calibration, ROI upper-corner masks,
updated inspector, robust matcher, persistence, settings and performance diagnostics.
The old local LearnedComparison hook is removed. The local matcher header now forwards
to the native implementation, so the test bed and production use one implementation.
Production core/service files have no test-bed modifications; logger.cpp remains provenance
only, while the test bed keeps its existing decision logger.

The local adapter still handles UI percentages (native threshold/score times 100), pixel
ROI coordinates, overlays, file datasets and manual region selection. It now uses native
Settings validation and frame-dependent metric conversion, camera-specific estimates,
calibration via API, 1 percent scale steps, corner/metric/learning diagnostics and the
production seam rule: sensitivity zero also skips the seam. Core API labels are
robust-v2 and native-52aa31ea, matching production. The inspection engine is
cpp-detector-20909141, with the full source commit exposed in state/results/config.
Direct executable startup now defaults to the actual frames36 directory.

Keep config_<dataset>_52aa31ea.json, its reference PNGs, learning directory and existing
log directory as compatibility paths. Changing those names would silently discard the
current scene settings and samples. Existing pixel limits acquire camera-specific mm
defaults through native withMetricDefaults; local comparison X/Y remain independent.
No stored config, reference image, learned case, dataset or old log is replaced by this sync.
The app persists the new fields when the operator next changes settings.

This file-based standalone app has no Java orchestrator, MVS camera capture, lighting,
PLC or React application. Production changes to those components (including Bayer8
capture and exposure/gain) have no corresponding runtime here. Decoded input images
are passed to the same production grayscale detector. Copying the whole main repository
would replace the test application rather than update its detector.

## Validation on Windows

- MSVC 19.44 / OpenCV 4.14 Release build, using already installed dependencies.
- Ten CTest targets: local moving shadow, local learning, native learned regions,
  native/local shift tolerance, production parity, adapter settings/persistence,
  production metric tolerance, production upper corners and production moving shadow.
- Parity over all 800 frames36 images (80 per camera): 8621 checks pass, including
  synthetic ROI/scale/shift/seam cases and repeated-cache checks. Scores, verdicts,
  shifts, zones, difference maps, accepted masks and heatmaps agree with native calls.
- HTTP save/retry/list/image/isolation/stale/malformed/restart/delete checks pass on
  temporary data and an ephemeral loopback port.
- Existing profile test runs the previous destination executable and the updated one
  against an isolated copy of config. Ten camera settings, loaded references/ROI/seam,
  and one learned case agree and survive save/restart. Zero/false values, per-axis
  pixel overrides, .73 scale and calibrated metric conversion are covered separately.
- JavaScript syntax and mock-DOM startup, diagnostics and region selection/save pass.

The initial legacy learned_region_test assumed pixel limits for a small image. The
current production test already sets explicit 12 px limits; that complete updated test
is imported. Production tolerances were not loosened to satisfy an outdated fixture.
Interactive browser operation, production Linux, camera/PLC integration, classification
quality on labelled defects and the complete 500 ms machine cycle are not validated.

## Existing data discrepancy

The saved camera 1–9 reference name f_0000001 is absent from the supplied frames36
dataset. Both the old and updated executable fall back to the first available frame
(for example camera 1: f_0000553). This predates the sync. Existing configuration is
not rewritten by deployment; missing images cannot be recovered from this dataset.
The isolated migration test compares loaded behavior with the previous executable,
instead of claiming that an absent reference image has been restored.

Earlier snapshot notes in this knowledge base are retained as history; their pinned
commit numbers and old hook descriptions are superseded by this report.
