"use strict";

const $ = (id) => document.getElementById(id);
const S = {
  cams: [],            // static info per camera (frames, params, roi, ...)
  status: null,
  cam: 0,
  mode: "ref",         // ref | frame | result
  roi: [],             // working copy of the ROI of the current camera
  img: { ref: null, frame: null },
  lastVer: [],         // last overlay version shown per camera
  drag: -1,
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
  const map = { OK: ["ok", "OK"], REJECT: ["bad", "БРАК"], NO_REFERENCE: ["warn", "нет эталона"] };
  const [cls, text] = map[v] || ["warn", v || "—"];
  return el("span", "badge " + cls, text);
}

function reasonText(r) {
  switch (r.reason) {
    case "score_over_threshold":
      return `Score ${fmt(r.score, 3)}% выше порога ${fmt(r.threshold, 2)}% — найдено зон: ${(r.zones || []).length}.`;
    case "shift_exceeded":
      return `Кадр сдвинут относительно эталона на (${fmt(r.shift && r.shift[0])}, ${fmt(r.shift && r.shift[1])}) px — больше допустимого. Увеличь допуск на сдвиг или проверь камеру.`;
    case "size_mismatch":
      return "Размер кадра не совпадает с размером эталона этой камеры — сравнение невозможно.";
    case "no_reference":
      return "Для камеры нет эталона (или downscale изменился после выбора эталона).";
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
  ["sensitivity", 2], ["reject_threshold", 2], ["shift_tolerance", 0], ["downscale", 2], ["min_blob_area", 0],
];

function selectCam(i) {
  S.cam = i;
  S.lastResultVer = null;
  document.querySelectorAll("#camPicker button").forEach((b, k) => b.classList.toggle("active", k === i));
  const c = cur();
  fillSelect($("refSelect"), c.frames, c.reference);
  fillSelect($("testSelect"), c.frames, c.frames[1] || c.frames[0]);
  $("refName").textContent = c.reference === "upload" ? "загруженный файл" : c.reference || "—";
  PARAMS.forEach(([k, d]) => { $(k).value = c[k]; $("v_" + k).textContent = fmt(c[k], d); });
  S.roi = c.roi.map((p) => p.slice());
  $("result").replaceChildren();
  $("allResults").replaceChildren();
  setMode("ref");
  reloadRefImage();
}

function reloadRefImage() {
  const img = new Image();
  img.onload = () => { S.img.ref = img; draw(); };
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
  const img = new Image();
  img.onload = () => { S.img.result = img; if (S.mode === "result") draw(); };
  img.src = `/api/overlay?cam=${S.cam}&v=${ver || Date.now()}`;
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
      ctx.fillStyle = i === S.drag ? "#fff" : roiColor;
      ctx.fill();
      ctx.lineWidth = 1.5 * k;
      ctx.strokeStyle = "#000";
      ctx.stroke();
    });
  }
  $("roiInfo").textContent = S.roi.length
    ? `точек: ${S.roi.length}` + (S.roi.length < 3 ? " (нужно минимум 3)" : "")
    : "ROI не задан — проверяется весь кадр";
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
  if (S.drag < 0) return;
  S.roi[S.drag] = canvasPoint(ev);
  draw();
});

canvas.addEventListener("pointerup", () => {
  if (S.drag < 0) return;
  S.drag = -1;
  draw();
  saveRoi();
});

canvas.addEventListener("contextmenu", (ev) => {
  ev.preventDefault();
  const i = hitPoint(canvasPoint(ev));
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

/* ----- parameters (auto-saved) ----- */

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
  add("Сдвиг, px", r.shift ? `${fmt(r.shift[0])}, ${fmt(r.shift[1])}` : "—");
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
  S.lastResultVer = r.ver;
  renderResult(r);
  loadResultImage(r.ver);
  setMode("result");
  refreshLogIfVisible();
}

$("runOne").addEventListener("click", async () => {
  try {
    const q = `cam=${S.cam}&frame=${encodeURIComponent($("testSelect").value)}&defect=${$("testDefect").checked ? 1 : 0}`;
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
    afterInspect(await api(`/api/inspect_upload?cam=${S.cam}&defect=${$("testDefect").checked ? 1 : 0}`, { method: "POST", body: fd }));
  } catch (e) { /* toast shown */ }
});

$("runAll").addEventListener("click", async () => {
  const box = $("allResults");
  box.replaceChildren(el("div", "muted small", "проверка…"));
  try {
    const rows = await api(`/api/inspect_all?cam=${S.cam}&defect=${$("testDefect").checked ? 1 : 0}`, { method: "POST" });
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
