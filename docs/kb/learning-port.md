Update 2026-11-02: the local matcher now has an optional moving-shadow extension; native mode remains
selectable. [Current rules and verification](local-learning-v2.md). The original import history follows.

# Detector 52aa31ea port (2026-11-02)

## Scope and reason

The owner requested the newer detector, including learning, from defect-detector-v3 commit
52aa31eacb16785ff33dc16b6937ab31c8ec7ee9 in this standalone test bed.
The old test bed mirrored 885a79ed plus bfd9b54a. Porting just the last commit would omit changed sensitivity,
connected-component filtering, sensitivity endpoints and independent alignment limits used by learning.
Therefore the core and service are pinned to the complete requested snapshot. Provenance: ../../src/production/SOURCE.md.
Existing local staged/unstaged edits were used as the baseline and were not reset or committed.

## Behaviour confirmed by code

- Threshold default is 60%; at sensitivity .5, full score corresponds to .2% anomalous ROI area.
  Sensitivity also changes the score scale; 0 accepts the surface result, 1 rejects it. Seam remains separate.
- max_alignment_shift_x/y (0..160 original px, default 12) are independent of comparison tolerances.
- Native filtering uses connected component pixel counts and anomaly energy; thin defects are retained.
- Learning stores alternate normal BGR crops, scoped to camera and exact reference fingerprint.
  Matching pixels reduce the anomaly map; new anomalies inside/outside the crop remain detectable.
  raw_anomaly_score, learned_normal_adjustment and matched_accepted_case_ids are exposed in JSON/logs.
- New _52aa31ea profiles isolate the new scale/settings. Existing reference/ROI/seam import on first start.
- Storage uses the native service, with atomic metadata replacement adapted for Windows.
  Alternate references are classic CV patches; no neural network or Python service is introduced.

## Use

Rebuild with .\rebuild-and-run.ps1 -NoRun, then .\run.ps1 (default dataset frames36).
Inspect a frame without injected defects. In the camera tab click «Выбрать участок», drag a rectangle,
then «Сохранить как норму». Emulation pauses to keep the selected source stable.
Repeat inspection to see the adjusted result. Saved crops can be viewed/deleted in the same card;
cases for another reference are marked inactive. Changes to the reference invalidate a pending selection.

API: GET/POST/DELETE /api/learning?cam=N; POST body {version,bbox:{x,y,width,height}}, DELETE adds id.
GET /api/learning/image?cam=N&id=...; GET /api/inspected?cam=N&version=... returns the untouched source.
--data DIR allows isolated runtime profiles/logs for tests without modifying normal user settings.

## Verification and limits

Windows Release compilation passed (existing native OpenCV/MSVC deprecation warnings).
CTest: local_learning_test, learned_region_test, native_shift_test, shift_tolerance_test,
production_parity_test all pass. Tests exercise learning/retry/restart/delete, camera/reference isolation,
scale/translation, a new defect inside/outside a learned patch, independent limit persistence,
invalid/stale source rejection and original immutable-cache regressions.
HTTP smoke tests/http_learning_test.cjs passed against the built executable on an ephemeral loopback port,
with an isolated temporary data directory. JavaScript syntax checks passed for app.js and learning.js.
Real-frame parity passed for all 1000 frames (10 cameras × 100): 10,389 assertions including synthetic/profile checks.
The local adapter matched native scores/verdicts/offsets/maps/masks/heatmaps and seam results on this set.

No interactive browser test, labelled defect-quality assessment, Linux build, PLC/camera test or full
10-camera wall-time benchmark was performed. These checks do not establish the production 500ms deadline.

Final destination verification: D:/Pj/CppDetector Release rebuild and all five CTest targets passed;
HTTP smoke passed against that rebuilt executable. UI startup/region drag/save flow passed a mock-DOM
Node VM test (tests/ui_startup_test.cjs); this also caught and corrected the old engine-name guard.
This is a JavaScript integration check, not interactive browser verification.
