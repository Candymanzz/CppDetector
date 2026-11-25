> Update, 2026-11-19: [atlas-quality corrections](atlas-quality-2026-11-19.md)
> add independent current-source motion and persistent fine manual matrices on
> top of this reference calibration. Automatic calibration remains reference-only
> and partial; manual controls do not certify complete geometric accuracy.

# Clear seams and reference-only alignment — 2026-11-19

The owner supplied a panorama with ghosted logos/text and mismatched scale.
The initial renderer averaged complete overlap sectors even though the starting
angles/boundaries were only approximate. This mixed two displaced text images;
it did not constitute measured geometric stitching.

## Current operator behavior

The display now chooses one real camera sample per pixel, with no transparent
overlap averaging. A separate worker automatically tries **Совмещать ракурсы по
эталонам** once per new reference/geometry signature. **Уточнить совмещение**
repeats it, and **Сбросить подгонку** disables automatic fitting and returns to the
manual angular layout. Existing files, base control points, seam origin and
inspection settings remain independently editable.

Only reference images determine the fit. The result is saved in the browser recipe
and applied identically to current and reference atlases. Changing a current frame
cannot recalibrate the surface to fit away its defect. Changed reference pixels,
dimensions, boundary points, angles, projection or participation invalidate the
signature. Old recipes enable this feature by default; disabled mode remains manual.

The status explicitly distinguishes complete, partial and unsuccessful alignment.
Unsupported joins retain their initial geometry. **Partial does not mean a complete
geometrically calibrated scan**, even if most display pixels have image support.
Changing a source/recipe cancels a pending calibration and invalidates its result.
Reset and JSON import/export cover the new state. The inspection report includes
the actual active calibration, not just an unverified stored recipe object.

## Implementation and rationale

`unwrap-stitch-worker.js` loads a pinned local OpenCV.js 4.12.0 distribution on
demand. It rectifies only the four references to 700×520 patches, applies CLAHE,
extracts up to 5000 ORB points and performs reciprocal two-neighbour Hamming
matching (ratio 0.82). All six camera pairs are examined, including non-neighbours.
RANSAC affine estimation uses a 3 px threshold and deterministic per-pair seeds.
At least 14 inliers, a minimum inlier proportion/spatial spread and plausible
orientation/scale are required before accepting an edge.

`unwrap-stitching.js` fits a bounded joint affine correction to each connected
camera component. One camera anchors each component; regularisation discourages
unconstrained scale/shear extrapolation from a narrow overlap. Unsafe transforms
or corrections that fail to improve correspondence error are refused. Disconnected
or refused cameras keep identity corrections. No point, text or missing surface is
generated to fill gaps.

The renderer's new cut mode uses the same fixed geometric ownership for both
inspection atlases, independent of source defects. Each camera has a forward and
inverse mapping between its existing six-point surface and the atlas. Ownership
is now per pixel, because vertical shear/scale can change support by row. UI source
clicks apply the inverse calibration as well as the later inspection translation;
guide boundaries follow the transformed footprints. Unsupported pixels remain
transparent and contribute to the actual support statistics. Old blended rendering
is retained only as the core's compatibility path; the UI explicitly requests cut
mode for both calibrated and manual layouts.

The local JS runtime is about 10.9 MB and loaded only during reference calibration.
Its source URL, licence and SHA256 are in `web/vendor/SOURCE.md`. The C++ EXE,
native libraries, profile/API contract and per-frame inspection module are unchanged.
There are no external image transfers or new server endpoints.

## Verified results and limits

The actual frames36 references support a reliable front overlap for bucket 1,
cameras **3 and 0**. The browser fixture finds **36 inliers**. Joint correspondence
RMS error falls from **38.47 to 2.96 px** on the 1200×312 inspection scale. Two
cameras are therefore reported as aligned; the other pairs do not meet the
acceptance criteria. This measures the fitted landmarks, not independent physical
accuracy. A screenshot shows removal of ghosted text, while hard discontinuities,
colour/exposure differences, some cropping and uncalibrated joins remain visible.

Other feature approaches were examined in task artifacts: AKAZE/KAZE, rectified
and raw descriptors, and a standalone native SIFT probe. They did not establish
the missing reliable overlap graph on these references. They are not added to
the product or used to justify relaxing acceptance thresholds. The native probe
never modifies the working application or its data.

Verification:

- Existing **6438** geometry assertions and **13** controlled inspection cases pass.
- New stitch suite covers inverse coordinates, complete/partial/refused graphs,
  fit improvement, bounds, reference-only signatures, non-averaged pixels, masks
  and self inspection after transformed rendering.
- Four existing UI suites and JS syntax pass.
- Chrome with the actual EXE/HTTP API, copied profile and frames36 passes **13
  interaction cases** plus 1440/1024/390 layouts. It checks actual partial fitting,
  input choice, unchanged calibration after changing current frames, an injected
  current-image defect, correct source coordinates, reset/deterministic refitting
  despite that defect and invalidation after editing geometry.
- The injected defect remains REJECT after initial fitting and reset/refitting;
  reference geometry is not fitted to the current defect. No browser exceptions
  or API write requests occur. Before/after images are kept in task artifacts.

There is no demonstrated full 360-degree physical calibration. In particular,
the remaining white-panel/text joins and the second bucket still need suitable
overlap/calibration; colour compensation between cameras is not implemented.
The new approach improves the visible front seam and removes overlap ghosting,
but is not a completed seamless scanner. Actual defect ground truth, physical
camera calibration/synchronisation, Linux, native CTest/rebuild and the 500 ms
machine deadline are unverified. First calibration takes several seconds; it is
separate from per-frame rendering and reuses a reference signature afterwards.
