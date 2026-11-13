# Label contour tracking ? 2026-11-13

[Confirmed contour, exposed-bucket masking and verification](label-tracking-2026-11-13.md).

# Inspection exclusions — 2026-11-06

Draw polygon zones in the camera setup to skip inspection inside them. Exclusions
are persistent per-camera holes in ROI and also apply to the seam.
[Behavior, reasons, checks and limits](exclusion-zones-2026-11-06.md).

# Learned pattern support — 2026-11-04

Pattern support is now computed against the primary reference at the matched placement,
rather than translated from its original bbox. This fixes normal edge regions that
continued to reject after learning. [Details and validation](learned-placement-fix-2026-11-04.md).

# Automatic learning — 2026-11-04

The current UI uses one «Дообучить» button and automatically encloses all nonzero
raw heatmap pixels with 10 source pixels of padding, matching the main application.
The inspected version pins the source crop. Manual selection notes below are historical.
[Implementation, compatibility and checks](automatic-learning-2026-11-04.md).

# Current update — 2026-11-04

Native core/service are now pinned to main-app 20909141. The production robust matcher
replaces the local callback hook; metrics, upper corners and sensitivity-zero seam
behavior follow production. Existing data paths remain compatible. Read
[the authoritative sync report](production-sync-2026-11-04.md) for current behavior,
validation and the pre-existing missing reference names. Older sections below are history.

Current experiment (2026-11-02): [local-learning-v2.md](local-learning-v2.md) adds independent search,
masked application, moving-shadow comparison and per-case diagnostics. Native matching remains selectable.

Current update (2026-11-02): core/service pinned to 52aa31ea, with region learning and independent alignment controls.
Read [learning-port.md](learning-port.md) and [native provenance](../../src/production/SOURCE.md).
The earlier snapshot descriptions below are historical.

# Knowledge Base (for Claude)

Current backend (2026-10-26): branch 123 mirrors position-x-y native core 885a79ed; see algorithms.md and
src/production/SOURCE.md first. Old score/working-pixel/edge notes below are historical. Use the separate native profile.

Purpose: before changing or adding anything, read the relevant file here to learn what may be changed,
what must not, and why a decision was made. Written in English, split by topic. Update immediately
after every significant decision.

| File | Content |
|---|---|
| architecture.md | modules, data flow, threading |
| decisions.md | decision log (ADR style: context -> decision -> reason -> consequences) |
| algorithms.md | inspection algorithm, parameters and their effect |
| constraints.md | invariants / things that must not change, with reasons |
| data.md | datasets (frame/frame, frame/frames36, frame/test-pr-ph) and per-camera peculiarities |
| integration-in-main-application.md | porting the inspection core into the main app (Defect Detector V3 / analisSurface): contract, plan, change log of every integration step |
| changelog.md | what changed and when |

Rules:
- Never use Python, legacy services or the old result.json/heatmap.u8 files as input or reference for CppDetector itself (user's explicit wish: build from scratch). The exception is the integration work, where the Python analisSurface API is the contract to reproduce (see integration-in-main-application.md).
- A decision without a recorded reason is not a decision: always add "why".
- Every integration action is logged in integration-in-main-application.md (section Change log).

Current surface algorithm (2026-11-13): [production inspection port](production-inspection-port.md).
