# Camera inspection header and shortcuts — 2026-11-16

Baseline: standalone `D:/Pj/CppDetector`, `main / a49069f`. The owner requested
the number of defects after inspecting every frame of a camera, an inspection
button in the same header and keyboard shortcuts.

## Operator behavior

The sticky page header has a camera toolbar in **Настройка и тест**. It contains
**Проверить кадр** and **Проверить все кадры камеры**, with visible shortcut hints:

- **Ctrl+Enter**: inspect the selected dataset frame.
- **Ctrl+Shift+Enter**: inspect all dataset frames of the selected camera.
- The corresponding **Cmd** combinations also work on macOS.

The toolbar reports **Дефектных кадров: X из N** for the last whole-camera run.
This counts `REJECT` verdicts, not rectangles/zones inside an image. The existing
`/api/inspect_all` response has no zone list. `NOT_INSPECTED` is shown separately
as **Без контроля**; missing-reference and unknown verdicts count as **Ошибок**.
They cannot silently turn into passed frames. N is the number of returned results:
the existing backend skips the reference frame and unreadable files.

Counts and the result table are retained separately for each camera until page
reload or another run for that camera. The tooltip identifies the last-run result;
changing parameters does not rerun it. An empty run says **Нет кадров для проверки**.
A failed or malformed batch says **Проверка всех кадров не завершена**, not zero
defects. Both inspection buttons are disabled while a toolbar request is pending,
and the other manual-inspection controls are temporarily disabled too.

Shortcuts work only in the camera tab, ignore held keys/composition and do not run
while an input, select, textarea or editable element has focus. Buttons and keys
call the same guarded handlers. A late response is associated with its original
camera. Single-frame image callbacks also check the camera before drawing.

## Design and scope

The existing `runOne` and `runAll` buttons are moved into the header with unique
IDs, avoiding duplicate actions. Camera batch snapshots live only in the UI;
polling/stream results do not overwrite them. Small screens wrap the toolbar.
The JS and CSS resource URLs are versioned to refresh cached assets.

No C++, detector thresholds, API response, references or saved configurations are
changed. A browser refresh loads these static files; no C++ rebuild is required
for this UI change. Existing unrelated edits to the local camera profile and
learned-case metadata are outside the installation file list.

## Verification and limits

JavaScript syntax and four mock-DOM UI suites pass: the new inspection-header
scenario plus existing startup/learning/label, exclusions and adaptive suites.
The new checks cover counts, incomplete/missing control, per-camera history,
pending requests, failure/retry, empty/malformed replies, late single/batch
responses, shortcuts, composition/repeat/edit-field guards and header markup.

Headless Chrome with a mock API passes real keyboard dispatch, header actions,
camera isolation and layouts at **1440, 1024 and 390 px**, without horizontal
overflow or runtime exceptions. Screenshots are inspected at wide/mobile sizes.
The first browser fixture incorrectly encoded Shift as Alt in CDP; its modifier
was corrected and the fixture passed without an application change.

Artifacts are in the main repository's ignored
`.verification/cppdetector-header-20261116/` (staging, original hashes, screenshots
and browser result). Full C++ tests and physical equipment are not rerun: their
code is unchanged, and these UI/browser checks use controlled API responses.
