# Camera menu scrolling and native seam diagnostics — 2026-11-24

## Follow-up: keep the whole bucket and verdict visible

The owner clarified that independent scrolling alone did not deliver the intended
view. The first version still placed the oversized image, diagnostics and result
in a scrolling left column. Its tests proved wheel isolation, not simultaneous
visibility of the whole image and verdict.

The left column is now a non-scrolling grid: mode/file selection, a canvas fitted
to both available dimensions while preserving source aspect ratio, and a fixed
compact verdict/score/file/reason. Every ROI/reference/inspection control, detailed
result and seam graph belongs to the right scroll menu. Source coordinate mapping
still uses the real canvas bounds, excluding surrounding black bars. Narrow camera
tabs scroll horizontally; on narrow screens the image/menu stack vertically. Camera
selection clears the compact summary. Other tabs retain their original page flow.

Four existing UI suites and JS syntax passed. Actual Chrome/HTTP with a real photo
and injected defect checked full-image/summary visibility, no left/document scroll,
real wheel input including menu boundaries and image, source ROI click mapping,
1440/1024/800/390 px layouts, camera switching and unwrap. Light/dark screenshots
were reviewed without runtime errors. C++/EXE and original data are unchanged;
no new C++ build or line benchmark was performed. Install UI/docs with backup,
without restarting the runtime. Existing tabs need Ctrl+F5 for the new asset URLs.

The two-scrolling-column description below records the previous implementation.

Scope: the separate `D:/Pj/CppDetector` checkout, `main / 035fa76` with existing
local changes. Main application's compensation/unwrap notes are not its runtime.

## Behavior

In **Настройка и тест**, the header and camera selector stay outside scrolling
columns. The entire right settings menu scrolls within the remaining viewport;
wheel input at its boundaries does not move the document or the image. The left
column scrolls independently to reach the result, ROI controls and diagnostics.
Below 980 px, the two independently scrolling areas are stacked. Other tabs,
including unwrap, retain their existing document scrolling.

**Стык этикетки · измерение** below the image shows the last inspected filename,
found-line coverage, native maximum deviation, its position and tolerance ratio.
The plot uses the actual worst of the two measured edges per cross-section,
signed in source pixels, against that inspection's positive/negative tolerances.
Green means within tolerance, red exceeds it; missing rows are magenta, excluded
rows orange and ignored ends grey. Unmeasured samples have null deviations and
are not connected to measured ones. **Настроить стык** opens and focuses the seam
controls inside the settings menu. Selecting a camera clears the old plot.

The old bottom-of-overlay graph existed, but the current production adapter only
forwarded aggregate seam metrics, centers and bad points. Its per-row arrays were
empty, so `drawSeamDetail` returned immediately. Native `seam.h/.cpp` now expose
trace values from the existing fits and verdict loop; the adapter forwards them
and maps their image coordinates through adaptive geometry when applicable.
The existing image detail and a separate readable SVG plot now use those values.
`seam.samples` adds t/state/deviation/tolerance to the standalone JSON result.
There is no alternative seam detector and no new rejection policy.

The seam must still be enabled and have two endpoints; sensitivity zero and
missing fits produce no invented trace. A missing line is explicitly reported
by the panel; the existing native verdict behavior remains unchanged. The plot
describes the last inspection, not an automatic reinspection after slider edits.

## Verification and limits

- Windows Release build and **21/21 CTest** passed, including a new synthetic
  regression relating the trace to the verdict ratio, coverage, source-coordinate
  geometry, edge/middle tolerances, ignored ends, excluded defects and gaps.
- Eight existing JS suites and JS syntax passed (camera/learning, exclusions,
  adaptive ROI, inspection header and four unwrap suites).
- Chrome with two real EXE/HTTP servers and copied profiles: **4 real-frame and
  4 synthetic inspections** retained the old binary's score, verdict, reason,
  positioning, zones and seam metrics. Straight/kink/gap/missing-line states,
  chart visibility, camera reset, wheel boundaries, focus shortcut, unwrap and
  1440/1024/800/390 px layouts were checked. Light/dark screenshots were reviewed.
- No camera/PLC line, labelled production defect quality, long stream benchmark
  or 500 ms deadline was tested. Additive trace allocation, serialization and
  overlay rendering have a cost; no new performance claim is made.

The source and EXE are installed with backups. Config/reference/learned-data
hashes are checked before and after installation. Build-time `CD_RUNTIME_DIR`
defaults to the source checkout; staging sets it explicitly to the real target,
so the installed EXE's default web/config/log paths do not point into staging.
Artifacts and test results: the main checkout's ignored
`.verification/cppdetector-sidebar-seam-20261124/`.
