> Update, 2026-11-19: global atlas registration has been superseded by per-source
> preparation with profile working areas and comparison tolerances. See
> [false-rejection correction](atlas-quality-2026-11-19.md) for current behaviour,
> same-file native comparisons and remaining limits. The earlier design below
> is preserved as history.

# Whole-atlas inspection experiment — 2026-11-19

Stitching follow-up: reference-only geometric corrections now apply to both
atlases, and the display uses clear cuts. Signatures, 2D ownership and source
coordinates are updated; the inspector itself and its thresholds are unchanged.
Partial fitting is explicit and does not establish full physical calibration.
See [current renderer and measured limits](reference-stitching-2026-11-19.md).

Follow-up explicitly authorised by the owner after the initial unwrap preview:
make a trial inspection through the developed surface. Scope remains the
standalone `D:/Pj/CppDetector`, `main / 035fa76`; the main application and native
per-camera inspector are unchanged. The earlier preview-only description is
history: the new workspace now produces its own experimental bucket verdict.

## Operator workflow

The later usability fix adds **Кадры для инспекции** at the top of the tab,
before the result: select a file per camera or use **Кадр № для всех камер**
to set the same ordinal in all lists. Inputs update the existing inspection
automatically. See [selection controls, synchronisation and checks](frame-selection-2026-11-19.md).

In **Развёртка ведра**, choose one product's side cameras and source files.
**Построить и проверить** builds and inspects; **Инспектировать развёртку** also
opens the inspection view. Changing inputs or parameters automatically rebuilds
and immediately invalidates the previous verdict, export and region list.

The new result is **ГОДЕН**, **БРАК**, or **Контроль не выполнен**. The panel shows
the reason, evaluated area, defect area, component count, X/Y shift, matching
correlation and brightness adjustment. Clicking a component opens its location
in the original camera frame. Up to 50 largest components are listed; all
retained components contribute to area/count. A JSON report preserves selected
frames, reference names, geometry, settings, registration, coverage and regions.
This local verdict is not sent to PLC, native inspection or its decision log.

Views are deliberately explicit:

- **Развёртка / Эталон** retain the feathered display panorama.
- **Различия RGB** retains the earlier unregistered per-camera maximum map.
- **Эталон контроля** is the single fixed-ownership, unblended reference atlas.
- **Совмещённая** is the current atlas registered to it and brightness-adjusted.
- **Инспекция** shows retained difference components in that reference coordinate
  system. Source clicks account for the measured translation and atlas ownership.

PNG exports the selected raster without UI boxes/guides. Inspection rasters and
pixel parameters use **1200×312** at every supported display width. Display width
1200/2000/3200 therefore does not silently change the meaning of area/tolerance.
New inspection settings are stored with the existing dataset/bucket recipe;
old preview recipes acquire defaults. Malformed settings are rejected before
replacing a usable recipe. No detector profile fields are written.

## Pipeline and decisions

The renderer creates two additional developed images. At each longitude the
camera with the greatest normalised distance from its angular edge owns that
column, independently of current/reference availability. The chosen camera's
pixels form both current and reference atlases without overlap averaging.
Missing pixels are not borrowed from a different camera. All four required
views must be enabled, readable, paired with a reference and size-compatible.
This avoids dilution/cancellation in display feathering and inconsistent
current/reference ownership. It still assumes the manual geometric recipe is
physically correct; it does not prove that overlapping print is calibrated.

`unwrap-inspection.js` inspects those two whole atlases, not the native camera
verdicts or a per-camera difference maximum:

1. Resize to the canonical width and apply a 3×3 colour blur; unsupported filter
   neighbourhoods are removed from support. Horizontal image topology is circular.
2. Find one global X/Y translation using three-scale gradient-magnitude normalised
   correlation, with three coarse candidates and subpixel peak refinement.
   Exact self comparison can register a textureless image. Otherwise correlation
   below 0.55, an ambiguous peak or a search-boundary maximum is incomplete control.
3. Optionally estimate a robust global luminance affine correction, applied equally
   to RGB: gain is bounded to 0.85–1.15 and offset to −15…15. This does not perform
   separate per-camera or local illumination correction.
4. Compute maximum RGB-channel residual on supported aligned pixels. Optional
   1–2 px relaxation uses a symmetric neighbourhood match. Default is strict 0 px.
5. Extract 8-connected components across the circular seam, filter by area, then
   sum retained pixels. A seam-crossing component has one identity and two boxes.
6. Issue a bucket verdict only if all required inputs, registration and minimum
   paired coverage are valid. Otherwise report **NOT_INSPECTED**, even when candidate
   regions are visible. Defect area is divided by actually evaluated pixels.

Defaults: difference 24/255, minimum component 18 px², rejection at **≥0.05%**
retained area, search ±24 px, minimum support 97%, brightness correction enabled,
relaxation 0 px. These are trial parameters, not validated production limits.
The filter/translation removes some vertical support, so even self comparison
reports about 99% support, not 100%. A user-requested 100% minimum may reject it.

Registration, photometry and component filtering run inside the worker after
surface construction. Old workers are terminated and late loads/results discarded.
The existing GET-only server contract and native binary remain sufficient.

## Verification and actual observations

- Existing unwrap geometry suite: **6438 assertions** passed.
- New Node suite: **13 controlled image scenarios**, covering self comparison,
  circular translation, brightness, retained coloured defects, simultaneous
  motion/light/defect, one seam-crossing component, dust area filtering, missing
  support/inputs/reference, failed registration, search-limit handling and bounded
  compensation. Separate checks cover unblended ownership and parameter validation.
- All four existing UI suites passed: startup/learning/label, header/shortcuts,
  exclusions and adaptive ROI. JavaScript syntax checks passed.
- Chrome with the actual CppDetector EXE/API, actual frames36 and a temporary
  profile passed **17 scenarios**, including all earlier unwrap interactions,
  the new verdicts, report export, source coordinates and invalidation. There
  were no browser exceptions or API write requests. Layouts 1440/1024/390 px fit.
- A controlled global `RGB×1.04+5` change of real reference photographs passes.
  A green control defect in camera 0 is rejected on the developed surface:
  **0.701%**, **one region**; clicking it identifies camera 0. This is an injected
  defect, not a real labelled production defect.
- Real bucket-1 list position 2 reports **БРАК**, **0.303% / six regions** in
  strict mode. Relaxation 1 px reduces it to **0.089%**, still above the unchanged
  0.05% limit. Position 83 reports **БРАК**, **30.938% / 92 regions**; its global
  brightness correction reaches the bounds. These frames have no verified defect
  ground truth. Do not label these detections true/false defects without review.
- Unlabelled-product list position 50 gives **Контроль не выполнен**, not acceptance:
  the registration/appearance change does not meet the control conditions.
- Initial 2000×520 construction plus canonical inspection took roughly **1.25 s**
  in one browser observation. This is not a benchmark or a 500 ms machine-cycle
  result. No acceleration claim is made.

## Open limitations

One global translation cannot independently align every camera or fix perspective,
nonrigid deformation, cone geometry, local shadows or bright seam changes. Manual
starting geometry, especially the duplicate print around stitches, remains approximate.
An RGB threshold, blur, area filter and neighbourhood tolerance can miss small/thin
defects; no labelled accuracy has been established. Normalisation intentionally
ignores bounded uniform brightness changes and can reduce their evidence.

The ownership atlas uses one central view per angular position; alternate overlap
observations are not jointly inspected. Calibration must ensure those pixels really
cover the surface intended for control. Frame-list ordinals still do not establish
a shared camera acquisition. Top/internal views remain separate. There is no whole
dataset inspection run, persistent inspection history, trained normals or native
exclusions in this experiment. Existing native controls continue to work separately.

Hardware/PLC, Linux, native CTest/rebuild and the 500 ms production deadline were
not tested. Installation preserves configuration/reference/learning files and the
unrelated local build-script change. The main project records the work in
`knowledge/analis-surface/2026-11-19-cppdetector-unwrap-inspection.md`.
