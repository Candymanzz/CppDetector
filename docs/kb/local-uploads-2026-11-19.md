# Local images for surface inspection — 2026-11-19

The owner asked to upload their own images in the unwrap source cards instead
of selecting server frames. A new **Свои изображения** input mode supports
one local file per side camera. **Загрузить фото / Убрать** controls are present
both above inspection and alongside the large original images.

Selecting the first file switches the whole product to uploads. Missing cameras
are explicit and never inherit server images. Filenames, native dimensions and
the N-of-4 summary synchronise between both control groups. The original image,
rectified strip, panorama and existing atlas inspector all use the decoded local
raster. File sizes are limited to 64 MiB, empty/unsupported images show an error;
browser-readable raster formats are supported, with PNG/JPEG/BMP/WebP suggested.
The normal image decoder is shared and retains the 1200-pixel working-size cap.

Dataset selectors/shared navigation are disabled in upload mode. Switching back
to dataset frames or reference self-test preserves their original behaviour and
separate choices; reference mode disables local selectors too. Uploads survive
bucket switching and a list/reference refresh for the same source directory.
Switching datasets clears them. Full page reload discards all local images; the
UI explicitly describes this memory-only lifetime. JSON contains input kind and
file name/size/lastModified, not bytes or local paths. Recipe import cannot
reconstruct file contents.

Input identities contain a unique selection serial, so replacement/removal and
out-of-order decoding cannot revive stale previews/verdicts, including identical
filenames. Object URLs are released after decode/error, including obsolete tasks.
Inspection waits while a selected file is decoding. No upload endpoint or API
write is used; references still come from the current profile. Missing images,
native-size mismatch or unreliable registration cannot produce OK.

The worker, comparison, stitching modules, native EXE, references/profile and
existing fine geometry remain unchanged. An arbitrary photo does not establish
a suitable reference or a calibrated whole-surface inspection.

Validation used the actual EXE/HTTP with a copy of the current f_0000273 profile
(20 listed frames per camera), rather than restoring older f_0000001 fixtures.
Chrome passes **16 scenarios**: actual PNG/JPEG file-input selection, preview/
summary synchronisation, missing cameras, injected local defect/source mapping,
JSON metadata, existing dataset HTTP selection, mode/group/list refreshes,
corrupt input, size mismatch/removal/replacement, deliberately reversed decodes
with the same filename, removal during decode, object-URL cleanup and full reload.
Layouts 1440/1024/390 have no horizontal overflow. No UI API writes or browser
exceptions occurred. Four existing native-inspection mock-DOM UI suites and JS
syntax checks pass. Native rebuild/CTest, physical geometry, defect quality and
line timing were not revalidated because their implementation did not change.

Release scope: three UI files plus KB documentation. Installation checks initial
and final hashes, keeps backups, and protects profile/reference/learned data,
the native runtime, inspection/stitching modules and unrelated local edits.
Artifacts reside in the parent workspace's
`.verification/cppdetector-upload-20261119/`. No service restart is needed;
refresh the browser with Ctrl+F5. No commit or push is part of this task.
