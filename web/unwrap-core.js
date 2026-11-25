/* Geometry and raster operations shared by the worker, preview and Node tests. */
(function (root) {
  'use strict';
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const wrap = a => ((a % 360) + 360) % 360;
  const delta = (a, b) => ((a - b + 540) % 360 + 360) % 360 - 180;
  const finite = Number.isFinite;

  function curve(a, m, b, t) {
    const arch = 4 * t * (1 - t);
    return [0, 1].map(k => a[k] * (1 - t) + b[k] * t + arch * (m[k] - (a[k] + b[k]) / 2));
  }

  // Points: upper left, upper centre, upper right, lower left, lower centre, lower right.
  // Upper/lower refer to the finished scan; a rotated camera needs no resampled intermediate image.
  function point(g, u, v) {
    if (g.flip) u = 1 - u;
    const half = g.fov * Math.PI / 360;
    const t = g.projection === 'linear' ? u : (1 + Math.sin((2 * u - 1) * half) / Math.sin(half)) / 2;
    const p = g.points, top = curve(p[0], p[1], p[2], t), bottom = curve(p[3], p[4], p[5], t);
    return [top[0] * (1 - v) + bottom[0] * v, top[1] * (1 - v) + bottom[1] * v];
  }

  function validateGeometry(g) {
    if (!g || !finite(g.center) || g.center < 0 || g.center >= 360 || !finite(g.fov) || g.fov < 30 || g.fov > 170 ||
        !['cylinder', 'linear'].includes(g.projection) || typeof g.flip !== 'boolean' || typeof g.enabled !== 'boolean' ||
        !Array.isArray(g.points) || g.points.length !== 6 ||
        g.points.some(p => !Array.isArray(p) || p.length !== 2 || p.some(n => !finite(n) || n < 0 || n > 1)))
      throw new Error('Некорректная геометрия ракурса');
    if(g.fine!==undefined)validateFine(g.fine);
    if(g.stitch!==undefined&&!validStitch(g.stitch))throw new Error('Некорректная подгонка ракурса');
    if(g.stitch&&g.fov*(Math.abs(g.stitch[2])+(Math.abs(g.stitch[0])+Math.abs(g.stitch[1]))/2)>=179)throw new Error('Подгонка ракурса выходит за допустимый угловой диапазон');
    let sign = 0;
    for (let y = 0; y <= 8; y++) for (let x = 0; x <= 16; x++) {
      const u = x / 16, v = y / 8, p = point(g, u, v);
      if (p.some(n => n < -1e-8 || n > 1 + 1e-8)) throw new Error('Дуга выходит за границу кадра');
      const du = point(g, u + 0.0001, v), dv = point(g, u, v + 0.0001);
      const det = ((du[0] - p[0]) * (dv[1] - p[1]) - (du[1] - p[1]) * (dv[0] - p[0])) * 1e8;
      if (Math.abs(det) < 0.0001 || (sign && Math.sign(det) !== sign))
        throw new Error('Границы пересекаются или поверхность слишком узкая');
      sign = Math.sign(det);
    }
    return g;
  }

  function validateRecipe(r, ids) {
    if (!r || r.version !== 1 || !['bucket1', 'bucket2'].includes(r.bucket) ||
        ![1200, 2000, 3200].includes(r.width) || !finite(r.seam) || r.seam < 0 || r.seam >= 360 ||
        !finite(r.threshold) || r.threshold < 1 || r.threshold > 120 ||
        !Array.isArray(r.cameras) || r.cameras.length !== ids.length ||
        new Set(r.cameras.map(c => c.id)).size !== ids.length || r.cameras.some(c => !ids.includes(c.id)))
      throw new Error('Этот файл не содержит схему выбранного ведра');
    r.cameras.forEach(validateGeometry);
    return r;
  }

  function sample(img, p, rgb) {
    if (!img || p[0] < 0 || p[0] > 1 || p[1] < 0 || p[1] > 1) return false;
    const x = p[0] * (img.width - 1), y = p[1] * (img.height - 1);
    const x0 = Math.floor(x), y0 = Math.floor(y), x1 = Math.min(x0 + 1, img.width - 1), y1 = Math.min(y0 + 1, img.height - 1);
    const dx = x - x0, dy = y - y0, d = img.data;
    const a = (y0 * img.width + x0) * 4, b = (y0 * img.width + x1) * 4;
    const c = (y1 * img.width + x0) * 4, e = (y1 * img.width + x1) * 4;
    if(d[a+3]<254||d[b+3]<254||d[c+3]<254||d[e+3]<254)return false;
    for (let k = 0; k < 3; k++) rgb[k] = (d[a+k]*(1-dx)+d[b+k]*dx)*(1-dy)+(d[c+k]*(1-dx)+d[e+k]*dx)*dy;
    return true;
  }

  function rectify(img, g, width = 360, height = 240) {
    const data = new Uint8ClampedArray(width * height * 4), rgb = [0, 0, 0];
    if (img) for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
      const p=point(g,(x+.5)/width,(y+.5)/height),current=img===g.current;
      if(current&&g.sourceShift){p[0]+=g.sourceShift.x/(img.width-1);p[1]+=g.sourceShift.y/(img.height-1);}
      if (!sample(img,p,rgb)) continue;
      if(current&&g.sourcePhoto)for(let c=0;c<3;c++)rgb[c]=clamp(rgb[c]*g.sourcePhoto.gain+g.sourcePhoto.offset,0,255);
      const i = (y * width + x) * 4;
      data[i] = rgb[0]; data[i+1] = rgb[1]; data[i+2] = rgb[2]; data[i+3] = 255;
    }
    return {width, height, data};
  }

  function render(job) {
    if(job.cut||job.cameras?.some(c=>c.stitch))return renderCut(job);
    const {width, height, cameras, seam, threshold} = job;
    if (!Number.isInteger(width) || width < 16 || width > 3200 || !Number.isInteger(height) || height < 8 || height > 900 ||
        !finite(seam) || !finite(threshold) || threshold < 1 || !Array.isArray(cameras) || cameras.length > 10)
      throw new Error('Некорректный размер развёртки');
    const n = width * height;
    const sum = new Float32Array(n * 4), ref = new Float32Array(n * 4), differences = new Float32Array(n);
    const paired = new Uint8Array(n), owner = new Int16Array(n).fill(-1), strongest = new Float32Array(n);
    const atlasCurrent=new Uint8ClampedArray(n*4),atlasReference=new Uint8ClampedArray(n*4);
    const atlasOwner=new Int16Array(width).fill(-1),atlasWeight=new Float64Array(width);
    const a = [0, 0, 0], b = [0, 0, 0];
    for (const cam of cameras) {
      validateGeometry(cam);
      if (!cam.enabled) continue;
      const g = cam, half = g.fov / 2;
      for (let x = 0; x < width; x++) {
        const d = delta(wrap((x + 0.5) * 360 / width + seam), g.center);
        if (Math.abs(d) >= half) continue;
        const u = (d / half + 1) / 2, weight = clamp((half - Math.abs(d)) / 8, 0.001, 1);
        // Fixed angular Voronoi ownership, identical for current/reference. Never feather inspection pixels.
        const controlWeight=1-Math.abs(d)/half,dominant=controlWeight>atlasWeight[x];
        if(dominant){atlasWeight[x]=controlWeight;atlasOwner[x]=cam.id;}
        for (let y = 0; y < height; y++) {
          const p = point(g, u, (y + 0.5) / height), i = y * width + x, k = i * 4;
          const has = sample(cam.current, p, a), hasRef = sample(cam.reference, p, b);
          if(dominant) {
            for(let c=0;c<3;c++){atlasCurrent[k+c]=has?a[c]:0;atlasReference[k+c]=hasRef?b[c]:0;}
            atlasCurrent[k+3]=has?255:0;atlasReference[k+3]=hasRef?255:0;
          }
          if (has) {
            for (let c = 0; c < 3; c++) sum[k+c] += a[c] * weight;
            sum[k+3] += weight;
            if (weight > strongest[i]) { strongest[i] = weight; owner[i] = cam.id; }
          }
          if (hasRef) {
            for (let c = 0; c < 3; c++) ref[k+c] += b[c] * weight;
            ref[k+3] += weight;
          }
          // Compare each camera BEFORE blending: overlapping differences cannot cancel one another.
          if (has && hasRef && cam.comparable !== false) {
            paired[i] = 1;
            differences[i] = Math.max(differences[i], Math.abs(a[0]-b[0]), Math.abs(a[1]-b[1]), Math.abs(a[2]-b[2]));
          }
        }
      }
    }
    const current = new Uint8ClampedArray(n*4), reference = new Uint8ClampedArray(n*4);
    const diff = new Uint8ClampedArray(n*4), coverage = new Uint8ClampedArray(n*4);
    let covered = 0, refCovered = 0, comparable = 0, changed = 0;
    for (let i = 0; i < n; i++) {
      const k = i*4, has = sum[k+3] > 0, hasRef = ref[k+3] > 0;
      if (has) { covered++; for(let c=0;c<3;c++) current[k+c] = sum[k+c]/sum[k+3]; current[k+3]=255; }
      if (hasRef) { refCovered++; for(let c=0;c<3;c++) reference[k+c]=ref[k+c]/ref[k+3]; reference[k+3]=255; }
      if (paired[i]) {
        comparable++;
        const hit = differences[i] >= threshold;
        if (hit) changed++;
        const gray = (current[k]+current[k+1]+current[k+2])/5;
        diff[k] = hit ? 255 : gray; diff[k+1] = hit ? 70 : gray; diff[k+2] = hit ? 40 : gray; diff[k+3]=255;
      }
      const color = paired[i] ? [35,166,131] : has ? [225,161,46] : [87,66,83];
      coverage[k]=color[0];coverage[k+1]=color[1];coverage[k+2]=color[2];coverage[k+3]=255;
    }
    return {width,height,current,reference,diff,coverage,owner,atlasCurrent,atlasReference,atlasOwner,
      stats:{coverage:covered/n,referenceCoverage:refCovered/n,pairedCoverage:comparable/n,changedFraction:comparable?changed/comparable:null},
      strips:cameras.map(c=>({id:c.id,...rectify(c.current,c)}))};
  }

  function validStitch(t) {
    return Array.isArray(t)&&t.length===6&&t.every(finite)&&t[0]>=.55&&t[0]<=1.6&&Math.abs(t[1])<=.25&&Math.abs(t[2])<=.5&&Math.abs(t[3])<=.22&&t[4]>=.65&&t[4]<=1.4&&Math.abs(t[5])<=.25&&t[0]*t[4]-t[1]*t[3]>=.4;
  }
  function validateFine(f) {
    if(!f||!finite(f.x)||Math.abs(f.x)>20||!finite(f.y)||Math.abs(f.y)>30||!finite(f.scaleX)||f.scaleX<.8||f.scaleX>1.2||!finite(f.scaleY)||f.scaleY<.8||f.scaleY>1.2)throw new Error('Некорректная точная подгонка ракурса');
    return f;
  }
  function fineStitch(g,base=[1,0,0,0,1,0]) {
    if(!g.fine)return base.slice();
    const f=validateFine(g.fine),t=[base[0]*f.scaleX,base[1]*f.scaleX,base[2]*f.scaleX+f.x/g.fov,base[3]*f.scaleY,base[4]*f.scaleY,base[5]*f.scaleY+f.y/312];
    validateGeometry({...g,stitch:t});return t;
  }
  function toAtlas(g,u,v) {
    const t=g.stitch||[1,0,0,0,1,0],x=u-.5,y=v-.5;
    return {angle:wrap(g.center+g.fov*(t[0]*x+t[1]*y+t[2])),v:.5+t[3]*x+t[4]*y+t[5]};
  }
  function fromAtlas(g,angle,v) {
    const t=g.stitch||[1,0,0,0,1,0],x=delta(angle,g.center)/g.fov-t[2],y=v-.5-t[5],det=t[0]*t[4]-t[1]*t[3];
    const u=.5+(t[4]*x-t[1]*y)/det,localV=.5+(-t[3]*x+t[0]*y)/det;
    return u>=0&&u<=1&&localV>=0&&localV<=1?[u,localV]:null;
  }
  function renderCut(job) {
    const {width,height,cameras,seam,threshold}=job;
    if(!Number.isInteger(width)||width<16||width>3200||!Number.isInteger(height)||height<8||height>900||!finite(seam)||!finite(threshold)||threshold<1||!Array.isArray(cameras)||cameras.length>10)throw new Error('Некорректный размер развёртки');
    const n=width*height,current=new Uint8ClampedArray(n*4),reference=new Uint8ClampedArray(n*4),diff=new Uint8ClampedArray(n*4),coverage=new Uint8ClampedArray(n*4);
    const atlasCurrent=new Uint8ClampedArray(n*4),atlasReference=new Uint8ClampedArray(n*4),owner=new Int16Array(n).fill(-1),atlasOwner=new Int16Array(n).fill(-1);
    const atlasLower=cameras.some(c=>c.comparisonBounds)?new Uint8ClampedArray(n*4):null,atlasUpper=atlasLower?new Uint8ClampedArray(n*4):null,lo=[0,0,0],hi=[0,0,0];
    const best=new Float32Array(n),bestCurrent=new Float32Array(n),bestReference=new Float32Array(n),difference=new Float32Array(n),paired=new Uint8Array(n);
    const a=[0,0,0],b=[0,0,0];
    for(const g of cameras) {
      validateGeometry(g);if(!g.enabled)continue;
      const t=g.stitch||[1,0,0,0,1,0],bound=(Math.abs(t[0])+Math.abs(t[1]))*.5,det=t[0]*t[4]-t[1]*t[3];
      for(let x=0;x<width;x++) {
        const px=delta(wrap((x+.5)*360/width+seam),g.center)/g.fov-t[2];if(Math.abs(px)>bound)continue;
        for(let y=0;y<height;y++) {
          const py=(y+.5)/height-.5-t[5],u=.5+(t[4]*px-t[1]*py)/det,v=.5+(-t[3]*px+t[0]*py)/det;
          if(u<0||u>1||v<0||v>1)continue;
          const p=point(g,u,v),i=y*width+x,k=i*4,q=Math.max(.00001,1-Math.abs(2*u-1));
          const sh=g.sourceShift,cp=sh?[p[0]+sh.x/(g.current.width-1),p[1]+sh.y/(g.current.height-1)]:p;
          const has=sample(g.current,cp,a),hasRef=sample(g.reference,p,b);
          const scope=!g.controlMask||g.controlMask[Math.min(g.reference.height-1,Math.round(p[1]*(g.reference.height-1)))*g.reference.width+Math.min(g.reference.width-1,Math.round(p[0]*(g.reference.width-1)))];
          if(has&&g.sourcePhoto)for(let c=0;c<3;c++)a[c]=clamp(a[c]*g.sourcePhoto.gain+g.sourcePhoto.offset,0,255);
          if(q>best[i]) {
            best[i]=q;atlasOwner[i]=g.id;
            for(let c=0;c<3;c++){atlasCurrent[k+c]=has?a[c]:0;atlasReference[k+c]=hasRef?b[c]:0;}
            atlasCurrent[k+3]=has&&scope?255:0;atlasReference[k+3]=hasRef&&scope?255:0;
            if(atlasLower) {
              const lower=g.comparisonBounds?.lower||g.reference,upper=g.comparisonBounds?.upper||g.reference;
              const valid=hasRef&&scope&&sample(lower,p,lo)&&sample(upper,p,hi);
              for(let c=0;c<3;c++){atlasLower[k+c]=lo[c];atlasUpper[k+c]=hi[c];}atlasLower[k+3]=atlasUpper[k+3]=valid?255:0;
            }
          }
          if(has&&q>bestCurrent[i]){bestCurrent[i]=q;owner[i]=g.id;for(let c=0;c<3;c++)current[k+c]=a[c];current[k+3]=255;}
          if(hasRef&&q>bestReference[i]){bestReference[i]=q;for(let c=0;c<3;c++)reference[k+c]=b[c];reference[k+3]=255;}
          if(has&&hasRef&&g.comparable!==false){paired[i]=1;difference[i]=Math.max(difference[i],Math.abs(a[0]-b[0]),Math.abs(a[1]-b[1]),Math.abs(a[2]-b[2]));}
        }
      }
    }
    let covered=0,refCovered=0,compared=0,changed=0;
    for(let i=0;i<n;i++) {
      const k=i*4;if(current[k+3])covered++;if(reference[k+3])refCovered++;
      if(paired[i]){compared++;const hit=difference[i]>=threshold;if(hit)changed++;const gray=(current[k]+current[k+1]+current[k+2])/5;diff[k]=hit?255:gray;diff[k+1]=hit?70:gray;diff[k+2]=hit?40:gray;diff[k+3]=255;}
      const color=paired[i]?[35,166,131]:current[k+3]?[225,161,46]:[87,66,83];coverage[k]=color[0];coverage[k+1]=color[1];coverage[k+2]=color[2];coverage[k+3]=255;
    }
    return {width,height,current,reference,diff,coverage,owner,atlasCurrent,atlasReference,atlasLower,atlasUpper,atlasOwner,stats:{coverage:covered/n,referenceCoverage:refCovered/n,pairedCoverage:compared/n,changedFraction:compared?changed/compared:null},strips:cameras.map(g=>({id:g.id,...rectify(g.current,g)}))};
  }
  const core = {clamp, wrap, delta, curve, point, validateGeometry, validateRecipe, rectify, render,validStitch,toAtlas,fromAtlas,validateFine,fineStitch};
  if (typeof module !== 'undefined' && module.exports) module.exports = core;
  else root.UnwrapCore = core;
})(typeof self !== 'undefined' ? self : globalThis);
