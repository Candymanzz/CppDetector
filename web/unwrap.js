/* Experimental surface workspace. Read-only server API; recipes stay separate from detector profiles. */
(() => {
  'use strict';
  const $ = id => document.getElementById(id), C = UnwrapCore, I = UnwrapInspection, T=UnwrapStitching;
  const GROUPS = {bucket1: [0,1,4,3], bucket2: [9,8,5,6]};
  // Approximate boundaries observed on frames36's first images, in source coordinates.
  // These are editable starting points, not measured camera calibration.
  const SEEDS = {
    0:[[.76,.65],[.47,.72],[.17,.70],[.70,.12],[.46,.07],[.22,.17]],
    1:[[.74,.82],[.48,.75],[.18,.60],[.84,.35],[.61,.20],[.40,.24]],
    3:[[.78,.68],[.46,.77],[.14,.78],[.64,.12],[.43,.09],[.16,.19]],
    4:[[.77,.76],[.45,.74],[.13,.63],[.72,.32],[.55,.19],[.32,.20]],
    5:[[.79,.60],[.50,.64],[.20,.70],[.70,.18],[.46,.09],[.22,.22]],
    6:[[.85,.68],[.57,.80],[.25,.75],[.73,.11],[.51,.12],[.27,.25]],
    8:[[.78,.81],[.44,.76],[.10,.75],[.72,.22],[.49,.13],[.21,.24]],
    9:[[.77,.89],[.48,.75],[.23,.59],[.84,.55],[.75,.34],[.54,.20]]
  };
  let state = null, bucket = 'bucket1', recipe = null, selected = 0, view = 'current';
  let items = [], output = null, worker = null, generation = 0, buildTimer = null, drag = -1, pin = null, opening = null;
  let stitchTask=null,activeCalibration=null;
  const stitchErrors=new Map();
  const cache = new Map(), frames = new Map();
  const uploads=new Map();let uploadSerial=0;
  const make = (tag, text, cls) => { const e=document.createElement(tag); if(text!==undefined)e.textContent=text; if(cls)e.className=cls; return e; };
  const camera = id => state?.cameras_info.find((c,i) => (c.id ?? i) === id);
  const geometry = () => recipe.cameras.find(c => c.id === selected);
  const inputName=id=>$('uwSource').value==='references'?'':$('uwSource').value==='uploads'?(uploads.get(id)?.name??null):frameName(id);
  const inputKey=id=>$('uwSource').value+':'+($('uwSource').value==='uploads'?(uploads.get(id)?.serial??'missing'):inputName(id));
  const item = () => items.find(c => c.id === selected && c.inputKey===inputKey(selected));
  const pct = n => (100*n).toFixed(1)+'%';
  const status = (text, error=false) => { $('uwStatus').textContent=text; $('uwStatus').classList.toggle('uw-error',error); };
  const key = () => 'cppdetector.unwrap.v1:'+state.source_dir+':'+bucket;
  const clone = x => JSON.parse(JSON.stringify(x));
  const inspectionViews=['inspection','aligned','controlReference'];
  const isInspectionView=()=>inspectionViews.includes(view);
  const isReferenceView=()=>view==='reference'||view==='controlReference';
  function sourcePoint(g,u,v) {
    const p=C.point(g,u,v),i=items.find(c=>c.id===g.id);
    if(!isReferenceView()&&i?.sourceShift&&i.current) {p[0]+=i.sourceShift.x/(i.current.width-1);p[1]+=i.sourceShift.y/(i.current.height-1);}
    return p;
  }
  function validateRecipe(r,ids) {
    C.validateRecipe(r,ids);r.inspection=I.settings(r.inspection);
    if(r.autoStitch===undefined)r.autoStitch=true;
    if(typeof r.autoStitch!=='boolean')throw new Error('Некорректный режим совмещения');
    if(r.calibration)T.validate(r.calibration,ids);
    return r;
  }
  function fillInspection() {
    const s=recipe.inspection;
    for(const [id,key] of [['uwInspectThreshold','threshold'],['uwMinArea','minArea'],['uwRejectPercent','rejectPercent'],['uwMaxShift','maxShift'],['uwInspectTolerance','tolerance']])$(id).value=s[key];
    $('uwMinCoverage').value=s.minCoverage*100;$('uwNormalize').checked=s.normalize;
    $('uwCameraTolerance').checked=s.cameraTolerance;
  }

  function initial(id) {
    const known = /(?:^|[\\/])frames36[\\/]?$/.test(state.source_dir || '') && camera(id)?.width === 1224 && camera(id)?.height === 1024;
    return {id,center:45+GROUPS[bucket].indexOf(id)*90,fov:120,projection:'cylinder',flip:false,enabled:true,
      points:clone(known ? SEEDS[id] : [[.15,.15],[.5,.15],[.85,.15],[.15,.85],[.5,.85],[.85,.85]])};
  }

  function save() {
    try { localStorage.setItem(key(),JSON.stringify(recipe)); $('uwStorage').textContent='Схема сохранена в этом браузере отдельно для выбранного ведра и набора. Для переноса скачайте JSON.'; }
    catch { $('uwStorage').textContent='Браузер не сохраняет схему. Скачайте JSON, чтобы сохранить настройку.'; }
  }

  function loadRecipe() {
    recipe = {version:1,bucket,dataset:state.source_dir,width:2000,seam:0,threshold:24,cameras:GROUPS[bucket].map(initial),inspection:I.settings(),autoStitch:true};
    try {
      const stored=localStorage.getItem(key());
      if(stored) {
        const parsed=validateRecipe(JSON.parse(stored),GROUPS[bucket]);
        if(parsed.bucket!==bucket||parsed.dataset!==state.source_dir)throw new Error('Stored recipe belongs to another dataset');
        recipe=parsed;
      }
    } catch { $('uwStorage').textContent='Сохранённая схема повреждена; загружена начальная геометрия.'; }
    selected=GROUPS[bucket][0]; pin=null;
    $('uwWidth').value=recipe.width; $('uwSeam').value=recipe.seam; $('uwThreshold').value=recipe.threshold;
    $('uwThresholdValue').textContent=recipe.threshold;
    $('uwAutoStitch').checked=recipe.autoStitch;
    fillInspection();
    updateFrameControls(); buildCards(); updateEditor();
  }

  async function open(reload=false) {
    if(opening) return opening;
    if(state && !reload) return;
    opening=(async()=>{
      try {
        invalidate();
        status('Читаю список камер и кадров…');
        const res=await fetch('/api/state',{cache:'no-store'});
        if(!res.ok) throw new Error('Не удалось получить список камер');
        const next=await res.json();
        if(!Array.isArray(next.cameras_info)) throw new Error('Сервер не передал список камер');
        const same=state && state.source_dir===next.source_dir;
        state=next; cache.clear();
        if(!same) { frames.clear();uploads.clear();items=[];loadRecipe(); } else { updateFrameControls(); buildCards(); }
        status('Начальные границы и углы приблизительные. Перетащите точки и проверьте стыки.');
        await build();
      } catch(e) { status(e.message,true); }
      finally { opening=null; }
    })();
    return opening;
  }

  function updateFrameControls() {
    const max=Math.max(1,...GROUPS[bucket].map(id=>camera(id)?.frames?.length||0));
    $('uwIndex').max=max;
    $('uwFrameCount').textContent='/ '+max;
    $('uwIndex').value=C.clamp(Math.trunc(Number($('uwIndex').value)||1),1,max);
    const fromFrames=$('uwSource').value==='frames';
    ['uwIndex','uwPrev','uwNext'].forEach(id=>$(id).disabled=!fromFrames);
    if(fromFrames) { $('uwPrev').disabled=+$('uwIndex').value<=1; $('uwNext').disabled=+$('uwIndex').value>=max; }
  }

  function frameName(id) {
    if(frames.has(id)) return frames.get(id);
    return camera(id)?.frames?.[Number($('uwIndex').value)-1] ?? null;
  }

  function sorted() { return [...recipe.cameras].sort((a,b)=>a.center-b.center||a.id-b.id); }

  function frameSelect(id,controlId) {
    const selectFrame=make('select');selectFrame.id=controlId;
    selectFrame.setAttribute('aria-label','Кадр для инспекции — камера '+id);
    if($('uwSource').value==='references') {
      const opt=make('option','Эталон: '+(camera(id)?.reference||'не задан'));opt.value='';selectFrame.append(opt);selectFrame.disabled=true;
    } else if($('uwSource').value==='uploads') {
      const opt=make('option','Свой файл: '+(uploads.get(id)?.name||'не загружен'));opt.value='';selectFrame.append(opt);selectFrame.disabled=true;
    } else {
      const absent=make('option','Нет кадра на этой позиции');absent.value='';selectFrame.append(absent);
      (camera(id)?.frames||[]).forEach((name,index)=>{const opt=make('option',(index+1)+'. '+name);opt.value=name;selectFrame.append(opt);});
      selectFrame.value=frameName(id)||'';
    }
    selectFrame.addEventListener('change',()=>chooseFrame(id,selectFrame.value||null));
    return selectFrame;
  }

  function buildFramePicker() {
    const box=$('uwFramePicker');box.replaceChildren();
    for(const id of [...GROUPS[bucket]].sort((a,b)=>a-b)) {
      const field=make('div',undefined,'uw-frame-field'),label=make('label','Камера '+id);label.htmlFor='uwFrameCam'+id;
      const selectFrame=frameSelect(id,'uwFrameCam'+id),nav=make('div',undefined,'uw-frame-nav'),position=make('span');position.id='uwFramePosition'+id;
      const buttons=[-1,1].map(offset=>{
        const b=make('button',offset<0?'←':'→');b.id=(offset<0?'uwFramePrev':'uwFrameNext')+id;b.title=(offset<0?'Предыдущий':'Следующий')+' кадр камеры '+id;b.setAttribute('aria-label',b.title);
        b.addEventListener('click',()=>{const list=camera(id)?.frames||[],index=list.indexOf(frameName(id)),name=list[index+offset];if(index>=0&&name)chooseFrame(id,name);});return b;
      });
      nav.append(buttons[0],position,buttons[1]);field.append(label,selectFrame,nav,uploadControl(id,'uwUploadCam'));box.append(field);
    }
  }

  function syncFrameChoices() {
    const refs=$('uwSource').value==='references',local=$('uwSource').value==='uploads',ids=[...GROUPS[bucket]].sort((a,b)=>a-b);let count=0;
    for(const id of ids) {
      const name=frameName(id),list=camera(id)?.frames||[],index=list.indexOf(name);
      for(const prefix of ['uwFrameCam','uwSourceFrameCam']){const control=$(prefix+id);if(control){control.value=refs||local?'':name||'';if(local&&control.options[0])control.options[0].textContent='Свой файл: '+(uploads.get(id)?.name||'не загружен');}}
      const previous=$('uwFramePrev'+id),next=$('uwFrameNext'+id),position=$('uwFramePosition'+id);
      if(previous)previous.disabled=refs||local||index<=0;
      if(next)next.disabled=refs||local||index<0||index>=list.length-1;
      if(position)position.textContent=refs?'Эталон':local?'С компьютера':(index>=0?(index+1)+' / '+list.length:'Нет кадра');
      if(refs?camera(id)?.reference:local?uploads.get(id)?.raster:index>=0)count++;
    }
    $('uwFrameSelection').textContent=(refs?'Самопроверка эталонов':local?'Загружено изображений: '+count+' из '+ids.length:'Выбрано кадров: '+count+' из '+ids.length)+'. '+ids.map(id=>'Камера '+id+': '+(refs?camera(id)?.reference||'эталон не задан':local?uploads.get(id)?.name||'файл не загружен':frameName(id)||'кадр не выбран')).join(' · ');
    $('uwFrameSelection').classList.toggle('uw-error',count<ids.length);
    for(const box of document.querySelectorAll('[data-uw-upload]')) {
      const id=Number(box.dataset.uwUpload),entry=uploads.get(id),text=box.querySelector('p');
      text.textContent=entry?(entry.name+' · '+(entry.loading?'загружается…':entry.error||`${entry.raster.nativeWidth} × ${entry.raster.nativeHeight}`)):'Файл с компьютера не выбран';
      text.classList.toggle('uw-error',!!entry?.error);box.querySelector('[data-remove]').disabled=refs||!entry;
      box.querySelector('input').disabled=refs;box.querySelector('[data-pick]').disabled=refs;
    }
  }

  function uploadControl(id,prefix) {
    const box=make('div',undefined,'uw-upload'),row=make('div',undefined,'uw-upload-actions'),input=make('input'),pick=make('button','Загрузить фото'),remove=make('button','Убрать'),text=make('p',undefined,'muted small');
    box.dataset.uwUpload=id;input.type='file';input.accept='image/*,.bmp';input.hidden=true;input.id=prefix+id;
    input.setAttribute('aria-label','Свой файл для инспекции — камера '+id);
    pick.type=remove.type='button';pick.dataset.pick='';remove.dataset.remove='';pick.setAttribute('aria-label','Загрузить фото камеры '+id);remove.setAttribute('aria-label','Убрать свой файл камеры '+id);
    pick.addEventListener('click',()=>input.click());
    input.addEventListener('change',()=>{const file=input.files?.[0];input.value='';if(file)loadUpload(id,file);});
    remove.addEventListener('click',()=>{uploads.delete(id);syncFrameChoices();if($('uwSource').value==='uploads'){items=items.filter(i=>i.inputKey===inputKey(i.id));drawThumbnails();updateEditor();schedule();}});
    row.append(pick,remove);box.append(row,input,text);return box;
  }

  async function loadUpload(id,file) {
    if(!state)return;
    const entry={serial:++uploadSerial,name:file.name,size:file.size,lastModified:file.lastModified,loading:true,raster:null,error:null};uploads.set(id,entry);
    if($('uwSource').value!=='uploads'){$('uwSource').value='uploads';updateFrameControls();buildCards();}
    invalidate();items=items.filter(i=>i.inputKey===inputKey(i.id));syncFrameChoices();drawThumbnails();updateEditor();status('Читаю своё изображение камеры '+id+'…');
    let url;
    try {
      if(!file.size)throw new Error('Файл пуст');
      if(file.size>64*1024*1024)throw new Error('Размер файла превышает 64 МБ');
      url=URL.createObjectURL(file);const raster=await decodeImage(url,'Не удалось прочитать изображение. Используйте PNG, JPEG, BMP или WebP.');
      if(uploads.get(id)!==entry)return;
      entry.raster=raster;
    } catch(e) {if(uploads.get(id)!==entry)return;entry.error=e.message;}
    finally {if(url)URL.revokeObjectURL(url);entry.loading=false;}
    if(uploads.get(id)!==entry)return;
    syncFrameChoices();
    if($('uwSource').value==='uploads'&&GROUPS[bucket].includes(id))schedule();
  }

  async function loadCurrent(id,name) {
    if($('uwSource').value!=='uploads')return loadImage(id,name);
    const entry=uploads.get(id);
    if(!entry?.raster)throw new Error(entry?.error||'свой файл не загружен');
    return entry.raster;
  }

  function chooseFrame(id,name) {
    if(!state||$('uwSource').value!=='frames')return;
    frames.set(id,name);syncFrameChoices();drawThumbnails();updateEditor();schedule();
  }

  function buildCards() {
    buildFramePicker();
    const box=$('uwSources'); box.replaceChildren();
    for(const g of sorted()) {
      const card=make('div',undefined,'uw-source'+(g.id===selected?' active':'')); card.dataset.cam=g.id;
      const button=make('button',undefined,'uw-thumbnail'); button.title='Настроить ракурс камеры '+g.id;
      button.setAttribute('aria-label',button.title);
      const canvas=make('canvas'); canvas.width=360; canvas.height=225; canvas.dataset.cam=g.id; button.append(canvas);
      button.addEventListener('click',()=>select(g.id));
      const meta=make('div',undefined,'uw-source-meta'), title=make('div');
      title.append(make('b','Камера '+g.id),make('span',Math.round(g.center)+'° · '+(g.enabled?'включена':'выключена'),'muted small'));
      const selectFrame=frameSelect(g.id,'uwSourceFrameCam'+g.id);
      const order=make('div'), arrows=make('span');
      for(const [text,offset] of [['←',-1],['→',1]]) {
        const b=make('button',text); b.title='Поменять с соседним ракурсом'; b.setAttribute('aria-label',`Камера ${g.id}: ${offset<0?'предыдущая':'следующая'} позиция`);
        b.addEventListener('click',()=>{const list=sorted(),i=list.findIndex(c=>c.id===g.id),other=list[(i+offset+list.length)%list.length];[g.center,other.center]=[other.center,g.center];save();buildCards();updateEditor();schedule();});arrows.append(b);
      }
      order.append(make('span','Порядок','muted small'),arrows);
      const info=make('p','Ожидает загрузки','muted'); info.dataset.info=g.id;
      const strip=make('canvas');strip.width=360;strip.height=110;strip.className='uw-strip-thumb';strip.setAttribute('aria-label','Выпрямленный ракурс камеры '+g.id);
      strip.addEventListener('click',()=>select(g.id));
      meta.append(title,selectFrame,uploadControl(g.id,'uwSourceUploadCam'),order,info,make('span','↓ выпрямленный ракурс','muted small'),strip); card.append(button,meta);box.append(card);
    }
    syncFrameChoices();drawThumbnails();
  }

  function select(id) {
    selected=id;
    for(const c of $('uwSources').children) c.classList.toggle('active',+c.dataset.cam===id);
    updateEditor();drawPanorama();
  }

  function updateEditor() {
    if(!recipe)return;
    const g=geometry(); $('uwEditorTitle').textContent='Камера '+selected+' · границы поверхности';
    $('uwEnabled').checked=g.enabled; $('uwCenter').value=g.center; $('uwFov').value=g.fov;
    $('uwProjection').value=g.projection; $('uwFlip').checked=g.flip;
    const fine=g.fine||{x:0,y:0,scaleX:1,scaleY:1};
    for(const [id,field,factor] of [['uwFineX','x',1],['uwFineY','y',1],['uwFineScaleX','scaleX',100],['uwFineScaleY','scaleY',100]])$(id).value=+(fine[field]*factor).toFixed(3);
    drawSource();drawStrip();
  }

  function clearOutput() {
    output=null;pin=null;$('uwExport').disabled=true;
    $('uwExportInspection').disabled=true;$('uwVerdict').textContent='Ещё не проверено';$('uwInspectionReason').textContent='Входы изменились; выполняется новая инспекция.';
    $('uwInspectionResult').className='uw-inspection-result';$('uwInspectionMetrics').textContent='';$('uwRegions').replaceChildren();
    ['uwSize','uwCoverage','uwPaired','uwChanged'].forEach(id=>$(id).textContent='—');
    const c=$('uwCanvas');c.getContext('2d').clearRect(0,0,c.width,c.height);
    $('uwEmpty').hidden=false;$('uwEmpty').textContent='Собираю развёртку…';
    $('uwPixel').textContent='Нажмите на развёртку — покажу исходную камеру и координаты участка.';
  }

  function invalidate() {
    generation++; if(worker){worker.terminate();worker=null;}
    if(stitchTask)stitchTask.cancel();
    activeCalibration=null;
    clearTimeout(buildTimer);clearOutput();
  }

  function schedule() {
    if(!state)return;
    invalidate();status('Настройки изменились. Пересобираю развёртку…');
    buildTimer=setTimeout(()=>build(),220);
  }

  async function loadImage(id, name) {
    if(name===null) throw new Error('нет кадра на выбранной позиции');
    const url='/api/frame?cam='+id+'&frame='+encodeURIComponent(name), cacheKey=url;
    if(cache.has(cacheKey))return cache.get(cacheKey);
    const promise=decodeImage(url,name?'кадр недоступен':'эталон недоступен');
    cache.set(cacheKey,promise);
    promise.catch(()=>cache.delete(cacheKey));
    if(cache.size>24)cache.delete(cache.keys().next().value);
    return promise;
  }

  function decodeImage(url,errorText) {
    return new Promise((resolve,reject)=>{
      const img=new Image(); let done=false;
      const timer=setTimeout(()=>{if(!done){done=true;img.src='';reject(new Error('истекло время загрузки'));}},15000);
      img.onload=()=>{
        if(done)return;done=true;clearTimeout(timer);
        try {
          const ratio=Math.min(1,1200/Math.max(img.naturalWidth,img.naturalHeight));
          const c=make('canvas');c.width=Math.max(1,Math.round(img.naturalWidth*ratio));c.height=Math.max(1,Math.round(img.naturalHeight*ratio));
          const ctx=c.getContext('2d',{willReadFrequently:true});ctx.drawImage(img,0,0,c.width,c.height);
          resolve({width:c.width,height:c.height,data:ctx.getImageData(0,0,c.width,c.height).data,nativeWidth:img.naturalWidth,nativeHeight:img.naturalHeight,canvas:c});
        } catch(e){reject(e);}
      };
      img.onerror=()=>{if(done)return;done=true;clearTimeout(timer);reject(new Error(errorText));};
      img.src=url;
    });
  }

  function showStitchStatus(text,warn=false) {
    $('uwStitchStatus').textContent=text;$('uwStitchStatus').classList.toggle('warn',warn);
  }
  function stitchReferences(cameras,signature) {
    return new Promise((resolve,reject)=>{
      const task={worker:new Worker('unwrap-stitch-worker.js?v=stitch-20261119')};stitchTask=task;
      const finish=(error,result)=>{if(task.done)return;task.done=true;clearTimeout(task.timer);task.worker.terminate();if(stitchTask===task)stitchTask=null;$('uwStitch').disabled=false;error?reject(error):resolve(result);};
      task.cancel=()=>finish(new DOMException('Совмещение отменено','AbortError'));
      task.timer=setTimeout(()=>finish(new Error('Превышено время поиска общих деталей')),30000);
      task.worker.onmessage=e=>e.data.error?finish(new Error(e.data.error)):finish(null,e.data);
      task.worker.onerror=()=>finish(new Error('Не удалось загрузить модуль совмещения'));
      $('uwStitch').disabled=true;
      task.worker.postMessage({signature,cameras:cameras.map(c=>({id:c.id,center:c.center,fov:c.fov,points:c.points,projection:c.projection,flip:c.flip,enabled:c.enabled,reference:{width:c.reference.width,height:c.reference.height,data:c.reference.data}}))});
    });
  }
  async function build(forceStitch=false) {
    if(!state||!recipe)return;
    if($('uwSource').value==='uploads'&&recipe.cameras.some(c=>c.enabled&&uploads.get(c.id)?.loading)){status('Читаю загруженные изображения…');return;}
    invalidate();const token=generation; $('uwBuild').textContent='Перестроить и проверить';status('Загружаю кадры и эталоны…');
    try {
      recipe.cameras.forEach(C.validateGeometry);
      const loaded=await Promise.all(recipe.cameras.map(async g=>{
        const name=inputName(g.id),identity=inputKey(g.id),inputKind=$('uwSource').value,upload=inputKind==='uploads'?uploads.get(g.id):null;
        const file=upload?{name:upload.name,size:upload.size,lastModified:upload.lastModified}:null;
        if(!g.enabled)return {...clone(g),name,inputKey:identity,inputKind,file,current:null,reference:null,errors:['Ракурс выключен']};
        const [a,b]=await Promise.allSettled([loadCurrent(g.id,name),loadImage(g.id,'')]);
        const current=a.status==='fulfilled'?a.value:null, reference=b.status==='fulfilled'?b.value:null;
        const errors=[];if(a.status==='rejected')errors.push(a.reason.message);if(b.status==='rejected')errors.push(b.reason.message);
        const comparable=!!(current&&reference&&current.nativeWidth===reference.nativeWidth&&current.nativeHeight===reference.nativeHeight);
        if(current&&reference&&!comparable)errors.push('размеры кадра и эталона различаются');
        const ci=camera(g.id),norm=p=>p.map(q=>[q[0]/Math.max(1,ci.width-1),q[1]/Math.max(1,ci.height-1)]);
        const control={roi:norm(ci.roi||[]),exclusions:(ci.exclusions||[]).map(norm),nativeWidth:ci.width,nativeHeight:ci.height,maxShiftX:ci.max_alignment_shift_x,maxShiftY:ci.max_alignment_shift_y,toleranceX:ci.shift_tolerance_x,toleranceY:ci.shift_tolerance_y};
        return {...clone(g),name,inputKey:identity,inputKind,file,current,reference,comparable,errors,control};
      }));
      if(token!==generation)return;
      if(recipe.autoStitch&&loaded.every(c=>c.enabled&&c.reference)) {
        const signature=T.signature(loaded);
        if(forceStitch===true||(!recipe.calibration||recipe.calibration.signature!==signature)&&!stitchErrors.has(signature)) {
          showStitchStatus('Ищу общие детали эталонов и уточняю положения ракурсов…');
          try {
            const calibration=await stitchReferences(loaded,signature);if(token!==generation)return;
            T.validate(calibration,GROUPS[bucket]);recipe.calibration=calibration;stitchErrors.delete(signature);save();
          } catch(e) {if(token!==generation)return;stitchErrors.set(signature,e.message);}
        }
        if(token!==generation)return;
        if(recipe.calibration?.signature===signature) {
          activeCalibration=recipe.calibration;
          loaded.forEach(c=>{c.stitch=clone(activeCalibration.transforms[c.id]);});
          const pairs=activeCalibration.pairs.filter(p=>p.accepted).map(p=>`${p.a}↔${p.b}: ${p.count} точек`).join('; ');
          const refreshError=stitchErrors.get(signature);
          showStitchStatus((activeCalibration.complete?'Ракурсы совмещены. ':'Подгонка частичная: '+activeCalibration.count+' из 4 ракурсов. ')+(pairs?pairs+'. ':'')+(activeCalibration.complete?'Проверьте стыки визуально.':'На остальных стыках общих деталей недостаточно; их начальная геометрия сохранена.')+(refreshError?' Новое совмещение не выполнено ('+refreshError+'); используется предыдущая подгонка.':''),!activeCalibration.complete||!!refreshError);
        } else showStitchStatus('Автосовмещение не выполнено: '+(stitchErrors.get(signature)||'изменились эталоны или границы')+'. Стыки показаны без прозрачного наложения.',true);
      } else showStitchStatus(recipe.autoStitch?'Для совмещения нужны все четыре эталона и включённые ракурсы.':'Ручные положения ракурсов. Стыки показаны без прозрачного наложения.',recipe.autoStitch);
      loaded.forEach(c=>{if(c.fine)c.stitch=C.fineStitch(c,c.stitch);});
      const manual=loaded.filter(c=>c.fine&&(c.fine.x||c.fine.y||c.fine.scaleX!==1||c.fine.scaleY!==1));
      if(manual.length)$('uwStitchStatus').textContent+=' Ручная подгонка камер: '+manual.map(c=>c.id).join(', ')+'. Проверьте совпадение деталей на эталонах.';
      items=loaded;drawThumbnails();updateEditor();status('Выпрямляю поверхность и соединяю ракурсы…');
      worker=new Worker('unwrap-worker.js?v=quality-20261119');
      const started=performance.now();
      worker.onmessage=event=>{
        if(token!==generation)return;
        worker.terminate();worker=null;
        if(event.data.error){status(event.data.error,true);$('uwEmpty').textContent='Не удалось построить развёртку';return;}
        output=event.data;
        for(const source of output.inspection.sources||[]) {const i=items.find(c=>c.id===source.id);if(i){i.sourceShift=source.sampleShift;i.sourcePhoto=source.photometry;}}
        $('uwSize').textContent=output.width+' × '+output.height;
        $('uwCoverage').textContent=pct(output.stats.coverage);$('uwPaired').textContent=pct(output.stats.pairedCoverage);
        $('uwChanged').textContent=output.stats.changedFraction===null?'Нет пары':pct(output.stats.changedFraction);
        $('uwEmpty').hidden=output.stats.coverage>0;
        if(!output.stats.coverage)$('uwEmpty').textContent='Нет доступных кадров в включённых ракурсах';
        $('uwExport').disabled=!output.stats.coverage;
        const failures=items.filter(i=>i.enabled&&i.errors.length).map(i=>'камера '+i.id+': '+i.errors.join(', '));
        status((failures.length?'Неполные данные: '+failures.join('; ')+'. ':'')+
          'Пробная инспекция · '+Math.round(performance.now()-started)+' мс на сборку и контроль в браузере · геометрия требует калибровки.',!!failures.length);
        renderInspection();
        drawPanorama();drawThumbnails();drawStrip();
      };
      worker.onerror=()=>{if(token!==generation)return;worker.terminate();worker=null;status('Ошибка обработки развёртки. Попробуйте построить снова.',true);$('uwEmpty').textContent='Ошибка обработки';};
      // Only the small raster payload crosses the worker boundary; DOM canvases stay in the UI.
      const raster=img=>img?{width:img.width,height:img.height,data:img.data}:null;
      worker.postMessage({width:recipe.width,height:Math.round(recipe.width*.26),seam:recipe.seam,threshold:recipe.threshold,inspection:recipe.inspection,cut:true,
        cameras:loaded.map(i=>({...i,current:raster(i.current),reference:raster(i.reference)}))});
    } catch(e) { if(token===generation){status(e.message,true);$('uwEmpty').textContent='Проверьте границы и повторите построение';} }
  }

  function drawThumbnails() {
    if(!recipe)return;
    for(const card of $('uwSources').children) {
      const id=+card.dataset.cam,desired=inputName(id);
      const i=items.find(i=>i.id===id&&i.inputKey===inputKey(id)), c=card.querySelector('canvas'),ctx=c.getContext('2d');
      ctx.clearRect(0,0,c.width,c.height);
      if(i?.current) {
        const scale=Math.min(c.width/i.current.width,c.height/i.current.height),w=i.current.width*scale,h=i.current.height*scale;
        ctx.save();ctx.translate((c.width-w)/2,(c.height-h)/2);ctx.drawImage(i.current.canvas,0,0,w,h);drawBoundary(ctx,recipe.cameras.find(g=>g.id===i.id),w,h,false,true);ctx.restore();
      }
      const strip=card.querySelector('.uw-strip-thumb'),s=output?.strips.find(s=>s.id===i?.id);
      if(s)put(strip,s);else strip.getContext('2d').clearRect(0,0,strip.width,strip.height);
      const text=card.querySelector('[data-info]');
      text.textContent=i?(i.errors.length?i.errors.join(' · '):((i.name||'Эталон')+' → '+Math.round(i.center)+'°')):(desired===null?'Кадр не выбран':'Загружается '+(desired||'эталон')+'…');
      text.classList.toggle('uw-error',!!i?.errors.length&&i.enabled);
      card.classList.toggle('failed',!!i?.errors.length&&i.enabled);
    }
  }

  function drawBoundary(ctx,g,w,h,handles,current=!isReferenceView()) {
    ctx.save();const i=items.find(c=>c.id===g.id);if(current&&i?.sourceShift&&i.current)ctx.translate(i.sourceShift.x/(i.current.width-1)*w,i.sourceShift.y/(i.current.height-1)*h);
    ctx.strokeStyle='#3be3c8';ctx.lineWidth=handles?3:2;ctx.fillStyle='rgba(40,220,180,.10)';ctx.beginPath();
    for(let n=0;n<=40;n++){const p=C.point(g,n/40,0);if(n===0)ctx.moveTo(p[0]*w,p[1]*h);else ctx.lineTo(p[0]*w,p[1]*h);}
    for(let n=40;n>=0;n--){const p=C.point(g,n/40,1);ctx.lineTo(p[0]*w,p[1]*h);}ctx.closePath();ctx.fill();ctx.stroke();
    if(handles)g.points.forEach((p,i)=>{ctx.beginPath();ctx.arc(p[0]*w,p[1]*h,9,0,Math.PI*2);ctx.fillStyle=i<3?'#ffe07c':'#4ce2df';ctx.fill();ctx.strokeStyle='#14252b';ctx.lineWidth=2;ctx.stroke();ctx.fillStyle='#0c2024';ctx.font='bold 12px sans-serif';ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(i+1,p[0]*w,p[1]*h);});
    ctx.restore();
  }

  function drawSource() {
    const canvas=$('uwSourceCanvas'),ctx=canvas.getContext('2d'),i=item(),img=(isReferenceView()?i?.reference:i?.current)||i?.reference;
    canvas.width=img?.width||800;canvas.height=img?.height||600;
    ctx.clearRect(0,0,canvas.width,canvas.height);
    if(img)ctx.drawImage(img.canvas,0,0,canvas.width,canvas.height);
    if(recipe)drawBoundary(ctx,geometry(),canvas.width,canvas.height,true);
    if(pin&&i?.id===pin.id&&img){const p=sourcePoint(geometry(),pin.u,pin.v);ctx.beginPath();ctx.arc(p[0]*canvas.width,p[1]*canvas.height,14,0,2*Math.PI);ctx.strokeStyle='#ff695b';ctx.lineWidth=4;ctx.stroke();}
  }

  function put(canvas,img) {canvas.width=img.width;canvas.height=img.height;canvas.getContext('2d').putImageData(new ImageData(img.data,img.width,img.height),0,0);}

  function drawStrip() {
    const c=$('uwStripCanvas'),g=recipe&&geometry();
    if(!g)return;
    try {
      C.validateGeometry(g);
      const i=item(),img=(isReferenceView()?i?.reference:i?.current)||i?.reference;
      if(img)put(c,C.rectify(img,{...g,current:i.current,sourceShift:i.sourceShift,sourcePhoto:i.sourcePhoto}));else c.getContext('2d').clearRect(0,0,c.width,c.height);
      $('uwGeometryStatus').textContent=img?'Так выбранная поверхность выпрямляется в полосу.':'Нет изображения для предпросмотра.';
      $('uwGeometryStatus').classList.remove('uw-error');
    } catch(e) {$('uwGeometryStatus').textContent=e.message;$('uwGeometryStatus').classList.add('uw-error');}
  }

  function drawPanorama() {
    if(!output)return;
    const canvas=$('uwCanvas'),raster=output.inspection.rasters[view]||{width:output.width,height:output.height,data:output[view]};put(canvas,raster);
    canvas.style.width=$('uwZoom').value==='fit'?'100%':raster.width*Number($('uwZoom').value)+'px';
    document.querySelectorAll('.uw-ruler span').forEach((e,i)=>e.textContent=(recipe.seam+i*90)+'°');
    const ctx=canvas.getContext('2d');
    if($('uwGuides').checked) {
      ctx.font='bold '+Math.max(12,Math.round(canvas.width/100))+'px sans-serif';ctx.textBaseline='top';
      for(const g of items.filter(c=>c.enabled)) {
        const center=C.toAtlas(g,.5,.5),x=C.wrap(center.angle-recipe.seam)*canvas.width/360;
        ctx.fillStyle='rgba(10,20,30,.75)';ctx.fillRect(x-40,10,110,30);ctx.fillStyle='#fff';ctx.fillText('Кам. '+g.id,x-30,14);
        for(const u of [0,1]) {
          ctx.strokeStyle=g.id===selected?'#53e6d4':'rgba(255,255,255,.55)';ctx.lineWidth=2;ctx.setLineDash([8,8]);ctx.beginPath();let last=null;
          for(let n=0;n<=20;n++){const p=C.toAtlas(g,u,n/20),ex=C.wrap(p.angle-recipe.seam)*canvas.width/360,ey=p.v*canvas.height;if(last===null||Math.abs(ex-last)>canvas.width/2)ctx.moveTo(ex,ey);else ctx.lineTo(ex,ey);last=ex;}
          ctx.stroke();
        }
      }
      ctx.setLineDash([]);
    }
    if(view==='inspection') {
      ctx.strokeStyle='#ffd369';ctx.lineWidth=2;
      for(const region of output.inspection.regions)for(const b of region.boxes){ctx.strokeRect(b.x,b.y,b.width,b.height);ctx.fillStyle='#ffe1a0';ctx.fillText('#'+region.id,b.x+3,Math.max(0,b.y-15));}
    }
    if(pin){
      const shift=isInspectionView()&&!isReferenceView()?output.inspection.shift:{x:0,y:0};
      const x=C.wrap(pin.angle-recipe.seam-shift.x*360/output.inspection.width)*canvas.width/360;
      const y=(pin.panoV-shift.y/output.inspection.height)*canvas.height;
      ctx.beginPath();ctx.arc(x,y,12,0,2*Math.PI);ctx.strokeStyle='#ff695b';ctx.lineWidth=4;ctx.stroke();
    }
  }

  $('uwCanvas').addEventListener('click',event=>{
    if(!output)return;
    const rect=$('uwCanvas').getBoundingClientRect(),cw=$('uwCanvas').width,ch=$('uwCanvas').height;
    const x=C.clamp((event.clientX-rect.left)*cw/rect.width,0,cw-1),y=C.clamp((event.clientY-rect.top)*ch/rect.height,0,ch-1);
    const shift=isInspectionView()&&!isReferenceView()?output.inspection.shift:{x:0,y:0};
    const angle=C.wrap((x/cw+shift.x/output.inspection.width)*360+recipe.seam),v=y/ch+shift.y/output.inspection.height;
    const candidates=items.map(i=>({camera:i,uv:C.fromAtlas(i,angle,v)})).filter(c=>c.uv&&c.camera.enabled&&(isReferenceView()?c.camera.reference:c.camera.current));
    const contributors=candidates.map(c=>c.camera);
    const sx=Math.floor(C.wrap(angle-recipe.seam)*output.width/360),sy=C.clamp(Math.floor(v*output.height),0,output.height-1);
    const owner=isInspectionView()?output.atlasOwner[output.atlasOwner.length===output.width?sx:sy*output.width+sx]:output.owner[sy*output.width+sx],best=contributors.find(i=>i.id===owner)||(!isInspectionView()?contributors[0]:null);
    if(!best){pin=null;$('uwPixel').textContent='В этой области нет доступного исходного кадра.';drawSource();drawPanorama();return;}
    if(v<0||v>1){$('uwPixel').textContent='Этот участок выходит за доступную поверхность после выравнивания.';return;}
    const [u,localV]=candidates.find(c=>c.camera.id===best.id).uv,p=sourcePoint(best,u,localV),img=isReferenceView()?best.reference:best.current;
    pin={angle,panoV:v,u,v:localV,id:best.id};select(best.id);
    $('uwPixel').textContent=`${angle.toFixed(1)}° · камера ${best.id} · ${isReferenceView()?'эталон':best.name||'эталон'} · исходный пиксель (${Math.round(p[0]*(img.nativeWidth-1))}, ${Math.round(p[1]*(img.nativeHeight-1))})`+(contributors.length>1?' · перекрытие камер '+contributors.map(c=>c.id).join(', '):'');
  });

  function pointer(event) {const r=$('uwSourceCanvas').getBoundingClientRect(),i=item(),sh=!isReferenceView()?i?.sourceShift:null;return [C.clamp((event.clientX-r.left)/r.width-(sh?sh.x/(i.current.width-1):0),0,1),C.clamp((event.clientY-r.top)/r.height-(sh?sh.y/(i.current.height-1):0),0,1)];}
  $('uwSourceCanvas').addEventListener('pointerdown',e=>{
    if(!recipe||!item()?.current)return;
    const p=pointer(e),r=$('uwSourceCanvas').getBoundingClientRect();let distance=22;
    geometry().points.forEach((v,i)=>{const d=Math.hypot((p[0]-v[0])*r.width,(p[1]-v[1])*r.height);if(d<distance){distance=d;drag=i;}});
    if(drag>=0){invalidate();$('uwSourceCanvas').setPointerCapture(e.pointerId);e.preventDefault();}
  });
  $('uwSourceCanvas').addEventListener('pointermove',e=>{if(drag<0)return;geometry().points[drag]=pointer(e);drawSource();drawStrip();});
  function endDrag(){if(drag<0)return;drag=-1;try{C.validateGeometry(geometry());save();schedule();}catch(e){status(e.message+'; поправьте точки.',true);}}
  $('uwSourceCanvas').addEventListener('pointerup',endDrag);$('uwSourceCanvas').addEventListener('pointercancel',endDrag);
  $('uwSourceCanvas').addEventListener('lostpointercapture',endDrag);

  function setView(name){view=name;pin=null;for(const b of $('uwViews').children)b.classList.toggle('active',b.dataset.view===name);drawPanorama();drawSource();drawStrip();}
  $('uwViews').addEventListener('click',e=>{const name=e.target.dataset.view;if(name)setView(name);});
  $('uwGuides').addEventListener('change',drawPanorama);$('uwZoom').addEventListener('change',drawPanorama);
  $('uwBucket').addEventListener('change',()=>{if(!state)return;invalidate();bucket=$('uwBucket').value;items=[];loadRecipe();schedule();});
  $('uwBuild').addEventListener('click',()=>{if(state)build();else open(true);});
  $('uwReload').addEventListener('click',()=>open(true));
  function changeIndex(value){if(!state)return;$('uwIndex').value=value;frames.clear();updateFrameControls();buildCards();schedule();}
  $('uwIndex').addEventListener('change',()=>changeIndex($('uwIndex').value));
  $('uwPrev').addEventListener('click',()=>changeIndex(+$('uwIndex').value-1));$('uwNext').addEventListener('click',()=>changeIndex(+$('uwIndex').value+1));
  $('uwSource').addEventListener('change',()=>{if(!state)return;updateFrameControls();buildCards();schedule();});
  $('uwAutoStitch').addEventListener('change',()=>{if(!recipe)return;recipe.autoStitch=$('uwAutoStitch').checked;save();schedule();});
  $('uwStitch').addEventListener('click',()=>{if(!recipe)return;recipe.autoStitch=true;$('uwAutoStitch').checked=true;save();build(true);});
  $('uwResetStitch').addEventListener('click',()=>{if(!recipe)return;delete recipe.calibration;recipe.cameras.forEach(c=>delete c.fine);recipe.autoStitch=false;$('uwAutoStitch').checked=false;save();updateEditor();schedule();});
  for(const [id,field,factor] of [['uwFineX','x',1],['uwFineY','y',1],['uwFineScaleX','scaleX',100],['uwFineScaleY','scaleY',100]])$(id).addEventListener('change',()=>{
    if(!recipe)return;const g=geometry(),old=g.fine;
    g.fine={x:0,y:0,scaleX:1,scaleY:1,...g.fine,[field]:Number($(id).value)/factor};
    try {C.fineStitch(g,activeCalibration?.transforms[g.id]);save();updateEditor();schedule();}
    catch(e){if(old)g.fine=old;else delete g.fine;updateEditor();status(e.message,true);}
  });
  $('uwResetFine').addEventListener('click',()=>{if(!recipe)return;delete geometry().fine;save();updateEditor();schedule();});
  for(const [id,field] of [['uwEnabled','enabled'],['uwCenter','center'],['uwFov','fov'],['uwProjection','projection'],['uwFlip','flip']]) {
    $(id).addEventListener('change',()=>{
      if(!recipe)return;const old=clone(geometry());
      geometry()[field]=['enabled','flip'].includes(field)?$(id).checked:['center','fov'].includes(field)?Number($(id).value):$(id).value;
      try{C.validateGeometry(geometry());save();buildCards();updateEditor();schedule();}
      catch(e){Object.assign(geometry(),old);updateEditor();status(e.message,true);}
    });
  }
  $('uwRotate').addEventListener('click',()=>{if(!recipe)return;geometry().points.reverse();save();updateEditor();schedule();});
  $('uwResetGeometry').addEventListener('click',()=>{if(!recipe)return;geometry().points=initial(selected).points;save();updateEditor();schedule();});
  for(const [id,field] of [['uwWidth','width'],['uwSeam','seam'],['uwThreshold','threshold']]) $(id).addEventListener('change',()=>{
    if(!recipe)return;const old=recipe[field];recipe[field]=Number($(id).value);
    try{validateRecipe(recipe,GROUPS[bucket]);save();$('uwThresholdValue').textContent=recipe.threshold;schedule();}
    catch(e){recipe[field]=old;$(id).value=old;status(e.message,true);}
  });
  $('uwThreshold').addEventListener('input',()=>{$('uwThresholdValue').textContent=$('uwThreshold').value;});

  function download(blob,name) {const url=URL.createObjectURL(blob),a=make('a');a.href=url;a.download=name;document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  $('uwExport').addEventListener('click',()=>{if(!output||!output.stats.coverage)return;const c=make('canvas');put(c,output.inspection.rasters[view]||{width:output.width,height:output.height,data:output[view]});c.toBlob(b=>{if(b)download(b,'unwrap-'+bucket+'-'+view+'.png');},'image/png');});
  $('uwExportRecipe').addEventListener('click',()=>{if(!recipe)return;try{validateRecipe(recipe,GROUPS[bucket]);download(new Blob([JSON.stringify({...recipe,selection:{source:$('uwSource').value,position:+$('uwIndex').value,frames:items.map(i=>({cam:i.id,frame:i.name,kind:i.inputKind,file:i.file}))}},null,2)],{type:'application/json'}),'unwrap-'+bucket+'.json');}catch(e){status(e.message,true);}});
  $('uwImport').addEventListener('change',async()=>{
    const file=$('uwImport').files[0];if(!file||!state)return;
    try {
      if(file.size>100000)throw new Error('Файл схемы слишком большой');
      const r=validateRecipe(JSON.parse(await file.text()),GROUPS[bucket]);
      if(r.bucket!==bucket||r.dataset!==state.source_dir)throw new Error('Схема относится к другому ведру или набору кадров');
      recipe=clone(r);save();$('uwWidth').value=r.width;$('uwSeam').value=r.seam;$('uwThreshold').value=r.threshold;$('uwThresholdValue').textContent=r.threshold;$('uwAutoStitch').checked=r.autoStitch;fillInspection();buildCards();updateEditor();schedule();
    } catch(e){status('Схема не загружена: '+e.message,true);}finally{$('uwImport').value='';}
  });
  function renderInspection() {
    const r=output.inspection,ok=r.verdict==='OK',bad=r.verdict==='REJECT';
    $('uwVerdict').textContent=ok?'ГОДЕН':bad?'БРАК':'Контроль не выполнен';
    $('uwInspectionResult').className='uw-inspection-result '+(ok?'ok':bad?'bad':'warn');
    const reasons={required_inputs_missing:r.issues.join('; '),insufficient_coverage:'Недостаточно общей поверхности после выравнивания',source_registration_unreliable:'Не удалось надёжно совместить одну из камер с её эталоном',registration_unreliable:'Не удалось надёжно совместить развёртку с эталоном',ambiguous_registration:'Несколько положений одинаково похожи на эталон',shift_limit:'Сопоставление достигло границы разрешённого сдвига',defects_over_threshold:'Площадь пятен отличий достигла порога брака',within_threshold:'Площадь пятен отличий ниже заданного порога'};
    $('uwInspectionReason').textContent=reasons[r.reason]||r.reason;
    $('uwInspectionMetrics').textContent=`Контроль ${r.width} × ${r.height} · покрытие ${pct(r.coverage)} · дефекты ${r.areaPercent===null?'—':r.areaPercent.toFixed(3)+'%'} / ${r.settings.rejectPercent}% · зон ${r.regionsCount} · сдвиг X ${r.shift.x.toFixed(2)}, Y ${r.shift.y.toFixed(2)} px · сходство ${r.shift.confidence.toFixed(3)} · яркость ×${r.photometry.gain.toFixed(3)} ${r.photometry.offset>=0?'+':''}${r.photometry.offset.toFixed(1)}`;
    if(r.sources?.length)$('uwInspectionMetrics').textContent=`Контроль ${r.width} × ${r.height} · рабочая область ${pct(r.scopeFraction)} панорамы · проверено ${pct(r.coverage)} рабочей области · дефекты ${r.areaPercent===null?'—':r.areaPercent.toFixed(3)+'%'} / ${r.settings.rejectPercent}% · зон ${r.regionsCount} · `+r.sources.map(c=>`кам. ${c.id}: X ${c.shift.x.toFixed(2)}, Y ${c.shift.y.toFixed(2)} px${c.shift.reliable?'':' — совмещение ненадёжно'}`).join('; ');
    $('uwRegions').replaceChildren();
    for(const region of r.regions){const b=make('button',`#${region.id} · ${region.area} px²`);b.addEventListener('click',()=>{setView('inspection');const c=$('uwCanvas'),rect=c.getBoundingClientRect();c.dispatchEvent(new MouseEvent('click',{bubbles:true,clientX:rect.left+region.center.x/r.width*rect.width,clientY:rect.top+region.center.y/r.height*rect.height}));c.scrollIntoView({block:'center'});});$('uwRegions').append(b);}
    if(r.regionsTruncated)$('uwRegions').append(make('span','Показаны 50 крупнейших зон; общая площадь учитывает все зоны.','muted small'));
    $('uwExportInspection').disabled=false;
  }
  $('uwInspect').addEventListener('click',()=>{setView('inspection');if(state)build();else open(true);});
  $('uwShowInspection').addEventListener('click',()=>{setView('inspection');$('uwScroll').scrollIntoView({block:'center'});});
  for(const [id,field] of [['uwInspectThreshold','threshold'],['uwMinArea','minArea'],['uwRejectPercent','rejectPercent'],['uwMaxShift','maxShift'],['uwMinCoverage','minCoverage'],['uwInspectTolerance','tolerance'],['uwNormalize','normalize'],['uwCameraTolerance','cameraTolerance']])$(id).addEventListener('change',()=>{
    if(!recipe)return;const old=clone(recipe.inspection),value=['uwNormalize','uwCameraTolerance'].includes(id)?$(id).checked:Number($(id).value)/(id==='uwMinCoverage'?100:1);
    try{recipe.inspection=I.settings({...old,[field]:value});save();schedule();}catch(e){recipe.inspection=old;fillInspection();status(e.message,true);}
  });
  $('uwExportInspection').addEventListener('click',()=>{
    if(!output)return;const {rasters,...report}=output.inspection;
    download(new Blob([JSON.stringify({version:1,engine:'unwrap-atlas-v2',createdAt:new Date().toISOString(),recipe:clone(recipe),activeCalibration:activeCalibration?clone(activeCalibration):null,geometry:items.map(i=>({id:i.id,stitch:i.stitch||[1,0,0,0,1,0]})),inputs:items.map(i=>({camera:i.id,frame:i.name,kind:i.inputKind,file:i.file,reference:camera(i.id)?.reference,control:clone(i.control||{}),errors:i.errors})),...report},null,2)],{type:'application/json'}),'unwrap-'+bucket+'-inspection.json');
  });
  window.UnwrapUI={open};
})();
