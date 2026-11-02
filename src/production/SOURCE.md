# Local experiment amendment (2026-11-02)

inspector.h/cpp now contain an optional LearnedComparison callback argument, default empty.
No-hook behaviour keeps the original 52aa31ea learned matcher. The local adapter supplies a callback for
its new robust-learning mode; its results with learned samples intentionally extend production behaviour.
Other imported core files remain pinned. See ../../docs/kb/local-learning-v2.md.
The exact-snapshot statements below describe the previous import, before this explicit local hook amendment.

# Native detector snapshot: 52aa31ea

Imported 2026-11-02 from local D:/Pj/defect-detector-v3, commit
52aa31eacb16785ff33dc16b6937ab31c8ec7ee9, cppAnalisSurface/src/core and src/service.
This is the full detector at that commit, including its prerequisite changes, not only the last commit diff.
The current source checkout remains on cpp-detector / 9ef2e2c8; no source code was switched or modified.

All core files are byte-identical to the pinned commit. service.h is unchanged.
The only service.cpp adaptation is Windows atomic replacement of cases.json with MoveFileExW;
std::filesystem::rename does not replace an existing destination on Windows. POSIX keeps native rename.
logger.cpp is included as provenance but is not linked; the test bed retains its own decision logging.

The local adapter preserves native score/threshold calculations, ROI normalisation, original-pixel units,
seam merge rules and heatmaps. UI score/threshold = native value * 100; downscale = inspect_scale.
It adds no detector model. Classic CV compares the primary reference and saved alternate normal patches.

New profiles: config/config_<dataset>_52aa31ea.json; logs: logs/52aa31ea/.
On first start, import reference/ROI/seam from the old _position-x-y profile (fallback: original dataset profile).
Surface settings use fresh source defaults: sensitivity .5, threshold 60%, X/Y tolerance 3,
independent max alignment X/Y 12px, scale 1, min area 20. Existing profiles and uploaded references stay intact.
Learning data: config/config_<dataset>_52aa31ea_learning/learned-regions/{cases.json,normal-*.png}.
Cases are isolated by dataset profile, camera and exact reference fingerprint; ROI changes keep cases.
Replacing a reference deactivates its cases; restoring identical pixels reactivates them.

App stores an untouched copy of the most recent displayed inspection for region selection.
Learning requests require that inspection version and valid original-pixel bounds; injected defects and stale
selections are rejected. The browser pauses emulation before selection and never crops a colour heatmap.
Native Service persists and attaches cases; the same native Reference/inspectFrame algorithm evaluates them.
The local API source identity includes image fingerprint and bbox, allowing multiple regions per frame.

Validation 2026-11-02: MSVC 19.44 / OpenCV 4.14 / Windows Release build; five CTest targets pass.
HTTP smoke: save/retry/list/previews/isolation/stale and malformed rejection/restart/delete.
JavaScript syntax checked with node --check. Full real-frame parity results are in docs/kb/learning-port.md.
No GUI interaction or production hardware verification; no full-cycle 500ms claim.

Final destination verification: D:/Pj/CppDetector Release rebuild and all five CTest targets passed;
HTTP smoke passed against that rebuilt executable. UI startup/region drag/save flow passed a mock-DOM
Node VM test (tests/ui_startup_test.cjs); this also caught and corrected the old engine-name guard.
This is a JavaScript integration check, not interactive browser verification.
