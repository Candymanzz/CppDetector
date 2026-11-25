/* Position each source before the surface warp. No per-camera defect verdicts. */
(function(root){
  'use strict';
  function mask(width,height,polygons,exclusions=[]) {
    const data=new Uint8Array(width*height).fill(polygons.length?0:1);
    const paint=(polygon,value)=>{
      if(polygon.length<3)return;
      for(let y=0;y<height;y++) {
        const yy=(y+.5)/height,cross=[];
        for(let i=0,j=polygon.length-1;i<polygon.length;j=i++) {
          const a=polygon[j],b=polygon[i];
          if((a[1]>yy)!==(b[1]>yy))cross.push((a[0]+(yy-a[1])*(b[0]-a[0])/(b[1]-a[1]))*width-.5);
        }
        cross.sort((a,b)=>a-b);
        for(let i=0;i+1<cross.length;i+=2)data.fill(value,y*width+Math.max(0,Math.ceil(cross[i])),y*width+Math.min(width,Math.ceil(cross[i+1])));
      }
    };
    polygons.forEach(p=>paint(p,1));exclusions.forEach(p=>paint(p,0));return data;
  }
  function prepare(cameras,settings,I) {
    const sources=[];
    for(const camera of cameras) {
      if(!camera.enabled||!camera.current||!camera.reference||camera.comparable===false)continue;
      const ref=camera.reference,cur=camera.current,scope=camera.control||{},w=ref.width,h=ref.height;
      camera.controlMask=mask(w,h,scope.roi?.length>=3?[scope.roi]:[],scope.exclusions||[]);
      const width=Math.min(600,w),height=Math.round(h*width/w),scale=width/w;
      const masked=image=>{
        const data=image.data.slice();for(let i=0;i<camera.controlMask.length;i++)if(!camera.controlMask[i])data[i*4+3]=0;
        return I.prepare({width:w,height:h,data},width,height);
      };
      const a=masked(ref),b=masked(cur),nativeX=Math.min(scope.maxShiftX??settings.maxShift,settings.maxShift),nativeY=Math.min(scope.maxShiftY??settings.maxShift,settings.maxShift);
      const limit=Math.max(2,Math.ceil(Math.max(nativeX/(scope.nativeWidth||w),nativeY/(scope.nativeHeight||h))*w*scale));
      const shift=I.register(a,b,Math.min(48,limit));
      shift.x/=scale;shift.y/=scale;
      const maxX=nativeX*w/(scope.nativeWidth||w),maxY=nativeY*h/(scope.nativeHeight||h);
      if(Math.abs(shift.x)>maxX||Math.abs(shift.y)>maxY){shift.reliable=false;shift.reason='shift_limit';}
      const photo=I.photometry(a,b,{x:shift.x*scale,y:shift.y*scale},settings.normalize);
      camera.sourceShift=shift;camera.sourcePhoto=photo;
      const tx=Math.min(12,Math.max(0,Math.round((scope.toleranceX||0)*w/(scope.nativeWidth||w)))),ty=Math.min(12,Math.max(0,Math.round((scope.toleranceY||0)*h/(scope.nativeHeight||h))));
      if(settings.cameraTolerance&&(tx||ty))camera.comparisonBounds=envelope(ref,tx,ty);
      sources.push({id:camera.id,frame:camera.name,shift:{...shift,x:shift.x*(scope.nativeWidth||w)/w,y:shift.y*(scope.nativeHeight||h)/h},sampleShift:{x:shift.x,y:shift.y},photometry:photo,tolerance:settings.cameraTolerance?{x:scope.toleranceX||0,y:scope.toleranceY||0}:{x:0,y:0},roi:scope.roi?.length>=3,exclusions:(scope.exclusions||[]).length});
    }
    return sources;
  }
  function envelope(image,rx,ry) {
    const {width:w,height:h,data}=image;
    const pass=(lower,upper,radius,vertical)=>{
      const lo=new Uint8ClampedArray(data.length),hi=new Uint8ClampedArray(data.length),length=vertical?h:w,lines=vertical?w:h,stride=vertical?w*4:4;
      const minQueue=new Int32Array(length),maxQueue=new Int32Array(length);
      for(let line=0;line<lines;line++)for(let c=0;c<3;c++) {
        const base=(vertical?line:line*w)*4+c;let minHead=0,minTail=0,maxHead=0,maxTail=0,next=0;
        for(let p=0;p<length;p++) {
          const right=Math.min(length-1,p+radius),left=Math.max(0,p-radius);
          while(next<=right) {
            const k=base+next*stride;
            while(minTail>minHead&&lower[base+minQueue[minTail-1]*stride]>=lower[k])minTail--;
            while(maxTail>maxHead&&upper[base+maxQueue[maxTail-1]*stride]<=upper[k])maxTail--;
            minQueue[minTail++]=next;maxQueue[maxTail++]=next;next++;
          }
          while(minQueue[minHead]<left)minHead++;while(maxQueue[maxHead]<left)maxHead++;
          lo[base+p*stride]=lower[base+minQueue[minHead]*stride];hi[base+p*stride]=upper[base+maxQueue[maxHead]*stride];
        }
      }
      for(let k=3;k<lo.length;k+=4)lo[k]=hi[k]=255;
      return {lo,hi};
    };
    const horizontal=pass(data,data,rx,false),both=pass(horizontal.lo,horizontal.hi,ry,true);
    return {lower:{width:w,height:h,data:both.lo},upper:{width:w,height:h,data:both.hi}};
  }
  const api={mask,prepare,envelope};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.UnwrapSource=api;
})(typeof self!=='undefined'?self:globalThis);
