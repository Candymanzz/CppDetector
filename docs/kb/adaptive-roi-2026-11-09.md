# Updated integration — 2026-11-16

Adaptive ROI is now combined with main/b0535b0 and its eefefce4 native core, using
Reference::prepared(scale, exclusions). Confirmed labels restrict feature selection
and comparison; the final adaptive mask includes their inset. Native mode uses
phase256/ECC512, while adaptive mode keeps its own registration and movement limit.
The original baseline and validation below describe November 9; current Release,
20/20 CTest and 243 adaptive assertions pass. See [resolution, preservation and checks](git-pull-2026-11-16.md).

# Adaptive reference ROI — 2026-11-09

Standalone baseline: `main / bf74c6b`, with the existing user-modified
`config/config_frames36_52aa31ea.json`. The imported core still identifies its
base as production `20909141`; this is an explicit local extension, not a new
production import. The main application's detector is not changed.

## Operator workflow

Open a camera in **Настройка и тест**, draw the reference ROI as usual, and enable
**Адаптивный ROI → Следовать за знакомой поверхностью**. The default is off,
preserving old profiles and native comparisons. Settings are per camera:

| API field | Default | Allowed values | Meaning |
| --- | --- | --- | --- |
| `adaptive_roi_enabled` | false | boolean | Select the local geometry path |
| `adaptive_roi_max_motion` | 96 | 8..240 | Maximum model displacement in original pixels |
| `adaptive_roi_min_coverage` | 0.70 | 0.50..1 | Required inspected fraction of the reference ROI |

The motion setting replaces the old global X/Y alignment gate in this mode.
Comparison relaxation X/Y, sensitivity, threshold, minimum blob area and downscale
still apply. This is surface correspondence, not a calibrated measurement of the
label's position relative to the bucket. The old mm controls do not constrain
adaptive motion; reported displacement is the movement of the image centre.

On the result image, yellow is the reference ROI, green is the mapped checked
area, and green points/short connecting lines show accepted correspondences.
The portion of the original ROI outside the checked current mask is dimmed grey.
Heatmap and defect rectangles use current-image coordinates. Result diagnostics
show coverage, landmarks, residual in original pixels and geometry time.

`NOT_INSPECTED` / **Контроль не выполнен** means correspondence, coverage or
enabled seam inspection is unavailable. It never becomes OK. With sensitivity
above zero it also sets `reject=true`; sensitivity zero retains the disabled
rejection convention while still reporting incomplete control.

Existing learned normal crops are inactive in adaptive mode and new learning is
blocked in both UI and API. Their raw-frame placement lacks the transform needed
for correct reference-coordinate comparison. Disable adaptive ROI to use them;
saved cases and files are retained. Manual exclusions do follow the reference.

## Geometry and inspection

`AdaptiveRoiReference` is immutable. It caches a tracking image with maximum side
768, pyramids and up to six Shi–Tomasi features per cell of an 8x6 grid, selected
inside an eroded reference ROI minus exclusions. Phase correlation at maximum
side 256 initializes forward/backward pyramidal Lucas–Kanade matching. Every
inspection starts from the primary reference, never the previous product.

With normalized coordinates `X=x/width-0.5`, `Y=y/height-0.5`, the single model is:

```text
X' = a0 + a1*X + a2*Y + a3*X*X
Y' = b0 + b1*X + b2*Y
```

Deterministic RANSAC and least-squares refinement reject inconsistent matches.
Acceptance requires at least 16 inliers, 55% of surviving tracks, spatial spread
over the ROI, at least four image cells, bounded displacement, positive Jacobian,
bounded scale, shear and horizontal curvature. Forward/backward track disagreement
and large photometric errors are rejected before fitting. There is no independently
movable vertex or local flow field that can bend around each defect.

The current raw image is sampled in reference coordinates before grayscale,
downscale and blur for comparison. A local `InspectionGeometry` supplies an
explicit prealigned flag and request mask; the second native translation is
skipped. The cached `Prepared` masks are cloned before modification. The optional
argument is null in the old path, preserving native calculations.

The comparison mask is reference ROI minus manual exclusions, intersected with
the transformed input-image footprint. Boundary support is conservative for
interpolation, resizing and the comparison blur. Coverage and score denominator
use the actual work-resolution mask. Discarded feature matches never remove
individual interior pixels or regions from inspection. The displayed mask,
heatmap and zones are mapped back to the current image.

The seam is measured in the rectified image with geometrically unavailable
samples and manual exclusions masked. An enabled seam that is not found or has
coverage below 60% makes control incomplete. Local seam kinks remain measurable.

## Deliberate limits

This is a bounded smooth approximation to small pose changes. A cylinder/cone
model with calibrated silhouette and camera parameters was not introduced.
The mask proves image-footprint availability under the fitted model; it is not
a calibrated determination of physical self-occlusion at the bucket silhouette.
Large axial turns, strongly compressed edges, repetitive print, missing texture,
reflections and illumination changes need real examples and can fail registration.
Uniformly plausible false correspondences remain possible on repetitive artwork.

The prototype maps only the operator's reference ROI. Newly revealed material
outside its image is not inspected. A missing match inside the expected area is
not sufficient evidence to exclude it. Low coverage is reported separately from
defect score, rather than making a smaller area silently pass.

The UI remains an editable reference polygon. This does not build a panorama,
learn new sides, or provide independent metric label-position inspection.

## Verification

Windows MSVC 19.44, OpenCV 4.14 Release, installed dependencies; OpenCV `video`
is now explicitly required by CMake. Staged Release and all 14 CTest targets pass;
the new test contains 228 checks. Native mode matches direct native calls over
800 frames / 8621 assertions. Existing HTTP learning/exclusion scenarios and
three mock-DOM UI suites pass.

The new HTTP scenario checks 50 frames (five indices per camera): 49 register and
one camera-7 frame reports NOT_INSPECTED. Four complete ten-camera file-stream
batches, including read/inspection/overlay/logging: 173.75, 164.37, 167.32 and
163.45 ms on this Windows host. These counts do not label the frames as good;
this is not a physical trigger-to-output measurement. Installation and target
verification are additionally recorded in the main repository's task note.

The new C++ test covers known smooth transforms at 100/73/50%, centre/edge defects,
clipped coverage, missing/different print, textureless references, moving manual
exclusions, disabled mode, parallel reuse, mask-cache immutability, small true
cylindrical texture rotations, local seam kinks, API validation, camera isolation
and restart persistence. The cylinder fixture uses an orthographic radius of
360 pixels, ROI x=70..570 and rotations of +/-0.05 radians; it is not a general
angle guarantee for a real bucket.

HTTP tests exercise diagnostics, overlay requests, malformed updates, the learning
guard, restart and a real ten-camera file stream. UI tests use a mock DOM and check
toggle persistence, camera switching, save-failure rollback, incomplete status,
coverage and disabled learning. Native parity uses all 800 available frames36
images. Those images have no confirmed defect labels, so parity and acceptance
counts do not establish classification quality. No physical camera/PLC or manual
browser acceptance test is claimed.
