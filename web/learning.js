"use strict";
function cancelLearning() {
  S.learning = false; S.learnBox = null;
  if (typeof draw === "function") draw();
}
function canLearnFrame(r) {
  return r && r.cam === S.cam && r.ver && !r.defect_injected && r.verdict !== "NO_REFERENCE" && r.learning_bbox;
}
function updateLearningAction(resetInfo = false) {
  const r = S.learningResult, saved = r && S.learnedResult === r;
  $("learnFrame").disabled = !!S.learningSaving || !canLearnFrame(r) || saved;
  $("learnFrame").textContent = S.learningSaving ? "Добавление…" : saved ? "Добавлено в анализ" : "Дообучить";
  if (!S.learningSaving && !saved && (resetInfo || !canLearnFrame(r))) $("learnInfo").textContent = !r
    ? "Сначала проверьте кадр."
    : r.defect_injected ? "Тестовый дефект нельзя сохранять как норму."
    : !r.learning_bbox ? "На карте нет аномалий для дообучения."
    : "Участок будет выбран автоматически по карте аномалий с запасом 10 px.";
}
async function refreshLearned() {
  const cam = S.cam;
  try {
    const data = await api("/api/learning?cam=" + cam);
    if (S.cam !== cam) return;
    const box = $("learnedCases"); box.replaceChildren();
    for (const item of data.cases) {
      const row = el("div", "row");
      const img = el("img"); img.width = 80; img.alt = "Образец нормы";
      img.src = "/api/learning/image?cam=" + cam + "&id=" + encodeURIComponent(item.id);
      const del = el("button", "", "Удалить");
      del.onclick = async () => {
        try { await api("/api/learning?cam=" + cam + "&id=" + encodeURIComponent(item.id), {method:"DELETE"}); await refreshLearned(); }
        catch(e) { /* api displays error */ }
      };
      const b=item.bbox;
      row.append(img,el("span","small",(item.active ? "Активен" : "Другой эталон") + " · " + [b.x,b.y,b.width,b.height].join(", ")),del);
      box.append(row);
    }
    if(!data.cases.length) box.append(el("p","muted small","Образцов пока нет"));
  } catch(e) { /* api displays error */ }
}

$("learnRefresh").onclick = refreshLearned;
$("learnFrame").onclick = async () => {
  const r = S.learningResult, cam = S.cam;
  if (S.learningSaving || !canLearnFrame(r) || S.learnedResult === r) return;
  S.learningSaving = true;
  updateLearningAction();
  try {
    // Pin the inspected frame before preview/save; the server also checks its version.
    await api("/api/run?on=0", {method:"POST"});
    const image = await new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = () => reject(new Error("Кадр изменился. Проверьте его повторно."));
      img.src = "/api/inspected?cam=" + cam + "&version=" + r.ver;
    });
    if (S.cam !== cam || S.learningResult !== r) throw new Error("Кадр изменился. Проверьте его повторно.");
    setMode("result");
    S.learning = true; S.learningImage = image; S.learnBox = r.learning_bbox;
    draw();
    await post("/api/learning?cam=" + cam, {version:r.ver});
    if (S.cam === cam && S.learningResult === r) {
      S.learnedResult = r;
      $("learnInfo").textContent = "Выделенный участок сохранён как норма. Повторите проверку кадра.";
      await refreshLearned();
    }
    toast("Образец сохранён. Повторите проверку кадра для новой оценки");
  } catch(e) {
    if (S.cam === cam && S.learningResult === r) {
      cancelLearning();
      $("learnInfo").textContent = e.message;
    }
    toast(e.message, true);
  } finally {
    S.learningSaving = false;
    updateLearningAction();
  }
};

for (const [id,key] of [["learningRobust","learning_robust_enabled"],["learningShadows","learning_shadow_enabled"]]) {
  $(id).addEventListener("change",async()=>{
    const cam=S.cam,value=$(id).checked;
    try {await post("/api/camera?cam="+cam,{[key]:value});if(S.cam===cam)cur()[key]=value;cancelLearning();toast("Настройка сохранена. Повторите проверку кадра");}
    catch(e){if(S.cam===cam)$(id).checked=!!cur()[key];}
  });
}
function renderLearningDiagnostics(box,r) {
  if(!r.learning_robust_enabled) {
    box.append(el("p","muted small","Образцы: прежнее сопоставление, устойчивый режим выключен"));return;
  }
  const details=el("details"),summary=el("summary","small","Применение образцов нормы · "+fmt(r.learning_ms,1)+" мс");
  details.append(summary);
  const statuses={matched:"применён",no_effect:"не уменьшил отличие",outside_region:"за пределами области проверки",no_anomaly:"нет аномалий рядом",crop_too_small:"слишком маленький участок",no_learned_difference:"образец почти не отличается от эталона"};
  const reasons={unreliable_match:"тень не сопоставлена уверенно",not_smooth:"изменение не похоже на плавную тень",lighting_change_too_large:"слишком сильно изменилась яркость"};
  const methods={shadow:"плавная тень",pattern:"изображение",pattern_and_shadow:"изображение + тень",none:"—"};
  for(const d of r.learning_diagnostics||[]) {
    const text=d.id+": "+(statuses[d.status]||d.status)+"; способ: "+(methods[d.method]||d.method)+
      "; убрано рабочих пикселей: "+d.removed_pixels+"; сдвиг: "+(d.shift||[]).map(v=>fmt(v,1)).join(", ")+" px"+
      "; сходство изображения: "+fmt(d.pattern_correlation,2)+"; тени: "+fmt(d.shadow_correlation,2)+
      (d.shadow_skip_reason?"; "+(reasons[d.shadow_skip_reason]||d.shadow_skip_reason):"");
    details.append(el("p","small",text));
  }
  if(!(r.learning_diagnostics||[]).length)details.append(el("p","muted small","Для текущего эталона нет активных образцов"));
  box.append(details);
}
