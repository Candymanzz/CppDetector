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

# Local robust region learning experiment (2026-11-02)

## Scope and motivation

Owner requested improving cases where a learned normal shadow moves and triggers another rejection.
Only D:/Pj/CppDetector changes (current branch merg / 0c03567 with pre-existing staged/unstaged edits). Main-app code remains untouched on cpp-detector/9ef2e2c8.
This experiment intentionally extends learning beyond snapshot 52aa31ea. A native comparison switch remains.
Prior history and storage contract: [learning-port.md](learning-port.md).

## Algorithm and controls

The local Inspector passes an optional comparison hook to native inspectFrame before connected-component
filtering and scoring. Without a hook the original 52aa31ea matcher runs unchanged. Native Service uses no hook.
With no active learned cases the hook is not used; main alignment, surface settings, seam and scoring stay native.
Local code is src/inspect/learning_matcher.{h,cpp}. Storage/fingerprint/camera isolation are unchanged.

Settings are per camera, persisted in the existing _52aa31ea profile:
- learning_robust_enabled: true by default; false restores the original native matcher.
- learning_shadow_enabled: true; independently enables the smooth illumination path.
- learning_search_radius: 24 original pixels by default, range 0..64, independent of camera alignment.
Defaults apply to missing fields in existing profiles. No existing thresholds, crops or references are reset.

The matcher searches a context expanded around the saved rectangle, but applies changes only inside a mask
of the sample's difference from the primary reference (absolute blurred-gray difference > 6 levels).
A single coherent placement is selected per sample. Search is clipped to complete placements in the processing
region: no wrapped or replicated source pixels. Correlation uses TM_CCOEFF_NORMED, minimum .8, and requires
sample standard deviation >= 1 gray level. This is similarity, not a probability of a good part.
If appearance matching is unreliable, the original saved position can still be checked pixel by pixel.
Crops smaller than 8 working pixels on either side, outside the processing region, or with no learned change
are skipped and diagnosed. ROI masking still bounds all changes.

For shadow handling, subtract the primary reference from the saved sample, then smooth the signed residual
with Gaussian sigma=max(1,6*inspect_scale). A sample is eligible if the mean absolute high-frequency residual
inside its support is <= max(2,.18*mean absolute residual) and its peak <=18 gray levels.
The smooth residual is matched against the current frame-minus-reference residual in the expanded context.
This lets the shadow move while printed texture stays stationary. Only the learned smooth component is
subtracted from the current image; fine details are checked against the primary reference using the existing
X/Y comparison envelope. A fitted illumination amplitude must be .7..1.3 of the sample and offset within +/-12
levels. If that model is valid it is used instead of mixing alternate placements from appearance matching.
These are initial engineering bounds, not production-calibrated constants.

The changed mask moves with the accepted placement, including beyond the original rectangle, but never beyond
the configured search range. Background elsewhere in the selected rectangle is not blanket-excluded.
Filtering current shadow residuals is limited to the search context plus a 4-sigma halo; full-frame filtering
was rejected because it roughly doubled the synthetic batch latency.

## UI and diagnostics

The camera learning card provides both switches and the separate radius slider. Existing saved samples work
without migration; recapture a crop if it does not contain the full shadow plus some surrounding margin.
Inspect again after changing settings. The expandable result section «Применение образцов нормы» reports
per-case status, method, similarity, applied shift, removed working pixels, and shadow rejection reason.
JSON/decision logs also include learning_engine, learning_diagnostics and learning_ms. Diagnostics include
candidate/remaining pixels, work_scale, mask size, pattern alignment validity, roughness and illumination fit.
Native mode is explicitly marked and emits no local per-case diagnostics.

## Validation

Windows Release, MSVC 19.44, OpenCV 4.14. Six CTest targets passed in the isolated build:
moving_shadow_test, local_learning_test, learned_region_test, native_shift_test, shift_tolerance_test,
production_parity_test. UI startup, drag/save and diagnostics rendered successfully in a Node mock-DOM test.
HTTP smoke verified mode/radius state, diagnostics, old-mode switch, persistence/restart, save/delete and guards.
Syntax checks passed. No real browser interaction was performed.

moving_shadow_test uses a reproducible textured printed surface with a Gaussian shadow:
- independent motion (+20,+8), (-18,-7), (0,+18) at scales 1/.7/.5 passes;
- old native mode, shadow path disabled, too-small search radius, outside-radius motion reject the same case;
- new scratch over the moved shadow, missing printed detail, a hard black spot, unrelated outside defect,
  excessive darkness still reject; sensitivity endpoints and repeated results remain consistent;
- small shadow width/depth variation passes.
These are synthetic regression observations, not evidence about the owner's actual failing frame.

Optional benchmark: moving_shadow_test.exe --bench, 10 parallel 1224x1024 images with one active 76x76 case
per camera, 2 warmups + 20 measured batches per mode, single-thread OpenCV inside each camera.
Native: mean 78.83 / p95 82.57 / max 84.70 ms, 200/200 false rejects on the moved-shadow example.
Robust: mean 82.34 / p95 90.18 / max 91.12 ms, 0/200 rejects; mean local match stage 3.18 ms per camera.
Includes algorithm/heatmap and task scheduling, excludes file decode, colour overlay, HTTP, capture and PLC.
The production 500ms full cycle is not established. Many/larger learned crops may cost more.

## Limits and rollback

Disable «Устойчивое сопоставление образцов» to return to the imported matcher. Disable only the shadow switch
to retain expanded appearance search. Actual failing shadow frames were not supplied/tested.
The system cannot distinguish a real defect that visually reproduces a learned acceptable illumination pattern.
Unrelated deformations/rotations or strongly changed shadows remain unmodelled. Very small/thin defects may
still be lost to the existing comparison tolerance/min-area/scale settings. Thresholds require real-frame tuning.
This experiment is not deployed into the main application.
