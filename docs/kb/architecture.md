# Seam diagnostic snapshot — 2026-11-24

App JSON includes used_params and per-row width-event qualification. C++
overlay and SVG consume the same event states; the frontend compares its
working draft to the inspected snapshot and requests reinspection explicitly.
[Diagnosis, behavior and evidence](seam-plot-2026-11-24.md).

# Paired mask adapter — 2026-11-24

Inspector routes ridge mask to inspectMaskedSeam with primary pixels and pose.
Per-inspection preparation avoids a stale model lifecycle; sides/width states feed
JSON, debug logs, overlay and versioned PNG. Imported production core is unchanged.
[Method, evidence and limitations](seam-pair-2026-11-24.md).

# Reference-seam cache and confirmation — 2026-11-24

Inspector owns an immutable model rebuilt only from the primary reference and
validated against its persisted pixel/point/preparation signature. A dedicated
confirmation endpoint captures the model. The adapter matches current pixels
after existing pose compensation, then uses uniform deviation limits. Rebuilds
invalidate stale backend result visuals; per-camera UI queues isolate late saves.
[Contract and checks](seam-reference-2026-11-24.md).

# Optional standalone mask seam inspector — 2026-11-24

`src/inspect/seam_mask` is selected explicitly by the local adapter, without
changing imported native core/service. Its typed quality/trace/preview feed the
existing verdict/overlay and versioned PNG endpoint. Failed mask control cannot
produce OK; old native missing-line behavior stays compatible.
[Contract and validation](seam-mask-2026-11-24.md).

# Camera viewport and native seam trace — 2026-11-24

Follow-up: the left camera grid no longer scrolls. The canvas fits both dimensions
and a compact `renderResult` summary remains visible below it. All tools, complete
results and seam diagnostics live in the right scroll menu. The independent-left
scroll description below is historical.
[Current layout and evidence](sidebar-seam-2026-11-24.md#follow-up-keep-the-whole-bucket-and-verdict-visible).

The camera tab uses viewport-height flex/grid with independent left/right scroll
containers; other tabs retain page flow. Native seam fits now expose per-row
diagnostics through the standalone adapter and JSON to the existing overlay and
a separate SVG plot. Adaptive mapping includes diagnostic coordinates. Decisions
and numerical comparison are preserved in the checked cases.
[Contract and verification](sidebar-seam-2026-11-24.md).

# Browser-local file sources — 2026-11-19

`unwrap.js` separates frames/uploads/references. Local File decoding shares the
normal raster path; memory maps are keyed by camera and unique selection serial.
No new endpoint is used. Old decodes cannot restore replaced/removed images;
temporary URLs are released. The unchanged worker receives normal RGBA inputs,
while reports carry file provenance/metadata only.
[Source lifecycle and checks](local-uploads-2026-11-19.md).

# Source preparation before surface comparison — 2026-11-19

`unwrap-source.js` adds independent source positioning/photometry, reference
ROI/exclusion support and profile-tolerance envelopes. Their fixed-owner atlases
are compared without another global shift. Scope/coverage are reported separately;
source previews/inverse coordinates agree. Fine manual matrices compose after
reference-only calibration and enter the v2 report. Native/API are unchanged.
[Pipeline and tests](atlas-quality-2026-11-19.md).

# Reference-stitching worker — 2026-11-19

A separate on-demand OpenCV.js worker extracts/matches reference features and
fits bounded corrections. Browser recipes retain a content/geometry signature;
current frames never determine it. Cut rendering and 2D ownership apply the same
correction to both atlases, with inverse mapping for source clicks. Native/API
and the inspector module remain unchanged. [Pipeline and partial-fit limits](reference-stitching-2026-11-19.md).

# Whole-atlas inspection worker — 2026-11-19

The renderer additionally builds current/reference atlases with identical fixed
angular ownership and no overlap averaging. `unwrap-inspection.js` registers and
compares the complete canonical atlas, produces circular connected regions and
gates its independent verdict on input/support/registration completeness. All
state remains browser-local and GET-only. [Pipeline and limits](surface-atlas-inspection-2026-11-19.md).

# Browser surface workspace — 2026-11-19

The independent unwrap UI reads camera state/images through the existing GET API.
Six-point boundary geometry, angular placement and source/reference comparison run
in a Web Worker using a pure JS raster core. Browser-local recipes are scoped by
dataset/bucket and never enter native camera settings. Cancelled jobs cannot replace
newer inputs. [Data flow, geometry and limitations](surface-unwrap-2026-11-19.md).

# UI inspection state — 2026-11-16

The camera toolbar owns UI-only per-camera batch snapshots and one pending
toolbar-request guard. Buttons and shortcuts share handlers; late replies retain
their original camera identity. The existing backend API is unchanged.
[Interaction and verification](inspection-header-2026-11-16.md).

# Core/cache integration — 2026-11-16

Both geometry paths call the single eefefce4-based src/production/core inspector.
Reference::prepared(scale, exclusions) supplies immutable mask variants to feature
preparation and comparison; InspectionGeometry detaches the per-request valid mask.
Confirmed-label settings and adaptive settings serialize independently. See [resolution, preservation and checks](git-pull-2026-11-16.md).

# Local adaptive ROI extension — 2026-11-09

The local Inspector optionally uses immutable AdaptiveRoiReference features and
per-request geometry before native comparison. It rectifies raw current pixels,
passes a detached comparison mask through InspectionGeometry, and maps displays
back to current-image coordinates. App owns settings, incomplete verdicts and
learning guards. [Details](adaptive-roi-2026-11-09.md).

# Inspection exclusions — 2026-11-06

Draw polygon zones in the camera setup to skip inspection inside them. Exclusions
are persistent per-camera holes in ROI and also apply to the seam.
[Behavior, reasons, checks and limits](exclusion-zones-2026-11-06.md).

# Automatic learning — 2026-11-04

The current UI uses one «Дообучить» button and automatically encloses all nonzero
raw heatmap pixels with 10 source pixels of padding, matching the main application.
The inspected version pins the source crop. Manual selection notes below are historical.
[Implementation, compatibility and checks](automatic-learning-2026-11-04.md).

# Current update — 2026-11-04

Native core/service are now pinned to main-app 20909141. The production robust matcher
replaces the local callback hook; metrics, upper corners and sensitivity-zero seam
behavior follow production. Existing data paths remain compatible. Read
[the authoritative sync report](production-sync-2026-11-04.md) for current behavior,
validation and the pre-existing missing reference names. Older sections below are history.

## Local learning hook (2026-11-02)

Local Inspector optionally supplies LearnedComparison to native inspectFrame. It updates the difference map
before native component filtering/scoring; Service and no-hook callers keep the original matcher.
Storage and locking stay unchanged. See [local-learning-v2.md](local-learning-v2.md).

## Update 2026-11-02

Native core/service now use snapshot 52aa31ea. App owns native Service for learning persistence and attaches
its camera/reference-scoped cases to the local Inspector. Per-camera locks protect the untouched latest source
image and version used by learning API requests. New routes/UI and data paths: [learning-port.md](learning-port.md).

# Architecture

FrameSource (frame/camera_N/*) -> BatchScheduler (every period_ms: next frame of each of 10 cameras)
 -> ThreadPool -> Inspector[camera] (reference + ROI + params) -> DecisionLogger (JSONL) + WebServer (HTTP/JSON + static UI)

Modules (src/): source, pipeline, inspect, reference (references, ROI, auto-ROI), config, log, web.
Status: CppDetector (test bed): inspect/, app/, web/ + UI. The production module now lives in defect-detector-v3/cppAnalisSurface (see integration-in-main-application.md).

Endpoints: GET /api/testfiles, /api/state, /api/status, /api/frame, /api/overlay, /api/log, /api/reject_image;
POST /api/camera, /api/reference_testfile, /api/inspect_testfile, /api/global, /api/run, /api/reference, /api/reference_upload, /api/inspect, /api/inspect_upload, /api/inspect_all, /api/auto_roi.
Emulated stream: App::loop -> tick() every period_ms, one frame per camera (cursor cycles through frames except the reference), std::async per camera.
