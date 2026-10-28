# Current inspection: position-x-y mirror (2026-10-26)

CppDetector branch 123 is now a local UI harness for the native core from defect-detector-v3,
position-x-y commit 885a79ed plus the reference-cache correction from cpp-detector commit bfd9b54a.
See src/production/SOURCE.md for provenance and compatibility details.
The local score calibration x3 is described below. Earlier full-frame alignment and polyline/edge decisions are historical.

Pipeline: native reference preparation and ROI bounding box -> grey/blur/downscale -> phase correlation on an
ROI thumbnail (max side 256, Hanning/confidence rules) -> translation compensation -> ROI brightness normalisation
-> rectangular min/max comparison envelope -> sensitivity threshold -> morphology/minimum blob area -> score/verdict.
Reference/current resizing uses identical rounded destination dimensions at every scale (D24). Blob areas are
counts of 8-connected anomalous pixels after morphology; clean holes are excluded from both size filtering and score.
Phase correlation receives a private copy of the reference thumbnail so repeated and concurrent inspections
cannot mutate the shared prepared-reference cache through OpenCV's in-place Hann window application.

Score (local calibration D21, 2026-10-27): anomaly_score = min(1, anomalous_ROI_area_percent / 5 * 3).
The score is three times the previous value until it saturates at 100%, at about 1.667% anomalous ROI area.
Local UI score and reject_threshold use 0..100 percent; verdict is native score >= native threshold.
The existing default threshold 25% now rejects at about 0.417% anomalous ROI area; score_multiplier=3 is logged.
Defaults: sensitivity=0.5, reject_threshold=25, shift_tolerance_x/y=3, downscale=1, min_blob_area=20.
The UI provides horizontal/vertical tolerances in mm immediately, using provisional per-camera scales (D23).
Metric tolerances are converted to fractional original-image pixels,
then rounded once at the analysis resolution. The same physical values bound global alignment independent of Downscale.
For direct legacy pixel callers without metric tolerances, position limits remain max(8,4*round(tolerance*scale))/scale in original px.
Areas remain original-image px squared. No rotation correction is added.
See metric-calibration.md for local physical calibration, digital resize, sampling error and perspective limitations.

Only the native two-endpoint bright-seam straightness check may additionally reject.
The legacy edge/step/polyline calculation is not called or linked into the active backend.
A seam rejection preserves any earlier surface rejection reason. Since D24 an enabled check also rejects when
the line cannot be measured, with reason seam_not_found. Surface score can still be zero for this rejection.
Configured min_contrast is applied; line endpoints must be at least 50 original-image pixels apart. Replacing the
reference at a different resolution clears old ROI and seam geometry. Camera writes are serialized and pending
changes are saved before manual inspection or starting the emulated stream.
Local per-camera ROI editing, uploaded references, frame testing, deliberate test-defect injection, camera grid
and colour overlays remain UI features; artificial defects are explicitly marked in logs.

Config and logs are separate from legacy local experiments. The first native profile imports reference/ROI/seam,
not old amplified thresholds or working-pixel settings. The original files remain intact.
OpenCV inner threads are set to one in UI mode as in the production service; cameras run concurrently.

Validation: CTest synthetic/native adapter comparisons, profile import/restart preservation and optional real-frame
parity: production_parity_test <path-to-frames36>. Checks compare raw maps/blobs, score/verdict, offsets/limits,
heatmap pixels and seam results, not just the displayed percentage. This is not ground-truth defect validation.
