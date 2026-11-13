# Local label-tracking extension ? 2026-11-13

reference.h/cpp and inspector.h/cpp now extend the eefefce4 baseline with an optional
confirmed label contour, immutable per-frame comparison masks and verification guards.
label_tracking.h is new. Historical byte-identity claims below describe the baseline.
[Behavior and tests](../../docs/kb/label-tracking-2026-11-13.md).

# Unified core ? 2026-11-13

The active core is now eefefce4, with the incoming standalone learning service.
See [hash manifest](CORE_UPSTREAM.md) and [merge report](../../docs/kb/merge-core-2026-11-13.md).
The following snapshot descriptions are historical.

# Standalone exclusion extension — 2026-11-06

reference.h/.cpp, inspector.cpp and seam.h/.cpp now intentionally extend the
20909141 import with optional exclusion polygons/masks. The standalone adapter
owns their API/persistence. The remaining imported sources retain their previous
lineage; empty exclusions preserve native behavior. Older byte-identity claims
below do not include these five files. The API commit remains the base commit.
[Details and verification](../../docs/kb/exclusion-zones-2026-11-06.md).

# Working-tree correction — 2026-11-04

The 20909141 snapshot now includes the common main-app working-tree correction to
core/learning_matcher.cpp: pattern support follows primary(patternBox). All imported
core/service files match the current D:/Pj/defect-detector-v3 working tree. The base
commit reported by the API is unchanged; this correction has not been committed.
[Reproduction and validation](../../docs/kb/learned-placement-fix-2026-11-04.md).

# Current native snapshot — 20909141, imported 2026-11-04

Source: D:/Pj/defect-detector-v3, boobs+pasha,
2090914128af2822c5a94720a89555c02238642b, cppAnalisSurface/src/{core,service}.
All imported C++ files are byte-identical to that checkout. No local callback patch.
Local inspector and learning headers adapt/forward to the native code; logger.cpp is
retained but not linked. Source identity is published by the local API and UI.
Historical data paths with _52aa31ea are intentionally retained for compatibility.
See [scope, checks and limitations](../../docs/kb/production-sync-2026-11-04.md).
Everything below is historical provenance superseded by this section.

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
