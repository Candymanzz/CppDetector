'use strict';
const assert=require('node:assert/strict'),C=require('../web/unwrap-core.js'),T=require('../web/unwrap-stitching.js'),I=require('../web/unwrap-inspection.js');
const base={fov:120,flip:false,enabled:true,projection:'cylinder',points:[[.1,.1],[.5,.1],[.9,.1],[.1,.9],[.5,.9],[.9,.9]]};
const truth=[[1,0,0,0,1,0],[1.02,.005,.035,.01,.99,.01],[.98,-.003,-.02,-.01,1.01,-.005],[1.03,.004,-.04,.005,.98,.003]];
const cams=[0,1,2,3].map((id,i)=>({...base,id,center:45+i*90})),pairs=[];
for(let i=0;i<4;i++){
 const a=cams[i],b=cams[(i+1)%4],matches=[];
 for(let n=0;n<80;n++){const p=[.78+(n%8)*.025,.12+Math.floor(n/8)*.075],at=C.toAtlas({...a,stitch:truth[a.id]},...p),q=C.fromAtlas({...b,stitch:truth[b.id]},at.angle,at.v);if(q)matches.push({p,q});}
 pairs.push({a:a.id,b:b.id,accepted:true,count:matches.length,candidates:matches.length,matches});
}
const fit=T.fit(C,cams,pairs);assert.equal(fit.complete,true);assert.equal(fit.count,4);assert.ok(fit.components[0].afterError<fit.components[0].beforeError*.3);
for(const cam of cams)for(let n=0;n<20;n++){const g={...cam,stitch:truth[cam.id]},u=.05+.9*n/19,v=.1+.8*(n%7)/6,p=C.toAtlas(g,u,v),q=C.fromAtlas(g,p.angle,p.v);assert.ok(q&&Math.abs(q[0]-u)<1e-12&&Math.abs(q[1]-v)<1e-12);}
const partial=T.fit(C,cams,[pairs[0]]);assert.equal(partial.count,2);assert.equal(partial.complete,false);assert.deepEqual(partial.transforms[2],T.identity());
const none=T.fit(C,cams,pairs.map(p=>({...p,accepted:false})));assert.equal(none.count,0);assert.ok(Object.values(none.transforms).every(t=>JSON.stringify(t)===JSON.stringify(T.identity())));
const image=value=>({width:8,height:8,data:new Uint8ClampedArray(Array.from({length:64},()=>[value,value,value,255]).flat())});
const a={...cams[0],current:image(140),reference:image(100)},b={...cams[0],id:1,current:image(60),reference:image(100)};
const cut=C.render({width:160,height:40,cameras:[a,b],seam:0,threshold:24,cut:true});
for(let i=0;i<cut.current.length;i+=4)if(cut.current[i+3])assert.ok(cut.current[i]===140||cut.current[i]===60,'cut display never averages opposite pixels');
assert.equal(cut.atlasOwner.length,160*40,'atlas owner includes vertical geometry');
const full=C.render({width:240,height:80,cameras:cams.map(c=>({...c,stitch:truth[c.id],current:image(100),reference:image(100)})),seam:359,threshold:24,cut:true});
assert.ok(full.stats.coverage>.95);assert.equal(full.stats.changedFraction,0);
const report=I.inspect({width:full.width,height:full.height,current:full.atlasCurrent,reference:full.atlasReference},{minCoverage:.85,maxShift:8});assert.equal(report.verdict,'OK');
const signatureCams=cams.map(c=>({...c,reference:image(100)})),sig=T.signature(signatureCams);assert.equal(sig,T.signature(signatureCams.map(c=>({...c,current:image(250)}))),'current frames cannot change calibration');
assert.notEqual(sig,T.signature(signatureCams.map(c=>c.id===0?{...c,reference:image(101)}:c)));assert.notEqual(sig,T.signature(signatureCams.map(c=>c.id===0?{...c,fov:121}:c)));
const stored={...fit,signature:sig};T.validate(stored,[0,1,2,3]);assert.throws(()=>T.validate({...stored,transforms:{...stored.transforms,0:[NaN,0,0,0,1,0]}},[0,1,2,3]));
assert.throws(()=>C.validateGeometry({...cams[0],stitch:[1,0,0,0,-1,0]}));assert.throws(()=>C.validateGeometry({...cams[0],fov:170,stitch:[1.5,0,.5,0,1,0]}));
console.log('Stitching: transform inversion, complete/partial/refused graphs, improvement, reference signatures, cut rendering, masks and inspection passed.');
