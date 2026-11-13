# Automatic region learning — 2026-11-04

The main application already selects a normal region with learnedRegionBounds in
front-end/src/shared/learnedRegion.ts, called by ModalWrapper's LearnFrameAction.
After the user presses «Дообучить», every nonzero raw heatmap pixel is enclosed,
mapped to original-frame coordinates using floor/ceil, padded by 10 source pixels
and clipped to the image. Disconnected anomalies share one rectangle. This is an
explicit operator acceptance of that inspection, not unattended training.

CppDetector now has the same one-click behavior. The three manual drag/save/cancel
buttons are replaced by «Дообучить». It pauses the emulated stream, displays the
versioned original inspection image, draws the automatic green rectangle and saves.
The rectangle remains until the operator changes view/camera or inspects a new frame.
An empty map, injected defect or missing reference disables the button. Success
disables repeat saving of that displayed inspection. Failed saves can be retried.

The equivalent bounds calculation lives in src/inspect/learning_region.h because
the local UI receives a JPEG overlay rather than raw heatmap bytes. App::inspectLocked
computes learning_bbox from the raw native heatmap of that exact inspection. No
bounds are inferred from colored/JPEG overlay pixels or anomaly contour rectangles.
The source detector and its heatmap resolution are unchanged. POST /api/learning
accepts {version} and selects the stored learning_bbox while holding the camera lock;
the crop still comes from untouched c.inspected. Explicit bbox requests remain API
compatible. Native matcher, data format and reference fingerprints are unchanged.

No empty-map fallback to the whole frame is used. Version, source image and injected
defect checks remain active; camera switches while an image loads cancel the save.
Settings/reference changes invalidate the inspected source as before. No profiles,
reference PNGs or existing learned cases are rewritten by installing this change.

Validation: Windows Release, 11/11 CTest; main-frontend bounds fixtures (disjoint
anomalies, frame edges, zero maps, fractional scaling); local automatic learning,
new-defect rejection and restart; HTTP automatic save without bbox, retry, empty
map/stale/injected/settings guards, new defect and restart; existing HTTP learning;
JavaScript syntax and mock-DOM one-click/green rectangle/duplicate/empty/injected/
missing-reference/stale/camera-switch checks. Production parity remains in CTest;
the previous 800-frame benchmark is not rerun because native code is unchanged.
Interactive browser and production hardware are not verified by these checks.
