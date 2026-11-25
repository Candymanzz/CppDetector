# Atlas false rejections and manual seam correction — 2026-11-19

The owner reported seam/edge defects on images accepted by native inspection.
This change supersedes the global atlas-registration path described in earlier
notes. Native C++/API, profile, references, learned cases and machine decisions
are unchanged.

## Evidence and change

The same files f_0000001–4 from cameras 0/1/3/4 were inspected by the real
native EXE against a copy of the current profile. All 16 native requests returned
OK, score 0. Before this change, atlas positions 2/3/4 rejected with respectively
0.3873%/13, 0.5406%/42 and 1.3474%/79 regions. Afterward positions 1–4 return
OK with zero retained regions, at the unchanged RGB threshold 24, minimum
region 18 px² and rejection threshold 0.05%. File-list ordinals do not prove
synchronised acquisition; this comparison checks identical file inputs.

`unwrap-source.js` now estimates each source's independent translation against
its reference using bounded gradient NCC at up to 600 px. Both UI and camera
profile alignment limits apply. Robust bounded photometry is per source.
Current pixels are sampled at the translated source coordinates before the
surface warp. The resulting atlas uses fixed reference ownership and zero
additional global translation; artificial seams cannot move across the reference.
Unreliable source registration prevents a passing result.

The camera's reference-coordinate ROI and exclusions from GET `/api/state`
limit inspection support. Empty ROI means the whole source. The colour panorama
remains visible outside this support. Coverage is the fraction of the reference
working area actually paired, with the working area's fraction of the entire
atlas shown separately. Transparent inspection pixels were not checked. Empty
support, missing inputs, dimensions mismatch and inadequate coverage cannot pass.

An explicit, persisted **Учитывать допуски сравнения камер** checkbox defaults
on, including old recipes. The profile's shift_tolerance_x/y are scaled to source
pixels and capped at 12. Per-channel reference min/max envelopes are computed
with linear-time monotonic queues, then warped using the same reference geometry.
Atlas differences are measured against these bounds. The existing optional 0–2
atlas-pixel relaxation remains independent. Without the camera envelope, position
2 still rejects at 0.1146%/11 regions after registration/ROI alone; disabling the
checkbox reproduces this stricter comparison. No rejection threshold was raised.

Connected regions, defect area and the bucket verdict are still computed on one
surface image, not by merging native camera verdicts. Registered strips, source
boundaries/dragging and inverse source coordinates follow the source translation.
The v2 JSON report includes actual final matrices, source translations/photometry,
profile working areas/tolerances and scope fraction.

## Geometry remains partial

Reference-only ORB calibration still reliably connects only bucket-1 cameras 0/3.
Position compensation does not calibrate perspective, cone shape or unsupported
camera joins. Hard cuts and scale/colour discontinuities can remain.

The selected-camera editor adds fine horizontal displacement ±20°, vertical
displacement ±30 px at canonical height 312, and width/height scales 80–120%.
These validated corrections compose on top of automatic reference calibration,
apply identically to both surfaces, and persist across file changes. There is a
per-camera reset; resetting all stitching clears them too. Initial corrections
are neutral: no invented measurements or frame-dependent geometric fit.

## Validation and limits

- Existing geometry 6438 assertions, 13 inspection scenarios and stitching tests
  pass. New source tests cover independent motion, fixed seams, ROI/exclusions,
  interior/join defects, alpha holes/missing inputs/empty scope/unreliable motion,
  fine matrix/inverse coordinates and an independent brute-force envelope oracle.
- Chrome with real HTTP and an isolated profile passes 16 interaction scenarios
  and layouts at 1440/1024/390 px. Green defects survive calibration/reset, source
  provenance is correct, partial calibration remains explicit, manual corrections
  persist/reset, tolerance toggles strictly, and positions 1–4 pass. No UI API
  writes or browser exceptions.
- Installation uses a fixed file whitelist, baseline/installed hashes and backup.
  All configuration/reference/learned files, native EXE and the unrelated local
  rebuild script are protected. Artifacts: the parent workspace's
  `.verification/cppdetector-atlas-quality-20261119/`.

This is not native parity: native uses grayscale/blur, energy scoring, ECC and
learned normal cases; the atlas uses RGB/area without learned cases. Small defects
within the envelope or blur may be lost. ROI and geometric gaps are not recovered.
These accepted samples do not validate all real defects, a complete seamless scan,
physical coverage or the 500 ms line deadline.
