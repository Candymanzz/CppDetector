# Dataset

- Location: <repo>/frame/frame (git-ignored). Layout: camera_N/f_XXXXXXX/frame.jpg, N = 0..9, 20 frames per camera.
- Frame ids differ between cameras -> order frames by folder name within each camera, never match across cameras.
- Resolution 960x803 (already downscaled). Reference = earliest frame of the camera; frames after it are inspected.
- Cameras 0-5: white bucket on a dark background, frames nearly identical.
- Cameras 6-9: different view, strong overexposure (large saturated white area) -> expect separate sensitivity/threshold tuning and brightness normalization.
- Non-used files: result.json, heatmap.u8 (legacy, see constraints C7).

## Second dataset: frame/test-pr-ph (real product shots)
- Files: good1, good2, bad1, bad2, bad21 (.jpg), 2448x2048, dark grayscale shots of a paint bucket with a printed label (small text). Different size than the camera frames (960x803).
- Handled via UI: any of them can be used as a camera's reference ("Эталон из test-pr-ph") or inspected ("Проверить файл"). A camera whose reference has another size than its stream frames reports `size_mismatch` (verdict NO_REFERENCE) for stream frames.
- Real defects visible: smudge on a pictogram (bad2, bad21), smeared text on the right column (bad2), gaps in text lines and a thin vertical line near the bottom (bad1).
- Bucket pose differs slightly between shots (shift up to ~20 px at full res and a small rotation): the label's left/right edges give false zones in bad1.

## Production dataset: frame/frames36 (main working set from now on, default in main.cpp)
- camera_0..9, 100 frames per camera, 1224x1024, colour. Same layout (f_XXXXXXX/frame.jpg); result.json/.u8 ignored (C7). Frame numbers are NOT contiguous: 1-81 and 378-396 (two captures); order = sorted folder name.
- Content by list index (cam 0,1,3,4,5,6,8,9): idx 1-41 labelled blue bucket (reference = idx 1); idx ~42-62 a white bucket WITHOUT label (score 12-45% vs reference at sens 0.5 -> clearly different, probably the intended "defect": missing label - to be confirmed by the user); idx 63-81 labelled bucket again (score ~0); idx 82-100 (frames 378-396) labelled bucket again with a slightly different pose (score 1-12%, cam 5/6/8 highest).
- Cameras 2 and 7 look into the bucket from above (inside is plain white and identical for every product) -> score 0 everywhere; they cannot see label defects. Camera 6 sees a different side of the bucket (photo of a woman on the label).
- Settings per dataset: config/config_<datasetfolder>.json, uploaded references config/ref_config_<dataset>_cam<N>.png.
