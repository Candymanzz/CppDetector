# Width events and actual inspection settings — 2026-11-24

Both bottom/SVG plots now separate side deviation from width; the extra
worst-side source line is removed. Short changes are orange, enough-length
events red. Used settings and stale-result feedback are explicit. [Diagnosis, behavior and evidence](seam-plot-2026-11-24.md).

# Both mask boundaries and width — 2026-11-24

The owner returned to mask/path development. One surviving edge no longer
hides loss of the other; cross-sections check both sides and width independently.
23 CTest, eight JS suites and nine Chrome/HTTP groups pass, including two real
reference paint uploads. Original photos need labelled calibration. [Method, evidence and limitations](seam-pair-2026-11-24.md).

# Seam captured on reference, one tolerance — 2026-11-24

After the owner identified a jump to a black square, camera 6 now uses a frozen
reference boundary with one uniform source-pixel tolerance. Shape/identity no
longer refit on each current image. Capture/persistence/invalidations and real
photo checks pass; f92 remains explicitly incomplete.
[Workflow, implementation and limits](seam-reference-2026-11-24.md).

# Connected seam mask — 2026-11-24

New opt-in mask/path search detects the weak camera-6 seam along 98.79–100% of
16 snapshot frames, instead of fragmented native bright-ridge samples. There is
a real mask preview and explicit incomplete-control handling for lost/gapped
paths. Four deviations remain unlabelled. [Method, checks and limits](seam-mask-2026-11-24.md).

# Camera sidebar and seam visualization — 2026-11-24

Follow-up: the whole image and compact verdict now remain fixed on the left;
all tools, detailed results and seam plot scroll inside the right menu. Real
photo/wheel/ROI/viewport checks and four UI suites passed.
[Correction and checks](sidebar-seam-2026-11-24.md#follow-up-keep-the-whole-bucket-and-verdict-visible).

Camera settings now scroll independently from the image/document. Native per-row
seam diagnostics restore the old overlay graph and drive a readable plot with
tolerances, overshoots, gaps and exclusions. [Workflow, cause and checks](sidebar-seam-2026-11-24.md).

# Local images in the unwrap workspace — 2026-11-19

Use **Свои изображения** or **Загрузить фото** for each camera. Uploaded files
drive previews, the panorama and existing inspection; missing cameras remain
explicit. Files stay in browser memory, with current profile references retained.
16 real Chrome/HTTP scenarios and 3 layouts pass.
[Workflow, validation and lifetime](local-uploads-2026-11-19.md).

# Atlas false rejections — 2026-11-19

Sources now align independently before unwrapping, with camera ROI/exclusions
and explicit profile comparison tolerance. Accepted positions 2–4 no longer
reject at the unchanged area threshold; injected defects remain detectable.
Manual seam position/scale controls are available. Full automatic geometry
remains partial. [Evidence, implementation and limits](atlas-quality-2026-11-19.md).

# Reference alignment and clear seams — 2026-11-19

The unwrap display uses one view per pixel, removing overlap ghosting. A local
reference-only ORB worker fits supported camera overlaps and explicitly marks
partial/failed joins. Bucket-1 front views 0/3 improve; a complete calibrated
panorama is still open. [Behavior, measurements and limitations](reference-stitching-2026-11-19.md).

# Frame selection at the top of the unwrap workspace — 2026-11-19

**Кадры для инспекции** now shows four per-camera file lists before the result,
with previous/next, explicit filenames and a shared frame-number control. Source
previews and reference mode remain synchronised. [Workflow and checks](frame-selection-2026-11-19.md).

# Whole-surface atlas inspection — 2026-11-19

The unwrap tab now inspects one developed surface: fixed camera ownership without
feathering, global registration, bounded brightness correction, connected defect
regions and an experimental bucket verdict with coverage/failure guards. There
are aligned/control-reference views and a JSON report. [Pipeline, tests and actual observations](surface-atlas-inspection-2026-11-19.md).

# Bucket surface unwrap — 2026-11-19

New **Развёртка ведра** tab: separate product groups, editable surface boundaries,
raw-to-rectified previews, circular image, source provenance, coverage and exploratory
reference differences. Browser recipes and PNG/JSON export are separate from native
profiles; no automatic defect verdict. [Workflow, geometry, checks and limits](surface-unwrap-2026-11-19.md).

# Camera inspection toolbar — 2026-11-16

The sticky camera header shows the last whole-camera defective-frame count and
separate incomplete/error counts. It contains both inspection buttons and
Ctrl+Enter / Ctrl+Shift+Enter shortcuts. [Behavior and checks](inspection-header-2026-11-16.md).

# Current combined source — 2026-11-16

main now follows origin/main b0535b0. Its eefefce4/ECC512 core and confirmed-label
tracking coexist with the local adaptive ROI extension. Local camera settings and
five learned cases are preserved. Release and 20/20 CTest pass; see [resolution, preservation and checks](git-pull-2026-11-16.md).

# Label contour tracking ? 2026-11-13

[Confirmed contour, exposed-bucket masking and verification](label-tracking-2026-11-13.md).

# Adaptive reference ROI — 2026-11-09

Optional per-camera feature correspondence, bounded smooth geometry, checked-area
mask and explicit incomplete-control verdict. Old profiles default to off; learned
normal crops are inactive in this mode. [Operation, implementation, checks and
limits](adaptive-roi-2026-11-09.md).

# Inspection exclusions — 2026-11-06

Draw polygon zones in the camera setup to skip inspection inside them. Exclusions
are persistent per-camera holes in ROI and also apply to the seam.
[Behavior, reasons, checks and limits](exclusion-zones-2026-11-06.md).

# Learned pattern support — 2026-11-04

Pattern support is now computed against the primary reference at the matched placement,
rather than translated from its original bbox. This fixes normal edge regions that
continued to reject after learning. [Details and validation](learned-placement-fix-2026-11-04.md).

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

Current experiment (2026-11-02): [local-learning-v2.md](local-learning-v2.md) adds independent search,
masked application, moving-shadow comparison and per-case diagnostics. Native matching remains selectable.

Current update (2026-11-02): core/service pinned to 52aa31ea, with region learning and independent alignment controls.
Read [learning-port.md](learning-port.md) and [native provenance](../../src/production/SOURCE.md).
The earlier snapshot descriptions below are historical.

# Knowledge Base (for Claude)

Current backend (2026-10-26): branch 123 mirrors position-x-y native core 885a79ed; see algorithms.md and
src/production/SOURCE.md first. Old score/working-pixel/edge notes below are historical. Use the separate native profile.

Purpose: before changing or adding anything, read the relevant file here to learn what may be changed,
what must not, and why a decision was made. Written in English, split by topic. Update immediately
after every significant decision.

| File | Content |
|---|---|
| architecture.md | modules, data flow, threading |
| decisions.md | decision log (ADR style: context -> decision -> reason -> consequences) |
| algorithms.md | inspection algorithm, parameters and their effect |
| constraints.md | invariants / things that must not change, with reasons |
| data.md | datasets (frame/frame, frame/frames36, frame/test-pr-ph) and per-camera peculiarities |
| integration-in-main-application.md | porting the inspection core into the main app (Defect Detector V3 / analisSurface): contract, plan, change log of every integration step |
| changelog.md | what changed and when |

Rules:
- Never use Python, legacy services or the old result.json/heatmap.u8 files as input or reference for CppDetector itself (user's explicit wish: build from scratch). The exception is the integration work, where the Python analisSurface API is the contract to reproduce (see integration-in-main-application.md).
- A decision without a recorded reason is not a decision: always add "why".
- Every integration action is logged in integration-in-main-application.md (section Change log).

Current surface algorithm (2026-11-13): [production inspection port](production-inspection-port.md).
