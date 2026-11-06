# User-drawn inspection exclusions — 2026-11-06

Requested scope: standalone D:/Pj/CppDetector, branch merg, base 0f3c211 plus the
existing uncommitted production/learning updates. The main application's detector,
Java and React code are not changed by this feature.

## Behavior and UI

Open a camera in «Настройка и тест». In «Зоны исключения инспекции», press
«Нарисовать зону», click at least three points on the reference, then press
«Сохранить зону». A pink polygon marks the excluded area. Undo removes the last
draft point; Cancel/Escape discards the draft. Each saved zone can be deleted, or
all zones cleared. Camera/view changes discard unfinished drawings. The existing
ROI editor is disabled during drawing; exclusion clicks never change its vertices.
Saving errors retain the draft for correction/retry. Exclusions are per camera.

POST /api/camera?cam=N accepts `exclusions: [[[x,y],...], ...]`, in reference-image
pixels. GET state/config and inspection results/logs expose the same field. An
empty array restores inspection everywhere in the ROI. Omitted fields preserve
existing zones; old profiles load with none. Up to 32 polygons and 128 points per
polygon are accepted. Non-numeric, non-finite, out-of-frame, short or zero-area
polygons are rejected before updating any camera settings. Overlaps form a union.
Same-size reference replacement preserves zones like ROI; size changes clear both.
Existing profiles, references and learned samples are not rewritten on installation.

## Decision and implementation

Exclusions are independent polygon holes in the existing ROI. They are subtracted
before brightness statistics, comparison, learning/component filtering and score
normalisation, and are excluded from the alignment thumbnail's Hanning window.
Upper-corner geometry is computed before subtracting holes, so an interior hole
cannot become a new top edge. Reference cache invalidates when exclusions change.
Coordinates follow the reference after translation compensation. Pink contours
are also drawn in exported JPEG overlays; decorative heatmap halos are removed
inside holes. Leave a small drawing margin for blur at the surrounding boundary.

The seam ignores complete cross-sections whose search band intersects a zone,
including polynomial fitting and coverage counts. The surface's compensated shift
maps reference polygons into the raw seam frame. The rest of the seam still runs,
including if the surface ROI is fully excluded but the seam lies outside it. A fully
excluded surface returns score zero and reason `inspection_excluded`, including at
sensitivity one. With no exclusions, existing thresholds and behavior are retained.

Why mask before scoring: hiding a colored overlay or dropping entire anomaly
bounding boxes would still affect the verdict or also discard defects outside a
zone. A separate exclusion array retains the existing editable ROI and adds holes
without forcing it into a single self-intersecting polygon. These are unconditional
operator exclusions, unlike learned normal crops that still compare new defects.

This intentionally extends the imported core in reference.h/.cpp, inspector.cpp
and seam.h/.cpp. Their old byte-identical production statements are historical.
The base commit exposed by the API remains 20909141; it does not identify this
uncommitted extension. Native Service has no new public exclusion endpoint; the
standalone adapter owns persistence and supplies masks. See [provenance](../../src/production/SOURCE.md).

## Verification and limits

Verified in an isolated Windows MSVC 19.44/OpenCV 4.14 Release build:

- 13/13 CTest: new exclusion test plus all 12 existing targets.
- Exclusion negative controls: an inside defect rejects before enabling the zone,
  scores zero after; an outside defect still rejects at scales 1/.73/.5. Cache,
  denominator, stable corner geometry, union overlaps, fully excluded ROI, seam
  kink inside/outside, malformed update atomicity, camera isolation, clear, restart
  and reference-size changes are covered.
- 800 frames36 images: 8621 local/native parity checks pass with no exclusions.
  This compares implementations, not labelled defect quality.
- New HTTP test verifies inside/outside defects, camera isolation, validation,
  result configuration, served UI, restart and deletion, using temporary profiles.
- New mock-DOM UI test exercises polygon clicks, ROI isolation, undo/cancel,
  camera/view switches, saving failure/retry, single/all deletion. Existing learning
  HTTP, automatic-learning HTTP and startup/learning UI tests also pass.

Interactive browser operation, Linux and production hardware were not verified.
The complete machine-cycle budget and production calibration were not measured.
No dependencies were added; the existing native dependency installation was reused.
