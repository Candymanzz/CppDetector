# Algorithms

Planned pipeline per frame (see PLAN.md section 4): downscale -> phaseCorrelate shift -> brightness normalization in ROI
-> absdiff (+blur) with shift tolerance -> sensitivity -> threshold -> morphology/min blob -> score = anomalous px / ROI px (%)
-> verdict score > reject_threshold -> heatmap + contours.
Implemented in src/inspect/inspector.cpp (no downscale tuning yet; ROI polygon supported, auto-ROI not).
