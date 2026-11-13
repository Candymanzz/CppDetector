# Algorithms

As of 2026-11-13 the active surface inspector is the imported production core,
wrapped by `src/inspect/inspector.cpp`. See [source, mapping and validation](production-inspection-port.md).
It uses native phase256/ECC512 positioning, compensation before blur, independent
mm limits and the production sensitivity-dependent score. The following original
pipeline notes describe the earlier test-bed implementation.

Planned pipeline per frame (see PLAN.md section 4): downscale -> phaseCorrelate shift -> brightness normalization in ROI
-> absdiff (+blur) with shift tolerance -> sensitivity -> threshold -> morphology/min blob -> score = anomalous px / ROI px (%)
-> verdict score > reject_threshold -> heatmap + contours.
Implemented in src/inspect/inspector.cpp (no downscale tuning yet; ROI polygon supported, auto-ROI not).

## Seam straightness (D18)
ridge (gauss1.2 - gauss6) -> coarse line in band -> half-height left/right edges per row -> robust quadratic fit per edge -> residual vs position-dependent tolerance (tol_mid / tol_edge within edge_frac of the ends; first/last 3% ignored). Only measures shape, never compares with the reference.
