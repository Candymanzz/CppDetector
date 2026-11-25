'use strict';
const assert=require('node:assert/strict');
const C=require('../web/unwrap-core.js');
const image=(r,g,b)=>({width:8,height:8,data:new Uint8ClampedArray(Array.from({length:64},()=>[r,g,b,255]).flat())});
const base={id:0,center:45,fov:120,projection:'cylinder',flip:false,enabled:true,points:[[.1,.1],[.5,.1],[.9,.1],[.1,.9],[.5,.9],[.9,.9]]};
let checks=0;const eq=(a,b)=>{checks++;assert.deepEqual(a,b);},ok=v=>{checks++;assert.ok(v);};
C.validateGeometry(base);
eq(C.point(base,0,0),[.1,.1]);eq(C.point(base,1,1),[.9,.9]);
eq(C.point(base,.5,.5),[.5,.5]);
eq(C.point({...base,points:[...base.points].reverse()},0,0),[.9,.9]);
ok(C.point(base,.25,.5)[0]<C.point({...base,projection:'linear'},.25,.5)[0]);
ok(Math.abs(C.point({...base,flip:true},.25,.5)[0]+C.point(base,.25,.5)[0]-1)<1e-12);
eq(C.delta(2,358),4);eq(C.delta(358,2),-4);eq(C.wrap(-30),330);
for(const broken of [{...base,fov:0},{...base,center:Infinity},{...base,points:[[.1,.1],[.5,.1],[.9,.1],[.9,.9],[.5,.9],[.1,.9]]},{...base,points:base.points.map(()=>[.5,.5])}]) {
  checks++;assert.throws(()=>C.validateGeometry(broken));
}
const cams=[0,1,2,3].map(id=>({...base,id,center:45+id*90,current:image(100,80,60),reference:image(100,80,60)}));
const job={width:160,height:40,cameras:cams,seam:0,threshold:20};
const same=C.render(job);eq(same.stats.coverage,1);eq(same.stats.pairedCoverage,1);eq(same.stats.changedFraction,0);
eq(new Set(same.owner),new Set([0,1,2,3]));
for(let i=0;i<same.current.length;i+=4){ok(same.current[i]===100&&same.current[i+1]===80&&same.current[i+2]===60&&same.current[i+3]===255);}
const missing=C.render({...job,cameras:cams.map((c,i)=>i===0?{...c,current:null}:c)});
ok(missing.stats.coverage<1);ok(missing.current.some((v,i)=>i%4===3&&v===0));ok(missing.owner.includes(-1));
const missingRef=C.render({...job,cameras:cams.map(c=>({...c,reference:null}))});
eq(missingRef.stats.coverage,1);eq(missingRef.stats.pairedCoverage,0);eq(missingRef.stats.changedFraction,null);eq(new Set(missingRef.diff),new Set([0]));
const wrongSize=C.render({...job,cameras:cams.map(c=>({...c,comparable:false}))});eq(wrongSize.stats.pairedCoverage,0);
const changed=C.render({...job,cameras:cams.map((c,i)=>i===0?{...c,current:image(170,80,60)}:c)});
ok(changed.stats.changedFraction>0);ok(changed.stats.changedFraction<.5);
// Opposite deviations in an overlap must not disappear through averaging.
const cancel=C.render({...job,cameras:[{...cams[0],current:image(140,80,60)},{...cams[0],id:1,current:image(60,80,60)}]});
eq(cancel.stats.changedFraction,1);
const seam=C.render({...job,seam:359});eq(seam.stats.coverage,1);eq(seam.stats.changedFraction,0);
const none=C.render({...job,cameras:cams.map(c=>({...c,enabled:false}))});eq(none.stats.coverage,0);eq(none.stats.changedFraction,null);
const recipe={version:1,bucket:'bucket1',width:2000,seam:0,threshold:24,cameras:cams.map(({current,reference,...g})=>g)};
C.validateRecipe(recipe,[0,1,2,3]);
for(const bad of [{...recipe,width:100000},{...recipe,cameras:[...recipe.cameras.slice(1),recipe.cameras[1]]},{...recipe,seam:NaN}]){checks++;assert.throws(()=>C.validateRecipe(bad,[0,1,2,3]));}
const strip=C.rectify(image(30,40,50),base,16,8);eq(strip.width,16);eq(strip.data[0],30);eq(strip.data[3],255);
console.log(`Unwrap geometry, provenance, coverage and comparison: ${checks} assertions passed`);
