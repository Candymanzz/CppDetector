# Algorithms

Planned pipeline per frame (see PLAN.md section 4): downscale -> phaseCorrelate shift -> brightness normalization in ROI
-> absdiff (+blur) with shift tolerance -> sensitivity -> threshold -> morphology/min blob -> score = anomalous px / ROI px (%)
-> verdict score > reject_threshold -> heatmap + contours.
Implemented in src/inspect/inspector.cpp (no downscale tuning yet; ROI polygon supported, auto-ROI not).

## Independent X/Y shift tolerances (2026-10-26)
`shift_tolerance_x` and `shift_tolerance_y` replace the single UI control. They independently set the comparison
envelope `(2*X+1) x (2*Y+1)` and the alignment limits `max(8,4*X)` / `max(8,4*Y)`. Defaults are 3/3; UI range 0..15,
API range 0..30. As before in CppDetector, these are **working-image pixels after downscale**, not original pixels.
Zero removes envelope expansion along that axis but does not disable positioning (the 8 px minimum remains).
Legacy `shift_tolerance` initializes both axes; explicit axis fields override it. Saves/state emit only the new fields.
Decision records include both tolerances, `shift_limit`, `envelope_kernel` and `downscale` (shift/limits use working px).
Adapted from defect-detector-v3 `position-x-y` / 885a79ed: this is only the X/Y split; CppDetector's score, downscale,
full-frame phase correlation, seam/edge checks and rejection policy are unchanged. Equal axes preserve old results.

## Seam straightness (D18)
ridge (gauss1.2 - gauss6) -> coarse line in band -> half-height left/right edges per row -> robust quadratic fit per edge -> residual vs position-dependent tolerance (tol_mid / tol_edge within edge_frac of the ends; first/last 3% ignored). Only measures shape, never compares with the reference.
