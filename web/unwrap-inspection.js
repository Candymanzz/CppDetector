/* Experimental inspection of a complete, unblended surface atlas. No native detector state. */
(function(root) {
  'use strict';
  const defaults = Object.freeze({maxShift:24,threshold:24,minArea:18,rejectPercent:0.05,minCoverage:0.97,normalize:true,tolerance:0,cameraTolerance:true});
  const clamp=(v,a,b)=>Math.max(a,Math.min(b,v)), mod=(x,w)=>(x%w+w)%w;
  function settings(value={}) {
    const s={...defaults,...value};
    for(const [key,min,max] of [['maxShift',2,48],['threshold',1,120],['minArea',1,5000],['rejectPercent',0.001,20],['minCoverage',0.8,1],['tolerance',0,2]])
      if(!Number.isFinite(s[key])||s[key]<min||s[key]>max)throw Error('Некорректный параметр инспекции: '+key);
    for(const key of ['maxShift','minArea','tolerance'])if(!Number.isInteger(s[key]))throw Error('Параметр должен быть целым: '+key);
    if(typeof s.normalize!=='boolean')throw Error('Некорректная настройка компенсации яркости');
    if(typeof s.cameraTolerance!=='boolean')throw Error('Некорректная настройка допусков камер');
    return s;
  }

  function read(image,x,y,out) {
    const {width:w,height:h,data:d}=image;
    if(y<0||y>h-1)return false;
    x=mod(x,w);const x0=Math.floor(x),x1=(x0+1)%w,y0=Math.floor(y),y1=Math.min(h-1,y0+1),dx=x-x0,dy=y-y0;
    const a=(y0*w+x0)*4,b=(y0*w+x1)*4,c=(y1*w+x0)*4,e=(y1*w+x1)*4;
    if(d[a+3]<254||d[b+3]<254||d[c+3]<254||d[e+3]<254)return false;
    for(let k=0;k<3;k++)out[k]=(d[a+k]*(1-dx)+d[b+k]*dx)*(1-dy)+(d[c+k]*(1-dx)+d[e+k]*dx)*dy;
    return true;
  }

  function resize(image,width,height) {
    const data=new Float32Array(width*height*4),rgb=[0,0,0];
    for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
      const sx=(x+.5)*image.width/width-.5,sy=clamp((y+.5)*image.height/height-.5,0,image.height-1),k=(y*width+x)*4;
      if(read(image,sx,sy,rgb)){data[k]=rgb[0];data[k+1]=rgb[1];data[k+2]=rgb[2];data[k+3]=255;}
    }
    return {width,height,data};
  }

  function blur(image) {
    const {width:w,height:h,data:d}=image,result=new Float32Array(d.length);
    for(let y=1;y<h-1;y++)for(let x=0;x<w;x++) {
      const k=(y*w+x)*4;let valid=true;
      for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++) {
        const q=((y+dy)*w+mod(x+dx,w))*4;if(d[q+3]<254){valid=false;continue;}
        for(let c=0;c<3;c++)result[k+c]+=d[q+c]/9;
      }
      if(valid)result[k+3]=255;else result[k]=result[k+1]=result[k+2]=0;
    }
    return {width:w,height:h,data:result};
  }

  function gradient(image) {
    const {width:w,height:h,data:d}=image,g=new Float32Array(w*h),valid=new Uint8Array(w*h),gray=new Float32Array(w*h);
    for(let i=0;i<gray.length;i++)gray[i]=.299*d[i*4]+.587*d[i*4+1]+.114*d[i*4+2];
    for(let y=1;y<h-1;y++)for(let x=0;x<w;x++) {
      const i=y*w+x,l=y*w+mod(x-1,w),r=y*w+(x+1)%w;
      if([i,l,r,i-w,i+w].some(k=>d[k*4+3]<254))continue;
      valid[i]=1;g[i]=Math.min(100,Math.hypot(gray[r]-gray[l],gray[i+w]-gray[i-w]));
    }
    return {width:w,height:h,data:g,valid};
  }

  function correlation(a,b,dx,dy) {
    const w=a.width,h=a.height;let n=0,sa=0,sb=0,saa=0,sbb=0,sab=0;
    // Sparse samples keep the search bounded; its finest level still uses the canonical raster.
    const step=w>=600?2:1;
    for(let y=2;y<h-2;y+=step) {
      const yy=y+dy;if(yy<2||yy>=h-2)continue;
      for(let x=0;x<w;x+=step) {
        const i=y*w+x,j=yy*w+mod(x+dx,w);if(!a.valid[i]||!b.valid[j])continue;
        const va=a.data[i],vb=b.data[j];n++;sa+=va;sb+=vb;saa+=va*va;sbb+=vb*vb;sab+=va*vb;
      }
    }
    const va=saa-sa*sa/Math.max(n,1),vb=sbb-sb*sb/Math.max(n,1);
    if(n<100||va/n<2||vb/n<2)return -1;
    return (sab-sa*sb/n)/Math.sqrt(va*vb);
  }

  function register(reference,current,maxShift) {
    // An exact self comparison is well-defined even for a textureless reference.
    let equal=true,valid=0;
    for(let k=0;k<reference.data.length;k+=4) {
      if(reference.data[k+3]!==current.data[k+3]){equal=false;break;}
      if(reference.data[k+3]<254)continue;valid++;
      if(Math.abs(reference.data[k]-current.data[k])>.001||Math.abs(reference.data[k+1]-current.data[k+1])>.001||Math.abs(reference.data[k+2]-current.data[k+2])>.001){equal=false;break;}
    }
    if(equal&&valid)return {x:0,y:0,confidence:1,reliable:true,method:'identical'};
    let candidates=[{x:0,y:0,score:-1}], finalA,finalB;
    for(const scale of [4,2,1]) {
      const w=Math.max(16,Math.round(reference.width/scale)),h=Math.max(8,Math.round(reference.height/scale));
      const a=gradient(resize(reference,w,h)),b=gradient(resize(current,w,h)),limit=Math.ceil(maxShift/scale),tested=new Map();
      const visit=(x,y)=>{
        if(Math.abs(x)>limit||Math.abs(y)>limit)return;
        const key=x+','+y;if(tested.has(key))return;
        tested.set(key,{x,y,score:correlation(a,b,x,y)});
      };
      if(scale===4)for(let y=-limit;y<=limit;y++)for(let x=-limit;x<=limit;x++)visit(x,y);
      else for(const seed of candidates)for(let y=-2;y<=2;y++)for(let x=-2;x<=2;x++)visit(seed.x*2+x,seed.y*2+y);
      const list=[...tested.values()].sort((a,b)=>b.score-a.score);candidates=[];
      for(const p of list)if(candidates.every(q=>Math.hypot(p.x-q.x,p.y-q.y)>2)){candidates.push(p);if(candidates.length===3)break;}
      if(!candidates.length)candidates=[{x:0,y:0,score:-1}];
      if(scale===1){finalA=a;finalB=b;}
    }
    const best=candidates[0],sub=(left,middle,right)=>{const den=left-2*middle+right;return den<-.00001?clamp(.5*(left-right)/den,-.5,.5):0;};
    const fx=sub(correlation(finalA,finalB,best.x-1,best.y),best.score,correlation(finalA,finalB,best.x+1,best.y));
    const fy=sub(correlation(finalA,finalB,best.x,best.y-1),best.score,correlation(finalA,finalB,best.x,best.y+1));
    const atLimit=Math.abs(best.x)>=maxShift||Math.abs(best.y)>=maxShift;
    const ambiguous=candidates.slice(1).some(c=>Math.hypot(c.x-best.x,c.y-best.y)>4&&best.score-c.score<.015);
    const reliable=best.score>=.55&&!atLimit&&!ambiguous;
    return {x:best.x+fx,y:best.y+fy,confidence:best.score,reliable,
      method:'gradient_ncc',reason:reliable?'registered':atLimit?'shift_limit':ambiguous?'ambiguous_registration':'registration_unreliable'};
  }

  function photometry(reference,current,shift,enabled) {
    if(!enabled)return {gain:1,offset:0,enabled:false};
    const samples=[],rgb=[0,0,0],{width:w,height:h,data:d}=reference;
    for(let y=2;y<h-2;y+=3)for(let x=0;x<w;x+=3) {
      const k=(y*w+x)*4;if(d[k+3]<254||!read(current,x+shift.x,y+shift.y,rgb))continue;
      samples.push([.299*rgb[0]+.587*rgb[1]+.114*rgb[2],.299*d[k]+.587*d[k+1]+.114*d[k+2]]);
    }
    let gain=1,offset=0;
    if(samples.length<100)return {gain,offset,enabled:true};
    const median=a=>{a.sort((a,b)=>a-b);return a[Math.floor(a.length/2)];};
    offset=clamp(median(samples.map(p=>p[1]-p[0])),-15,15);
    for(let pass=0;pass<3;pass++) {
      const residuals=samples.map(p=>Math.abs(p[1]-gain*p[0]-offset)),cut=Math.max(6,median(residuals)*2.5);
      let n=0,sx=0,sy=0,sxx=0,sxy=0;
      for(const [x,y] of samples)if(Math.abs(y-gain*x-offset)<=cut){n++;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;}
      const den=sxx-sx*sx/Math.max(n,1);
      if(n<100||den/n<20)break;
      gain=clamp((sxy-sx*sy/n)/den,.85,1.15);offset=clamp((sy-gain*sx)/n,-15,15);
    }
    return {gain,offset,enabled:true};
  }

  function regions(mask,difference,width,height,minArea) {
    const visited=new Uint8Array(mask.length),queue=new Int32Array(mask.length),kept=new Uint8Array(mask.length),found=[];
    let defectPixels=0,total=0;
    for(let start=0;start<mask.length;start++) {
      if(!mask[start]||visited[start])continue;
      let head=0,tail=1,minY=height,maxY=0,maximum=0,sumY=0;const columns=new Uint8Array(width);queue[0]=start;visited[start]=1;
      while(head<tail) {
        const i=queue[head++],x=i%width,y=Math.floor(i/width);columns[x]=1;minY=Math.min(minY,y);maxY=Math.max(maxY,y);sumY+=y;maximum=Math.max(maximum,difference[i]);
        for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++) {
          const yy=y+dy;if(yy<0||yy>=height)continue;const next=yy*width+mod(x+dx,width);
          if(mask[next]&&!visited[next]){visited[next]=1;queue[tail++]=next;}
        }
      }
      if(tail<minArea)continue;
      total++;defectPixels+=tail;for(let j=0;j<tail;j++)kept[queue[j]]=1;
      // Minimum circular interval yields two boxes when one defect crosses the 0/360 seam.
      let gap=0,bestGap=0,end=0;
      for(let x=0;x<width*2;x++){if(!columns[x%width]){gap=Math.min(width,gap+1);if(gap>bestGap){bestGap=gap;end=x;}}else gap=0;}
      const x=mod(end+1,width),span=width-bestGap,boxHeight=maxY-minY+1;
      const boxes=x+span<=width?[{x,y:minY,width:span,height:boxHeight}]:[{x,y:minY,width:width-x,height:boxHeight},{x:0,y:minY,width:x+span-width,height:boxHeight}];
      found.push({area:tail,boxes,center:{x:mod(x+span/2,width),y:sumY/tail},maxDifference:maximum});
    }
    found.sort((a,b)=>b.area-a.area);
    return {kept,defectPixels,regionsCount:total,regions:found.slice(0,50).map((r,i)=>({...r,id:i+1}))};
  }

  function inspect(input,options={}) {
    const s=settings(options),width=Math.min(1200,input.width),height=Math.round(input.height*width/input.width);
    if(!Number.isInteger(input.width)||!Number.isInteger(input.height)||input.width<32||input.height<16||input.current?.length!==input.width*input.height*4||input.reference?.length!==input.current.length)
      throw Error('Некорректные изображения развёртки для инспекции');
    const ref=blur(resize({width:input.width,height:input.height,data:input.reference},width,height));
    const cur=blur(resize({width:input.width,height:input.height,data:input.current},width,height));
    const bounds=s.cameraTolerance&&input.lower&&input.upper?['lower','upper'].map(key=>blur(resize({width:input.width,height:input.height,data:input[key]},width,height))):null;
    const sourceReliable=(input.sources||[]).every(c=>c.shift.reliable);
    const shift=input.prealigned?{x:0,y:0,confidence:Math.min(...(input.sources||[]).map(c=>c.shift.confidence),1),reliable:sourceReliable,method:'per_source',reason:sourceReliable?'registered':'source_registration_unreliable'}:register(ref,cur,s.maxShift);
    const photo=input.prealigned?{gain:1,offset:0,enabled:s.normalize,method:'per_source'}:photometry(ref,cur,shift,s.normalize),n=width*height;
    const mask=new Uint8Array(n),difference=new Float32Array(n),valid=new Uint8Array(n);
    const aligned=new Uint8ClampedArray(n*4),reference=new Uint8ClampedArray(n*4),overlay=new Uint8ClampedArray(n*4),rgb=[0,0,0];
    let paired=0;
    for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
      const i=y*width+x,k=i*4;
      if(ref.data[k+3]>=254){for(let c=0;c<3;c++)reference[k+c]=ref.data[k+c];reference[k+3]=255;}
      if(!read(cur,x+shift.x,y+shift.y,rgb))continue;
      for(let c=0;c<3;c++)aligned[k+c]=clamp(rgb[c]*photo.gain+photo.offset,0,255);aligned[k+3]=255;
      if(ref.data[k+3]<254)continue;
      valid[i]=1;paired++;
      let value=Math.max(...rgb.map((v,c)=>Math.abs(clamp(v*photo.gain+photo.offset,0,255)-ref.data[k+c])));
      if(bounds&&bounds[0].data[k+3]>=254&&bounds[1].data[k+3]>=254)value=Math.max(0,...rgb.map((v,c)=>Math.max(bounds[0].data[k+c]-clamp(v*photo.gain+photo.offset,0,255),clamp(v*photo.gain+photo.offset,0,255)-bounds[1].data[k+c])));
      if(s.tolerance>0) {
        let forward=value,backward=value;const here=[...rgb],other=[0,0,0];
        for(let dy=-s.tolerance;dy<=s.tolerance;dy++)for(let dx=-s.tolerance;dx<=s.tolerance;dx++) {
          const yy=y+dy;if(yy<0||yy>=height)continue;const q=(yy*width+mod(x+dx,width))*4;
          if(ref.data[q+3]>=254)forward=Math.min(forward,Math.max(...here.map((v,c)=>Math.abs(clamp(v*photo.gain+photo.offset,0,255)-ref.data[q+c]))));
          if(read(cur,x+shift.x+dx,y+shift.y+dy,other))backward=Math.min(backward,Math.max(...other.map((v,c)=>Math.abs(clamp(v*photo.gain+photo.offset,0,255)-ref.data[k+c]))));
        }
        value=Math.max(forward,backward);
      }
      difference[i]=value;if(value>=s.threshold)mask[i]=1;
    }
    const components=regions(mask,difference,width,height,s.minArea);
    let expected=0;for(let i=0;i<n;i++)if(ref.data[i*4+3]>=254)expected++;
    const coverage=input.prealigned?(expected>=64?paired/expected:0):paired/n,areaPercent=paired?components.defectPixels*100/paired:null;
    const issues=Array.isArray(input.issues)?input.issues:[];
    const reason=issues.length?'required_inputs_missing':coverage<s.minCoverage?'insufficient_coverage':!shift.reliable?shift.reason:areaPercent>=s.rejectPercent?'defects_over_threshold':'within_threshold';
    const controlComplete=!issues.length&&coverage>=s.minCoverage&&shift.reliable;
    for(let i=0;i<n;i++)if(valid[i]) {
      const k=i*4,gray=(aligned[k]+aligned[k+1]+aligned[k+2])/5;
      overlay[k]=components.kept[i]?255:gray;overlay[k+1]=components.kept[i]?65:gray;overlay[k+2]=components.kept[i]?35:gray;overlay[k+3]=255;
    }
    return {verdict:controlComplete?(areaPercent>=s.rejectPercent?'REJECT':'OK'):'NOT_INSPECTED',reason,controlComplete,coverage,areaPercent,
      defectPixels:components.defectPixels,regionsCount:components.regionsCount,regions:components.regions,regionsTruncated:components.regionsCount>50,shift:{...shift,angle:shift.x*360/width},photometry:photo,
      issues,settings:s,width,height,scopeFraction:expected/n,sources:input.sources||[],rasters:{inspection:{width,height,data:overlay},aligned:{width,height,data:aligned},controlReference:{width,height,data:reference}}};
  }
  const api={defaults,settings,inspect,register,regions,prepare:(image,w=image.width,h=image.height)=>blur(resize(image,w,h)),photometry};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.UnwrapInspection=api;
})(typeof self!=='undefined'?self:globalThis);
