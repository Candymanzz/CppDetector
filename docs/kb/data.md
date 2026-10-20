# Dataset

- Location: <repo>/frame/frame (git-ignored). Layout: camera_N/f_XXXXXXX/frame.jpg, N = 0..9, 20 frames per camera.
- Frame ids differ between cameras -> order frames by folder name within each camera, never match across cameras.
- Resolution 960x803 (already downscaled). Reference = earliest frame of the camera; frames after it are inspected.
- Cameras 0-5: white bucket on a dark background, frames nearly identical.
- Cameras 6-9: different view, strong overexposure (large saturated white area) -> expect separate sensitivity/threshold tuning and brightness normalization.
- Non-used files: result.json, heatmap.u8 (legacy, see constraints C7).
