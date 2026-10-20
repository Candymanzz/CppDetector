# Knowledge Base (for Claude)

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
