"use strict";
function cancelLearning() {
  S.learning = false; S.learnBox = null; S.learnStart = null;
  $("learnSave").disabled = true;
  if (typeof draw === "function") draw();
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
$("learnRefresh").onclick=refreshLearned;
$("learnCancel").onclick=()=>{cancelLearning();draw();};
$("learnSelect").onclick=async()=>{
  const r=S.learningResult, cam=S.cam;
  if(!r || r.cam!==cam || !r.ver || r.defect_injected || r.verdict==="NO_REFERENCE") {
    toast("Сначала проверьте кадр без тестового дефекта",true); return;
  }
  // Freeze the emulated stream so its next frame cannot replace the selected source.
  await api("/api/run?on=0",{method:"POST"});
  const img=new Image();
  img.onload=()=>{
    if(S.cam!==cam) return;
    cancelLearning(); S.learningImage=img; S.learning=true; S.learningVersion=r.ver;
    $("learnInfo").textContent="Выделите участок мышью. Кадр: " + r.frame;
    draw();
  };
  img.onerror=()=>toast("Кадр изменился. Проверьте его повторно",true);
  img.src="/api/inspected?cam="+cam+"&version="+r.ver;
};
canvas.addEventListener("pointerdown",ev=>{
  if(!S.learning || ev.button!==0) return;
  ev.stopImmediatePropagation(); S.learnStart=canvasPoint(ev); canvas.setPointerCapture(ev.pointerId);
},true);
canvas.addEventListener("pointermove",ev=>{
  if(!S.learning) return;
  ev.stopImmediatePropagation(); if(!S.learnStart) return;
  const p=canvasPoint(ev),a=S.learnStart;
  S.learnBox={x:Math.min(a[0],p[0]),y:Math.min(a[1],p[1]),width:Math.abs(a[0]-p[0])+1,height:Math.abs(a[1]-p[1])+1};
  draw();
},true);
canvas.addEventListener("pointerup",ev=>{
  if(!S.learning) return;
  ev.stopImmediatePropagation(); S.learnStart=null; $("learnSave").disabled=!S.learnBox;
},true);
canvas.addEventListener("contextmenu",ev=>{if(S.learning){ev.preventDefault();ev.stopImmediatePropagation();}},true);
$("learnSave").onclick=async()=>{
  if(!S.learning || !S.learnBox) return;
  try {
    await post("/api/learning?cam="+S.cam,{version:S.learningVersion,bbox:S.learnBox});
    cancelLearning(); draw(); await refreshLearned();
    toast("Образец сохранён. Повторите проверку кадра для новой оценки");
  } catch(e) { /* api displays stale frame or validation error */ }
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
