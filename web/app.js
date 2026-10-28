"use strict";

const $ = (id) => document.getElementById(id);

/* Production-compatible seam only; the local edge algorithm must not affect test verdicts. */
const LINES = [
  { key: "seam", title: "Стык этикетки", hint: "тонкая яркая линия", color: "#00e5ff", step: false },
];
// name: [min, max, step, decimals, label]
const LINE_PARAM_DEFS = {
  band: [5, 200, 1, 0, "Полоса поиска (±), px"],
  tol_mid: [0.1, 50, 0.1, 1, "Допуск в середине, px"],
  tol_edge: [0.1, 50, 0.1, 1, "Допуск на краях линии, px"],
  edge_frac: [0, 0.5, 0.01, 2, "Длина краевой части (доля)"],
  smooth_win: [20, 200, 5, 0, "Окно сравнения, px"],
  min_contrast: [2, 40, 1, 0, "Мин. контраст, ур. серого"],
  max_gap: [0.05, 1, 0.05, 2, "Макс. разрыв линии (доля)"],
};
const lineParams = (ln) => (ln.step ? ["band", "tol_mid", "tol_edge", "edge_frac", "smooth_win", "min_contrast", "max_gap"] : ["band", "tol_mid", "tol_edge", "edge_frac", "min_contrast"]);
const S = {
  cams: [],            // static info per camera (frames, params, roi, ...)
  status: null,
  cam: 0,
  mode: "ref",         // ref | frame | result
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

// Serialize snapshots per camera, including changes still waiting for the debounce.
const cameraWrites = new Map(), scheduledSaves = new Map();
function cancelScheduledSave(cam, key) {
  const id = `${cam}:${key}`, job = scheduledSaves.get(id);
  if (job) { clearTimeout(job.timer); scheduledSaves.delete(id); }
}
function scheduleSave(cam, key, save) {
  cancelScheduledSave(cam, key);
  const id = `${cam}:${key}`;
  scheduledSaves.set(id, { cam, save, timer: setTimeout(() => {
    scheduledSaves.delete(id); save();
  }, 300) });
}
function queueCameraSave(cam, body) {
  const snapshot = JSON.parse(JSON.stringify(body));
  const next = (cameraWrites.get(cam) || Promise.resolve()).catch(() => {}).then(() =>
    post("/api/camera?cam=" + cam, snapshot));
  cameraWrites.set(cam, next);
  next.catch(() => {}); // keep the failure available to flushSettings
  return next;
}
async function flushSettings(cam) {
  for (const [id, job] of [...scheduledSaves]) {
    if (job.cam !== cam) continue;
    clearTimeout(job.timer); scheduledSaves.delete(id); job.save();
  }
  await cameraWrites.get(cam);
}

function verdictBadge(v) {
  const map = { OK: ["ok", "OK"], REJECT: ["bad", "БРАК"], NO_REFERENCE: ["warn", "нет эталона"] };
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
    case "score_over_threshold":
      return `Оценка ${fmt(r.score, 3)}% достигла порога ${fmt(r.threshold, 2)}% — найдено зон: ${(r.zones || []).length}.`;
    case "shift_exceeded": {
      const shift = r.shift_raw_mm || r.shift_raw, limit = r.shift_limit_mm || r.shift_limit;
      const units = r.shift_raw_mm ? "мм" : "px исходного кадра";
      return `Оценённое смещение (${fmt(shift?.[0], 3)}, ${fmt(shift?.[1], 3)}) ${units}; допуски (${fmt(limit?.[0], 3)}, ${fmt(limit?.[1], 3)}) ${units}. Достоверность ${fmt(r.shift_response, 3)}.`;
    }
    case "seam_deviation":
      return lineText("Стык", r.seam);
    case "seam_not_found":
      return "Проверка стыка включена, но линия не найдена или видна недостаточно хорошо. Проверь две точки линии, полосу поиска и контраст.";
    case "edge_deviation":
      return lineText("Кромка", r.edge);
    case "size_mismatch":
      return "Размер кадра не совпадает с размером эталона этой камеры — сравнение невозможно.";
    case "no_reference":
      return "Для камеры нет эталона.";
    default:
      return r.reject ? r.reason : "";
  }
}

/* ---------- tabs ---------- */

document.querySelectorAll("#tabs button").forEach((b) =>
  b.addEventListener("click", () => showTab(b.dataset.tab))
);

function showTab(name) {
  document.querySelectorAll("#tabs button").forEach((b) => b.classList.toggle("active", b.dataset.tab === name));
  document.querySelectorAll(".tab").forEach((t) => t.classList.toggle("active", t.id === "tab-" + name));
  if (name === "log") refreshLog();
  if (name === "cam") draw();
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
  try {
    const on = !(S.status && S.status.running);
    if (on) await Promise.all(S.cams.map((_, cam) => flushSettings(cam)));
    await api("/api/run?on=" + (on ? 1 : 0), { method: "POST" });
  } catch (e) { /* toast shown */ }
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
  ["sensitivity", 2], ["reject_threshold", 2], ["horizontal_shift_tolerance_mm", 2], ["vertical_shift_tolerance_mm", 2], ["downscale", 2], ["min_blob_area", 0],
];
function refreshMetricUi() {
  $("metricStatus").textContent = cur().metric_scale_source === "calibrated"
    ? "Допуски заданы в мм. Значения учитывают разрешение кадра и Downscale."
    : "Миллиметры пока приблизительные. Масштаб уточним опытным путём.";
}

function selectCam(i) {
  S.cam = i;
  S.lastResultVer = null;
  document.querySelectorAll("#camPicker button").forEach((b, k) => b.classList.toggle("active", k === i));
  const c = cur();
  fillSelect($("refSelect"), c.frames, c.reference);
  fillSelect($("testSelect"), c.frames, c.frames[1] || c.frames[0]);
  $("refName").textContent = c.reference === "upload" ? "загруженный файл" : c.reference || "—";
  PARAMS.forEach(([k, d]) => { $(k).value = c[k] ?? 0; $("v_" + k).textContent = c[k] == null ? "—" : fmt(c[k], d); });
  refreshMetricUi();
  S.roi = c.roi.map((p) => p.slice());
  LINES.forEach(({ key }) => {
    S.ln[key] = { d: JSON.parse(JSON.stringify(c[key])), pick: false, skipMode: false, roiSel: null, drag: -1 };
    fillLineUi(key);
  });
  $("result").replaceChildren();
  $("allResults").replaceChildren();
  setMode("ref");
  reloadRefImage();
}

function reloadRefImage() {
  const cam = S.cam;
  const img = new Image();
  S.img.ref = null;
  img.onload = () => { if (S.cam === cam) { S.img.ref = img; draw(); } };
  img.src = `/api/frame?cam=${cam}&frame=&t=${Date.now()}`;
}

function loadFrameImage() {
  const cam = S.cam;
  const name = $("testSelect").value;
  if (!name) return;
  const img = new Image();
  S.img.frame = null;
  img.onload = () => { if (S.cam === cam && $("testSelect").value === name) { S.img.frame = img; if (S.mode === "frame") draw(); } };
  img.src = `/api/frame?cam=${cam}&frame=${encodeURIComponent(name)}`;
}

function loadResultImage(ver) {
  const cam = S.cam;
  const img = new Image();
  S.img.result = null;
  img.onload = () => { if (S.cam === cam && S.lastResultVer === ver) { S.img.result = img; if (S.mode === "result") draw(); } };
  img.src = `/api/overlay?cam=${cam}&v=${ver || Date.now()}`;
}

function setMode(m) {
  S.mode = m;
  document.querySelectorAll("#viewModes button").forEach((b) => b.classList.toggle("active", b.dataset.mode === m));
  if (m === "frame") loadFrameImage();
  draw();
}
document.querySelectorAll("#viewModes button").forEach((b) => b.addEventListener("click", () => setMode(b.dataset.mode)));
$("testSelect").addEventListener("change", () => { if (S.mode === "frame") loadFrameImage(); });

/* ----- canvas + ROI editor ----- */

const canvas = $("canvas");
const ctx = canvas.getContext("2d");

function draw() {
  const img = S.mode === "frame" ? S.img.frame : S.mode === "result" ? S.img.result : S.img.ref;
  const base = S.img.ref;
  if (!base) return;
  if (canvas.width !== base.naturalWidth || canvas.height !== base.naturalHeight) {
    canvas.width = base.naturalWidth;
    canvas.height = base.naturalHeight;
  }
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(img || base, 0, 0, canvas.width, canvas.height);
  $("viewInfo").textContent =
    S.mode === "ref" ? "эталон" : S.mode === "frame" ? $("testSelect").value : "тепловая карта и зоны аномалий";
  const k = canvas.width / 900;
  const css = (n) => getComputedStyle(document.documentElement).getPropertyValue(n).trim();
  const roiColor = css("--roi") || "#ffd400";
  if (S.roi.length) {
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
  LINES.forEach((ln) => drawLine(ln, k));
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
  for (let sg = 0; sg + 1 < sm.pts.length; sg++) {
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
  if (ev.button !== 0) return;
  const pt = canvasPoint(ev);
  if (ev.shiftKey) { toast("В position-x-y линия задаётся двумя точками"); return; }
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
  for (const { key } of LINES) {
    const l = S.ln[key];
    if (l.drag >= 0) { l.d.pts[l.drag] = canvasPoint(ev); draw(); return; }
  }
  if (S.drag < 0) return;
  S.roi[S.drag] = canvasPoint(ev);
  draw();
});

canvas.addEventListener("pointerup", () => {
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
  ev.preventDefault();
  const pt = canvasPoint(ev);
  const hr = hitRadius();
  for (const { key } of LINES) {
    const d = S.ln[key].d;
    const si = d.pts.findIndex((p) => Math.hypot(p[0] - pt[0], p[1] - pt[1]) <= hr);
    if (si >= 0) { d.pts.splice(si, 1); d.skip = []; if (d.pts.length < 2) d.enabled = false; draw(); fillLineUi(key); saveLine(key); return; }
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

async function saveRoi(cam = S.cam, roi = S.roi.map((p) => p.slice())) {
  if (roi.length && roi.length < 3) return;       // incomplete polygon stays a local draft
  S.cams[cam].roi = roi;
  try {
    await queueCameraSave(cam, { roi });
    if (cam === S.cam) flashSaved();
  } catch (e) { /* toast shown */ }
}

/* ----- geometry zones (spline through control points; one card per zone) ----- */

function buildLineCards() {
  const host = $("lineCards");
  LINES.forEach((ln) => {
    const k = ln.key;
    const card = el("div", "card");
    card.innerHTML = `
      <h2>${ln.title} <span class="muted small">— ${ln.hint}</span></h2>
      <div class="row"><label class="check"><input type="checkbox" id="${k}_enabled"> проверять геометрию</label></div>
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
      ${lineParams(ln).map((p) => {
        const [mn, mx, st, , label] = LINE_PARAM_DEFS[p];
        return `<div class="param"><label>${label} <b id="v_${k}_${p}"></b></label>
          <input type="range" id="${k}_${p}" min="${mn}" max="${mx}" step="${st}"></div>`;
      }).join("")}`;
    host.append(card);
  });
}
buildLineCards();

function fillLineUi(key) {
  const ln = LINES.find((x) => x.key === key), l = S.ln[key], d = l.d;
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
  lineParams(ln).forEach((p) => { $(`${key}_${p}`).value = d[p]; $(`v_${key}_${p}`).textContent = fmt(d[p], LINE_PARAM_DEFS[p][3]); });
  const n = d.pts.length;
  $(key + "_info").textContent = l.skipMode ? `кликай по участкам линии, которые не проверять (оранжевые, исключено: ${(d.skip || []).length}); повторный клик возвращает участок` :
    l.roiSel ? `кликай по точкам ROI вдоль линии (выбрано: ${l.roiSel.length}), затем «Готово»` :
    l.pick ? `кликай по линии (точек: ${n}), затем «Готово»` :
    n < 2 ? "Нужно задать две крайние точки линии, как в position-x-y." :
    "Две точки линии position-x-y. Тяни точку мышью, ПКМ — удалить.";
}

async function saveLine(key, cam = S.cam, draft = null) {
  const ln = LINES.find((x) => x.key === key), l = S.ln[key];
  if (!draft && (l.pick || l.roiSel)) return;      // unfinished manual selection stays a draft
  const d = draft || JSON.parse(JSON.stringify(l.d));
  cancelScheduledSave(cam, key);
  const body = { enabled: d.enabled && d.pts.length === 2, pts: d.pts };
  if (ln.step) body.polarity = d.polarity || 0;
  lineParams(ln).forEach((p) => (body[p] = d[p]));
  S.cams[cam][key] = JSON.parse(JSON.stringify(d));
  try {
    await queueCameraSave(cam, { [key]: body });
    if (cam === S.cam) flashSaved();
  } catch (e) { /* toast shown */ }
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

const measurableLine = (d) => d.pts.length === 2 && Math.hypot(d.pts[1][0] - d.pts[0][0], d.pts[1][1] - d.pts[0][1]) >= 50;
LINES.forEach((ln) => {
  const k = ln.key;
  const L = () => S.ln[k];
  const redraw = () => { fillLineUi(k); draw(); };

  $(k + "_enabled").addEventListener("change", () => {
    if ($(k + "_enabled").checked && !measurableLine(L().d)) {
      $(k + "_enabled").checked = false;
      toast("Задай две точки стыка на расстоянии не меньше 50 px", true);
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
    } else if (l.d.pts.length < 2) { toast("Нужно минимум 2 точки", true); return; }
    if (!measurableLine(l.d)) { toast("Точки стыка должны быть на расстоянии не меньше 50 px", true); return; }
    l.pick = false;
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
      $(`v_${k}_${p}`).textContent = fmt(L().d[p], LINE_PARAM_DEFS[p][3]);
      draw();
      const cam = S.cam, draft = JSON.parse(JSON.stringify(L().d));
      S.cams[cam][k] = draft;
      if (!L().pick && !L().roiSel) scheduleSave(cam, k, () => saveLine(k, cam, draft));
    });
  });
});

/* ----- parameters (auto-saved) ----- */

$("resetDetector").addEventListener("click", async () => {
  try {
    const cam = S.cam;
    await flushSettings(cam);
    await queueCameraSave(cam, { sensitivity: 0.5, reject_threshold: 25,
      shift_tolerance_x: 3, shift_tolerance_y: 3, downscale: 1, min_blob_area: 20, seam: { enabled: false } });
    await reloadState();
    toast("Настройки position-x-y восстановлены; эталон и ROI сохранены");
  } catch (e) { /* toast shown */ }
});

function parameterBody() {
  const body = {};
  PARAMS.forEach(([k]) => { if (!$(k).disabled) body[k] = Number($(k).value); });
  return body;
}
PARAMS.forEach(([k, d]) => {
  $(k).addEventListener("input", () => {
    $("v_" + k).textContent = fmt(Number($(k).value), d);
    const cam = S.cam, body = parameterBody();
    Object.assign(S.cams[cam], body);
    scheduleSave(cam, "params", () => saveParams(cam, body));
  });
});

async function saveParams(cam = S.cam, body = parameterBody()) {
  cancelScheduledSave(cam, "params");
  Object.assign(S.cams[cam], body);
  try {
    await queueCameraSave(cam, body);
    if (cam === S.cam) { refreshMetricUi(); flashSaved(); }
  } catch (e) { /* toast shown */ }
}

function flashSaved() {
  $("saveState").textContent = "сохранено";
  clearTimeout(flashSaved.t);
  flashSaved.t = setTimeout(() => ($("saveState").innerHTML = "&nbsp;"), 1200);
}

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
  await Promise.all(S.cams.map((_, cam) => flushSettings(cam)));
  const st = await api("/api/state");
  S.cams = st.cameras_info;
  selectCam(S.cam);
}

/* ----- testing ----- */

function renderResult(r) {
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
  const shift = r.shift_mm || r.shift;
  add(r.shift_mm ? "Смещение, мм (гориз., верт.)" : "Смещение, px", shift ? `${fmt(shift[0], 3)}, ${fmt(shift[1], 3)}` : "—");
  if (r.effective_shift_tolerance_mm) add("Допуски при разрешении анализа, мм", r.effective_shift_tolerance_mm.map((v) => fmt(v, 3)).join(", "));
  [["seam", "Стык"], ["edge", "Кромка"]].forEach(([key, name]) => {
    const z = r[key];
    if (!z) return;
    add(name, !z.found ? "не найден" : `${fmt(z.max_dev, 2)} px на ${Math.round(z.worst_t * 100)}% (${fmt(z.ratio, 2)}× допуска)` + (z.reject ? " — БРАК" : "") + (z.noise != null ? `; шум ${fmt(z.noise, 2)} px (допуск лучше ≥ ${fmt(Math.max(z.noise * 1.5, 0.5), 1)})` : ""));
  });
  add("Время", fmt(r.ms, 1) + " мс");
  box.append(kv);
  const zones = r.zones || [];
  if (zones.length) {
    const z = el("div", "muted small", "Зоны (x, y, w, h): " + zones.slice(0, 8).map((q) => q.join(",")).join(" · ") + (zones.length > 8 ? " …" : ""));
    z.style.marginTop = "6px";
    box.append(z);
  }
  const why = r.reject ? reasonText(r) : r.verdict === "NO_REFERENCE" ? reasonText(r) : `Score ${fmt(r.score, 3)}% не превышает порог ${fmt(r.threshold, 2)}% — норма.`;
  box.append(el("div", "reasonbox " + (r.reject ? "bad" : r.verdict === "OK" ? "ok" : "warn"), why));
}

function afterInspect(r) {
  if (r.cam !== S.cam) { refreshLogIfVisible(); return; }
  S.lastResultVer = r.ver;
  renderResult(r);
  loadResultImage(r.ver);
  setMode("result");
  refreshLogIfVisible();
}

$("runOne").addEventListener("click", async () => {
  try {
    const cam = S.cam;
    const q = `cam=${S.cam}&frame=${encodeURIComponent($("testSelect").value)}&defect=${$("testDefect").checked ? 1 : 0}`;
    await flushSettings(cam);
    afterInspect(await api("/api/inspect?" + q, { method: "POST" }));
  } catch (e) { /* toast shown */ }
});

$("testFile").addEventListener("change", async (ev) => {
  const f = ev.target.files[0];
  ev.target.value = "";
  if (!f) return;
  const fd = new FormData();
  fd.append("file", f);
  try {
    const cam = S.cam, defect = $("testDefect").checked ? 1 : 0;
    await flushSettings(cam);
    afterInspect(await api(`/api/inspect_upload?cam=${cam}&defect=${defect}`, { method: "POST", body: fd }));
  } catch (e) { /* toast shown */ }
});

$("runAll").addEventListener("click", async () => {
  const cam = S.cam, defect = $("testDefect").checked ? 1 : 0;
  const box = $("allResults");
  box.replaceChildren(el("div", "muted small", "проверка…"));
  try {
    await flushSettings(cam);
    const rows = await api(`/api/inspect_all?cam=${cam}&defect=${defect}`, { method: "POST" });
    if (S.cam !== cam) return;
    const t = el("table");
    const hd = el("tr");
    ["Кадр", "Score, %", "Вердикт", "мс"].forEach((h) => hd.append(el("th", "", h)));
    t.append(hd);
    rows.forEach((r) => {
      const tr = el("tr");
      const v = el("td");
      v.append(verdictBadge(r.verdict));
      tr.append(el("td", "mono", r.frame), el("td", "num", fmt(r.score, 3)), v, el("td", "num", fmt(r.ms, 1)));
      tr.style.cursor = "pointer";
      tr.addEventListener("click", () => { $("testSelect").value = r.frame; setMode("frame"); });
      t.append(tr);
    });
    const worst = rows.reduce((m, r) => Math.max(m, r.score), 0);
    box.replaceChildren(el("div", "muted small", `${rows.length} кадров, максимальный score ${fmt(worst, 3)}%`), t);
    refreshLogIfVisible();
  } catch (e) { box.replaceChildren(); }
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
  if (st.inspection_engine !== "position-x-y") {
    $("engineInfo").textContent = "Запущен старый executable. Нужна пересборка и перезапуск приложения.";
    throw new Error("Native position-x-y engine is not running");
  }
  $("engineInfo").textContent = "Ядро position-x-y · " + st.engine_source_commit.slice(0, 8) + " · отдельный профиль · кромка не влияет на вердикт";
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
    const cam = S.cam, defect = $("testDefect").checked ? 1 : 0;
    await flushSettings(cam);
    afterInspect(await api(`/api/inspect_testfile?cam=${cam}&file=${encodeURIComponent(file)}&defect=${defect}`, { method: "POST" }));
  } catch (e) { /* toast shown */ }
});

$("runAllTestFiles").addEventListener("click", async () => {
  const cam = S.cam;
  try { await flushSettings(cam); } catch (e) { return; }
  const box = $("allResults");
  box.replaceChildren(el("div", "muted small", "проверка…"));
  const t = el("table");
  const hd = el("tr");
  ["Файл", "Score, %", "Вердикт", "мс"].forEach((h) => hd.append(el("th", "", h)));
  t.append(hd);
  let last = null;
  for (const f of S.testFiles || []) {
    if (S.cam !== cam) return;
    try {
      const r = await api(`/api/inspect_testfile?cam=${cam}&file=${encodeURIComponent(f)}&defect=0`, { method: "POST" });
      if (S.cam !== cam) return;
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
  if (S.cam !== cam) return;
  box.replaceChildren(t);
  if (last) afterInspect(last);
  refreshLogIfVisible();
});

loadTestFiles();
