> After the subsequent merge, the core moved to src/production/core and the richer
> incoming adapter/learning/exclusions UI was retained. The report below describes
> the original transfer, before that merge. [Current state](merge-core-2026-11-13.md).

# Production inspection imported into the test bed — 2026-11-13

Source: [defect-detector-v3, boobs+pasha, eefefce455ea93a2184c8d4e9178a0f1eb45847a](https://github.com/Candymanzz/defect-detector-v3/tree/eefefce455ea93a2184c8d4e9178a0f1eb45847a/cppAnalisSurface).
Destination: CppDetector/main, starting at e90b027 with a clean working tree.

The copied core is in `src/inspect/production/core`. It is byte-for-byte identical
to this upstream commit; its provenance and SHA256 hashes are recorded alongside it.
`cd::Inspector` now adapts the test application's reference, ROI, controls and results
to `cas::Reference` / `cas::inspectFrame`. There is one algorithm implementation,
not a separately rewritten approximation. No source repository files were modified.

Imported behavior:

- Shared preprocessing and native positioning: phase correlation at 256, optional
  translation-only ECC refinement at 512; confidence and per-axis limit checks.
- Full-source compensation before comparison resize/blur; immutable reference cache.
- Brightness normalization, per-axis envelope, connected-component filtering,
  sensitivity-dependent normalizer `n`, and the source upper-corner sensitivity boost.
- Camera-specific provisional metric scales and independent horizontal/vertical limits.

Application mapping:

- Sensitivity is now 0..1, including production's forced surface endpoints. Legacy
  saved values above 1 are clamped to 1 when loaded, while the other fields still load.
- Existing score / reject-threshold controls remain 0..100 and map to source values
  divided/multiplied by 100. The existing default reject threshold 10 is retained.
  Production's comparison uses `>=`, not the old test bed's `>`.
- `shift_tolerance` is local comparison relaxation in original pixels and no longer
  determines the maximum allowed positioning shift. Two mm controls are 0..30,
  step 0.05, default 3 / 3.96. Metric scales use the actual camera id.
- `min_blob_area` now uses original-frame pixels. The core scales it for analysis.
- Raw shifts are original-frame pixels at every analysis scale. JSON also exposes
  `shift_mm`, `applied_shift`, `shift_response` and `shift_refined`.
- Downscale changes no longer invalidate the reference; preparations are cached by scale.
- The overlay uses the source's accepted-blob mask. Existing test-bed seam / edge
  checks, auto ROI, frame sources and UI workflows remain separate application features.
  No UI for production learned regions or exclusion polygons was added in this port.

Validation: Windows Release application build passed. Three unmodified upstream
tests (shift accuracy, shared preprocessing, comparison tolerance) passed, along
with the adapter parity test and application integration test: five CTest tests total.
The parity executable checked 288 cases: synthetic odd-size image plus real camera
0/4 JPEGs, scales 1/.5/.37, four sensitivities, shifts and inserted defects. Scores,
raw positions, boxes and verdicts match direct source-core calls. The application
test verifies self comparison, metric validation and settings reload after restart.
JavaScript syntax and git whitespace checks passed.

Artifacts: `artifacts/inspection-port-20261113` (upstream snapshot, build and logs).
Only frame JPEGs were used; archived result JSON / heatmaps were not used as truth.
No full archive sweep, hardware check or full 10-camera latency claim is made here.
The regular runtime binary was not replaced or restarted; the verified executable
is in `artifacts/inspection-port-20261113/build/Release/cppdetector.exe`.

This source branch does not implement automatic label segmentation / a moving label
mask. Importing it does not by itself resolve the white exposed-bucket strip issue
discussed separately. That feature still needs its own implementation and validation.
