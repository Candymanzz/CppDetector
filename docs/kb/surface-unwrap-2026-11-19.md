# Experimental bucket surface unwrap — 2026-11-19

Stitching follow-up: the display now uses clear single-view seams and attempts
bounded reference feature alignment, with explicit partial status. The initial
full-overlap averaging described below is historical. See [current stitching and
remaining discontinuities](reference-stitching-2026-11-19.md).

Follow-up: the owner subsequently requested actual inspection through unfolding.
The new workspace now has an independent whole-atlas verdict, registration and
component analysis; see [current pipeline, results and limitations](surface-atlas-inspection-2026-11-19.md).
The preview-only behavior below records the first implementation before that follow-up.

Scope: standalone `D:/Pj/CppDetector`, branch `main`. This is a new browser
workspace, not a replacement of the eefefce4 native inspector. The main
defect-detector repository currently has a different compensation implementation;
its November 18 notes do not describe the standalone inspector automatically.

## Operator workflow

Open **Развёртка ведра**, or append `#unwrap` to the local application URL.
Choose **Ведро 1** (side cameras 0, 1, 3, 4) or **Ведро 2** (5, 6, 8, 9).
Cameras 2 and 7 look into the bucket from above and are deliberately omitted.
The two products are never combined into one panorama.

Choose a one-based position in the camera frame lists, then override individual
frames in the four source cards if necessary. This is only a convenient ordinal
selection: frame identifiers differ between cameras, and neither the API nor this
UI establishes that selected frames came from the same physical acquisition.
The operator must choose frames of one product. **Эталоны камер** instead uses
the current per-camera references as all sources. **Обновить кадры и эталоны**
re-reads API state and image content; it does not rescan dataset folders inside
the already running native application.

Each card shows the raw source, its surface boundary and its rectified strip.
Select a camera and drag six handles in the large source editor. Points 1–3
define the upper boundary of the finished scan; points 4–6 define its lower
boundary. The middle points control the curvature. The order is defined in
finished-image coordinates, so an upside-down source has its upper scan boundary
at the bottom of the source image. A 180-degree action swaps/reverses the curves;
an independent horizontal mirror handles a reversed view.

For the known `frames36` path and 1224×1024 references, hand-estimated initial
boundaries are supplied for the first images; other datasets start with a generic
rectangle. Starting camera order and angles are assumptions, not calibration.
Change a camera's centre angle, visible angular span (30–170 degrees), projection,
or neighbouring position to align matching print. Exclude an unusable camera to
leave a visible gap rather than stretch the remaining image over its sector.

The panorama has current, reference, differences and coverage views; a seam-origin
setting, optional guides and fit/1:1/2:1 display. Clicking the panorama identifies
the contributing camera, frame and original pixel, and marks its position on the
source. Overlap contributors are listed. Output widths are 1200/2000/3200 pixels
with heights 312/520/832. These dimensions are a display convention, not physical
millimetres. The source editor preserves aspect ratio for accurate pointer input.

PNG export contains the chosen raster view without guides or the selection marker;
unavailable image pixels remain transparent. Geometry recipes are saved in browser
localStorage by dataset and bucket, with JSON export/import for transfer. JSON
contains dataset identity, geometry and a selection manifest. Import restores
geometry only; selected input files remain operator choices. A foreign dataset,
foreign bucket, malformed, duplicate-camera or degenerate recipe is rejected.
Storage failure is visible, and JSON export remains available. These recipes do
not change the detector's ROI, references, camera configuration or learned cases.

## Geometry and experimental comparison

`web/unwrap-core.js` is shared by Node tests, small previews and the worker.
Each boundary is a quadratic curve through its left/middle/right source points.
For cylindrical projection, angular position maps horizontally through a sine;
the source point then interpolates between upper and lower curves. Linear mode
only straightens the boundary patch. Pixels use bilinear interpolation.
Changing row widths can describe a taper approximately; this is not a calibrated
perspective model or an exact developable conical-sector construction.

Four angular sectors wrap across 0/360 degrees. Overlap edges feather over up to
eight degrees. Missing/disabled inputs contribute no colour or weight; supported
pixels are normalised by available weights. **Заполнено кадрами** measures raster
support under the chosen geometry, not proven coverage of the real bucket.
The initial 120-degree spans at 90-degree centres fill the display, but their
physical footprint and overlap still need checking.

The reference is rendered using the same geometry. Differences use the maximum
absolute RGB-channel difference, thresholded by an independent browser control.
They are computed per camera before blending and combined with a maximum, so
opposite deviations in overlapping cameras cannot cancel. Statistics use only
pixels with a source/reference pair from the same camera. Differently sized
source/reference images do not form a valid pair. No reference means no comparison,
not a zero-difference success. Coverage distinguishes paired support, current-only
support and absent current pixels.

There is no automated registration, illumination compensation, defect component
filtering, learned normals, detector exclusions or acceptance/rejection verdict in
this workspace. Motion, exposure, interpolation and seam mismatch can all produce
differences. The new RGB threshold is unrelated to the native sensitivity and
reject threshold. Native inspection and its decision log keep their existing API.

`unwrap.js` uses only GET `/api/state` and `/api/frame`. Images are reduced to at
most 1200 pixels on their longest side for browser memory use; a larger output
does not recover more source detail. `unwrap-worker.js` performs large raster work
off the UI thread. Rebuilds terminate the old worker; generation checks discard
late image loads. Any changed inputs immediately invalidate export and statistics,
including a state refresh. Existing application assets use a new app.js cache key.

## Verification and boundaries

- `node tests/unwrap_core_test.cjs`: 6438 assertions covering source mapping,
  rotations/mirroring, cylindrical projection, recipe validation/fold rejection,
  circular wrap, self comparison, absent/disabled cameras, absent references,
  incompatible pairs and opposite differences in an overlap.
- Syntax and all four existing UI suites pass: startup/learning/label controls,
  inspection header and shortcuts, exclusions, adaptive ROI.
- Headless Chrome uses the actual existing CppDetector executable and HTTP API,
  a temporary copy of its profile and actual frames36 images. Both bucket groups,
  source provenance, self comparison, missing-image 404 handling, recovery,
  disabled-sector holes, pointer editing, JSON import/rollback, PNG/JSON export,
  rapid bucket changes, and all supported output sizes pass.
- On the selected frame-list position 50, the visual RGB comparison marks 98.3%
  of paired support as different from the chosen references; the photos show an
  unlabelled product. This is a local observation, not defect-classification
  accuracy or a verified same-cycle batch. Self comparison gives 0%.
- Layouts at 1440, 1024 and 390 pixels have no horizontal document overflow;
  wide/mobile screenshots are inspected. No browser exceptions or server write
  requests occurred in the browser scenario.

No C++ rebuild/CTest, physical camera/PLC cycle, Linux, calibrated geometry,
measurement accuracy, labelled production defect evaluation or 500 ms machine
budget verification is performed for this browser-only extension. Initial seams
still show scale, brightness and duplicate-print discrepancies. Next work should
establish source grouping/synchronisation and geometric calibration before
selecting registration, comparison and production verdict rules.

The main repository records the task in
`knowledge/analis-surface/2026-11-19-cppdetector-surface-unwrap.md`. Local task
artifacts contain the staged files, baseline/data hashes, browser screenshots,
temporary profile and installation backup. They are not runtime dependencies.
