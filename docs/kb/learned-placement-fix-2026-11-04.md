# Learned boundary remains rejected after learning — fixed 2026-11-04

Actual report: camera 1, f_0000577, 740x520. The saved normal PNG is byte-for-byte
the corresponding crop of that same frame (pixel norm difference zero). It is active,
not a stale reference. Before the fix the matcher reports correlation .999809,
placement +5/0 px, removes 243 of 773 candidate pixels and leaves 530. Final
score 70.729444% exceeds the unchanged 60% reject threshold. The result matches
the operator's narrow red regions at the left ROI boundary; the seam is disabled.

Cause: the stored crop is raw-frame data, while the current inspection is globally
aligned (-4.93693/+0.02736 original px in this case). The pattern support mask was
computed against the primary reference at the original bbox, then translated to the
matched location. Near a steep label boundary those two reference regions differ,
so translated support misses part of the already learned normal. A sample can even
equal the reference at its original position and be incorrectly skipped before locating.

Fix: compute pattern support against primary(patternBox) at the matched position,
and test both source and placed support before deciding there is no learned difference.
The smooth-shadow model retains its original source residual and is only evaluated
when source support exists. Pixel comparison to the saved normal still runs; no
rectangle is blindly ignored, and thresholds/ROI/global shift limits are unchanged.
mask_pixels now reports the support actually used by the pattern path.

The common source cppAnalisSurface/src/core/learning_matcher.cpp was fixed, and
the identical file is imported here. Native lineage is main-app 20909141 plus this
uncommitted working-tree fix; engine_source_commit continues to identify the base
commit, not a new commit. Production service deployment is outside this update.
Existing learned cases and image fingerprints are compatible; no retraining is needed.

Validation:
- The actual stored case removes all 773 candidate pixels after the fix, leaves zero,
  and scores zero. At scales 1/.73/.5 the same frame passes; a new black 22x36 mark
  inside the learned bbox scores 100% and rejects at all three scales.
- New deterministic learned_placement_test fails on the old matcher and passes on
  the fix, including nonzero ROI origin, source support zero and both shadow switches.
  A new defect inside the learned support remains visible.
- Main detector isolated Windows Release + 8/8 CTest + robust-learning HTTP pass.
- Standalone Release + 12/12 CTest cover old/native/local/shadow/corner/metric paths;
  existing and automatic-learning HTTP and UI checks are included in installation.
The small initial 12x20 injected mark scored 44.7572%, below the configured 60%; it
was not evidence of a missed reject. The larger negative control above exceeds the
unchanged threshold. This is not a labelled production-quality benchmark.

No saved profiles, reference images or learned PNG/JSON files are replaced. The test
application is rebuilt/restarted with its previous arguments; the main runtime is
not restarted. Linux and physical production inspection are not verified here.
