"use strict";

function drawExclusions(k) {
  ctx.save();
  ctx.lineWidth = 2 * k;
  ctx.strokeStyle = "#ff50ff";
  ctx.fillStyle = "rgba(255, 80, 255, 0.18)";
  const polygon = (points, closed) => {
    if (!points.length) return;
    ctx.beginPath();
    points.forEach((p, i) => i ? ctx.lineTo(...p) : ctx.moveTo(...p));
    if (closed) { ctx.closePath(); ctx.fill(); }
    ctx.stroke();
  };
  S.exclusions.forEach(points => polygon(points, true));
  if (S.exclusionDraft) {
    ctx.setLineDash([6 * k, 4 * k]);
    polygon(S.exclusionDraft, S.exclusionDraft.length >= 3);
    ctx.setLineDash([]);
    for (const p of S.exclusionDraft) {
      ctx.beginPath(); ctx.arc(...p, 4 * k, 0, Math.PI * 2); ctx.fill(); ctx.stroke();
    }
  }
  ctx.restore();
}

function updateExclusions() {
  const editing = S.exclusionDraft !== null, busy = !!S.exclusionsSaving;
  for (const id of ["autoRoi", "undoRoi", "clearRoi"]) $(id).disabled = busy || editing;
  $("addExclusion").disabled = busy || editing || S.exclusions.length >= 32 || !cur()?.width || !S.img.ref;
  $("saveExclusion").disabled = busy || !editing || S.exclusionDraft.length < 3;
  $("undoExclusion").disabled = busy || !editing || !S.exclusionDraft.length;
  $("cancelExclusion").disabled = busy || !editing;
  $("clearExclusions").disabled = busy || editing || !S.exclusions.length;
  $("exclusionInfo").textContent = busy ? "Сохранение…" : editing
    ? `Отметьте границу точками и нажмите «Сохранить зону». Точек: ${S.exclusionDraft.length}.`
    : `Зон исключения: ${S.exclusions.length}. Внутри них инспекция отключена.`;
  const list = $("exclusionList"); list.replaceChildren();
  S.exclusions.forEach((points, index) => {
    const row = el("div", "row"), remove = el("button", "", "Удалить");
    remove.disabled = busy || editing;
    remove.onclick = () => saveExclusions(S.exclusions.filter((_, i) => i !== index));
    row.append(el("span", "small", `Зона ${index + 1} · ${points.length} точек`), remove);
    list.append(row);
  });
}

function cancelExclusion() {
  S.exclusionDraft = null;
  updateExclusions(); draw();
}

async function saveExclusions(polygons) {
  if (S.exclusionsSaving) return;
  const cam = S.cam, camera = cur(), saved = polygons.map(points => points.map(p => p.slice()));
  S.exclusionsSaving = true; updateExclusions();
  try {
    await post("/api/camera?cam=" + cam, {exclusions: saved});
    camera.exclusions = saved;
    if (S.cam === cam) {
      S.exclusions = saved; S.exclusionDraft = null;
      S.learningResult = null; S.learnedResult = null;
      cancelLearning(); updateLearningAction(true);
      $("result").replaceChildren();
    }
    toast("Зоны сохранены. Повторите проверку кадра");
  } catch (e) { /* API displays the error; keep the draft for correction/retry. */ }
  finally { S.exclusionsSaving = false; updateExclusions(); draw(); }
}

$("addExclusion").onclick = () => {
  if (S.exclusionsSaving || S.exclusionDraft || S.exclusions.length >= 32 || !S.img.ref) return;
  cancelLearning(); setMode("ref");
  S.learningResult = null; updateLearningAction(true);
  for (const {key} of LINES) {
    Object.assign(S.ln[key], {pick: false, skipMode: false, roiSel: null, drag: -1});
    fillLineUi(key);
  }
  S.drag = -1; S.exclusionDraft = []; updateExclusions(); draw();
};
$("saveExclusion").onclick = () => {
  if (!S.exclusionDraft || S.exclusionDraft.length < 3) return;
  return saveExclusions([...S.exclusions, S.exclusionDraft]);
};
$("undoExclusion").onclick = () => {
  if (S.exclusionsSaving || !S.exclusionDraft) return;
  S.exclusionDraft.pop(); updateExclusions(); draw();
};
$("cancelExclusion").onclick = () => { if (!S.exclusionsSaving) cancelExclusion(); };
$("clearExclusions").onclick = () => saveExclusions([]);
canvas.addEventListener("pointerdown", ev => {
  if (!S.exclusionDraft || S.exclusionsSaving || ev.button !== 0) return;
  if (S.exclusionDraft.length >= 128) { toast("В зоне может быть не более 128 точек", true); return; }
  const point = canvasPoint(ev), last = S.exclusionDraft.at(-1);
  if (!last || last[0] !== point[0] || last[1] !== point[1]) S.exclusionDraft.push(point);
  updateExclusions(); draw();
});
canvas.addEventListener("contextmenu", ev => {
  if (!S.exclusionDraft) return;
  ev.preventDefault();
  if (!S.exclusionsSaving) { S.exclusionDraft.pop(); updateExclusions(); draw(); }
});
window.addEventListener("keydown", ev => {
  if (ev.key === "Escape" && S.exclusionDraft && !S.exclusionsSaving) cancelExclusion();
});
