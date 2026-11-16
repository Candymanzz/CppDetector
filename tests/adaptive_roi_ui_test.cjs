'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const web=path.resolve(process.argv[2]||path.join(__dirname,'../web')),elements=new Map(),requests=[];
function element(){return {value:'',textContent:'',style:{},dataset:{},width:640,height:480,children:[],listeners:{},classList:{toggle(){},contains(){return false;}},
 addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);},append(...v){this.children.push(...v);},replaceChildren(...v){this.children=v;},replaceChild(){},
 querySelector(){return element();},getContext(){return new Proxy({},{get:()=>()=>{}});},getBoundingClientRect(){return {left:0,top:0,width:640,height:480};},setPointerCapture(){}};}
const document={getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},querySelectorAll(){return [];},createElement:element,createTextNode:t=>t,documentElement:element()};
const camera={frames:['first','next'],reference:'first',width:640,height:480,roi:[],seam:{enabled:false,pts:[],skip:[]},adaptive_roi_enabled:false,adaptive_roi_max_motion:96,adaptive_roi_min_coverage:.7};
const state={inspection_engine:'cpp-detector-eefefce4',engine_source_commit:'eefefce455ea93a2184c8d4e9178a0f1eb45847a',cameras_info:Array.from({length:10},()=>structuredClone(camera))};
let fail=false;
const context=vm.createContext({document,console,window:{addEventListener(){}},location:{hash:''},setTimeout(){return 1;},clearTimeout(){},setInterval(){return 1;},getComputedStyle(){return {getPropertyValue(){return '';}};},
 Image:class{constructor(){this.naturalWidth=640;this.naturalHeight=480;}set src(s){this.onload?.();}},
 async fetch(route,opts){requests.push({route,opts});const failed=fail&&route==='/api/camera?cam=0';const data=route==='/api/state'?state:route==='/api/testfiles'?[]:route.startsWith('/api/status')?{last:[],running:false,period_ms:1000,batch:0,budget_ms:500}:{cases:[],error:'failed'};return {ok:!failed,status:failed?400:200,async json(){return data;}};}});
(async()=>{
 for(const file of ['app.js','learning.js'])vm.runInContext(fs.readFileSync(path.join(web,file),'utf8'),context);
 for(let i=0;i<4;i++)await new Promise(setImmediate);
 const e=id=>document.getElementById(id);
 assert.equal(e('adaptiveRoiEnabled').checked,false);e('adaptiveRoiEnabled').checked=true;await e('adaptiveRoiEnabled').listeners.change[0]();
 assert.deepEqual(JSON.parse(requests.at(-1).opts.body),{adaptive_roi_enabled:true});
 vm.runInContext('selectCam(1)',context);assert.equal(e('adaptiveRoiEnabled').checked,false);
 vm.runInContext('selectCam(0)',context);assert.equal(e('adaptiveRoiEnabled').checked,true);
 fail=true;e('adaptiveRoiEnabled').checked=false;await e('adaptiveRoiEnabled').listeners.change[0]();fail=false;
 assert.equal(e('adaptiveRoiEnabled').checked,true,'failed save restores checkbox');
 vm.runInContext('renderResult({cam:0,ver:1,frame:"next",score:0,threshold:60,sensitivity:.5,verdict:"NOT_INSPECTED",reject:true,control_complete:false,reason:"adaptive_low_coverage",adaptive_roi:{enabled:true,coverage:.6,min_coverage:.7,inliers:20,features:100,residual_px:.5,ms:4},zones:[],learning_bbox:{x:10,y:10,width:20,height:20}})',context);
 assert.equal(e('learnFrame').disabled,true,'adaptive results cannot be learned');
 const texts=node=>[String(node?.textContent||''),...(node?.children||[]).flatMap(texts)];
 assert.ok(texts(e('result')).some(t=>t.includes('Контроль не выполнен')));
 assert.ok(texts(e('result')).some(t=>t.includes('60.0%')),'coverage is visible');
 console.log('Adaptive ROI UI: toggle, camera isolation, failure rollback, incomplete verdict, coverage and learning guard passed (mock DOM)');
})().catch(error=>{console.error(error);process.exitCode=1;});
