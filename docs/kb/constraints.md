# Diagnose tuning without silently replacing it — 2026-11-24

The owner is actively tuning the mask. Preserve live settings and show which
ones produced the displayed result. Width/side loss and geometric displacement
are separate signals; short width violations must not imply a rejected frame.
Physical seam identity and production calibration remain open. [Diagnosis, behavior and evidence](seam-plot-2026-11-24.md).

# A surviving side cannot hide the other — 2026-11-24

Mask ridge requires a supported reference pair and checks each side independently.
Unknown reference/current rows remain null; a short local disappearance can reject
even below global missing-length limits. More original photos now reject or lack
control; these are not validated manufacturing-defect labels. Preserve owner tuning.
[Method, evidence and limitations](seam-pair-2026-11-24.md).

# Reference seam identity is frozen — 2026-11-24

The owner requested reference-derived limits and one uniform tolerance. Current
frames cannot redefine that reference. Tolerance must not affect matching or
invalidate confirmation; changed primary pixels/points must require recapture.
Missing/invisible evidence is incomplete control. One selected boundary and
unlabelled photo outcomes do not establish complete seam/stripe defect quality.
[Evidence and bounds](seam-reference-2026-11-24.md).

# Mask seam experiment and explicit loss of control — 2026-11-24

The owner requested a mask/path alternative to fragmented bright-ridge detection.
It is explicit per camera; no automatic geometric rescaling or masked-out new
defects. Missing path evidence stays missing, and failed coverage/gap guards give
NOT_INSPECTED. Improved path coverage does not establish correct edge identity or
labelled defect accuracy. [Implementation and boundaries](seam-mask-2026-11-24.md).

# Local-image lifetime and compatibility — 2026-11-19

Uploads live only until full page reload and cannot be recovered from recipe JSON.
References remain profile-owned; arbitrary product photos or different native
sizes do not become valid comparisons automatically. Missing/invalid images never
fall back to dataset frames. Existing geometry/quality/timing limits remain.
[Workflow and validation](local-uploads-2026-11-19.md).

# Accepted-image regression and remaining geometry limits — 2026-11-19

Positions 2–4 now pass with zero regions at unchanged atlas rejection thresholds.
This is a local same-file regression, not native parity or labelled-defect quality.
Working areas are explicit; unchecked pixels remain transparent. Tolerance/blur
can hide small changes. Full reference geometry and the line deadline are still
unverified. Manual position/scale correction does not establish calibration.
[Evidence and open work](atlas-quality-2026-11-19.md).

# Reference-derived geometry only — 2026-11-19

The owner reported ghosting and requested better stitching. Overlap averaging
is removed from the display. Only reference pixels can determine a new geometric
fit; it is frozen for current-image inspection. Unsupported matches must remain
explicitly partial, with no fabricated surface or silent source stretching.
Native constraints still apply separately. [Decision and checks](reference-stitching-2026-11-19.md).

# Whole-atlas verdict experiment — 2026-11-19

The owner explicitly authorised inspecting the unfolded image. This adds a separate
experimental whole-bucket verdict to the browser workspace, preserving native C3
comparison and machine outputs. Missing required views/references, inadequate
support or unreliable registration cannot produce OK. Calibration and 500 ms
timing remain unverified. [Parameters and tradeoffs](surface-atlas-inspection-2026-11-19.md).

# Surface unwrap experiment — 2026-11-19

The owner authorised a separate multi-camera image workspace. It groups side views
of one bucket, without changing C3's native per-camera comparison or issuing a native
verdict. Top cameras and different products cannot be merged into its side image.
Approximate coverage/geometry and browser runtime do not certify physical coverage,
metric accuracy or C1's machine-cycle budget. [Reasons and tests](surface-unwrap-2026-11-19.md).

# Local data during Git synchronization — 2026-11-16

The pulled profile belongs to a different machine. Retain this checkout's camera
profiles, paths and learned samples; do not attach another reference's confirmed
label outline. The merged implementation keeps both optional geometry paths.
See [resolution, preservation and checks](git-pull-2026-11-16.md).

# Adaptive ROI amendment — 2026-11-09

The owner authorized an adaptive reference ROI experiment in the standalone app.
The manually editable reference polygon remains authoritative; each result has a
separate mapped checked mask and coverage. Unknown geometry cannot yield OK.
The single primary reference and classic CV constraints remain. Learned raw-frame
crops are inactive in this mode until a correct geometry-aware representation
exists. [Reasons and limits](adaptive-roi-2026-11-09.md).

# Explicit exclusion zones — 2026-11-06

The owner requested unconditional user-drawn inspection exclusions. C4 still uses
a polygon ROI; independent exclusion polygons subtract holes from it and skip
intersecting seam cross-sections. This differs from learned normal samples.
[Decision and verification](exclusion-zones-2026-11-06.md).

# Current update — 2026-11-04

Native core/service are now pinned to main-app 20909141. The production robust matcher
replaces the local callback hook; metrics, upper corners and sensitivity-zero seam
behavior follow production. Existing data paths remain compatible. Read
[the authoritative sync report](production-sync-2026-11-04.md) for current behavior,
validation and the pre-existing missing reference names. Older sections below are history.

## Amendment 2026-11-02

Owner explicitly requested learned normal regions. C2 still excludes neural-network training; the new feature
stores alternate image crops and compares them with classic CV. C3 means one primary reference per camera,
plus explicit camera/reference-scoped alternate normal regions. See [learning-port.md](learning-port.md).

# Constraints (do not change without recording a new decision)

- C1. Whole batch of 10 frames (one per camera) must be processed in < 500 ms. User-set hard goal; measure every batch.
- C2. No machine learning. Core mechanic = reference image + ROI + sensitivity + reject threshold. Classic CV (OpenCV) only.
- C3. One reference per camera (10 cameras). Each camera frame is compared only with its own reference.
- C4. ROI is a user-defined polygon (points), not a rectangle. Auto-ROI only proposes; human can always edit.
- C5. Result per frame must expose: anomaly score (%), anomaly zone (heatmap/contours), verdict, rejection reason (logged).
- C6. Default downscale = 1.0 (source frames are already downscaled). Downscale must stay configurable.
- C7. Ignore result.json and heatmap.u8 in the dataset; use frame.jpg only.
- C8. Dependencies live in vcpkg (D:\vcpkg, manifest mode, outside the repo). Triplet x64-windows-static-md. Toolchain: MSVC 14.44 (VS Build Tools 2022), CMake 4.x.
- C9. UI texts are Russian; code, logs, KB are English-keyed (reason codes: score_over_threshold, shift_exceeded, no_reference). Reason codes are consumed by the UI (web/app.js reasonText) - keep them in sync.
- C10. Batch time shown in the UI is the full wall time of a tick (read + inspect + overlay + log) for all 10 cameras, compared with 500 ms.
- C11. Never commit/push in D:\GitHubPj\defect-detector-v3 (the user does it). Do not touch unrelated parts of that repo; keep the Python path working (`python_detector.backend: python` is the default).
- C12. cppAnalisSurface API compatibility: keep the Python field names in InspectResponse (the Java pipeline reads them), keep ROI normalized 0..1, status strings "ГОДЕН"/"БРАК", `detector_id` in every JSON object, error body `{detail,error_code}` with `no_reference` code (the Java client relies on it for auto-recovery).
- C13. HTTP thread pool of cppAnalisSurface must stay larger than the number of concurrent client connections (see D14).
