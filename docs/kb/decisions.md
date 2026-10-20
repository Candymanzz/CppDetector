# Decisions

## D1 - Build stack (2026-10-20)
Context: no C++ OpenCV on machine; Python OpenCV unusable; MSVC Build Tools 2022 present.
Decision: CMake + vcpkg manifest (vcpkg at D:\vcpkg), opencv4 minimal features (jpeg, png), cpp-httplib, nlohmann-json.
Why: one reproducible dependency flow, header-only web server keeps the app a single exe.
Consequences: first configure builds OpenCV (20-40 min); later builds are cached.

## D2 - Alignment strategy (2026-10-20)
Decision: only translation via cv::phaseCorrelate + a per-camera shift tolerance (px) applied in diff (min over neighborhood). No ORB/ECC.
Why: 500 ms budget for 10 frames; user said micro-shifts are minor and should be handled by a tolerance setting.
Consequences: rotations/large shifts are not corrected; flagged as reason `shift_exceeded`.

## D3 - Reference selection (2026-10-20)
Decision: reference per camera = earliest frame by folder name; can be re-picked from UI.
Why: user wants to emulate real inspection: first frame becomes reference, later frames are checked against it.
