'use strict';
const assert=require('assert/strict'),S=require('../web/unwrap-source.js'),C=require('../web/unwrap-core.js'),I=require('../web/unwrap-inspection.js');
const w=256,h=192,shifts=[[4,-3],[-4,3],[6,2],[-5,-2]],settings=I.settings({minCoverage:.97});
const image=id=>{const data=new Uint8ClampedArray(w*h*4);for(let y=0;y<h;y++)for(let x=0;x<w;x++){
  const k=(y*w+x)*4,v=95+28*Math.sin(x*.14+id)+22*Math.cos(y*.19)+18*Math.sin(x*.09+y*.17),box=x%47>15&&x%47<30&&y%35>11&&y%35<25;
  data[k]=v+(box?45:0);data[k+1]=v*.8+(box?25:0);data[k+2]=v*.65;data[k+3]=255;
}return {width:w,height:h,data};};
const translated=(ref,dx,dy)=>{const data=new Uint8ClampedArray(ref.data.length);for(let y=0;y<h;y++)for(let x=0;x<w;x++){
 const xx=Math.max(0,Math.min(w-1,x-dx)),yy=Math.max(0,Math.min(h-1,y-dy));data.set(ref.data.subarray((yy*w+xx)*4,(yy*w+xx)*4+4),(y*w+x)*4);
}return {...ref,data};};
const cameras=()=>[0,1,2,3].map(id=>{const reference=image(id);return {id,center:45+90*id,fov:120,projection:'linear',flip:false,enabled:true,points:[[.02,.02],[.5,.02],[.98,.02],[.02,.98],[.5,.98],[.98,.98]],reference,current:translated(reference,...shifts[id]),comparable:true,control:{nativeWidth:w,nativeHeight:h,maxShiftX:12,maxShiftY:12,toleranceX:3,toleranceY:3,roi:[[.06,.06],[.94,.06],[.94,.94],[.06,.94]],exclusions:[]}};});
function run(cams,opts={}) {
 const cfg=I.settings({...settings,...opts}),sources=S.prepare(cams,cfg,I),r=C.render({width:480,height:160,seam:0,threshold:24,cut:true,cameras:cams});
 return {r,report:I.inspect({width:r.width,height:r.height,current:r.atlasCurrent,reference:r.atlasReference,lower:r.atlasLower,upper:r.atlasUpper,prealigned:true,sources},cfg)};
}
const normal=run(cameras());assert.equal(normal.report.verdict,'OK');assert.equal(normal.report.regionsCount,0);assert.equal(normal.report.shift.x,0);assert.ok(normal.report.scopeFraction<.9);assert.ok(normal.report.coverage>.99);
assert.equal(normal.report.shift.reason,'registered');
for(const s of normal.report.sources){assert.ok(s.shift.reliable);assert.ok(Math.abs(s.shift.x-shifts[s.id][0])<.6);assert.ok(Math.abs(s.shift.y-shifts[s.id][1])<.6);}
function spot(cam,x,y,size=22) {for(let yy=y;yy<y+size;yy++)for(let xx=x;xx<x+size;xx++)cam.current.data.set([10,240,10,255],(yy*w+xx)*4);}
const interior=cameras();spot(interior[0],105,80);const bad=run(interior);assert.equal(bad.report.verdict,'REJECT');assert.ok(bad.report.regionsCount>0);
// x at the 90 degree ownership boundary, not a discarded seam band.
const seam=cameras();spot(seam[0],216,80,20);assert.equal(run(seam).report.verdict,'REJECT','defect adjacent to a camera join survives');
const outside=cameras();spot(outside[0],110,0,9);assert.equal(run(outside).report.verdict,'OK','outside reference ROI stays outside control');
const excluded=cameras();excluded[0].control.exclusions=[[[.35,.3],[.7,.3],[.7,.7],[.35,.7]]];spot(excluded[0],110,80);assert.equal(run(excluded).report.verdict,'OK','explicit native exclusion respected');
const lost=cameras();lost[0].current.data.fill(170);for(let i=3;i<lost[0].current.data.length;i+=4)lost[0].current.data[i]=255;assert.equal(run(lost).report.verdict,'NOT_INSPECTED','lost positioning cannot pass');
const absent=cameras();absent[0].current=null;const no=run(absent);assert.equal(no.report.verdict,'NOT_INSPECTED','missing owned sector cannot pass');
const empty=cameras();empty.forEach(c=>c.control.exclusions=[[[0,0],[1,0],[1,1],[0,1]]]);assert.equal(run(empty).report.verdict,'NOT_INSPECTED','empty working area cannot pass');
const small={width:8,height:8,data:new Uint8ClampedArray(8*8*4).fill(100)};for(let i=3;i<small.data.length;i+=4)small.data[i]=255;small.data[(4*8+4)*4]=200;
const e=S.envelope(small,1,2);assert.equal(e.upper.data[(2*8+3)*4],200);assert.equal(e.upper.data[(1*8+3)*4],100);assert.equal(e.lower.data[(4*8+4)*4],100);
assert.throws(()=>I.settings({cameraTolerance:'yes'}));
const holes=cameras();for(let y=40;y<150;y++)for(let x=65;x<190;x++)holes[0].current.data[(y*w+x)*4+3]=0;assert.equal(run(holes).report.verdict,'NOT_INSPECTED','missing image pixels cannot be counted as inspected');
const fine={...cameras()[0],fine:{x:2,y:4,scaleX:1.03,scaleY:.98}},base=[1,.02,.01,.01,1,.02],matrix=C.fineStitch(fine,base);
assert.ok(C.validStitch(matrix));assert.ok(Math.abs(matrix[2]-(base[2]*1.03+2/120))<1e-10);assert.ok(Math.abs(matrix[5]-(base[5]*.98+4/312))<1e-10);
for(const [u,v] of [[.1,.2],[.5,.5],[.9,.8]]){const g={...fine,stitch:matrix},p=C.toAtlas(g,u,v),uv=C.fromAtlas(g,p.angle,p.v);assert.ok(Math.abs(uv[0]-u)<1e-10);assert.ok(Math.abs(uv[1]-v)<1e-10);}
assert.throws(()=>C.validateGeometry({...fine,fine:{...fine.fine,scaleX:5}}));assert.throws(()=>C.validateGeometry({...fine,fine:{...fine.fine,y:NaN}}));
// Compare the linear-time min/max implementation with an independent brute-force oracle.
const oracle={width:13,height:9,data:Uint8ClampedArray.from({length:13*9*4},(_,i)=>i%4===3?255:(i*73+i*i*17)%251)};
for(const [rx,ry] of [[0,0],[1,2],[3,1],[12,12]]) {
 const b=S.envelope(oracle,rx,ry);
 for(let y=0;y<9;y++)for(let x=0;x<13;x++)for(let c=0;c<3;c++){
  let low=255,high=0;for(let yy=Math.max(0,y-ry);yy<=Math.min(8,y+ry);yy++)for(let xx=Math.max(0,x-rx);xx<=Math.min(12,x+rx);xx++){const v=oracle.data[(yy*13+xx)*4+c];low=Math.min(low,v);high=Math.max(high,v);}
  assert.equal(b.lower.data[(y*13+x)*4+c],low);assert.equal(b.upper.data[(y*13+x)*4+c],high);
 }
}
console.log('Source registration: independent camera shifts, fixed joins, ROI/exclusions, interior/join defects, missing/empty/unreliable control and asymmetric envelope passed.');
