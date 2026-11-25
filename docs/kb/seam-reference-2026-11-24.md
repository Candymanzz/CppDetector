# Reference-bound seam with one uniform tolerance — 2026-11-24

> Subsequent owner request: continue mask/path with mandatory two-side and width checks.
> [Current behavior and limitations](seam-pair-2026-11-24.md).

The owner identified a neighboring black square as the source of a jump in the
previous mask path. They requested capturing the seam on the primary reference
and widening one common allowance along its entire shape.

## Operator workflow

On the camera reference, place the two seam endpoints and press **Запомнить стык
эталона**. The active seam control is **Допуск стыка, px** (0.1–50 source pixels).
It expands both sides equally at every sample; separate middle/end tolerances,
end fractions, mask thresholds and search width are not active controls of this
mode. Previous native/mask controls remain in a collapsed compatibility section.
Reference-view guide lines update while moving the slider. A new inspection
applies the changed limit to the actual input; the result reports its used limit.

Changing the primary reference or endpoints requires explicit confirmation again.
The captured seam survives restart and a tolerance change does not invalidate it.
Camera 6 is migrated to this mode, initially using its former middle limit 6.3 px
uniformly; the old native/mask tuning is retained for comparison.

## Method and lifecycle

`src/inspect/seam_reference` prepares one persistent boundary from the reference
mask, selecting between the two polarity paths by coverage and smoothness. It
keeps the observed reference form, including non-quadratic curvature, instead of
fitting a new baseline to every current image. Reference preparation requires
95% support and no long reference gap. The same runtime sampling grid supplies
the frozen subpixel positions, giving zero reference self-deviation.

Current inspection follows that fixed identity and polarity, using local profile
appearance, contrast, robust brightness offset and continuity. Normal whole-image
X/Y compensation places reference coordinates on the current image; adaptive
inspection uses its already rectified image. No current-frame polynomial fit can
move the reference toward a new defect. A fixed ±96 px capture area is independent
of the tolerance, so changing the tolerance cannot change the measured path.

The normal deviation is current edge minus reference edge. All `rowTol` values
use `reference_tolerance`, including both end regions. The existing first/last
3% remain unmeasured. Missing/ambiguous observations stay gaps; 70% coverage and
20% maximum continuous-gap guards are internal, with NOT_INSPECTED when failed.
Expected reference points outside the current image cannot be replaced by another
visible edge even at the maximum tolerance. This checks one selected boundary,
not every possible edge or the width of a bright stripe.

The per-camera immutable cache belongs to `Inspector`. Confirmation persists a
signature of reference BGR pixels/dimensions, endpoints, effective preparation
band and algorithm version. Tolerance is excluded. Rebuild validates the saved
signature; reference replacement clears it explicitly. No separate image or
learned-normal case is created. Stale backend inspection/overlay/mask data are
invalidated during rebuild/confirmation.

`POST /api/seam_reference?cam=N` captures the configured line and returns `seam`
and `reference` metadata. `seam.search_method=reference`, `reference_tolerance`
and server-owned `reference_signature` persist in the existing profile.
`camera.seam_reference` reports ready/reason/path/normal/endpoints. Clients may
echo an unchanged signature but cannot set a new one via ordinary settings.
Unconfirmed/changed references use `seam_reference_required` or
`seam_reference_changed`; failed capture is rejected without replacing the old
configuration. Existing versioned mask PNG and source-coordinate overlay show
the frozen curve ± tolerance. UI saves are serialized per camera and late
capture responses cannot overwrite a different selected camera.

## Evidence and limits

- Windows Release, **23/23 CTest**, eight existing JS suites and JS syntax pass.
  The new test covers a nearby moving black square, true local kinks, a curved
  reference, brightness, global pose, identical measurement under changed
  tolerance, both end regions, lost seam, invisible expected points, missing
  endpoints, persistence, reference/point/pixel invalidation and signature guards.
- Ten Chrome/real-HTTP groups pass: six legacy photo comparisons, actual capture
  and exactly one visible slider, f80 and uniform per-row limits, unchanged path
  and signature when widening, fixed viewport, delayed camera-switch response,
  signature rejection, all available camera-6 photos, reference invalidation and
  the existing unwrap workspace. Screenshots are reviewed.
- With the recorded primary reference f79 and uniform 6.3 px, the full HTTP
  camera-6 pipeline gives **15 OK / 1 NOT_INSPECTED on 16 snapshot photos**.
  f80 no longer has the black-square jump; its maximum relative deviation is
  about 2.36 px. f92 lacks enough supported reference-edge samples (~63%) and is
  explicitly incomplete. This is not production defect labelling or a guarantee
  that every chosen boundary is the physical seam.

Tiny/rapid changes, a poor or ambiguous primary reference, different material or
lighting, one-boundary coverage and incorrect global registration remain limits.
No PLC/camera line, labelled defect accuracy or 500 ms deadline is certified.
Install source/EXE/profile with backup; preserve existing image references,
learned data, ROI, endpoints and legacy tuning. Artifacts live in the main
checkout's ignored `.verification/cppdetector-seam-reference-20261124/`.
