"use strict";

const $ = (id) => document.getElementById(id);

/* Production-compatible seam only; the local edge algorithm must not affect test verdicts. */
const LINES = [
  { key: "seam", title: "Стык этикетки", hint: "тонкая яркая линия", color: "#00e5ff", step: false },
];
// name: [min, max, step, decimals, label]
const LINE_PARAM_DEFS = {
  reference_tolerance: [0.1, 50, 0.1, 1, "Допуск стыка, px"],
  mask_low: [0, 255, 1, 0, "Маска: нижний порог границы"],
  mask_high: [0, 255, 1, 0, "Маска: верхний порог границы"],
  mask_coverage: [0.3, 1, 0.01, 2, "Минимальная доля найденного стыка"],
  mask_max_gap: [0, 1, 0.01, 2, "Максимальный непрерывный пропуск"],
  mask_defect_run: [3, 100, 1, 0, "Мин. длина пропажи края / изменения ширины, px"],
  mask_width_tolerance: [0.05, 2, 0.05, 2, "Допуск изменения ширины"],
  band: [5, 200, 1, 0, "Полоса поиска (±), px"],
  tol_mid: [0.1, 50, 0.1, 1, "Допуск в середине, px"],
  tol_edge: [0.1, 50, 0.1, 1, "Допуск на краях линии, px"],
  edge_frac: [0, 0.5, 0.01, 2, "Длина краевой части (доля)"],
  smooth_win: [20, 200, 5, 0, "Окно сравнения, px"],
  min_contrast: [2, 40, 1, 0, "Мин. контраст, ур. серого"],
  max_gap: [0.05, 1, 0.05, 2, "Макс. разрыв линии (доля)"],
};
const lineParams = (ln) => (ln.step ? ["band", "tol_mid", "tol_edge", "edge_frac", "smooth_win", "min_contrast", "max_gap"] : ["reference_tolerance", "band", "tol_mid", "tol_edge", "edge_frac", "mask_low", "mask_high", "mask_coverage", "mask_max_gap", "mask_defect_run", "mask_width_tolerance"]);
const lineParamText = (p,value) => p==="mask_width_tolerance"?fmt(value*100,0)+"%":fmt(value,LINE_PARAM_DEFS[p][3]);
const S = {
  cams: [],            // static info per camera (frames, params, roi, ...)
  status: null,
  cam: 0,
  tab: "grid",
  cameraBatches: [],
  inspectionPending: null,
  mode: "ref",         // ref | frame | result
  exclusions: [], exclusionDraft: null, exclusionsSaving: false,
  roi: [],             // working copy of the ROI of the current camera
  img: { ref: null, frame: null },
  lastVer: [],         // last overlay version shown per camera
  drag: -1,
  ln: {},              // per zone key: { d: working copy of the settings, pick, skipMode, roiSel, drag }
};

/* ---------- helpers ---------- */

function toast(msg, isErr) {
  const t = $("toast");
  t.textContent = msg;
  t.className = "toast show" + (isErr ? " err" : "");
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => (t.className = "toast"), isErr ? 4500 : 1800);
}

async function api(path, opts) {
  let res;
  try {
    res = await fetch(path, opts);
  } catch (e) {
    toast("Нет связи с сервером", true);
    throw e;
  }
  const data = await res.json().catch(() => ({}));
  if (!res.ok) {
    toast(data.error || "Ошибка " + res.status, true);
    throw new Error(data.error || res.status);
  }
  return data;
}

const post = (path, body) =>
  api(path, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body || {}) });

function el(tag, cls, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
}

const fmt = (v, d = 2) => (typeof v === "number" ? v.toFixed(d) : "—");
const cur = () => S.cams[S.cam];

function verdictBadge(v) {
  const map = { OK: ["ok", "OK"], REJECT: ["bad", "БРАК"], NO_REFERENCE: ["warn", "нет эталона"], NOT_INSPECTED: ["warn", "Контроль не выполнен"] };
  const [cls, text] = map[v] || ["warn", v || "—"];
  return el("span", "badge " + cls, text);
}

function lineText(name, s) {
  if (!s) return name + ": отклонение линии.";
  if (s.gap_reject) return `${name}: в линии разрыв ${Math.round(s.gap * 100)}% длины — линия не найдена на этом участке.`;
  return `${name}: линия искривлена, отклонение ${fmt(s.max_dev, 2)} px на ${Math.round(s.worst_t * 100)}% длины (${fmt(s.ratio, 2)}× от допуска).`;
}

function reasonText(r) {
  switch (r.reason) {
    case "seam_reference_required": return "Задайте стык на эталоне и нажмите «Запомнить стык эталона». Контроль стыка не выполнен.";
    case "seam_reference_changed": return "Изменился эталон или линия поиска. Стык нужно запомнить заново. Контроль стыка не выполнен.";
    case "seam_reference_not_found": return "На эталоне не удалось уверенно выделить стык. Уточните два конца линии или выберите другой эталон.";
    case "seam_mask_outside_frame": return "Точки стыка находятся вне текущего кадра. Задайте линию заново. Контроль стыка не выполнен.";
    case "seam_mask_line_invalid": return "Для поиска стыка нужны два конца линии длиной не менее 50 px. Контроль стыка не выполнен.";
    case "seam_mask_not_found": return r.seam?.search_method==="reference" ? "Не удалось уверенно сопоставить стык с эталоном. Проверьте кадр и выбранный эталон. Контроль стыка не выполнен." : "Маска не подтверждает достаточную длину стыка. Проверьте положение полосы и пороги маски. Контроль стыка не выполнен.";
    case "seam_mask_gap": return "В найденном стыке слишком длинный пропуск без подтверждённой границы. Контроль стыка не выполнен.";
    case "seam_mask_reference_pair": return "На эталоне не подтверждены обе стороны стыка. Проверьте линию, ширину полосы и пороги маски. Контроль двух краёв не выполнен.";
    case "seam_mask_side_missing": return "Пропал участок одного или обоих краёв стыка.";
    case "seam_mask_width": return "Ширина стыка изменилась относительно эталона.";
    case "label_position_unreliable": return "Не удалось достоверно измерить положение этикетки — нужна проверка кадра.";
    case "label_boundary_unreliable": return "Граница этикетки не подтверждается. Проверьте контур, освещение и наличие этикетки.";
    case "label_coverage_insufficient": return "Недостаточно общей видимой области этикетки для проверки.";
    case "adaptive_registration_failed":
      return "Не удалось уверенно сопоставить знакомую поверхность. Контроль не выполнен.";
    case "adaptive_low_coverage":
      return `Проверяемая поверхность: ${fmt((r.adaptive_roi?.coverage || 0) * 100, 1)}%, требуется ${fmt((r.adaptive_roi?.min_coverage || 0) * 100, 0)}%. Контроль не выполнен.`;
    case "adaptive_seam_unavailable":
      return "Недостаточно видимой линии шва для проверки. Контроль не выполнен.";
    case "score_over_threshold":
      return `Оценка ${fmt(r.score, 3)}% достигла порога ${fmt(r.threshold, 2)}% — найдено зон: ${(r.zones || []).length}.`;
    case "shift_exceeded":
      return `Оценённый сдвиг (${fmt(r.shift_raw?.[0])}, ${fmt(r.shift_raw?.[1])}) px исходного кадра; пределы (${fmt(r.shift_limit?.[0])}, ${fmt(r.shift_limit?.[1])}) px. Достоверность ${fmt(r.shift_response, 3)}.`;
    case "seam_deviation":
      return lineText("Стык", r.seam);
    case "edge_deviation":
      return lineText("Кромка", r.edge);
    case "size_mismatch":
      return "Размер кадра не совпадает с размером эталона этой камеры — сравнение невозможно.";
    case "no_reference":
      return "Для камеры нет эталона.";
    case "inspection_excluded":
      return "Вся область проверки исключена из инспекции.";
    default:
      return r.reject ? r.reason : "";
  }
}

/* ---------- tabs ---------- */

document.querySelectorAll("#tabs button").forEach((b) =>
  b.addEventListener("click", () => showTab(b.dataset.tab))
);

function showTab(name) {
  S.tab = name;
  document.documentElement.classList.toggle("camera-workspace", name === "cam");
  $("inspectionHeader").hidden = name !== "cam";
  document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
  document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("active", t.id === "tab-" + name));
  if (name === "log") refreshLog();
  if (name === "cam") draw();
  if (name === "unwrap") window.UnwrapUI?.open();
}

/* ---------- grid ---------- */

function buildGrid() {
  const g = $("grid");
  g.replaceChildren();
  S.cams.forEach((c, i) => {
    const tile = el("div", "tile");
    tile.id = "tile" + i;
    tile.tabIndex = 0;
    const imgBox = el("div", "img");
    const img = el("img");
    img.alt = "Камера " + i;
    img.src = `/api/frame?cam=${i}&frame=&t=${Date.now()}`;
    imgBox.append(img);
    const meta = el("div", "meta");
    const head = el("div", "head");
    head.append(el("b", "", "Камера " + i), el("span", "badge warn", "—"));
    meta.append(head, el("div", "muted small", "ожидание результата"));
    tile.append(imgBox, meta);
    tile.addEventListener("click", () => openCam(i));
    tile.addEventListener("keydown", (e) => { if (e.key === "Enter") openCam(i); });
    g.append(tile);
  });
}

function updateTiles(status) {
  status.last.forEach((r, i) => {
    const tile = $("tile" + i);
    if (!tile || !r.frame) return;
    tile.classList.toggle("reject", !!r.reject);
    const head = tile.querySelector(".head");
    head.replaceChild(verdictBadge(r.verdict), head.lastChild);
    tile.querySelector(".meta .muted").textContent =
      `${r.frame} · score ${fmt(r.score, 2)}% / порог ${fmt(r.threshold, 1)}% · ${fmt(r.ms, 0)} мс` +
      (r.reject && r.reason !== "score_over_threshold" ? " · " + r.reason : "");
    if (r.ver && S.lastVer[i] !== r.ver) {
      S.lastVer[i] = r.ver;
      tile.querySelector("img").src = `/api/overlay?cam=${i}&v=${r.ver}`;
    }
  });
}

/* ---------- status polling ---------- */

async function poll() {
  try {
    const s = await api("/api/status");
    S.status = s;
    const btn = $("runBtn");
    btn.textContent = s.running ? "■ Стоп" : "▶ Старт потока";
    btn.classList.toggle("on", s.running);
    if (document.activeElement !== $("period")) $("period").value = s.period_ms;
    $("globalDefect").checked = s.defect;
    $("batchNum").textContent = s.batch ? "пачка #" + s.batch : "пачка —";
    $("batchMs").textContent = s.batch ? Math.round(s.batch_ms) + " мс" : "— мс";
    const bar = $("batchBar");
    bar.style.width = Math.min(100, (s.batch_ms / s.budget_ms) * 100) + "%";
    bar.classList.toggle("over", s.batch_ms > s.budget_ms);
    updateTiles(s);
    updateCamPickerMarks(s);
    if (s.running && $("tab-cam").classList.contains("active") && S.mode === "result") {
      const r = s.last[S.cam];
      if (r && r.ver && S.lastResultVer !== r.ver) { S.lastResultVer = r.ver; renderResult(r); loadResultImage(r.ver); }
    }
  } catch (e) { /* toast already shown */ }
  setTimeout(poll, 500);
}

$("runBtn").addEventListener("click", async () => {
  await api("/api/run?on=" + (S.status && S.status.running ? 0 : 1), { method: "POST" });
});
$("period").addEventListener("change", () => post("/api/global", { period_ms: Number($("period").value) }).catch(() => {}));
$("globalDefect").addEventListener("change", () => post("/api/global", { defect: $("globalDefect").checked }));

/* ---------- camera tab ---------- */

function buildPicker() {
  const p = $("camPicker");
  p.replaceChildren();
  S.cams.forEach((c, i) => {
    const b = el("button", "", "Камера " + i);
    b.id = "pick" + i;
    b.addEventListener("click", () => selectCam(i));
    p.append(b);
  });
  const lc = $("logCam");
  S.cams.forEach((c, i) => { const o = el("option", "", "камера " + i); o.value = i; lc.append(o); });
}

function updateCamPickerMarks(s) {
  s.last.forEach((r, i) => $("pick" + i) && $("pick" + i).classList.toggle("rej", !!r.reject));
}

function openCam(i) { showTab("cam"); selectCam(i); }

function fillSelect(sel, frames, selected) {
  sel.replaceChildren();
  frames.forEach((f) => { const o = el("option", "", f); o.value = f; sel.append(o); });
  if (selected && frames.includes(selected)) sel.value = selected;
}

const PARAMS = [
  ["adaptive_roi_max_motion", 0], ["adaptive_roi_min_coverage", 2],
  ["sensitivity", 2], ["reject_threshold", 2], ["shift_tolerance_x", 0], ["horizontal_shift_tolerance_mm", 2], ["vertical_shift_tolerance_mm", 2], ["learning_search_radius", 0], ["downscale", 2], ["min_blob_area", 0],
];

function selectCam(i) {
  S.exclusionDraft = null;
  cancelLearning();
  S.learningResult = null; S.learnedResult = null;
  updateLearningAction(true);
  S.cam = i;
  S.lastResultVer = null;
  document.querySelectorAll("#camPicker button").forEach((b, k) => b.classList.toggle("active", k === i));
  const c = cur();
  fillSelect($("refSelect"), c.frames, c.reference);
  fillSelect($("testSelect"), c.frames, c.frames[1] || c.frames[0]);
  $("refName").textContent = c.reference === "upload" ? "загруженный файл" : c.reference || "—";
  PARAMS.forEach(([k, d]) => {
    const value = k === "shift_tolerance_x" ? Math.min(c.shift_tolerance_x, c.shift_tolerance_y) : c[k];
    $(k).value = value; $("v_" + k).textContent = fmt(value, d);
  });
  $("learningRobust").checked = c.learning_robust_enabled;
  $("learningShadows").checked = c.learning_shadow_enabled;
  $("adaptiveRoiEnabled").checked = !!c.adaptive_roi_enabled;
  S.roi = c.roi.map((p) => p.slice());
  S.exclusions = (c.exclusions || []).map(points => points.map(p => p.slice()));
  if (typeof updateExclusions === "function") updateExclusions();
  LINES.forEach(({ key }) => {
    S.ln[key] = { d: JSON.parse(JSON.stringify(c[key])), pick: false, skipMode: false, roiSel: null, drag: -1 };
    fillLineUi(key);
  });
  $("result").replaceChildren();
  renderSeamDiagnostic(null);
  renderViewerSummary(null);
  renderCameraBatch();
  updateInspectionHeader();
  setMode("ref");
  reloadRefImage();
  refreshLearned();
}

function reloadRefImage() {
  const cam = S.cam;
  S.img.ref = null;
  if (typeof updateExclusions === "function") updateExclusions();
  const img = new Image();
  img.onload = () => {
    if (S.cam !== cam) return;
    S.img.ref = img;
    if (typeof updateExclusions === "function") updateExclusions();
    draw();
  };
  img.src = `/api/frame?cam=${S.cam}&frame=&t=${Date.now()}`;
}

function loadFrameImage() {
  const name = $("testSelect").value;
  if (!name) return;
  const img = new Image();
  img.onload = () => { S.img.frame = img; if (S.mode === "frame") draw(); };
  img.src = `/api/frame?cam=${S.cam}&frame=${encodeURIComponent(name)}`;
}

function loadResultImage(ver) {
  const camera = S.cam;
  const img = new Image();
  img.onload = () => { if (S.cam !== camera) return; S.img.result = img; if (S.mode === "result") draw(); };
  img.src = `/api/overlay?cam=${S.cam}&v=${ver || Date.now()}`;
}

function setMode(m) {
  if (S.exclusionDraft && m !== "ref") { S.exclusionDraft = null; if (typeof updateExclusions === "function") updateExclusions(); }
  cancelLearning();
  S.mode = m;
  document.querySelectorAll("#viewModes button").forEach((b) => b.classList.toggle("active", b.dataset.mode === m));
  if (m === "frame") loadFrameImage();
  draw();
}
document.querySelectorAll("#viewModes button").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));
$("testSelect").addEventListener("change", () => { if (S.mode === "frame") loadFrameImage(); updateInspectionHeader(); });

/* ----- canvas + ROI editor ----- */

const canvas = $("canvas");
const ctx = canvas.getContext("2d");
const labelTrackingSaves = new Set();

function draw() {
  const label = cur()?.label_outline || [];
  const labelEnabled = label.length >= 3 && cur()?.label_tracking_enabled !== false;
  $("labelTrackingEnabled").checked = labelEnabled;
  $("labelTrackingEnabled").disabled = labelTrackingSaves.has(S.cam);
  $("labelInfo").textContent = label.length >= 3
    ? (labelEnabled
      ? "Компенсация включена. Контур этикетки зелёный; ROI редактируется отдельно."
      : "Компенсация выключена. Контур сохранён (серый); сравнивается обычный ROI.")
    : "Для включения обведите этикетку на эталоне и нажмите «Подтвердить ROI как контур этикетки».";
  const img = S.learning ? S.learningImage : S.mode === "frame" ? S.img.frame : S.mode === "result" ? S.img.result : S.img.ref;
  const base = S.img.ref;
  if (!base) return;
  if (canvas.width !== base.naturalWidth || canvas.height !== base.naturalHeight) {
    canvas.width = base.naturalWidth;
    canvas.height = base.naturalHeight;
  }
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(img || base, 0, 0, canvas.width, canvas.height);
  $("viewInfo").textContent =
    S.learning ? "участок дообучения · " + (S.learningResult?.frame || "") :
    S.mode === "ref" ? "эталон" : S.mode === "frame" ? $("testSelect").value : "тепловая карта и зоны аномалий";
  const k = canvas.width / 900;
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const roiColor = css("--roi") || "#ffd400";
  if (S.roi.length && S.mode !== "result") {
    ctx.lineWidth = 2 * k;
    ctx.strokeStyle = roiColor;
    ctx.fillStyle = "rgba(255, 212, 0, 0.12)";
    ctx.beginPath();
    S.roi.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])));
    if (S.roi.length >= 3) { ctx.closePath(); if (S.mode !== "result") ctx.fill(); }
    ctx.stroke();
    S.roi.forEach((p, i) => {
      ctx.beginPath();
      ctx.arc(p[0], p[1], 5 * k, 0, Math.PI * 2);
      const sel = LINES.find(({ key }) => S.ln[key] && S.ln[key].roiSel && S.ln[key].roiSel.includes(i));
      ctx.fillStyle = i === S.drag ? "#fff" : sel ? sel.color : roiColor;
      ctx.fill();
      ctx.lineWidth = 1.5 * k;
      ctx.strokeStyle = "#000";
      ctx.stroke();
    });
  }
  if (typeof drawExclusions === "function") drawExclusions(k);
  LINES.forEach((ln) => drawLine(ln, k));
  if (S.learning && S.learnBox) {
    const b = S.learnBox; ctx.strokeStyle = "#00ff88"; ctx.lineWidth = 3*k;
    ctx.strokeRect(b.x,b.y,b.width,b.height);
  }
  if (label.length >= 3 && S.mode === "ref" && !S.learning) {
    ctx.save(); ctx.strokeStyle = labelEnabled ? "#36e797" : "#999999"; ctx.lineWidth = 2;
    ctx.beginPath(); label.forEach((p,i) => i ? ctx.lineTo(p[0],p[1]) : ctx.moveTo(p[0],p[1]));
    ctx.closePath(); ctx.stroke(); ctx.restore();
  }
  $("roiInfo").textContent = S.roi.length
    ? `точек: ${S.roi.length}` + (S.roi.length < 3 ? " (нужно минимум 3)" : "")
    : "ROI не задан — проверяется весь кадр";
}

/* Catmull-Rom through the control points, same as the server (a straight line for two points). */
function seamSegment(pts, s) {
  const n = pts.length;
  const p0 = pts[s > 0 ? s - 1 : s], p1 = pts[s], p2 = pts[s + 1], p3 = pts[s + 2 < n ? s + 2 : s + 1];
  const steps = Math.max(8, Math.round(Math.hypot(p2[0] - p1[0], p2[1] - p1[1]) / 4));
  const out = [];
  for (let i = 0; i <= steps; i++) {
    const t = i / steps, t2 = t * t, t3 = t2 * t;
    out.push([0, 1].map((c) => 0.5 * (2 * p1[c] + (p2[c] - p0[c]) * t + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t2 +
      (3 * p1[c] - p0[c] - 3 * p2[c] + p3[c]) * t3)));
  }
  return out;
}

function seamSpline(pts) {
  let out = [];
  for (let s = 0; s + 1 < pts.length; s++) out = out.concat(seamSegment(pts, s).slice(s ? 1 : 0));
  return out;
}

function drawLine(ln, k) {
  const l = S.ln[ln.key];
  if (!l || !l.d.pts || !l.d.pts.length) return;
  const sm = l.d;
  ctx.save();
  ctx.lineWidth = 1.5 * k;
  const skip = sm.skip || [];
  const captured=cur()?.seam_reference;
  if(sm.search_method==="reference"&&S.mode!=="result"&&referenceReady(ln.key)){
    const path=captured.path,normal=captured.normal,tolerance=sm.reference_tolerance||3;
    for(const sign of [-1,0,1]){
      ctx.beginPath();ctx.strokeStyle=sign?ln.color:"#ffffff";
      path.forEach((p,i)=>{const x=p[0]+normal[0]*tolerance*sign,y=p[1]+normal[1]*tolerance*sign;i?ctx.lineTo(x,y):ctx.moveTo(x,y);});ctx.stroke();
    }
  }
  if(sm.search_method==="reference"&&S.mode!=="result"&&!referenceReady(ln.key)){
    ctx.strokeStyle=ln.color;ctx.setLineDash([5*k,4*k]);ctx.beginPath();
    sm.pts.forEach((p,i)=>i?ctx.lineTo(p[0],p[1]):ctx.moveTo(p[0],p[1]));ctx.stroke();ctx.setLineDash([]);
  }
  for (let sg = 0; sm.search_method!=="reference" && sg + 1 < sm.pts.length; sg++) {
    const c = seamSegment(sm.pts, sg);
    const off = skip.includes(sg);
    ctx.strokeStyle = off ? "#ff9800" : sm.enabled ? ln.color : ln.color + "73";
    ctx.setLineDash(off ? [6 * k, 5 * k] : []);
    if (!off) {
      const side = (sign) => c.map((p, i) => {
        const a = c[Math.max(i - 1, 0)], b = c[Math.min(i + 1, c.length - 1)];
        const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
        return [p[0] - ((b[1] - a[1]) / len) * sm.band * sign, p[1] + ((b[0] - a[0]) / len) * sm.band * sign];
      });
      [1, -1].forEach((sign) => {
        ctx.beginPath();
        side(sign).forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])));
        ctx.stroke();
      });
    }
    ctx.beginPath();
    c.forEach((p, i) => (i ? ctx.lineTo(p[0], p[1]) : ctx.moveTo(p[0], p[1])));
    ctx.stroke();
  }
  ctx.setLineDash([]);
  sm.pts.forEach((p, i) => {
    ctx.beginPath(); ctx.arc(p[0], p[1], 5 * k, 0, Math.PI * 2);
    ctx.fillStyle = i === l.drag ? "#fff" : ln.color; ctx.fill();
    ctx.lineWidth = 1.5 * k; ctx.strokeStyle = "#000"; ctx.stroke();
  });
  ctx.restore();
}

function canvasPoint(ev) {
  const r = canvas.getBoundingClientRect();
  return [
    Math.max(0, Math.min(canvas.width - 1, Math.round(((ev.clientX - r.left) * canvas.width) / r.width))),
    Math.max(0, Math.min(canvas.height - 1, Math.round(((ev.clientY - r.top) * canvas.height) / r.height))),
  ];
}

const hitRadius = () => 11 * (canvas.width / 900);

function hitPoint(pt) {
  let best = -1, bd = hitRadius();
  S.roi.forEach((p, i) => { const d = Math.hypot(p[0] - pt[0], p[1] - pt[1]); if (d <= bd) { bd = d; best = i; } });
  return best;
}

function segDist(p, a, b) {
  const dx = b[0] - a[0], dy = b[1] - a[1];
  const l2 = dx * dx + dy * dy;
  let t = l2 ? ((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2 : 0;
  t = Math.max(0, Math.min(1, t));
  return Math.hypot(p[0] - (a[0] + t * dx), p[1] - (a[1] + t * dy));
}

canvas.addEventListener("pointerdown", (ev) => {
  if(Object.values(S.ln).some(line=>line.referencePending))return;
  if (S.learning || S.exclusionDraft || S.exclusionsSaving) return;
  if (ev.button !== 0) return;
  const pt = canvasPoint(ev);
  if (ev.shiftKey) { toast("В детекторе линия задаётся двумя точками"); return; }
  for (const { key } of LINES) {
    const l = S.ln[key], d = l.d;
    if (l.skipMode && d.pts.length >= 2) {            // toggle the nearest line segment
      let best = -1, bd = hitRadius() * 2.5;
      for (let sg = 0; sg + 1 < d.pts.length; sg++) {
        const c = seamSegment(d.pts, sg);
        for (let n = 0; n + 1 < c.length; n++) {
          const dist = segDist(pt, c[n], c[n + 1]);
          if (dist < bd) { bd = dist; best = sg; }
        }
      }
      if (best >= 0) {
        const at = d.skip.indexOf(best);
        if (at >= 0) d.skip.splice(at, 1); else d.skip.push(best);
        fillLineUi(key); draw(); saveLine(key);
      }
      return;
    }
    if (l.roiSel) {                                   // picking existing ROI vertices as the line
      const h = hitPoint(pt);
      if (h >= 0) {
        const at = l.roiSel.indexOf(h);
        if (at >= 0) l.roiSel.splice(at, 1); else l.roiSel.push(h);
        fillLineUi(key);
        draw();
      }
      return;
    }
    if (l.pick) {                                     // the source detector uses exactly two endpoints
      if (d.pts.length >= 2) { toast("Две точки уже заданы — нажми «Готово»"); return; }
      d.pts.push(pt);
      fillLineUi(key);
      draw();
      return;
    }
  }
  for (const { key } of LINES) {                      // drag an existing control point
    const l = S.ln[key];
    const e = l.d.pts.findIndex((p) => Math.hypot(p[0] - pt[0], p[1] - pt[1]) <= hitRadius());
    if (e >= 0 && hitPoint(pt) < 0) { l.drag = e; canvas.setPointerCapture(ev.pointerId); draw(); return; }
  }
  let i = hitPoint(pt);
  if (i < 0) {
    // insert into the nearest edge, otherwise append
    let edge = -1, bd = hitRadius();
    if (S.roi.length >= 3)
      S.roi.forEach((p, k) => {
        const d = segDist(pt, p, S.roi[(k + 1) % S.roi.length]);
        if (d < bd) { bd = d; edge = k; }
      });
    if (edge >= 0) { S.roi.splice(edge + 1, 0, pt); i = edge + 1; }
    else { S.roi.push(pt); i = S.roi.length - 1; }
  }
  S.drag = i;
  canvas.setPointerCapture(ev.pointerId);
  draw();
});

canvas.addEventListener("pointermove", (ev) => {
  if (S.learning || S.exclusionDraft || S.exclusionsSaving) return;
  for (const { key } of LINES) {
    const l = S.ln[key];
    if (l.drag >= 0) { l.d.pts[l.drag] = canvasPoint(ev); draw(); return; }
  }
  if (S.drag < 0) return;
  S.roi[S.drag] = canvasPoint(ev);
  draw();
});

canvas.addEventListener("pointerup", () => {
  if (S.learning || S.exclusionDraft || S.exclusionsSaving) return;
  for (const { key } of LINES) {
    const l = S.ln[key];
    if (l.drag >= 0) { l.drag = -1; draw(); saveLine(key); return; }
  }
  if (S.drag < 0) return;
  S.drag = -1;
  draw();
  saveRoi();
});

canvas.addEventListener("contextmenu", (ev) => {
  if (S.learning || S.exclusionDraft || S.exclusionsSaving) return;
  ev.preventDefault();
  const pt = canvasPoint(ev);
  const hr = hitRadius();
  for (const { key } of LINES) {
    const d = S.ln[key].d;
    const si = d.pts.findIndex((p) => Math.hypot(p[0] - pt[0], p[1] - pt[1]) <= hr);
    if (si >= 0) { d.pts.splice(si, 1); d.skip = []; draw(); fillLineUi(key); saveLine(key); return; }
  }
  const i = hitPoint(pt);
  if (i >= 0) { S.roi.splice(i, 1); draw(); saveRoi(); }
});

$("clearRoi").addEventListener("click", () => { S.roi = []; draw(); saveRoi(); });

$("autoRoi").addEventListener("click", async () => {
  const frame = S.mode === "frame" ? $("testSelect").value : "";
  try {
    const r = await api(`/api/auto_roi?cam=${S.cam}&frame=${encodeURIComponent(frame)}`, { method: "POST" });
    S.roi = r.roi;
    draw();
    await saveRoi();
    toast(`Авто-ROI: ${r.roi.length} точек. Поправь вершины мышью при необходимости.`);
  } catch (e) { /* toast shown */ }
});

async function saveRoi() {
  if (S.roi.length && S.roi.length < 3) return;       // incomplete polygon stays a local draft
  try {
    await post("/api/camera?cam=" + S.cam, { roi: S.roi });
    cur().roi = S.roi.map((p) => p.slice());
    flashSaved();
  } catch (e) { /* toast shown */ }
}

$("confirmLabel").addEventListener("click", async () => {
  if (S.mode !== "ref" || S.learning) { toast("Переключитесь на эталон перед подтверждением контура", true); return; }
  if (S.roi.length < 3) { toast("Обведите границу этикетки минимум тремя точками", true); return; }
  const camera = S.cam, outline = S.roi.map(p => p.slice());
  try {
    await post("/api/camera?cam=" + camera, { label_outline: outline, label_tracking_enabled: true });
    S.cams[camera].label_outline = outline;
    S.cams[camera].label_tracking_enabled = true;
    if (S.cam === camera) draw();
    toast("Контур этикетки подтверждён");
  } catch(e) { toast(e.message, true); }
});
$("clearLabel").addEventListener("click", async () => {
  const camera = S.cam;
  try {
    await post("/api/camera?cam=" + camera, { label_outline: [] });
    S.cams[camera].label_outline = [];
    if (S.cam === camera) draw();
  } catch(e) { toast(e.message, true); }
});
$("labelTrackingEnabled").addEventListener("change", async () => {
  const camera = S.cam, enabled = $("labelTrackingEnabled").checked;
  if (labelTrackingSaves.has(camera)) return;
  if (enabled && (cur()?.label_outline || []).length < 3) {
    draw();
    toast("Сначала обведите этикетку на эталоне и нажмите «Подтвердить ROI как контур этикетки».", true);
    return;
  }
  labelTrackingSaves.add(camera);
  $("labelTrackingEnabled").disabled = true;
  try {
    await post("/api/camera?cam=" + camera, { label_tracking_enabled: enabled });
    S.cams[camera].label_tracking_enabled = enabled;
    flashSaved();
  } catch(e) { toast(e.message, true); }
  finally { labelTrackingSaves.delete(camera); if (S.cam === camera) draw(); }
});

/* ----- geometry zones (spline through control points; one card per zone) ----- */

function buildLineCards() {
  const host = $("lineCards");
  LINES.forEach((ln) => {
    const k = ln.key;
    const card = el("div", "card");
    const parameter=(p)=>{
      const [mn,mx,st,,label]=LINE_PARAM_DEFS[p];
      return `<div class="param" id="${k}_${p}_row"><label>${label} <b id="v_${k}_${p}"></b></label><input type="range" id="${k}_${p}" min="${mn}" max="${mx}" step="${st}"></div>`;
    };
    card.innerHTML = `
      <details class="fold" id="${k}_fold">
      <summary><h2>${ln.title} <span class="muted small">— ${ln.hint}</span></h2></summary>
      <div class="row"><label class="check"><input type="checkbox" id="${k}_enabled"> проверять геометрию</label></div>
      <div class="row"><label>Поиск стыка <select id="${k}_search_method"><option value="mask">Маска + связный путь · два края</option><option value="reference">По эталону · один допуск</option><option value="native">Яркая линия · прежний</option></select></label></div>
      <p class="muted small">Задайте два конца стыка на эталоне. В режиме маски каждый край проверяется отдельно; поперечные сечения контролируют ширину и пропажу сторон.</p>
      ${ln.step ? `<div class="row"><label>Яркая сторона&nbsp;
        <select id="${k}_polarity">
          <option value="0">любая</option>
          <option value="1">справа/снизу от линии</option>
          <option value="-1">слева/сверху от линии</option>
        </select></label></div>` : ""}
      <div class="row">
        <button id="${k}_fromRoi">Взять из точек ROI</button> <button id="${k}_pick">Задать вручную</button>
        <button id="${k}_done" hidden>Готово</button> <button id="${k}_skip" hidden>Исключить участки</button>
        ${ln.step ? `<button id="${k}_preset" title="допуск 1.5 px, контраст 10, окно 60 px">Значения для кромки</button>` : ""}
        <button id="${k}_clear">Сбросить</button>
      </div>
      <div class="muted small" id="${k}_info">&nbsp;</div>
      <div class="row"><button id="${k}_capture">Запомнить стык эталона</button></div>
      <p id="${k}_reference_status" class="muted small" role="status"></p>
      ${parameter("reference_tolerance")}
      <div>
      <p id="${k}_mask_hint" class="muted small" hidden>Маска ищет связный путь для каждой стороны. Эталон определяет положение двух краёв и исходную ширину автоматически. Пропажа одного края не скрывается вторым. Кнопка запоминания для этого режима не нужна.</p>
      ${lineParams(ln).filter(p=>p!=="reference_tolerance").map(parameter).join("")}
      </div>
      </details>`;
    host.append(card);
    // folded by default; the state is remembered per browser
    const fold = card.querySelector("details");
    let open = false;
    try { open = localStorage.getItem("cd_fold_" + k) === "1"; } catch (e) { /* storage unavailable */ }
    fold.open = open;
    fold.addEventListener("toggle", () => {
      try { localStorage.setItem("cd_fold_" + k, fold.open ? "1" : "0"); } catch (e) { /* ignore */ }
    });
  });
}
buildLineCards();

function referenceReady(key) {
  const model=cur()?.seam_reference,d=S.ln[key]?.d;
  return !!(model?.ready&&d&&d.reference_signature===model.signature&&model.endpoints?.length===d.pts.length&&model.endpoints.every((p,i)=>Math.hypot(p[0]-d.pts[i][0],p[1]-d.pts[i][1])<.01));
}

function fillLineUi(key) {
  const ln = LINES.find((x) => x.key === key), l = S.ln[key], d = l.d;
  for(const [field,value] of Object.entries({reference_tolerance:3,mask_low:8,mask_high:24,mask_coverage:.7,mask_max_gap:.2,mask_defect_run:6,mask_width_tolerance:.5})) if(d[field]==null)d[field]=value;
  $(key + "_search_method").value = d.search_method || "native";
  $(key + "_mask_hint").hidden = d.search_method !== "mask";
  for(const field of ["mask_low","mask_high","mask_coverage","mask_max_gap","mask_defect_run","mask_width_tolerance"]) $(key+"_"+field+"_row").hidden=d.search_method!=="mask";
  const isReference=d.search_method==="reference";
  $(key+"_reference_tolerance_row").hidden=!isReference;
  $(key+"_capture").hidden=$(key+"_reference_status").hidden=!isReference;
  for(const field of ["band","tol_mid","tol_edge","edge_frac"])$(key+"_"+field+"_row").hidden=isReference;
  const busy = l.pick || !!l.roiSel;
  $(key + "_enabled").checked = !!d.enabled;
  if (ln.step) {
    $(key + "_polarity").value = String(d.polarity || 0);
    $(key + "_preset").hidden = false;
  }
  $(key + "_pick").hidden = busy;
  $(key + "_fromRoi").hidden = busy;
  $(key + "_done").hidden = !busy;
  $(key + "_skip").hidden = true; // production seam has no excluded polyline segments
  $(key + "_skip").textContent = l.skipMode ? "Закончить исключение" : "Исключить участки";
  lineParams(ln).forEach((p) => { $(`${key}_${p}`).value = d[p]; $(`${key}_${p}`).disabled=!!l.referencePending; $(`v_${key}_${p}`).textContent = lineParamText(p,d[p]); });
  for(const control of ["enabled","search_method","fromRoi","pick","clear"])$(key+"_"+control).disabled=!!l.referencePending;
  const n = d.pts.length;
  $(key + "_info").textContent = l.skipMode ? `кликай по участкам линии, которые не проверять (оранжевые, исключено: ${(d.skip || []).length}); повторный клик возвращает участок` :
    l.roiSel ? `кликай по точкам ROI вдоль линии (выбрано: ${l.roiSel.length}), затем «Готово»` :
    l.pick ? `кликай по линии (точек: ${n}), затем «Готово»` :
    n < 2 ? "Нужно задать две крайние точки линии, двумя концами." :
    "Две точки линии шва. Тяни точку мышью, ПКМ — удалить.";
  if(!busy&&d.pts.some(p=>p[0]<0||p[1]<0||p[0]>=cur().width||p[1]>=cur().height)) $(key+"_info").textContent="Точки стыка за пределами текущего кадра. Задайте два конца линии заново.";
  $(key+"_capture").disabled=busy||!!l.referencePending||d.pts.length!==2||d.pts.some(p=>p[0]<0||p[1]<0||p[0]>=cur().width||p[1]>=cur().height);
  $(key+"_reference_status").textContent=l.referencePending?"Запоминаю стык эталона…":referenceReady(key)?"Стык эталона запомнен. Допуск одинаковый по всей длине.":"Стык эталона не запомнен или изменился. Подтвердите форму на эталоне.";
}

async function saveLine(key) {
  const ln = LINES.find((x) => x.key === key), l = S.ln[key], d = l.d;
  refreshSeamSettingsStatus();
  const camera=S.cam;
  if (d.pts.length === 1 || l.pick || l.roiSel) return;      // an unfinished line is only a draft
  const body = { enabled: d.enabled && d.pts.length === 2, pts: d.pts, search_method: d.search_method || "native" };
  if (ln.step) body.polarity = d.polarity || 0;
  lineParams(ln).forEach((p) => (body[p] = d[p]));
  const saved=JSON.parse(JSON.stringify(d));
  const pointsChanged=JSON.stringify(S.cams[camera][key].pts)!==JSON.stringify(d.pts);
  if(pointsChanged){saved.reference_signature="";d.reference_signature="";S.cams[camera].seam_reference={ready:false};}
  const queueKey=camera+":"+key;
  const operation=(lineSaveQueue.get(queueKey)||Promise.resolve()).catch(()=>{}).then(()=>post("/api/camera?cam="+camera,{[key]:body}));
  lineSaveQueue.set(queueKey,operation);
  try {
    await operation;
    S.cams[camera][key] = saved;
    if(S.cam===camera)flashSaved();
    return true;
  } catch (e) { return false; /* toast shown */ }
  finally { if(lineSaveQueue.get(queueKey)===operation)lineSaveQueue.delete(queueKey); }
}

/* ROI vertices (polygon order) picked by the user, rotated so the run does not start in the middle of the
   chosen stretch: the line starts after the largest gap between the picked indices. */
function orderedRoiPick(sel, n) {
  const a = sel.slice().sort((x, y) => x - y);
  if (a.length < 2) return a;
  let best = 0, bestGap = -1;
  a.forEach((v, i) => {
    const gap = (a[(i + 1) % a.length] - v + n) % n || n;
    if (gap > bestGap) { bestGap = gap; best = (i + 1) % a.length; }
  });
  return a.slice(best).concat(a.slice(0, best));
}

/* Only one zone can be in an editing mode (picking / excluding) at a time. */
function stopOtherModes(key) {
  LINES.forEach(({ key: o }) => {
    if (o === key) return;
    const l = S.ln[o];
    if (l.pick || l.skipMode || l.roiSel) { l.pick = false; l.skipMode = false; l.roiSel = null; fillLineUi(o); saveLine(o); }
  });
}

let lineTimer = {};
const lineSaveQueue=new Map();
LINES.forEach((ln) => {
  const k = ln.key;
  const L = () => S.ln[k];
  const redraw = () => { fillLineUi(k); draw(); };
  $(k+"_capture").addEventListener("click",async()=>{
    const camera=S.cam,line=L(),points=JSON.stringify(line.d.pts);
    if(line.referencePending)return;
    clearTimeout(lineTimer[k]);
    line.referencePending=true;setMode("ref");redraw();
    try{
      if(!await saveLine(k))return;
      if(JSON.stringify(line.d.pts)!==points){toast("Точки изменились. Нажмите «Запомнить» ещё раз.",true);return;}
      const response=await post("/api/seam_reference?cam="+camera,{});
      S.cams[camera][k]=response.seam;S.cams[camera].seam_reference=response.reference;
      if(S.cam===camera&&S.ln[k]===line&&JSON.stringify(line.d.pts)===points){
        line.d=JSON.parse(JSON.stringify(response.seam));S.lastResultVer=null;S.img.result=null;
        $("result").replaceChildren();renderViewerSummary(null);renderSeamDiagnostic(null);redraw();
      }
      toast("Стык эталона запомнен");
    }catch(e){toast(reasonText({reason:e.message})||e.message,true);}
    finally{line.referencePending=false;if(S.cam===camera&&S.ln[k]===line)redraw();}
  });
  $(k + "_search_method").addEventListener("change", () => { L().d.search_method=$(k+"_search_method").value;redraw();saveLine(k); });

  $(k + "_enabled").addEventListener("change", () => {
    if ($(k + "_enabled").checked && L().d.pts.length < 2) {
      $(k + "_enabled").checked = false;
      toast("Сначала задай линию на кадре", true);
      return;
    }
    L().d.enabled = $(k + "_enabled").checked;
    draw();
    saveLine(k);
  });
  if (ln.step) {
    $(k + "_polarity").addEventListener("change", () => { L().d.polarity = Number($(k + "_polarity").value); saveLine(k); });
    /* Working values for a label edge measured as a step: ~1.5 px tolerance, a 60 px local reference, tolerant to gaps. */
    $(k + "_preset").addEventListener("click", () => {
      Object.assign(L().d, { band: 10, tol_mid: 1.5, tol_edge: 1.5, edge_frac: 0, smooth_win: 60, min_contrast: 10, max_gap: 0.25, polarity: L().d.polarity || -1 });
      redraw(); saveLine(k);
      toast("Значения для кромки применены");
    });
  }
  $(k + "_skip").addEventListener("click", () => {
    stopOtherModes(k);
    L().skipMode = !L().skipMode;
    redraw();
  });
  $(k + "_fromRoi").addEventListener("click", () => {
    if (S.roi.length < 3) { toast("Сначала задай ROI", true); return; }
    stopOtherModes(k);
    Object.assign(L(), { roiSel: [], pick: false, skipMode: false });
    redraw();
  });
  $(k + "_pick").addEventListener("click", () => {
    stopOtherModes(k);
    Object.assign(L().d, { pts: [], skip: [], enabled: false });
    Object.assign(L(), { pick: true, roiSel: null, skipMode: false });
    redraw();
  });
  $(k + "_done").addEventListener("click", () => {
    const l = L();
    if (l.roiSel) {
      if (l.roiSel.length !== 2) { toast("Выбери ровно две крайние точки ROI", true); return; }
      l.d.pts = orderedRoiPick(l.roiSel, S.roi.length).map((i) => S.roi[i].slice());
      l.d.skip = [];
      l.roiSel = null;
    } else {
      if (l.d.pts.length < 2) { toast("Нужно минимум 2 точки", true); return; }
      l.pick = false;
    }
    l.d.enabled = true;
    redraw(); saveLine(k);
  });
  $(k + "_clear").addEventListener("click", () => {
    Object.assign(L().d, { pts: [], skip: [], enabled: false });
    Object.assign(L(), { pick: false, roiSel: null, skipMode: false });
    redraw(); saveLine(k);
  });
  lineParams(ln).forEach((p) => {
    $(`${k}_${p}`).addEventListener("input", () => {
      L().d[p] = Number($(`${k}_${p}`).value);
      if(p==="mask_low"&&L().d.mask_low>L().d.mask_high)L().d.mask_high=L().d.mask_low;
      if(p==="mask_high"&&L().d.mask_high<L().d.mask_low)L().d.mask_low=L().d.mask_high;
      if(p==="mask_low"||p==="mask_high")fillLineUi(k);
      $(`v_${k}_${p}`).textContent = lineParamText(p,L().d[p]);
      refreshSeamSettingsStatus();
      draw();
      clearTimeout(lineTimer[k]);
      lineTimer[k] = setTimeout(() => saveLine(k), 300);
    });
  });
});

/* ----- parameters (auto-saved) ----- */

$("resetDetector").addEventListener("click", async () => {
  try {
    await post("/api/camera?cam=" + S.cam, { learning_robust_enabled: true, learning_shadow_enabled: true, learning_search_radius: 24, sensitivity: 0.5, reject_threshold: 60,
      shift_tolerance_x: 3, shift_tolerance_y: 3, max_alignment_shift_x: 12, max_alignment_shift_y: 12, downscale: 1, min_blob_area: 20, seam: { enabled: false } });
    await reloadState();
    toast("Настройки eefefce4 восстановлены; эталон и ROI сохранены");
  } catch (e) { /* toast shown */ }
});

let paramTimer;
PARAMS.forEach(([k, d]) => {
  $(k).addEventListener("input", () => {
    $("v_" + k).textContent = fmt(Number($(k).value), d);
    clearTimeout(paramTimer);
    paramTimer = setTimeout(saveParams, 300);
  });
});

async function saveParams() {
  const body = {};
  PARAMS.forEach(([k]) => (body[k] = Number($(k).value)));
  body.shift_tolerance_y = body.shift_tolerance_x;
  try {
    await post("/api/camera?cam=" + S.cam, body);
    Object.assign(cur(), body);
    flashSaved();
  } catch (e) { /* toast shown */ }
}

function flashSaved() {
  $("saveState").textContent = "сохранено";
  clearTimeout(flashSaved.t);
  flashSaved.t = setTimeout(() => ($("saveState").innerHTML = "&nbsp;"), 1200);
}

$("adaptiveRoiEnabled").addEventListener("change",async()=>{
  const cam=S.cam,value=$("adaptiveRoiEnabled").checked;
  try {
    await post("/api/camera?cam="+cam,{adaptive_roi_enabled:value});
    if(S.cam===cam){cur().adaptive_roi_enabled=value;S.learningResult=null;updateLearningAction(true);}
    flashSaved();toast("Режим сохранён. Проверьте кадр повторно");
  }catch(e){if(S.cam===cam)$("adaptiveRoiEnabled").checked=!!cur().adaptive_roi_enabled;}
});

/* ----- reference ----- */

$("setRef").addEventListener("click", async () => {
  const frame = $("refSelect").value;
  try {
    await api(`/api/reference?cam=${S.cam}&frame=${encodeURIComponent(frame)}`, { method: "POST" });
    await reloadState();
    toast("Эталон камеры " + S.cam + ": " + frame);
  } catch (e) { /* toast shown */ }
});

$("refFile").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  ev.target.value = "";
  if (!f) return;
  const fd = new FormData();
  fd.append("file", f);
  try {
    await api("/api/reference_upload?cam=" + S.cam, { method: "POST", body: fd });
    await reloadState();
    toast("Эталон загружен из файла");
  } catch (e) { /* toast shown */ }
});

async function reloadState() {
  const st = await api("/api/state");
  S.cams = st.cameras_info;
  selectCam(S.cam);
}

/* ----- testing ----- */

function updateInspectionHeader() {
  const pending = S.inspectionPending;
  const camera = cur();
  $("inspectionCamera").textContent = "Камера " + S.cam;
  $("runOne").disabled = !!pending || !camera?.frames?.length || !$("testSelect").value;
  $("runAll").disabled = !!pending || !camera?.frames?.some(frame => frame !== camera.reference);
  ["runTestFile", "runAllTestFiles", "testFile"].forEach(id => { $(id).disabled = !!pending; });
  const summary = $("inspectionSummary");
  summary.className = "inspection-summary muted";
  if (pending) {
    summary.textContent = `Камера ${pending.cam}: ${pending.all ? "проверяем все кадры…" : "проверяем кадр…"}`;
    return;
  }
  const batch = S.cameraBatches[S.cam];
  if (!batch) { summary.textContent = "Все кадры ещё не проверялись"; return; }
  if (batch.error) {
    summary.textContent = "Проверка всех кадров не завершена";
    summary.className = "inspection-summary warn";
    return;
  }
  const rows = batch.rows;
  if (!rows.length) { summary.textContent = "Нет кадров для проверки"; return; }
  const rejected = rows.filter(r => r.verdict === "REJECT").length;
  const incomplete = rows.filter(r => r.verdict === "NOT_INSPECTED").length;
  const errors = rows.filter(r => !["OK", "REJECT", "NOT_INSPECTED"].includes(r.verdict)).length;
  summary.textContent = `Дефектных кадров: ${rejected} из ${rows.length}` +
    (incomplete ? ` · Без контроля: ${incomplete}` : "") + (errors ? ` · Ошибок: ${errors}` : "");
  summary.className = "inspection-summary " + (errors || incomplete ? "warn" : rejected ? "bad" : "ok");
}

function beginInspection(all) {
  if(Object.values(S.ln).some(line=>line.referencePending))return null;
  if (S.inspectionPending || !cur()?.frames?.length || (all ? $("runAll").disabled : $("runOne").disabled)) return null;
  const pending = { cam: S.cam, all };
  S.inspectionPending = pending;
  updateInspectionHeader();
  return pending;
}

function finishInspection(pending) {
  if (S.inspectionPending === pending) S.inspectionPending = null;
  updateInspectionHeader();
}

function renderCameraBatch() {
  const box = $("allResults"), batch = S.cameraBatches[S.cam];
  box.replaceChildren();
  if (!batch) return;
  if (batch.error) { box.append(el("div", "muted small", "Проверка всех кадров не завершена. Повторите запуск.")); return; }
  const rows = batch.rows, table = el("table"), head = el("tr");
  ["Кадр", "Score, %", "Вердикт", "мс"].forEach(text => head.append(el("th", "", text)));
  table.append(head);
  rows.forEach(r => {
    const tr = el("tr"), verdict = el("td");
    verdict.append(verdictBadge(r.verdict));
    tr.append(el("td", "mono", r.frame), el("td", "num", fmt(r.score, 3)), verdict, el("td", "num", fmt(r.ms, 1)));
    tr.style.cursor = "pointer";
    tr.addEventListener("click", () => { $("testSelect").value = r.frame; setMode("frame"); updateInspectionHeader(); });
    table.append(tr);
  });
  const worst = rows.reduce((max, r) => Math.max(max, r.score || 0), 0);
  box.append(el("div", "muted small", `${rows.length} кадров, максимальный score ${fmt(worst, 3)}%`), table);
}

// Plot the native fit's actual worst edge at each cross-section, with that
// inspection's tolerances. Missing/excluded samples are never joined by a curve.
function refreshSeamSettingsStatus(r=S.learningResult) {
  const used=r?.seam?.used_params,current=S.ln.seam?.d;
  $("seamUsedSettings").hidden=!used;
  $("seamUsedSettings").textContent=used
    ?(used.search_method==="reference"?`В этой проверке: эталонный допуск ±${fmt(used.reference_tolerance,1)} px`:`В этой проверке: допуск ${fmt(used.tol_mid,1)}/${fmt(used.tol_edge,1)} px`)
      +(used.search_method==="mask"?`, пороги маски ${fmt(used.mask_low,0)}/${fmt(used.mask_high,0)}, длина дефекта ${fmt(used.mask_defect_run,0)} px, допуск ширины ${fmt(used.mask_width_tolerance*100,0)}%.`:".")
    :"";
  const fields=["enabled","pts","search_method","mode",...(used?.search_method==="reference"?["reference_tolerance","reference_signature"]:["band","tol_mid","tol_edge","edge_frac",...(used?.search_method==="mask"?["mask_low","mask_high","mask_coverage","mask_max_gap","mask_defect_run","mask_width_tolerance"]:[])])];
  const changed=!!(used&&current&&r.cam===S.cam&&fields.some(k=>JSON.stringify(used[k])!==JSON.stringify(current[k])));
  $("seamSettingsChanged").hidden=!changed;
  $("seamSettingsChanged").textContent=changed?"Настройки изменены после проверки. Изображение и графики показывают предыдущий результат — проверьте кадр заново.":"";
}

function renderSeamDiagnostic(r) {
  const status = $("seamDebugStatus"), plot = $("seamPlot"), legend = $("seamLegend");
  plot.innerHTML = "";
  plot.hidden = legend.hidden = true;
  $("seamMaskLink").hidden=$("seamMaskCaption").hidden=true;
  $("seamMaskImage").onload=null;
  $("seamSideStatus").hidden=true;$("seamSideStatus").textContent="";
  refreshSeamSettingsStatus(r);
  const z = r?.seam;
  if (!z) {
    status.textContent = r
      ? "Кадр " + r.frame + ": измерение стыка не выполнялось. Проверьте включение стыка, две точки линии и чувствительность."
      : "Проверьте кадр, чтобы увидеть измерения. В меню «Стык этикетки» задайте два конца линии и включите проверку геометрии.";
    return;
  }
  status.textContent = "Кадр " + r.frame + " · " + (z.control_complete===false ? "Контроль стыка не выполнен" : !z.found ? "Стык не найден" : z.reject ? "Стык: БРАК" : "Стык в допуске")
    + " · найдено " + fmt(z.coverage * 100, 1) + "% линии"
    + (z.found ? " · макс. отклонение " + fmt(z.max_dev, 2) + " px на " + Math.round(z.worst_t * 100) + "% длины · " + fmt(z.ratio, 2) + "× допуска" : "");
  if(z.search_method==="mask")status.textContent+=" · маска + связный путь · пропуск "+fmt((z.gap||0)*100,1)+"%";
  if(z.paired_mask){
    $("seamSideStatus").hidden=false;
    $("seamSideStatus").textContent=(z.sides||[]).map((s,i)=>`Край ${i+1}: найдено ${fmt(s.coverage*100,1)}%, пропажа до ${fmt(s.longest_missing_px,0)} px`).join(" · ")
      +` · Изменение ширины до ${fmt(z.longest_width_run_px||0,0)} px подряд; порог ${fmt(z.defect_run_px,0)} px, допуск ширины ${fmt(z.width_tolerance*100,0)}%`
      +((z.longest_width_run_px||0)>0&&(z.longest_width_run_px||0)<z.defect_run_px?" — короче порога длины, само по себе не даёт брак":"");
  }
  if(z.search_method==="reference")status.textContent+=" · по эталону · допуск ±"+fmt(z.reference_tolerance,1)+" px по всей длине";
  $("seamMaskCaption").textContent=z.search_method==="reference"
    ?"Сверху исходная полоса, снизу найденные границы. Синие линии — форма эталона ± общий допуск, цветные точки — измеренный стык. Нажмите, чтобы открыть крупно."
    :z.paired_mask?"Сверху исходная полоса, снизу маска. Голубые сечения соединяют ожидаемые края, зелёные точки — найденные стороны, пурпурные — пропавший край. Оранжевый — изменение ширины короче порога длины, красный — отклонение или достаточно длинный дефект ширины. Нажмите, чтобы открыть крупно."
    :"Сверху исходная полоса, снизу маска границ. Цветные точки — только измеренные участки пути. Нажмите на маску, чтобы открыть крупно.";
  if(z.mask_reason)status.textContent+=" · "+reasonText({reason:z.mask_reason,seam:z});
  if(z.mask_preview&&r.ver){
    const url=`/api/seam_mask?cam=${r.cam??S.cam}&version=${r.ver}`;
    $("seamMaskLink").href=url;
    $("seamMaskImage").onload=()=>{if($("seamMaskImage").getAttribute("src")===url)$("seamMaskLink").hidden=$("seamMaskCaption").hidden=false;};
    $("seamMaskImage").onerror=()=>{$("seamMaskLink").hidden=$("seamMaskCaption").hidden=true;};
    $("seamMaskImage").src=url;
  }
  const samples = (z.samples || []).filter(s => Number.isFinite(s.t) && Number.isFinite(s.tolerance));
  if (samples.length < 2) {
    status.textContent += " · Нет данных для графика: не удалось построить опорную линию.";
    return;
  }
  const range = Math.max(1, ...samples.map(s => Math.max(s.tolerance * 1.35, Math.abs(s.deviation || 0) * 1.15)));
  const x = t => 64 + 816 * t, y = d => 94 - d / range * 70;
  const point = (s, sign) => x(s.t).toFixed(2) + "," + y(sign * s.tolerance).toFixed(2);
  let svg = '<title>Отклонение стыка: зелёные точки в допуске, красные за допуском</title>';
  svg += '<text x="8" y="14" fill="currentColor" font-size="12">Δ, px</text>';
  for (const value of [-range, 0, range]) {
    svg += `<line x1="64" x2="880" y1="${y(value)}" y2="${y(value)}" stroke="currentColor" opacity=".25"/><text x="57" y="${y(value)+4}" text-anchor="end" fill="currentColor" font-size="12">${fmt(value, 1)}</text>`;
  }
  for (const t of [0, .25, .5, .75, 1]) svg += `<text x="${x(t)}" y="188" text-anchor="middle" fill="currentColor" font-size="12">${t*100}%</text>`;
  svg += '<text x="472" y="207" text-anchor="middle" fill="currentColor" font-size="12">Положение от первого конца линии ко второму</text>';
  svg += `<polygon points="${samples.map(s=>point(s,1)).concat([...samples].reverse().map(s=>point(s,-1))).join(" ")}" fill="var(--warn)" opacity=".12"/>`;
  for (const sign of [1,-1]) svg += `<polyline points="${samples.map(s=>point(s,sign)).join(" ")}" stroke="var(--warn)" fill="none" stroke-width="1.5"/>`;
  for (const s of z.paired_mask?[]:samples) {
    const measured = s.state === 0 && Number.isFinite(s.deviation);
    const color = measured ? Math.abs(s.deviation) > s.tolerance ? "var(--bad)" : "var(--ok)" : s.state === 1 ? "#c77516" : s.state === 3 ? "#ce49c5" : "var(--muted)";
    const cy = measured ? y(s.deviation) : 172;
    svg += `<circle cx="${x(s.t)}" cy="${cy}" r="1.7" fill="${color}"><title>${fmt(s.t*100,1)}%: ${measured ? fmt(s.deviation,2)+" px; допуск ±"+fmt(s.tolerance,2)+" px" : s.state===1 ? "зона исключения" : s.state===3 ? "линия не найдена" : "конец не оценивается"}</title></circle>`;
  }
  if(z.paired_mask)for(const [sideIndex,side] of (z.sides||[]).entries())for(const [i,s] of (side.samples||[]).entries()){
    const measured=s.state===0&&Number.isFinite(s.deviation),tol=samples[i]?.tolerance||1,widthBad=z.samples?.[i]?.width_bad,widthReject=z.samples?.[i]?.width_reject;
    const color=measured?(Math.abs(s.deviation)>tol||widthReject?"var(--bad)":widthBad?"#c77516":sideIndex?"#15979b":"var(--ok)"):s.state===3?"#ce49c5":s.state===1?"#c77516":"var(--muted)";
    svg+=`<circle cx="${x(s.t)}" cy="${measured?y(s.deviation):164+sideIndex*10}" r="${sideIndex?1.8:1.3}" fill="${color}"><title>Край ${sideIndex+1}, ${fmt(s.t*100,1)}%: ${measured?fmt(s.deviation,2)+" px"+(widthBad?"; ширина вне допуска":""):s.state===3?"край пропал":s.state===4?"нет опоры на эталоне":s.state===1?"исключено":"конец не оценивается"}</title></circle>`;
  }
  if(z.paired_mask){
    const widths=(z.samples||[]).filter(s=>Number.isFinite(s.width)&&s.reference_width>0),limit=z.width_tolerance||.5;
    const wrange=Math.max(.75,limit*1.35,...widths.map(s=>Math.abs(s.width/s.reference_width-1)*1.15)),wy=d=>286-d/wrange*22;
    svg+='<g id="seamWidthPlot"><text x="64" y="231" fill="currentColor" font-size="13">Изменение ширины относительно эталона, %</text>';
    svg+=`<text x="64" y="248" fill="currentColor" font-size="11">Допуск ±${fmt(limit*100,0)}%; длина дефекта от ${fmt(z.defect_run_px,0)} px; оранжевое — короче порога</text>`;
    for(const v of [-wrange,0,wrange])svg+=`<line x1="64" x2="880" y1="${wy(v)}" y2="${wy(v)}" stroke="currentColor" opacity=".2"/><text x="57" y="${wy(v)+4}" fill="currentColor" text-anchor="end" font-size="11">${fmt(v*100,0)}%</text>`;
    for(const sign of [-1,1])svg+=`<line x1="64" x2="880" y1="${wy(sign*limit)}" y2="${wy(sign*limit)}" stroke="var(--warn)" stroke-width="1.5"/>`;
    for(const s of widths){const d=s.width/s.reference_width-1,color=s.width_reject?"var(--bad)":s.width_bad?"#c77516":"var(--ok)";
      svg+=`<circle cx="${x(s.t)}" cy="${wy(d)}" r="1.8" fill="${color}"><title>${fmt(s.t*100,1)}% длины: ширина ${fmt(s.width,2)} px, эталон ${fmt(s.reference_width,2)} px; изменение ${fmt(d*100,1)}%${s.width_bad?(s.width_reject?" — достигнут порог длины дефекта":" — короче порога длины дефекта"):""}</title></circle>`;
    }
    svg+='</g>';
  }
  if (z.found) svg += `<line x1="${x(z.worst_t)}" x2="${x(z.worst_t)}" y1="20" y2="165" stroke="currentColor" stroke-dasharray="4 4" opacity=".6"/>`;
  plot.innerHTML = `<svg viewBox="0 0 900 ${z.paired_mask?325:210}" role="img" aria-label="Отклонение двух краёв и изменение ширины стыка">` + svg + '</svg>';
  plot.hidden = legend.hidden = false;
}

$("showSeamSettings").addEventListener("click", () => {
  $("seam_fold").open = true;
  $("seam_fold").scrollIntoView({ block: "nearest", behavior: "smooth" });
  $("seam_enabled").focus({ preventScroll: true });
});

function renderViewerSummary(r) {
  const box = $("viewerSummary");
  box.replaceChildren();
  box.hidden = !r;
  if (!r) return;
  box.append(verdictBadge(r.verdict), el("strong", "", fmt(r.score, 3) + "%"), el("span", "mono", r.frame || ""));
  if (r.reject || r.control_complete === false || r.verdict === "NO_REFERENCE") {
    box.append(el("span", "viewer-reason", reasonText(r)));
  }
}

function renderResult(r) {
  renderViewerSummary(r);
  cancelLearning();
  S.learningResult = r;
  renderSeamDiagnostic(r);
  updateLearningAction(true);
  const box = $("result");
  box.replaceChildren();
  const top = el("div");
  top.append(el("span", "score", fmt(r.score, 3) + "%"), document.createTextNode(" "), verdictBadge(r.verdict));
  box.append(top);
  const kv = el("div", "kv");
  const add = (k, v) => kv.append(el("span", "", k), el("span", "", v));
  add("Кадр", r.frame + (r.defect_injected ? " + тестовый дефект" : ""));
  add("Порог брака", fmt(r.threshold, 2) + "%");
  add("Чувствительность", fmt(r.sensitivity, 2));
  if(r.adaptive_roi?.enabled) {
    add("Покрытие ROI",fmt(r.adaptive_roi.coverage*100,1)+"%");
    add("Опорные точки",`${r.adaptive_roi.inliers} / ${r.adaptive_roi.features}`);
    add("Ошибка сопоставления",fmt(r.adaptive_roi.residual_px,2)+" px");
    add("Адаптация ROI",fmt(r.adaptive_roi.ms,1)+" мс");
  }
  add("До образцов нормы", fmt(r.raw_anomaly_score * 100, 3) + "%");
  add("Совпавших образцов", String(r.learned_normal_matches_count || 0));
  if (r.shift_mm) add("Сдвиг, мм", r.shift_mm.map(v => fmt(v, 2)).join(", "));
  if (r.shift_limit_mm) add("Допуск, мм", r.shift_limit_mm.map(v => fmt(v, 2)).join(", "));
  if (r.metric_scale_source) add("Масштаб мм", r.metric_scale_source === "calibrated" ? "калиброванный" : "приблизительный");
  if (r.upper_corner_area > 0) add("Верхние углы ROI", "чувствительность " + fmt(r.upper_corner_sensitivity, 2) + ", вклад " + fmt(r.upper_corner_extra_score * 100, 3) + "%");
  add("Сдвиг, px", r.shift ? `${fmt(r.shift[0])}, ${fmt(r.shift[1])}` : "—");
  [["seam", "Стык"], ["edge", "Кромка"]].forEach(([key, name]) => {
    const z = r[key];
    if (!z) return;
    add(name, !z.found ? "не найден" : `${fmt(z.max_dev, 2)} px на ${Math.round(z.worst_t * 100)}% (${fmt(z.ratio, 2)}× допуска)` + (z.reject ? " — БРАК" : "") + (z.noise != null ? `; шум ${fmt(z.noise, 2)} px (допуск лучше ≥ ${fmt(Math.max(z.noise * 1.5, 0.5), 1)})` : ""));
  });
  add("Время", fmt(r.ms, 1) + " мс");
  box.append(kv);
  renderLearningDiagnostics(box, r);
  const zones = r.zones || [];
  if (zones.length) {
    const z = el("div", "muted small", "Зоны (x, y, w, h): " + zones.slice(0, 8).map((q) => q.join(",")).join(" · ") + (zones.length > 8 ? " …" : ""));
    z.style.marginTop = "6px";
    box.append(z);
  }
  const why = r.reject || r.control_complete === false ? reasonText(r) : r.verdict === "NO_REFERENCE" ? reasonText(r) : `Score ${fmt(r.score, 3)}% не превышает порог ${fmt(r.threshold, 2)}% — норма.`;
  box.append(el("div", "reasonbox " + (r.reject ? "bad" : r.verdict === "OK" ? "ok" : "warn"), why));
}

function afterInspect(r) {
  S.lastResultVer = r.ver;
  renderResult(r);
  loadResultImage(r.ver);
  setMode("result");
  refreshLogIfVisible();
}

async function inspectSelectedFrame() {
  const pending = beginInspection(false);
  if (!pending) return;
  try {
    const q = `cam=${pending.cam}&frame=${encodeURIComponent($("testSelect").value)}&defect=${$("testDefect").checked ? 1 : 0}`;
    const result = await api("/api/inspect?" + q, { method: "POST" });
    if (S.cam === pending.cam) afterInspect(result);
  } catch (e) { /* toast shown */ }
  finally { finishInspection(pending); }
}
$("runOne").addEventListener("click", inspectSelectedFrame);

$("testFile").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  ev.target.value = "";
  if (!f) return;
  const fd = new FormData();
  fd.append("file", f);
  try {
    afterInspect(await api(`/api/inspect_upload?cam=${S.cam}&defect=${$("testDefect").checked ? 1 : 0}`, { method: "POST", body: fd }));
  } catch (e) { /* toast shown */ }
});

async function inspectCameraFrames() {
  const pending = beginInspection(true);
  if (!pending) return;
  const box = $("allResults");
  box.replaceChildren(el("div", "muted small", "проверка…"));
  try {
    const rows = await api(`/api/inspect_all?cam=${pending.cam}&defect=${$("testDefect").checked ? 1 : 0}`, { method: "POST" });
    if (!Array.isArray(rows)) { toast("Некорректный список результатов проверки", true); throw new Error("invalid inspection results"); }
    S.cameraBatches[pending.cam] = { rows };
    if (S.cam === pending.cam) renderCameraBatch();
    refreshLogIfVisible();
  } catch (e) {
    S.cameraBatches[pending.cam] = { error: true };
    if (S.cam === pending.cam) renderCameraBatch();
  } finally { finishInspection(pending); }
}
$("runAll").addEventListener("click", inspectCameraFrames);

window.addEventListener("keydown", event => {
  if (S.tab !== "cam" || event.defaultPrevented || event.repeat || event.isComposing ||
      event.altKey || !(event.ctrlKey || event.metaKey) || event.key !== "Enter") return;
  if (event.target?.isContentEditable || event.target?.closest?.("input, textarea, select, [contenteditable], [role='textbox']")) return;
  event.preventDefault();
  if (event.shiftKey) void inspectCameraFrames();
  else void inspectSelectedFrame();
});

/* ---------- log ---------- */

async function refreshLog() {
  let rows;
  try { rows = await api("/api/log?limit=500"); } catch (e) { return; }
  const fv = $("logFilter").value, fc = $("logCam").value;
  rows = rows.filter((r) => (!fv || r.verdict === fv) && (fc === "" || String(r.cam) === fc)).reverse();
  const body = document.querySelector("#logTable tbody");
  body.replaceChildren();
  rows.slice(0, 300).forEach((r) => {
    const tr = el("tr");
    const d = new Date(r.ts);
    const v = el("td");
    v.append(verdictBadge(r.verdict));
    const link = el("td");
    if (r.overlay) {
      const a = el("a", "", "картинка");
      a.href = "/api/reject_image?name=" + encodeURIComponent(r.overlay);
      a.target = "_blank";
      link.append(a);
    }
    tr.append(
      el("td", "", d.toLocaleTimeString() + "." + String(d.getMilliseconds()).padStart(3, "0")),
      el("td", "num", String(r.cam)),
      el("td", "mono", r.frame),
      el("td", "", r.source + (r.defect_injected ? " +дефект" : "")),
      v,
      el("td", "num", fmt(r.score, 3)),
      el("td", "num", fmt(r.threshold, 2)),
      el("td", "", r.reason || ""),
      el("td", "num", String((r.zones || []).length)),
      el("td", "num", fmt(r.ms, 1)),
      link
    );
    body.append(tr);
  });
  if (!rows.length) {
    const tr = el("tr");
    const td = el("td", "muted", "Записей пока нет");
    td.colSpan = 11;
    tr.append(td);
    body.append(tr);
  }
}

function refreshLogIfVisible() { if ($("tab-log").classList.contains("active")) refreshLog(); }
$("logRefresh").addEventListener("click", refreshLog);
$("logFilter").addEventListener("change", refreshLog);
$("logCam").addEventListener("change", refreshLog);
setInterval(() => { if ($("logAuto").checked) refreshLogIfVisible(); }, 1500);

/* ---------- start ---------- */

(async function init() {
  const st = await api("/api/state");
  if (st.inspection_engine !== "cpp-detector-eefefce4") {
    $("engineInfo").textContent = "Запущен старый executable. Нужна пересборка и перезапуск приложения.";
    throw new Error("Native eefefce4 engine is not running");
  }
  $("engineInfo").textContent = "Основной детектор eefefce4 · мм, верхние углы, дообучение v2 · " + st.engine_source_commit.slice(0, 8) + " · отдельный профиль · кромка не влияет на вердикт";
  S.cams = st.cameras_info;
  buildGrid();
  buildPicker();
  selectCam(0);
  poll();
})().catch(() => {});

window.addEventListener("resize", draw);

$("undoRoi").addEventListener("click", () => { S.roi.pop(); draw(); saveRoi(); });

// deep links: #cam3 opens camera 3 setup, #log opens the log
window.addEventListener("load", () => {
  setTimeout(() => {
    const m = /^#cam(\d)$/.exec(location.hash);
    if (m && S.cams[Number(m[1])]) openCam(Number(m[1]));
    else if (location.hash === "#log") showTab("log");
    else if (location.hash === "#unwrap") showTab("unwrap");
  }, 300);
});

/* ---------- extra test images (frame/test-pr-ph) ---------- */

async function loadTestFiles() {
  let files = [];
  try { files = await api("/api/testfiles"); } catch (e) { return; }
  S.testFiles = files;
  fillSelect($("refTestSelect"), files, files.find((f) => /good/.test(f)));
  fillSelect($("testFileSelect"), files, files.find((f) => /bad/.test(f)));
}

$("setRefTest").addEventListener("click", async () => {
  const file = $("refTestSelect").value;
  if (!file) return;
  try {
    await api(`/api/reference_testfile?cam=${S.cam}&file=${encodeURIComponent(file)}`, { method: "POST" });
    await reloadState();
    toast("Эталон камеры " + S.cam + ": " + file);
  } catch (e) { /* toast shown */ }
});

$("runTestFile").addEventListener("click", async () => {
  const file = $("testFileSelect").value;
  if (!file) return;
  try {
    afterInspect(await api(`/api/inspect_testfile?cam=${S.cam}&file=${encodeURIComponent(file)}&defect=${$("testDefect").checked ? 1 : 0}`, { method: "POST" }));
  } catch (e) { /* toast shown */ }
});

$("runAllTestFiles").addEventListener("click", async () => {
  const box = $("allResults");
  box.replaceChildren(el("div", "muted small", "проверка…"));
  const t = el("table");
  const hd = el("tr");
  ["Файл", "Score, %", "Вердикт", "мс"].forEach((h) => hd.append(el("th", "", h)));
  t.append(hd);
  let last = null;
  for (const f of S.testFiles || []) {
    try {
      const r = await api(`/api/inspect_testfile?cam=${S.cam}&file=${encodeURIComponent(f)}&defect=0`, { method: "POST" });
      last = r;
      const tr = el("tr");
      const v = el("td");
      v.append(verdictBadge(r.verdict));
      tr.append(el("td", "mono", f), el("td", "num", fmt(r.score, 3)), v, el("td", "num", fmt(r.ms, 1)));
      tr.style.cursor = "pointer";
      tr.addEventListener("click", async () => { $("testFileSelect").value = f; $("runTestFile").click(); });
      t.append(tr);
    } catch (e) {
      const tr = el("tr");
      const td = el("td", "muted", f + ": ошибка (размер не совпадает с эталоном?)");
      td.colSpan = 4;
      tr.append(td);
      t.append(tr);
    }
  }
  box.replaceChildren(t);
  if (last) afterInspect(last);
  refreshLogIfVisible();
});

loadTestFiles();
