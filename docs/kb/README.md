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
| data.md | dataset layout and per-camera peculiarities |
| changelog.md | what changed and when |

Rules:
- Never use Python, legacy services or the old result.json/heatmap.u8 files as input or reference (user's explicit wish: build from scratch).
- A decision without a recorded reason is not a decision: always add "why".
