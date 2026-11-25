# Local OpenCV.js runtime

Version: OpenCV **4.12.0**, loaded only by the reference-stitching worker.

- Distribution: https://docs.opencv.org/4.12.0/opencv.js
- Licence: `OpenCV-LICENSE.txt`, from https://raw.githubusercontent.com/opencv/opencv/4.12.0/LICENSE
- SHA256 of `opencv-4.12.0.js`: `C7D6967284ACC7EB50897EEB4186690444396BBE834AEB7CAF7A565285976829`
- Downloaded 2026-11-19. The runtime is served locally; no CDN request is needed
  while running CppDetector and source images remain in the browser.

ORB/BF matching, CLAHE and robust affine fitting are used during reference
calibration, not in the per-frame native inspection path. The native OpenCV 4.14
installation is independent and unchanged. Runtime initialisation uses
`onRuntimeInitialized` because this build exports a self-resolving thenable.

The upstream JS export configuration for this pinned release is at
https://github.com/opencv/opencv/blob/4.12.0/platforms/js/opencv_js.config.py.
