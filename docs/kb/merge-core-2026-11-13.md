# Unified inspection merge — 2026-11-13

Resolved the main-branch merge between local `ed170af` and incoming `bf74c6b`.
Eight files conflicted: CMake, two knowledge files, application/inspector and web UI.
The two sides independently introduced production adapters at different revisions.

The incoming standalone harness is retained: manual/automatic learning, exclusions,
profile migration, native settings validation, calibration, diagnostics and UI.
The requested newer core `eefefce455ea93a2184c8d4e9178a0f1eb45847a` is retained as the
single linked implementation in `src/production/core`. Its native positioning cache,
phase256/ECC512, compensation before blur, 30mm validation and learning cache fix
are preserved. The duplicate `src/inspect/production` snapshot was removed; an
artifact backup and the original commit preserve it. Incoming `src/production/service`
remains the standalone learning store, rather than replacing the test application.

Exclusions now enter the core through per-inspection Settings as normalized polygons,
instead of the older Reference::setExclusions API. Pixel polygons and persistence in
the standalone API are preserved. Both metric controls retain 0..30mm / step 0.05.
Engine identity, UI startup guard and fixtures now agree on eefefce4. Both sides'
knowledge records were kept. Historical tests were adapted for the native alignment
cache and Prepared matcher argument; metric boundary expectations now use 30mm.

Verification:

- Windows Release application and test builds passed.
- All 18 CTest targets passed: 17 initially, then the metric target after updating
  its obsolete 10mm expectation. No algorithm tolerance was changed to pass a test.
- Native shift/cache concurrency, learned placement, surface parity, exclusions,
  learning, upper corners, metric conversion and settings restart are covered.
- HTTP automatic learning and exclusion tests passed on isolated temporary data,
  ephemeral loopback ports, including restart, validation, isolation and deletion.
- Mock-DOM startup/automatic-learning and exclusion editor tests passed.
- JavaScript syntax, Git whitespace and conflict-marker checks passed.

Artifacts: `artifacts/merge-core-20261113` contains the merge-stage backups, removed
snapshot backup, build and logs. Equipment, Linux and live-runtime deployment were
not tested. No commit, push, branch switch or application restart was performed.
Resolved files are staged so the user can finish the merge with `git commit`.
