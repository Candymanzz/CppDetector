# Integration in main application (Defect Detector V3)

Single place that records everything about porting CppDetector into the main application.
Rule: every integration step, decision and change is appended to the **Change log** at the bottom. Read this file first before touching integration code.

## 1. Goal and scope (user requests, 2026-10-20)
- A new C++ service **cppAnalisSurface** lives in the **main repo** `D:\GitHubPj\defect-detector-v3\cppAnalisSurface` (CppDetector is only the test bed where the module was born; further work on the module happens in the main repo).
- It replaces the Python service analisSurface for the Java orchestrator. The orchestrator and the front-end are **adapted to cppAnalisSurface**, not the other way round: legacy Python crutches (learning, FP zones, perspective line, algorithm_params, Simple/Pro knobs, defer_heatmap) must not leak into the new path.
- No new features beyond CppDetector: no learning, FP zones, sub-zones, presets/strengths.
- The user commits everything personally: **never commit or push in `defect-detector-v3`**.
- Testing happens on Windows locally and on a remote machine (6 cores / 12 threads, Linux in production).
- Not ported: CppDetector web test UI, emulated stream, `--bench` tool.

## 2. Reference source reviewed
- Repo `D:\GitHubPj\defect-detector-v3`, branch **core-dev-p** (HEAD `0a8a0409` 2026-10-16). The first review was on an older branch; differences: no `/render-heatmap` / `defer_heatmap`, `GET /roi-polygon/{product}` exists.
- Python backend `analisSurface/backend/app/{api,services}`; orchestrator client `orchestrator-java/.../integration/clientapi/AnalisSurfaceHttpBinaryRpcSupervisor.java` (only production caller), launcher `integration/python/AnalisSurfaceLauncher.java`, dispatcher `integration/pipeline/stages/InspectPythonExecutor.java`.

## 3. Facts about the old contract that shaped the design
- `product_type` is scoped by the orchestrator as `<product>#cam=<id>`; one reference + ROI per scoped product.
- Frames come through shared memory (`shm_name,width,height,stride,shm_offset`, BGR8). Linux `/dev/shm/<name>`, Windows `%LOCALAPPDATA%\iml_shm\<name>`. Output images (heatmap) are raw uint8 written to `heatmap_u8_output_path`; response carries `{path,width,height,stride,channels,dtype}`.
- ROI is **normalized 0..1** and re-sent with every inspect request.
- Response fields consumed by Java (`inspectJsonToStdioHeader`): `status` ("ГОДЕН"/"БРАК"), `anomaly_score` (0..1), `threshold`, `raw_anomaly_score`, rechecked_*, learned_*, `excluded_normal_zones`, `py_*_ms`, `heatmap_u8`. Java treats БРАК/FAIL/ERROR/REJECT as not ok.
- The orchestrator launches a process pool from `integration.analis_surface_command_*` (it replaces/adds `--port N`), env `ANALIS_INSPECT_WORKERS`, health probe `/health`.
- **Java serializes requests per client**: `InspectPythonExecutor` keeps `Semaphore(1, fair)` per client (Python had the GIL, parallelism came from processes) plus a global `python_request_parallelism` semaphore. A single multi-threaded C++ process would have received one request at a time -> fixed with `python_detector.server_concurrency` (see 6).
- Frames of the real system are 1224x1024 (camera binning), 10 cameras = same format as dataset `frames36`.

## 4. Decisions approved by the user (2026-10-20)
1. **Score scale**: `anomaly_score = min(1, area% / 5)`; threshold in the same 0..1 scale (default 0.25 ~ 1.25% of ROI area). Rejection when `anomaly_score >= threshold`.
2. **Settings**: only what the detector really has: `sensitivity, threshold, shift_tolerance, min_blob_area, inspect_scale`, per analysis profile (camera). Simple/Pro/Detailed/strengths and edge-suppression toggles are not supported.
3. **Processes**: freedom given. Decision: **one multi-threaded process**, workers = hardware threads (limiter), still launchable on several ports via the existing pool mechanism. Reason: no GIL, no duplicated memory, no IPC; measured best. See 8.
4. Folder name `cppAnalisSurface`, in the main repo. 5. No commits by Claude.

## 5. What was built (all in `defect-detector-v3`, nothing committed)

### 5.1 cppAnalisSurface (new, `cppAnalisSurface/`)
CMake + vcpkg manifest (OpenCV core/imgproc/imgcodecs, cpp-httplib, nlohmann-json), presets `windows` / `linux`. README.md documents build, run, API, settings, config.
- `src/core/settings.*` - `Settings`, `SettingsPatch`, `SettingsStore` (`data/settings.json`, re-read when the file changes, atomic save).
- `src/core/reference.*` - `Reference` (BGR + normalized ROI) with a cache of `Prepared` data per `inspect_scale` (blurred gray ref, ROI mask, ROI region, thumbnail for shift estimation). One reference serves requests with different scales.
- `src/core/inspector.*` - `inspectFrame` and `buildHeatmap`. Changes vs CppDetector's Inspector: analysis limited to the ROI bounding box (+pad); shift estimated with `phaseCorrelate` on a <=256 px thumbnail of the ROI box with a Hanning window (low confidence <0.05 => no shift); shift above max(8, 4*tolerance) px => reject `shift_exceeded`; heatmap = diff gated by accepted blobs + soft halo (like Python `_build_heatmap_gray`), downscaled to `heatmap_max_width` first (halo blurred on the small image), ROI mask re-applied after resize.
- `src/core/shm_io.*` - read BGR frame (copy), write raw uint8 image, path resolution identical in spirit to `shm_io.py` (Windows case-insensitive containment check, `.bin`/`iml_cam_*` fallbacks), UTF-8 safe image file reading.
- `src/service/service.*` - registry of references/ROI per product, concurrency limiter (`workers`), test-frame cache (4), visuals writer. `NoReference` exception -> HTTP 400 `error_code: no_reference`.
- `src/web/routes.*` - endpoints: `GET /health`, `GET /detector/health`, `POST /clear-inspection-context`, `POST /upload-ref-shm`, `POST /upload-ref` (multipart), `POST /roi-polygon`, `GET /roi-polygon/{p}`, `GET/PUT/DELETE /settings/{profile}`, `GET /settings/defaults`, `POST /inspect-shm`, `POST /inspect-shm-visuals`, `POST /inspect-test-frame`, `POST /inspect` (multipart). Every JSON object gets `detector_id`. Error body `{detail, error_code}`. Heatmap export failure never fails `/inspect-shm`; on `/inspect-shm-visuals` it is HTTP 500.
- Response keeps the Python field set (unused = empty/zero) + new `area_percent, reason, shift, zones, settings, frame_width/height`.
- `src/main.cpp` - args `--host --port --workers --data-dir`, env `ANALIS_INSPECT_WORKERS`, `cv::setNumThreads(1)`, HTTP thread pool `max(64, workers*4)` (**one HTTP thread per keep-alive connection**: a pool smaller than the number of client connections stalled batches for the 5 s keep-alive timeout - found and fixed during load testing).

### 5.2 Java orchestrator (`orchestrator-java`)
- NEW `integration/clientapi/CppAnalisSurfaceHttpSupervisor.java`: isolated HTTP client implementing `BinaryRpcSupervisor` with the same `op`s the pipeline uses (`set_reference_shm, inspect_shm, inspect_test_frame, sync_client_reference_bundle, set_active_reference_view, replace_fp_zones (no-op), clear_inspection_context, health, stop`). Sends only what the C++ service understands; ROI inside every inspect body; lazy reference upload with signature cache; **auto-recovery**: HTTP 400 `no_reference` -> re-upload reference and retry once; temporary UI settings from `analysis_test_settings.settings` (or legacy `simple`) become `settings` of the request; `inspect_scale`, `threshold`, algorithm params, learning flags are NOT forwarded.
- NEW `integration/config/DetectorBackend.java`: `python_detector.backend: cpp|python` (default python = backward compatible) and `server_concurrency`.
- NEW `integration/http/controller/OrchestratorDetectorSettingsHttpController.java`: `/api/orchestrator/detector-settings/{camera/<id>|defaults|<profile>}` -> cpp `/settings/...` (camera id resolved to analysis profile). Registered in `HttpFrontController`.
- EDITED (minimal): `ServicePoolLifecycle` (+overload `startAnalisSurfaceHttpPool(..., cppBackend)` chooses the client class), `IntegrationServicePoolFactory` / `DefaultIntegrationServicePoolFactory` (backend-aware overload), `ChildProcessStartupService` (reads backend, logs it), `InspectPythonExecutor` (fifo lane permits = `server_concurrency`), `AnalisSurfacePoolSupport` and `CriticalServiceWatchdog` (know the new client class), `BinaryInspectHeaders.putEphemeralTestKnobs` (passes native `settings`).
- The old Python client and Python-only controllers (`analysis-settings`, `fp-zones`, `learning`) are untouched and unused when `backend: cpp`.

### 5.3 Front-end (`front-end`)
- NEW `components/SettingList/DetectorSettingsPanel.tsx`: five settings with sliders + numeric inputs + hints, autosave (450 ms), "Сбросить", per camera or all cameras, handle `{save, getDraft}`; in the test-analyze modal drafts are sent as temporary `analysis.settings`.
- EDITED: `MainOverview.tsx` and `SettingList.tsx` use the new panel instead of `AnalysisSettingsPanel` (file kept, unused); `shared/api/{types,index,orchestratorApi}.ts` (types `DetectorSettings*`, methods `get/set/resetCameraDetectorSettings`, `getDefaultDetectorSettings`, `testAnalyzePinnedFrame(... analysis.settings)`).
- `npm ci --ignore-scripts` was run in `front-end/` (node_modules is git-ignored); `tsc --noEmit` and eslint on the changed files pass.

### 5.3a Build and run scripts (cppAnalisSurface/)
`rebuild-and-run.ps1|.cmd` and `run.ps1|.cmd` (+ `scripts/common.ps1`), Windows PowerShell 5.1 compatible, English messages (PS 5.1 mis-reads UTF-8 without BOM). rebuild-and-run: stops the running exe of this build, then checks and installs only what is missing: Visual Studio C++ tools (vswhere; downloads Build Tools 2022, needs UAC), CMake >= 3.25 (portable zip into `.tools/`), git (portable MinGit, only for vcpkg), vcpkg (VCPKG_ROOT, D:vcpkg, C:vcpkg, %USERPROFILE%vcpkg, else clone next to the project drive + bootstrap), libraries opencv4/cpp-httplib/nlohmann-json (probe headers in `buildvcpkg_installedd-windows-static-md`, SHA-256 stamp of `vcpkg.json` in `build.vcpkg-manifest.sha256`), then configure (generator detected from vswhere, so no preset is required), build, run. Options: `-CheckOnly -NoRun -Clean -Port -Workers -DataDir -BindHost -Config`. run: refuses to start when the port is busy or the exe is missing. `.cmd` wrappers clear `PSModulePath` (a polluted module path made `Get-FileHash` unavailable in a child process); the manifest hash is computed with .NET for the same reason.
Tested here: CheckOnly; all installed -> skips and builds in 2 s; `-Clean` -> libraries reinstalled from the vcpkg binary cache, configured and built in 72 s; run via `.cmd`, busy-port refusal, rebuild while running, missing exe message; simulated missing CMake/git -> portable download and second pass skip. NOT tested: real installation of Visual Studio Build Tools and a vcpkg clone on a clean machine (URLs verified to resolve).

### 5.3b Root launch scripts of the main app (defect-detector-v3/)
The original `run.ps1` / `rebuild-and-run.ps1` hard-required Python + venv and knew nothing about the C++ detector. Added `detector-backend.ps1` (`Test-CppDetectorBackend`: reads `python_detector.backend` from the config, following `imports:` of the aggregator `config.yaml`). With `backend: cpp`: `rebuild-and-run.ps1` skips the Python tool check and venv/pip and instead calls `cppAnalisSurfaceebuild-and-run.ps1 -NoRun` (installs only what is missing, builds), then checks the exe; `run.ps1` requires `cppAnalisSurfaceuildReleasep_analis_surface.exe` instead of the Python venv. With any other value the old Python behaviour is unchanged. Verified: PowerShell parser, backend detection (cpp via imports = true; python / key missing / file missing = false). Not run end to end here (no JDK17/Maven/dotnet/Python toolchain of the main app on this machine). Note: editing CRLF scripts with `perl -0pi` and backslashes in replacements corrupted paths (escape sequences); use literal .NET string replacement instead.

### 5.4 Config examples
`config.exemple/blocks/01-core.yaml` (cpp autostart commands, `inspect_workers: 12`, `python_parallelism: 10`, `python_server_pool_size: 1`, `python_request_parallelism: 10`), `blocks/20-detectors.yaml` (`backend: cpp`, `server_concurrency: 10`), `config.exemple/config.json` (same keys). The old Python command is kept as a comment.

## 6. Concurrency model (why it is fast)
Java: one client, lane permits = `server_concurrency` (10), global `python_request_parallelism` (10) -> up to 10 requests in flight. C++: HTTP threads >= connections; the `workers` limiter bounds concurrent inspections (default hardware threads, 12 on the target 6c/12t); each inspection is single-threaded OpenCV. Setting `workers` below the number of simultaneous requests only queues work; setting it near the logical thread count was best.

## 7. Verification performed (Windows, 8 logical threads)
- Service via real SHM files (`%LOCALAPPDATA%\iml_shm`): clean frame score 0, blank-bucket frame 45% area -> БРАК, heatmap file written with the requested size, error codes (missing SHM file 400, missing field 400, bad JSON 400, no reference 400, unknown ROI 404), settings GET/PUT/DELETE/persistence across restart, profile applied to inspections.
- Load: 10 concurrent `/inspect-shm` (SHM read + analysis + heatmap write), full 1224x1024: batch 152-179 ms; `inspect_scale` 0.75: 108-119 ms (workers 12 and 8). workers=3: 215 ms; 4: 206 ms; 6: 163 ms. Budget 500 ms.
- Java client against the live service (`CppSmoke`, `CppRecover` in the session scratchpad): lazy reference upload, heatmap fields, temporary settings, `no_reference` auto-recovery after a service restart, replace_fp_zones no-op. Java compiles (`mvn -o -DskipTests compile` and `test-compile`); unit tests could not be run offline (surefire plugin missing).
- Not verified: the full orchestrator with real cameras, the front-end panel in a browser, Linux build and `/dev/shm` path (code written, untested here).

## 8. Open items / risks
- Linux build on the remote machine: needs vcpkg (or system packages) and the first OpenCV build (~10-40 min); `shm_io` Linux branch untested.
- Remote machine: verify that the orchestrator on that branch really sends `reference_shm_name` with inspect requests (the client relies on it for lazy upload and auto-recovery); otherwise references are only set through `sync_client_reference_bundle`/`set_reference_shm`.
- `/inspect-test-frame` resizes the JPEG to the reference size (as Python).
- Threshold semantics changed (score scale in 5.x): saved thresholds from the Python system are not compatible; operators must re-tune with the new panel.
- Heatmap shows only accepted blobs (below the sensitivity threshold nothing is painted).
- Scores on real production frames still need tuning of ROI/threshold per camera (see data.md frames36: cameras 2 and 7 look inside the bucket).

## 9. Change log
- 2026-10-20: reviewed `defect-detector-v3` branch `core-dev-p`; plan approved by the user with the decisions in section 4.
- 2026-10-20: implemented cppAnalisSurface (C++), Java client/backend switch/settings proxy/concurrency lane, front-end settings panel, config examples, README; verified as in section 7. Nothing committed (user commits).
- 2026-10-20: added Windows scripts `rebuild-and-run` / `run` (.ps1 + .cmd) with dependency check/auto-install (see 5.3a) and README section; `.tools/` added to cppAnalisSurface/.gitignore.
- 2026-10-20: root run.ps1 / rebuild-and-run.ps1 of defect-detector-v3 made backend-aware (new detector-backend.ps1): cpp backend builds cppAnalisSurface and no longer needs Python (integration doc 5.3b).
- 2026-10-20: FIXED config.exemple / README: `python_parallelism` 1 -> 10 (it sizes the detector stage thread pool; 1 serialised the 10 cameras, cycle ~680 ms instead of ~350-400 ms). See decisions.md D16. The user's working config must be changed too.
