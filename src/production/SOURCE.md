# Native inspection snapshot

Origin: https://github.com/Candymanzz/defect-detector-v3/tree/position-x-y
Pinned base commit: 885a79ed66460440c7b22b6b5430425cd9d1849e (2026-10-26 import).
Reference-cache correction: bfd9b54ae708482ce83d7961ff8b2f0970deb3fc (`fix etalon cache`),
from https://github.com/Candymanzz/defect-detector-v3/tree/cpp-detector (2026-10-26).

The 11 files in core/ are copied from cppAnalisSurface/src/core/ at the base commit,
with the reference-cache correction, local score calibration, metric support and D24/D25 fixes described below.
They are intentionally isolated from the local UI. Further calculation changes require an explicit recorded decision.
Update the snapshot and its provenance together; run production_parity_test afterwards.
OpenCV is an external dependency: numerical parity requires the same library version/build as production.

Local score calibration (2026-10-27, D21): multiply the original area-based score by 3 before capping it at 1.
anomaly_score = min(1, area_percent / 5 * 3); the existing threshold is compared with this amplified score.
100% score now corresponds to about 1.667% anomalous ROI area. Profile defaults remain those of the base.
This calibration is local to CppDetector; adapter/core parity tests exercise this calibrated core.

Metric support (2026-10-27, D22): the adapter converts calibrated millimeter tolerances to fractional
original-frame pixels. Settings/SettingsPatch retain these fractions; the envelope rounds once at analysis scale.
Optional alignmentLimitX/Y are exact original-frame-pixel limits for calibrated cameras; they replace the legacy
fourfold/minimum-eight-working-pixel alignment margin so a configured 2 mm limit remains 2 mm at every Downscale.
Direct legacy pixel callers retain their original behavior. The local app now supplies provisional per-camera scales
when measured calibration is absent (D23); mm controls work immediately and results identify metric_scale_source=estimated.
See docs/kb/metric-calibration.md for calibration scope, perspective limitations and quantization.

The local adapter is src/inspect/inspector.cpp:
- local score and reject_threshold = native anomaly_score and threshold multiplied by 100;
- shift_tolerance_x/y and min_blob_area are in original-image units;
- downscale maps directly to inspect_scale;
- ROI pixels map to normalised coordinates using original width/height minus one;
- seam endpoints map to native normalised points; previous rejection reasons are preserved; failed enabled measurements use seam_not_found (D24);
- the old polyline/step edge algorithm is not built or invoked for inspection;
- heatmap pixels come from native buildHeatmap; only the local colour overlay is UI-specific.
The native request-timing logger is included to keep the copied core intact; it is inactive in this standalone app.

Profiles are isolated: config/config_<dataset>_position-x-y.json.
On first start only reference choice, uploaded reference image, ROI and seam endpoints/tolerances are imported
from config/config_<dataset>.json. Surface settings start from native defaults (0.5, 25%, X/Y=3, scale=1, area=20).
The old config and reference image files are never overwritten by the import.
New logs live in logs/position-x-y/decisions.jsonl and include engine/version, native_settings,
calibrated anomaly_score, score_multiplier, area_percent, shifts/confidence, limits, kernels and phase timings.

OpenCV phaseCorrelate may apply the Hann window in-place on some ROI/DFT sizes.
The imported correction passes a private clone of the alignment thumbnail, preserving the shared Prepared cache.
shift_tolerance_test checks unchanged reference/thumbnail/window data, repeated results and concurrent use of
both warm and cold caches at two scales, including a 256x256 thumbnail that needs no DFT padding.
production_parity_test also enforces stable repeated square-ROI results through the local adapter.

Inspection audit (2026-10-28, D24, local changes): reference/current resizing now uses the same explicit rounded
size at fractional scales. Grayscale preparation clones its input. Blob filtering/scoring uses connected foreground
pixel counts, retaining clean holes instead of filling external contours. SeamSettings carries min_contrast through
the adapter; enabled measurements that cannot locate/fit the line reject, including early exits and invalid geometry.
These are intentional local corrections to the pinned snapshot. inspection_regression_test checks the behavior
independently of adapter/core parity; see the D24 decision and the audit report for validation and limits.

Alignment zero-offset correction (2026-10-28, D25, local change): reference/current thumbnails and the
thumbnail's Hann window are zero-padded on the right/bottom to equal even optimal DFT dimensions before
phaseCorrelate. This avoids the half-pixel zero shift from an odd DFT dimension. Prepared::smallSize remains
the unpadded spatial size, while refSmall/hann contain the padded canvas; original-pixel conversion still uses
smallSize. The cached padded reference is cloned for each correlation, preserving the earlier cache fix.
Regression checks cover repeated self-comparisons with horizontal/vertical ROIs, signed/subpixel translations,
and the real camera-4 ROI at different scales and 0.5-4 mm tolerances in the optional real-frame parity run.
