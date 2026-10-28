# Decisions

## D25 - Remove the half-pixel alignment zero offset (2026-10-28)

Context: identical reference/current thumbnails can produce a confident 0.5-pixel shift when phaseCorrelate
uses an odd optimal DFT dimension. At Downscale 0.2 the real camera-4 ROI reproduced a false 0.675 mm shift
and shift_exceeded with a 0.5 mm tolerance, despite bitwise-equal prepared inputs.
Decision: zero-pad reference/current thumbnails and the existing Hann window on the right/bottom to equal,
even optimal DFT dimensions. Cache the padded reference/window. Retain the unpadded thumbnail size for
conversion to ROI/original-image coordinates; padding does not change image scale. Keep the private reference
clone before correlation, because OpenCV can modify working buffers. Do not alter thresholds or discard measured shifts.
Validation: regression fails before the fix; identical-input cases cover horizontal/vertical ROIs, repeated calls and
four scales. Known signed/subpixel motions retain their direction and original-image units within half an analysis
pixel. Existing cache/concurrency, metric and adapter/core parity tests remain required, including the actual camera-4
self-comparison and tolerances 0.5-4 mm when frames36 is supplied to production_parity_test.

## D24 - Inspection and seam correctness audit (2026-10-28)

At the user's request, correct abnormal inspection behavior in the local harness:
- Reference and current frame use the same explicit rounded destination size at fractional Downscale. Using fx/fy for only the reference caused an identical 383x321 image to score 4.487% at 0.73.
- Blob size and score use actual 8-connected foreground pixels after opening. External-contour filling counted a clean ring interior as anomalous (11549 pixels instead of 3930) and used the enclosed area for minimum-size filtering.
- An enabled seam check that cannot find/fit the seam rejects. The adapter uses seam_not_found unless another rejection already supplied a reason. A missing measurement must not silently certify a good seam; the score remains the surface score.
- Propagate min_contrast through native settings and expose it in the seam UI. Reject enabled lines shorter than 50 original-image pixels and zero-area ROI geometry; clear old pixel seam geometry when the reference resolution changes. Disabled seam checks retain no influence on the verdict.
- Capture and serialize camera setting snapshots, flush pending saves before inspection/start, ignore results/images from a different selected camera, and disable the stored line after endpoint deletion.

Evidence: inspection_regression_test covers identical downscales, ring holes/minimum area, missing seams, configured contrast, smooth curvature/local bends, grayscale immutability and invalid geometry. Existing core/adapter parity and shared-cache tests remain required. Real frames and browser checks are recorded in artifacts/inspection-audit-20261028/REPORT.md. Dataset agreement is a consistency check, not a measurement of defect recall against ground truth. The user profile was not changed by the audit; all seam checks in that profile were disabled.

## D23 - Millimeter controls without a calibration step (2026-10-27)
Context: the user explicitly deferred calibration and requested simple immediately available mm settings.
Decision: remove the calibration card, measurement overlays and picking workflow; allow physical tolerance fields
without measured calibration. Use per-camera provisional horizontal/vertical scales estimated from bucket views,
referenced to 1224x1024 and scaled with input resolution. Identify them as estimated in the UI and logs.
Preserve existing local pixel tolerances by converting them to provisional mm on first load; retain explicit mm values.
Expose estimated_mm_per_pixel for later empirical adjustment; existing backend calibration data remain compatible.
This supersedes D22's requirement to disable mm controls until calibration. Accuracy is provisional by user choice.

## D22 - Calibrated horizontal/vertical displacement tolerances in millimeters (2026-10-27)
Context: the user needs physical displacement tolerances independent of digital resize and analysis scale.
Known product dimensions: bottom diameter 190 mm, top diameter 193 mm, axial length interpreted as height 213 mm.
The views are oblique and the surface is curved; those dimensions alone do not establish a spatially accurate camera calibration.
Decision: store separate normalized horizontal/vertical measurement spans and their known lengths for each camera.
Store physical tolerances independently, derive floating-point pixel values at the current resolution, and round only
when building the analysis envelope. Calibrated global alignment uses the same physical limits, without the old factor four.
Until both axes are calibrated, keep legacy pixel inspection and disable the millimeter controls rather than fabricate a conversion.
Expose physical shifts, limits, effective tolerances and analysis pixel size. Document local calibration limits and rounding.
Validation: unequal axis scales, fractional pixels, doubled input resolution, several Downscale values, physical shifts
below/above 2 mm, invalid calibration atomicity and restart preservation; inspect the real UI on an isolated profile.

## D21 - Local linear score amplification x3 (2026-10-27)
Context: real defects have scores that are too small; the user requested a moderate threefold amplification.
Decision: calculate anomaly_score = min(1, area_percent / 5 * 3) in the local core before threshold comparison.
Keep the existing profile thresholds; expose score_multiplier=3 in inspection results and decision logs.
Why: this preserves proportionality to detected area until saturation and makes the displayed score drive rejection.
Consequences: this explicitly supersedes D20's unchanged-score requirement for CppDetector; the production repo is unaffected.
Record the calibration in snapshot provenance and cover clean frames, proportional scores, saturation and rejection.

## D20 - Local production-core mirror (2026-10-26)
Context: user needs to test the same inspection/settings as position-x-y in this local UI before deploying.
Decision: vendor the unchanged 885a79ed native core; use a thin unit/UI adapter, identical native seam and no edge verdict.
Native defaults/units/score replace the old local amplified score and working-pixel semantics. Preserve the existing UI.
Keep old configs/references intact; import scene geometry into a separate _position-x-y profile, with fresh native defaults.
Why: local outcomes must predict the source detector given the same images, ROI, settings and OpenCV build.
Consequences: no production repo/deployment change; native parity tests and provenance are required for updates.

## D1 - Build stack (2026-10-20)
Context: no C++ OpenCV on machine; Python OpenCV unusable; MSVC Build Tools 2022 present.
Decision: CMake + vcpkg manifest (vcpkg at D:\vcpkg), opencv4 minimal features (jpeg, png), cpp-httplib, nlohmann-json.
Why: one reproducible dependency flow, header-only web server keeps the app a single exe.
Consequences: first configure builds OpenCV (20-40 min); later builds are cached.

## D2 - Alignment strategy (2026-10-20)
Decision: only translation via cv::phaseCorrelate + a per-camera shift tolerance (px) applied in diff (min over neighborhood). No ORB/ECC.
Why: 500 ms budget for 10 frames; user said micro-shifts are minor and should be handled by a tolerance setting.
Consequences: rotations/large shifts are not corrected; flagged as reason `shift_exceeded`.

## D3 - Reference selection (2026-10-20)
Decision: reference per camera = earliest frame by folder name; can be re-picked from UI.
Why: user wants to emulate real inspection: first frame becomes reference, later frames are checked against it.

## D4 - First measurements (2026-10-20)
Console runner, 10 cameras in parallel via std::async, full-frame ROI, sens 0.5, includes JPEG decode: batch wall time 190-260 ms (budget 500).
Clean frames score ~0.00-0.05%. Synthetic defect (ellipse + line, ~0.5% of frame) scores 0.5-0.66% on all cameras and zones are localized correctly (also on overexposed cameras 6-9).
Known: cam 0 shows a tiny false zone on the bucket rim (sub-pixel/shift edge) -> an ROI polygon and/or higher shiftTolerance is the remedy, not code changes.
Open: sensitivity->threshold mapping (80..5) is a first guess, tune with real defects.

## D5 - Web UI and API (2026-10-20)
Decision: cpp-httplib server serves static files from <repo>/web (plain HTML/JS/CSS, no bundler) and a JSON API under /api. App (src/app) owns state; routes (src/web/routes.cpp) are thin.
Why: single exe, no Node toolchain, easy to iterate on the UI without rebuilding C++.
Consequences: the UI must be launched with cwd-independent paths (CD_SOURCE_DIR compile definition = repo root; override with --frames/--web/--port).
- Per-camera mutex in App::Cam guards reference/ROI/params/last result; the emulated stream and manual inspections serialize per camera, run in parallel across cameras.
- Config persisted to config/config.json (params, ROI, reference name; uploaded reference stored as config/ref_cam<N>.png). Every change autosaves.
- Frame names accepted by the API must exist in the camera's own listing (no path traversal). Reject images served only by plain file name from logs/rejects.
- Test aids: synthetic defect injection (global stream flag or per-run checkbox), "inspect all frames of camera", upload of an arbitrary frame/reference.

## D6 - Auto-ROI (2026-10-20)
Decision: blur -> Otsu -> open/close -> blob closest to centre and largest -> convex hull -> approxPolyDP (<=14 vertices). Only a proposal; applied then edited by hand.
Result: cam 0-5 get a plausible but too generous polygon (includes neighbouring bright objects at the left/bottom). Cam 6-9 (overexposed) get an almost full-frame polygon. Needs manual edit; improve only if the user asks.

## D7 - Heatmap rendering (2026-10-20)
Heatmap colours are normalised to the detection threshold (thr -> mid colour, 2*thr -> red); values below thr/2 are not painted. Otherwise JPEG/lighting noise paints the whole bucket.

## D8 - Findings on the real label shots (2026-10-20)
Setup: reference good1, ROI polygon over the bucket body (excludes lid reflections and neighbouring objects), sensitivity 1.0, reject threshold 0.1%.
- downscale 0.4: good2 0.00%, bad1 0.62%, bad2 0.38%, bad21 0.35% -> clean separation, ~60-110 ms per frame (single).
- downscale 0.6/0.8/1.0: bad scores grow (bad1 1.4/2.2/3.1%), good2 stays 0.00%, but time per frame grows 130/230/330-390 ms (single frame) - too slow for 10 parallel frames in 500 ms without enough cores.
- Without ROI, lid glare and a neighbouring object produce large false zones -> ROI is mandatory for this scene.
- Dark images: contrast of defects is low, sensitivity had to be at the maximum (diff threshold 5). Candidate improvement: contrast stretch by ROI percentile before diff (not done yet) so that sensitivity is meaningful across lighting.
- bad1 false zones along the label's left/right edges come from bucket rotation (translation-only alignment, D2). Candidate improvements: exclude edge band from ROI, or local/affine alignment.
- Added Inspector guard: frame size != reference size -> reason `size_mismatch` (no crash; verdict NO_REFERENCE).

## D9 - Benchmark: 10 cameras in parallel over all frames (2026-10-20)
Tool: `cppdetector --bench [--verbose]` (src/app/bench.cpp). Machine: 8 hardware threads. One batch = one frame per camera, 10 tasks via std::async.
- Camera dataset 960x803, downscale 1.0, ref = earliest frame, 19 batches x 10 cameras = 190 frames: total ~4.3 s, batch avg 225 / p95 255 / max 255 ms (with JPEG decode: avg 240 / max 287 ms). Budget 500 ms met with ~2x margin. 0 rejects (clean frames).
- Label shots 2448x2048 (reference good1, body ROI, sens 1.0, thr 0.1), 10 virtual cameras: downscale 0.4 -> batch avg 273-307 ms (OK); 0.5 -> 395-437 ms (OK, thin margin); 0.6 -> 592-653 ms (over budget). 30 of 40 frames rejected = exactly the 3 bad files x 10 cameras; good2 never rejected.
- A single frame alone takes ~60 ms (960x803) but ~210 ms when 10 run at once: the batch is CPU-bound (10 tasks on 8 logical threads, likely 4 physical cores). cv::setNumThreads(1) made no real difference.
- Next speed levers if needed: phaseCorrelate on a much smaller image (it is a full float DFT), avoid redundant float conversions, reuse buffers per camera, stable thread pool instead of std::async.

## D10 - Benchmark on frames36 (1224x1024, 10 cameras, 99 batches = 990 frames) (2026-10-20)
Defaults sens 0.5, no ROI, ref = idx 1 of each camera, 8 logical threads.
- downscale 1.0: batch avg 386 / p95 411 / max 490 ms (!thin margin; max with decode 431) - total 38 s for 990 frames.
- downscale 0.7: batch avg 191 / p95 241 / max 279 ms.
- downscale 0.5: batch avg 95 / p95 106 / max 115 ms (with decode up to 152 ms).
- Rejects at thr 2%: 199 / 178 / 160 of 990 (1.0 / 0.7 / 0.5) -> verdicts depend on downscale, so thresholds must be tuned at the downscale that will be used in production.
- Recommendation: run production at downscale 0.7 (or 0.5) for 1224x1024 frames; 1.0 leaves almost no margin to 500 ms.

## D11 - Integration into Defect Detector V3 (2026-10-20, planning only)
Decision: build a C++ drop-in replacement of analisSurface (same HTTP contract) using the CppDetector inspection core; no new features (no learning, FP zones, sub-zones). Details, contract facts and open questions: integration-in-main-application.md.
Why: the main app's Java orchestrator talks to analisSurface over HTTP + shared memory; a replacement is transparent if the contract is identical.
Status: reviewed branch core-dev-p; awaiting user approval before any code.

## D12 - Integration decisions approved by the user (2026-10-20)
Score scale `min(1, area%/5)`; only real settings (sensitivity, threshold, shift_tolerance, min_blob_area, inspect_scale) per camera profile; the orchestrator and the UI are adapted to cppAnalisSurface (isolated new Java client, new settings panel/proxy), legacy Python path left untouched behind `python_detector.backend`; one multi-threaded process; the module lives in the main repo `defect-detector-v3/cppAnalisSurface`; the user commits, Claude never commits there. Details and file list: integration-in-main-application.md.

## D13 - Java serialised requests per client (2026-10-20)
Context: `InspectPythonExecutor` used Semaphore(1) per client (Python/GIL). Decision: lane permits come from `python_detector.server_concurrency` (default 1 keeps Python behaviour). Why: a single C++ process must receive all 10 camera requests at once. Consequence: for cpp use `python_parallelism: 10` (see D16: 1 serialises the cameras), `python_server_pool_size: 1`, `server_concurrency: 10`, `python_request_parallelism: 10`.

## D14 - HTTP thread pool must exceed client connections (2026-10-20)
Finding: cpp-httplib serves one keep-alive connection per thread. With pool `workers+4` (8) and 10 Java connections, two requests waited the keep-alive timeout (5 s). Fix: pool `max(64, workers*4)`; CPU concurrency is bounded separately by the `workers` limiter. Do not shrink the pool.

## D15 - Inspector changes for the service (2026-10-20)
Per-scale prepared reference cache (inspect_scale arrives per request/profile); analysis restricted to ROI bounding box; shift estimation on a <=256 px thumbnail (cheap, subpixel via phaseCorrelate+Hanning); heatmap built on the downscaled image. Measured: 10 parallel full-size frames 152-179 ms (CppDetector's first version: 386 ms).

## D16 - python_parallelism = 1 serialised the whole detector stage (found 2026-10-20 from the user's log)
Symptom: with the cpp detector (38-44 ms per call) the 10-camera cycle still took ~680 ms: all cameras started together and captured in parallel (234-302 ms), but results finished one after another every ~40 ms (= one detector call), and `idle = total_ms - pipeline_ms` grew linearly (1, 45, 77, 112 ... 281 ms).
Cause: `PipelineCameraRuntimeService` creates the `stage-python` executor with `python_parallelism` threads (also the number of HTTP clients). My config.exemple set it to 1 ("one client per process") -> only one detector call at a time. My mistake in D13/integration doc.
Fix: `python_parallelism: 10` (= number of cameras processed at the same time). Expected cycle: capture of the slowest camera (~300 ms) + ~50 ms detection + decision, i.e. ~350-400 ms instead of ~680 ms. To be re-measured after the user's config change.
Rule: `python_parallelism` must be >= number of cameras whose frames can arrive together; `python_request_parallelism` is capped by `camera_parallelism`.

## D17 - cppAnalisSurface logging (2026-10-21)
Context: the user wants logs that let Claude tune the analysis (sensitivity, threshold, shift_tolerance, min_blob_area, inspect_scale, ROI) from real production data.
Decision: `cppAnalisSurface/logs/` (app root: nearest parent of the exe holding CMakeLists.txt; override `--log-dir`). `cas-YYYY-MM-DD.log` = events (start, reference/ROI/settings changes, errors, slow >400 ms, one summary line per product per minute). `inspections-YYYY-MM-DD.jsonl` = one record per inspection with settings, scale, shift + phase-correlation response, brightness gain/means, pixel threshold, diff mean/p99/max in ROI, blob counts, biggest dropped blob (near-miss), per-zone area and peak diff, timings incl. worker-slot wait and shm read, camera id parsed from `<product>#cam=<id>`.
Why sampling: 10 cameras every ~400 ms = ~25 records/s = ~2-3 GB/day if all are written. Rejects, near-misses (score >= threshold/2), requests >400 ms and all non-`/inspect-shm` calls are logged in full; calm frames 1 of 20 per product. Daily cap 500 MB for the jsonl, files older than 14 days are deleted at start.
Consequences: extra diagnostics are computed on every inspection (a few small OpenCV ops on the ROI box, histogram for p99); not part of the API response (C12 untouched). Verdicts depend on inspect_scale (D10), so the record always carries it.

## D18 - Label seam geometry check (2026-10-21)
Context: the seam (thin bright line along the label edge, camera 4 of frames36) must be straight; a kink in the middle = reject, the same kink near the ends = acceptable (tunable). Reference diff cannot express a position-dependent tolerance and the bucket pose changes between shots, so diff on the 2-3 px line gives false zones.
Decision: separate measurement, not a diff (src/inspect/seam.{h,cpp}). User sets two ends of the seam (original px) + search band. Per cross-section: coarse straight line from the ridge maximum, then left/right edge of the line at half height (sub-pixel). Each edge is fitted with a robust quadratic (line + cylinder curvature); deviation = residual. Tolerance by position: `tol_mid` in the middle, `tol_edge` within `edge_frac` of each end, first/last 3% not evaluated (line-end artefacts ~10 px). Reject if any |residual| > tolerance; reason `seam_deviation`. Frames where the line is not found (no label) do not reject by the seam (the main diff covers a missing label). Combined verdict = main diff OR seam; the primary reason is the main one, `seam` object is always in the result.
Evidence (python prototype, camera_4, 1224x1024): 80 labelled frames: edge residual 0.2-0.4 px; 20 unlabelled frames: seam not found. User examples: kink in the middle 3.7 px at t=0.43 (reject, ratio 3.7); end deviation ~3.4 px at t~0.04 (ratio 0.7 with tol_edge 5 -> ok); a straight seam 0.27.
Consequences: seam settings are per camera (config `seam`), UI card "Стык этикетки". The seam strip should be excluded from the diff ROI if it produces false zones. NOT yet compiled/tested on the build machine at the time of writing (no MSVC/OpenCV C++ on the dev box) - rebuild and check first. Not yet ported to cppAnalisSurface (would map to the `geometry_status` field of the Java contract).

## D19 - Seam check generalised to a polyline, label-edge (step) mode (2026-10-22)
Context: small defects on the label edge (a 3 px white tooth in the printed outline) show up as a local deformation of the edge line; checking its geometry like the seam (D18) finds them without reference diff, which is blind to such details when the pose changes.
Decision: `SeamParams.pts` = control points (>=2), a Catmull-Rom spline is the search axis. Mode `ridge` = D18 behaviour (one polynomial reference). Mode `step` = one edge (max signed gradient across the axis, parabola sub-pixel), `polarity` selects the wanted step direction, the reference is a LOCAL robust line fit in a sliding window `smooth_win` (px) - a global polynomial cannot follow an edge that is only approximately outlined and then every real bump drowns in the fit error. `skip` = segment numbers excluded from the check (static printed features that cross the edge, e.g. the vertical white stripe), with `skipMargin` px on both sides; skipped rows are also removed from the fits. `max_gap` rejects a too long continuous stretch without an edge (1 = off). Legacy `p0`/`p1` configs are read as two points. UI: "Взять из точек ROI" turns chosen ROI vertices into the line.
Evidence (frames36 camera 4, label top edge, 13 ROI vertices, polarity -1 = white outline -> blue, band 10, min_contrast 10): clean frames edge noise 0.4-0.6 px rms; f_0000010 has a tooth defect at t=0.617 -> deviation 4.8 px (ratio 3.2 at tol 1.5). 80 frames: f_0000010 is the only reject with tol_mid=tol_edge=1.5, max_gap=0.25, skip=[1] (the printed stripe at x~385). Without polarity the outline's outer edge (foam -> white) gives deviations up to 5 px along the foam: use polarity.
Limits: one tolerance for the whole line; no per-segment tolerances (skip is the only per-segment switch).
Update (2026-10-22): two independent zones per camera: `seam` (ridge, label seam) and `edge` (step, label edge), each with its own points, tolerances, skip list and verdict (`seam_deviation` / `edge_deviation`); response has `seam` and `edge` blocks, the overlay stacks one deviation graph per zone, logs/seam gets one file set per zone. The mode is fixed by the zone (the API ignores `mode`). An older config that has a step-mode `seam` is migrated to `edge` on load. UI cards are generated from one template (`LINES` in app.js).
