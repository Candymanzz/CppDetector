# Native inspection snapshot

Origin: https://github.com/Candymanzz/defect-detector-v3/tree/position-x-y
Pinned base commit: 885a79ed66460440c7b22b6b5430425cd9d1849e (2026-10-26 import).
Reference-cache correction: bfd9b54ae708482ce83d7961ff8b2f0970deb3fc (`fix etalon cache`),
from https://github.com/Candymanzz/defect-detector-v3/tree/cpp-detector (2026-10-26).

The 11 files in core/ are copied from cppAnalisSurface/src/core/ at the base commit,
with the reference-cache correction applied to inspector.cpp. Score and profile defaults remain those of the base.
They are intentionally isolated from the local UI and must not be independently retuned.
Update the snapshot and its provenance together; run production_parity_test afterwards.
OpenCV is an external dependency: numerical parity requires the same library version/build as production.

The local adapter is src/inspect/inspector.cpp:
- local score and reject_threshold = native anomaly_score and threshold multiplied by 100;
- shift_tolerance_x/y and min_blob_area are in original-image units;
- downscale maps directly to inspect_scale;
- ROI pixels map to normalised coordinates using original width/height minus one;
- seam endpoints map to native normalised points; native Service::applySeam decision merging is reproduced;
- the old polyline/step edge algorithm is not built or invoked for inspection;
- heatmap pixels come from native buildHeatmap; only the local colour overlay is UI-specific.
The native request-timing logger is included to keep the copied core intact; it is inactive in this standalone app.

Profiles are isolated: config/config_<dataset>_position-x-y.json.
On first start only reference choice, uploaded reference image, ROI and seam endpoints/tolerances are imported
from config/config_<dataset>.json. Surface settings start from native defaults (0.5, 25%, X/Y=3, scale=1, area=20).
The old config and reference image files are never overwritten by the import.
New logs live in logs/position-x-y/decisions.jsonl and include engine/version, native_settings,
native anomaly_score, area_percent, shifts/confidence, limits, kernels and phase timings.

OpenCV phaseCorrelate may apply the Hann window in-place on some ROI/DFT sizes.
The imported correction passes a private clone of the alignment thumbnail, preserving the shared Prepared cache.
shift_tolerance_test checks unchanged reference/thumbnail/window data, repeated results and concurrent use of
both warm and cold caches at two scales, including a 256x256 thumbnail that needs no DFT padding.
production_parity_test also enforces stable repeated square-ROI results through the local adapter.
