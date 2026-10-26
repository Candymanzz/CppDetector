# Native inspection snapshot

Origin: https://github.com/Candymanzz/defect-detector-v3/tree/position-x-y
Pinned commit: 885a79ed66460440c7b22b6b5430425cd9d1849e (2026-10-26 import).

The 11 files in core/ are copied without calculation changes from cppAnalisSurface/src/core/.
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

The source previously exhibited a mutable thumbnail cache on some ROI/DFT sizes with OpenCV 4.10.
This port does NOT silently change that algorithm. Repeated-ROI sequence parity and cache-mutation diagnostics
are included in production_parity_test; any correction should be made and validated in both repositories.
