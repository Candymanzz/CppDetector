# Confirmed label boundary and shift-aware inspection — 2026-11-13

User request: implement a confirmed reference label contour with displacement
tracking and automatic boundary verification, preventing exposed bucket plastic
from causing a surface defect while avoiding silent acceptance when tracking fails.

The optional `label_outline` camera field contains reference-image pixel vertices.
Draw the label using the existing ROI editor (Auto-ROI may provide a draft), then
click **Подтвердить ROI как контур этикетки** while viewing the reference. This
copies the contour into a separate persisted field. ROI can then be changed
independently. A green outline is shown on the reference; the clear button disables
label tracking. Replacing the reference clears the confirmation, even at the same size.
Existing profiles without this field retain their previous inspection behavior.

The per-camera **Компенсация по контуру этикетки** checkbox controls the separate
boolean `label_tracking_enabled` setting (default true for existing confirmed
contours). Turning it off preserves `label_outline`, but excludes it from native
reference preparation: positioning, masks and inspection use the original ROI
pipeline. Turning it back on reuses the saved outline. Confirmation explicitly
enables the feature. Without a contour the checkbox remains clickable, but an
enable attempt stays unchecked and explains that the reference contour must first
be confirmed; no ineffective enable request is sent. Guidance remains visible even
before the reference image loads. Only a pending save disables the checkbox. The flag persists
across restart and is validated as a JSON boolean. The reference contour is green
when active and grey when disabled. Reinspect the same frame after toggling to
compare results; the previous result is not recalculated automatically.

Switch validation: Release rebuild succeeded; all 19 CTest targets passed,
including disable/baseline equivalence, re-enable and persisted disabled state.
UI startup and exclusion tests passed, including toggle failure/retry; JavaScript
syntax passed. Logs: `artifacts/label-mask-20261113/toggle-build.log` and
`artifacts/label-mask-20261113/toggle-ctest.log`. Live runtime was not restarted.

Shared relaxation update: checked the actual Desktop/detector- source checkout
(`boobs+pasha`, bd9f704eade0adf9568b158b6efa1dc19bd15537). The UI now matches its
single 0..15 px, step-1 comparison slider; changing it writes both API axes. Old
asymmetric profiles display the smaller value, as in production. The native
comparison-envelope block is identical to that checkout. This does not imply
the entire optional label-tracking extension exists in the source repository.
Release rebuild, all 19 CTest targets, UI startup/exclusion tests and JS syntax
checks passed. The new fixture verifies that low chromatic boundary support with
unchanged inspection luminance reaches normal comparison and does not force a
reject. Logs: `artifacts/label-mask-20261113/shared-relaxation-{build,ctest}.log`.
No real-camera accuracy or timing sweep was performed for this update.

Reference preparation intersects the ROI with the confirmed label. Positioning uses
an interior window, and label/ROI changes invalidate the native alignment cache.
For each frame, the expected boundary is translated by the native measured shift.
The contour never adapts its shape to the current image, so a missing piece cannot
silently redefine the accepted outline. Color contrast across the predicted boundary
is checked against reference contrast; sample locations allow a +/-2 original-pixel
position uncertainty. At least 16 usable samples are needed to report nonzero support.
After matching production comparison behavior, support is diagnostic only: the old
70% early rejection gate has been removed. Boundary differences go through the
normal grayscale comparison, shared relaxation, blob filtering and score decision.
ECC refinement with correlation >=0.8 is required in this optional mode.

Comparison uses the reference label inside ROI intersected with the aligned visible
ROI/frame. It does not blindly trim the measured shift from every edge. At the user's
request the old perimeter erosion (3 analysis pixels plus local comparison
tolerance) was removed. The latest request reduces the fixed inset to 1 original-frame pixel
inset per axis to the valid geometric overlap. It is independent of comparison
tolerance and displacement. At reduced scale each axis is rounded to the nearest
analysis pixel (1 px at 100%; at 50% the half-pixel rounds to zero with cvRound).
Coverage must retain
at least 50% of the original label-in-ROI area and at least 64 analysis pixels.
Fixed-inset validation: Release rebuild and five targeted CTest targets passed
(label tracking, application settings, adapter parity, shift tolerance and alignment
preprocessing). Tests verify an exact two-pixel-per-axis native inset and a narrow
border at 50%, independent of relaxation values 0, 3 and 5. Existing motion/defect
fixtures pass. Logs: `artifacts/label-mask-20261113/guard-2px-{build,ctest}.log`.
Brightness normalization, score denominator, learned matching, corner masks and
heatmap use that effective mask. Cached masks are cloned before any operation.

Failures reject with `label_position_unreliable` or `label_coverage_insufficient`;
these are failed verification, not evidence of a
particular physical defect. The failure path uses score 100 as a forced rejection.
An already established `shift_exceeded` takes priority. Sensitivity zero still
disables rejection, as explicitly requested earlier. Diagnostics expose enabled,
boundary_support and coverage in `label_tracking`.

Limits: this is a translation model, not a cylindrical unwrapping or automatic
semantic segmentation. Weak-contrast contours, highlights and blurred edges may
affect positioning or comparison. The 1 px inset does not cover all blur/comparison
support at large relaxation values; real-camera false rejects need checking.
Fine defects inside that inset are excluded; detection of the remaining edge defects
depends on contrast, analysis scale, comparison tolerance and score thresholds.
Thresholds need evaluation on actual camera contours before production deployment.

After guard removal: Release build and five targeted CTest targets passed
(label tracking, application persistence, adapter parity, alignment preprocessing,
shift tolerance). Self-comparison preserves every label mask pixel at comparison
tolerances 0, 3 and 5; existing shifted-label/white-background fixtures still pass.
Logs: `artifacts/label-mask-20261113/no-guard-{build,ctest}.log`.
The user's supplied edge-defect photograph was not tested against its reference.

Validation: Release build; full CTest suite (19 targets). The new curved-label
fixture tests self comparison, up/down/left/right/diagonal motion at 100% and 50%,
interior and edge defects, missing label, sensitivity zero, exceeding displacement
limits and 30 repeated inspections without cache erosion. App tests verify contour
validation, camera isolation, persistence and clearing on reference replacement.
Mock-DOM tests exercise confirmation and removal; existing exclusion/learning UI
checks and JavaScript syntax pass. These are synthetic regression tests, not
accuracy claims for the user's photo or the production cameras.

Artifacts: `artifacts/label-mask-20261113`; executable/test build is under
`artifacts/merge-core-20261113/build/Release`. Live runtime was not restarted.
Latest 1 px update: Release application and label test rebuilt; label_tracking_test
passed, including exact native inset and zero rounded inset at 50%, independent
of relaxation values 0/3/5. Motion and defect fixtures still pass. Logs:
`artifacts/label-mask-20261113/guard-1px-{build,ctest}.log`. Runtime not restarted.
