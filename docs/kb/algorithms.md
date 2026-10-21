# Algorithms

Planned pipeline per frame (see PLAN.md section 4): downscale -> phaseCorrelate shift -> brightness normalization in ROI
-> absdiff (+blur) with shift tolerance -> sensitivity -> threshold -> morphology/min blob -> score = anomalous px / ROI px (%)
-> verdict score > reject_threshold -> heatmap + contours.
Implemented in src/inspect/inspector.cpp (no downscale tuning yet; ROI polygon supported, auto-ROI not).

## Seam straightness (D18)
ridge (gauss1.2 - gauss6) -> coarse line in band -> half-height left/right edges per row -> robust quadratic fit per edge -> residual vs position-dependent tolerance (tol_mid / tol_edge within edge_frac of the ends; first/last 3% ignored). Only measures shape, never compares with the reference.
