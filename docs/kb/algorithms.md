# Current inspection: position-x-y mirror (2026-10-26)

CppDetector branch 123 is now a local UI harness for the unchanged native core from defect-detector-v3,
position-x-y commit 885a79ed. See src/production/SOURCE.md for provenance and compatibility details.
Earlier full-frame alignment, amplified score and polyline/edge decisions are historical, not the active backend.

Pipeline: native reference preparation and ROI bounding box -> grey/blur/downscale -> phase correlation on an
ROI thumbnail (max side 256, Hanning/confidence rules) -> translation compensation -> ROI brightness normalisation
-> rectangular min/max comparison envelope -> sensitivity threshold -> morphology/minimum blob area -> score/verdict.

Score: native anomaly_score = min(1, anomalous_ROI_area_percent / 5).
Local UI score and reject_threshold use 0..100 percent; verdict is native score >= native threshold.
Defaults: sensitivity=0.5, reject_threshold=25, shift_tolerance_x/y=3, downscale=1, min_blob_area=20.
All pixel tolerances and areas are ORIGINAL-image units. The core converts them using inspect_scale and its square.
Position limits = max(8,4*round(tolerance*scale))/scale per axis, in original px.
Zero tolerance removes envelope expansion on that axis, not alignment. No rotation correction is added.

Only the native two-endpoint bright-seam straightness check may additionally reject.
The legacy edge/step/polyline calculation is not called or linked into the active backend.
A seam rejection uses native Service::applySeam merge rules, preserving any earlier surface rejection reason.
Local per-camera ROI editing, uploaded references, frame testing, deliberate test-defect injection, camera grid
and colour overlays remain UI features; artificial defects are explicitly marked in logs.

Config and logs are separate from legacy local experiments. The first native profile imports reference/ROI/seam,
not old amplified thresholds or working-pixel settings. The original files remain intact.
OpenCV inner threads are set to one in UI mode as in the production service; cameras run concurrently.

Validation: CTest synthetic/native adapter comparisons, profile import/restart preservation and optional real-frame
parity: production_parity_test <path-to-frames36>. Checks compare raw maps/blobs, score/verdict, offsets/limits,
heatmap pixels and seam results, not just the displayed percentage. This is not ground-truth defect validation.
