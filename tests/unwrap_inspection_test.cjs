'use strict';
const assert=require('node:assert/strict'),fs=require('fs'),path=require('path');
const I=require('../web/unwrap-inspection.js'),C=require('../web/unwrap-core.js');
const w=240,h=80,n=w*h,wrap=(x,v)=>(x%v+v)%v;
function texture(){const d=new Uint8ClampedArray(n*4);for(let y=0;y<h;y++)for(let x=0;x<w;x++){
 const k=(y*w+x)*4,v=85+35*Math.sin(x*.13)+26*Math.cos(y*.31)+20*Math.sin(x*.07+y*.21),box=x%57>14&&x%57<35&&y%41>8&&y%41<22;
 d[k]=v+(box?65:0);d[k+1]=v*.8+(box?35:0);d[k+2]=v*.65+(box?-20:0);d[k+3]=255;
}return d;}
const ref=texture();
function shifted(dx,dy,gain=1,offset=0){const d=new Uint8ClampedArray(n*4);for(let y=0;y<h;y++)for(let x=0;x<w;x++){
 const k=(y*w+x)*4,q=(wrap(y-dy,h)*w+wrap(x-dx,w))*4;for(let c=0;c<3;c++)d[k+c]=ref[q+c]*gain+offset;d[k+3]=255;
}return d;}
function spot(d,x,y,width,height,color=[15,220,15]){for(let yy=y;yy<y+height;yy++)for(let xx=x;xx<x+width;xx++){const k=(yy*w+wrap(xx,w))*4;for(let c=0;c<3;c++)d[k+c]=color[c];}return d;}
const cfg={minCoverage:.9,maxShift:16,threshold:24,minArea:12,rejectPercent:.05};
const observed=[];
function check(name,current,expect,options={},extra={}){const r=I.inspect({width:w,height:h,current,reference:ref,...extra},{...cfg,...options});assert.equal(r.verdict,expect,name+': '+JSON.stringify({...r,rasters:undefined}));observed.push({name,verdict:r.verdict,reason:r.reason,coverage:r.coverage,areaPercent:r.areaPercent,regions:r.regionsCount,shift:r.shift,photometry:r.photometry});return r;}
const same=check('identical atlas',ref.slice(),'OK');assert.equal(same.areaPercent,0);assert.equal(same.shift.x,0);
const move=check('circular translation and brightness',shifted(5,-2,1.08,8),'OK');assert.ok(Math.abs(move.shift.x-5)<.4);assert.ok(Math.abs(move.shift.y+2)<.4);assert.ok(Math.abs(move.photometry.gain-1/1.08)<.04);
check('translation without brightness correction',shifted(5,-2,1.08,8),'REJECT',{normalize:false,threshold:12});
const bad=check('new coloured defect on the atlas',spot(ref.slice(),94,35,18,14),'REJECT');assert.ok(bad.regionsCount>=1);assert.ok(bad.defectPixels>=150);
const movedBad=check('defect survives alignment and lighting compensation',spot(shifted(5,-2,1.08,8),99,33,18,14),'REJECT');assert.ok(Math.abs(movedBad.shift.x-5)<.5);
assert.equal(I.inspect({width:w,height:h,current:spot(shifted(5,-2,1.08,8),99,33,18,14),reference:ref},{...cfg,tolerance:1}).verdict,'REJECT','one-pixel relaxation retains the controlled defect');
const seam=check('one defect across circular seam',spot(ref.slice(),-5,35,10,14),'REJECT');assert.equal(seam.regionsCount,1);assert.equal(seam.regions[0].boxes.length,2);
check('subthreshold dust filtered by area',spot(ref.slice(),110,40,1,1,[0,0,0]),'OK',{minArea:30,threshold:10});
const missing=ref.slice();for(let y=0;y<h;y++)for(let x=0;x<40;x++)missing[(y*w+x)*4+3]=0;
check('missing angular sector prevents success',missing,'NOT_INSPECTED');
check('missing required camera prevents success despite raster overlap',ref.slice(),'NOT_INSPECTED',{}, {issues:['camera 0 missing']});
check('absent reference cannot pass',ref.slice(),'NOT_INSPECTED',{}, {reference:new Uint8ClampedArray(n*4)});
const blank=new Uint8ClampedArray(n*4);for(let i=0;i<n;i++){blank[i*4]=blank[i*4+1]=blank[i*4+2]=200;blank[i*4+3]=255;}
check('lost printing or unsupported alignment cannot pass',blank,'NOT_INSPECTED');
check('shift at search limit cannot pass',shifted(16,0),'NOT_INSPECTED');
check('large brightness loss is not normalised away',shifted(0,0,.4,-10),'REJECT');
const sparse=new Uint8ClampedArray(n),delta=new Float32Array(n).fill(100);sparse[20*w]=1;sparse[20*w+w-1]=1;
assert.equal(I.regions(sparse,delta,w,h,1).regionsCount,1,'connected components wrap horizontally');
const rgb=value=>({width:8,height:8,data:new Uint8ClampedArray(Array.from({length:64},()=>[value,value,value,255]).flat())});
const g={id:0,center:45,fov:120,projection:'cylinder',flip:false,enabled:true,points:[[.1,.1],[.5,.1],[.9,.1],[.1,.9],[.5,.9],[.9,.9]]};
const mixed=C.render({width:160,height:40,seam:0,threshold:20,cameras:[{...g,current:rgb(140),reference:rgb(100)},{...g,id:1,current:rgb(60),reference:rgb(100)}]});
assert.ok(mixed.current.some((v,k)=>k%4===0&&v===100),'visual panorama blends opposite differences');
assert.ok(mixed.atlasCurrent.some((v,k)=>k%4===0&&v===140),'inspection atlas retains a real camera sample');
assert.ok(!mixed.atlasCurrent.some((v,k)=>k%4===0&&v===100),'inspection atlas never averages a defect away');
for(const badOptions of [{threshold:0},{normalize:'yes'},{minArea:.5},{maxShift:1000},{minCoverage:.1}])assert.throws(()=>I.settings(badOptions));
if(process.argv[2])fs.writeFileSync(process.argv[2],JSON.stringify(observed,null,2));
console.log('Atlas inspection: 13 image scenarios, alignment/photometry, circular regions, no-blend support and settings validation passed.');
