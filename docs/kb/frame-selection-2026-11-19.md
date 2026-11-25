# Visible input-frame selection — 2026-11-19

> Update: [local image uploads](local-uploads-2026-11-19.md) add a separate input
> mode and file controls in both locations. Dataset navigation is disabled in
> that mode; unfilled cameras never inherit server frames. The dataset-selection
> workflow below remains available.

The owner could not find which frames the unwrap inspector uses. Previously,
per-camera selects were below the complete panorama and inspection parameters;
the top-level label "Позиция в списках" did not explain its shared effect.

The **Кадры для инспекции** block now precedes the result/panorama. It contains
four labelled file lists in camera-number order, per-camera previous/next buttons,
position/count and a summary of the exact selected names. **Кадр № для всех камер**
selects the same ordinal in all lists and clears individual overrides. A file list
changes only its own camera. A choice automatically starts the existing rebuild
and inspection; **Построить и проверить** repeats it.

**Что проверить → Выбранные кадры** is the normal input mode. **Эталоны ·
самопроверка** visibly identifies references and disables file selectors/navigation;
returning to frames restores the selected file choices. The lower source-preview
lists remain available and synchronise both ways through one frame map/handler.
The summary distinguishes missing files, which retain incomplete-control behavior.
Changing a select preserves keyboard focus.

Old source/strip previews are cleared when they no longer represent the chosen
frame. The previous verdict/report export is invalidated immediately. The underlying
unfolding, registration, thresholds, native settings and GET-only contract are
unchanged. A selected list ordinal still does not establish a common acquisition.

The compact top block was chosen because moving all large source previews would
push the result farther down the page. At mobile widths the shared ordinal field
gets its own row so its arrows/count do not overlap the width selector.

Verification: JS syntax and all four existing UI suites pass. Chrome with the actual
CppDetector HTTP API/frames36 and a temporary profile passes nine interaction cases:
visible selector ordering, actual requested frame changes, per-camera navigation,
two-way synchronisation, shared index, reference-mode restoration, bucket isolation,
missing selection and retained focus. Layouts 1440/1024/390 px fit; desktop controls
are above the result and visible without scrolling. Wide/mobile screenshots are
inspected. No browser exceptions or API write requests occurred.

No native rebuild/CTest or machine quality/timing checks are repeated for this UI
change. Protected hashes cover configuration/references/learning, the three existing
algorithm modules and the unrelated local build-script change.
