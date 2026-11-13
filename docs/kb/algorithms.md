# Unified active implementation ? 2026-11-13

The only linked core is src/production/core at eefefce4. The standalone adapter
retains incoming learning and exclusions, using immutable per-inspection masks.
Metric limits are 0..30mm. Earlier paths and snapshot descriptions below are historical.
[Merge choices and validation](merge-core-2026-11-13.md).

# Inspection exclusions — 2026-11-06

As of 2026-11-13 the active surface inspector is the imported production core,
wrapped by `src/inspect/inspector.cpp`. See [source, mapping and validation](production-inspection-port.md).
It uses native phase256/ECC512 positioning, compensation before blur, independent
mm limits and the production sensitivity-dependent score. The following original
pipeline notes describe the earlier test-bed implementation.

Planned pipeline per frame (see PLAN.md section 4): downscale -> phaseCorrelate shift -> brightness normalization in ROI
-> absdiff (+blur) with shift tolerance -> sensitivity -> threshold -> morphology/min blob -> score = anomalous px / ROI px (%)
-> verdict score > reject_threshold -> heatmap + contours.
Implemented in src/inspect/inspector.cpp (no downscale tuning yet; ROI polygon supported, auto-ROI not).
Draw polygon zones in the camera setup to skip inspection inside them. Exclusions
are persistent per-camera holes in ROI and also apply to the seam.
[Behavior, reasons, checks and limits](exclusion-zones-2026-11-06.md).

# Learned pattern support — 2026-11-04

Pattern support is now computed against the primary reference at the matched placement,
rather than translated from its original bbox. This fixes normal edge regions that
continued to reject after learning. [Details and validation](learned-placement-fix-2026-11-04.md).

# Current update — 2026-11-04

Native core/service are now pinned to main-app 20909141. The production robust matcher
replaces the local callback hook; metrics, upper corners and sensitivity-zero seam
behavior follow production. Existing data paths remain compatible. Read
[the authoritative sync report](production-sync-2026-11-04.md) for current behavior,
validation and the pre-existing missing reference names. Older sections below are history.

# Local learning extension (2026-11-02)

When active learned samples exist, the local robust mode extends 52aa31ea matching with a separate radius,
changed-pixel support mask and bounded smooth-shadow residual matching. Native mode is selectable.
Exact rules, initial constants, tests and limits: [local-learning-v2.md](local-learning-v2.md).

# Current detector: 52aa31ea (2026-11-02)

The active core now uses sensitivity-dependent scoring (.2% ROI full scale at sensitivity .5),
threshold .6, connected-component pixel filtering and independent max_alignment_shift_x/y (default 12px).
Saved alternate normal patches participate in native comparison, scoped to camera/reference.
See [learning-port.md](learning-port.md). The 885a79ed score/limit descriptions below are historical.

# Current inspection: position-x-y mirror (2026-10-26)

CppDetector branch 123 is now a local UI harness for the native core from defect-detector-v3,
position-x-y commit 885a79ed plus the reference-cache correction from cpp-detector commit bfd9b54a.
See src/production/SOURCE.md for provenance and compatibility details.
Earlier full-frame alignment, amplified score and polyline/edge decisions are historical, not the active backend.

Pipeline: native reference preparation and ROI bounding box -> grey/blur/downscale -> phase correlation on an
ROI thumbnail (max side 256, Hanning/confidence rules) -> translation compensation -> ROI brightness normalisation
-> rectangular min/max comparison envelope -> sensitivity threshold -> morphology/minimum blob area -> score/verdict.
Phase correlation receives a private copy of the reference thumbnail so repeated and concurrent inspections
cannot mutate the shared prepared-reference cache through OpenCV's in-place Hann window application.

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
