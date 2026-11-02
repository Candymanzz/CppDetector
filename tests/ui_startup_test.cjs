'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const web=path.resolve(process.argv[2]||path.join(__dirname,'../web'));
const elements=new Map(),requests=[];
function element(){return {value:'',textContent:'',style:{},dataset:{},width:384,height:320,children:[],listeners:{},classList:{toggle(){},contains(){return false;}},
 addEventListener(type,fn,capture){(this.listeners[type]??=[]).push({fn,capture});},append(...v){this.children.push(...v);},replaceChildren(...v){this.children=v;},replaceChild(){},
 querySelector(){return element();},getContext(){return new Proxy({},{get:()=>()=>{}});},getBoundingClientRect(){return {left:0,top:0,width:384,height:320};},setPointerCapture(){}};}
const document={getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},querySelectorAll(){return [];},createElement:element,createTextNode:t=>t,documentElement:element()};
const camera={frames:['first','next'],reference:'first',width:384,height:320,roi:[],seam:{enabled:false,pts:[],skip:[]},sensitivity:.5,reject_threshold:60,shift_tolerance_x:3,shift_tolerance_y:3,max_alignment_shift_x:12,max_alignment_shift_y:12,downscale:1,min_blob_area:20,learning_robust_enabled:true,learning_shadow_enabled:true,learning_search_radius:24};
const state={inspection_engine:'cpp-detector-52aa31ea',engine_source_commit:'52aa31eacb16785ff33dc16b6937ab31c8ec7ee9',cameras_info:Array.from({length:10},()=>structuredClone(camera))};
const context=vm.createContext({document,console,window:{addEventListener(){}},location:{hash:''},setTimeout(){return 1;},clearTimeout(){},setInterval(){return 1;},getComputedStyle(){return {getPropertyValue(){return '';}};},
 Image:class{constructor(){this.naturalWidth=384;this.naturalHeight=320;}set src(s){this.onload?.();}},
 async fetch(route,opts){requests.push({route,opts});const data=route==='/api/state'?state:route.startsWith('/api/status')?{last:[],running:false,period_ms:1000,batch:0,budget_ms:500}:route==='/api/testfiles'?[]:{cases:[],saved:true};return {ok:true,status:200,async json(){return data;}};}});
(async()=>{
 vm.runInContext(fs.readFileSync(path.join(web,'app.js'),'utf8'),context);
 vm.runInContext(fs.readFileSync(path.join(web,'learning.js'),'utf8'),context);
 for(let i=0;i<4;i++)await new Promise(setImmediate);
 assert.ok(document.getElementById('engineInfo').textContent.startsWith('Ядро 52aa31ea'));
 assert.equal(document.getElementById('grid').children.length,10,'UI initialisation must complete');
 vm.runInContext('renderResult({cam:0,ver:1,frame:"next",score:10,threshold:60,sensitivity:.5,raw_anomaly_score:.2,learned_normal_matches_count:0,verdict:"OK",zones:[],ms:1})',context);
 await document.getElementById('learnSelect').onclick();
 assert.ok(vm.runInContext('S.learning',context));
 assert.equal(document.getElementById('learningRobust').checked,true);
 const canvas=document.getElementById('canvas');
 const event=(x,y)=>({button:0,clientX:x,clientY:y,pointerId:1,stopImmediatePropagation(){}});
 for(const [type,x,y] of [['pointerdown',10,20],['pointermove',30,50],['pointerup',30,50]])canvas.listeners[type].find(x=>x.capture).fn(event(x,y));
 assert.equal(document.getElementById('learnSave').disabled,false);
 await document.getElementById('learnSave').onclick();
 const saved=requests.find(x=>x.route==='/api/learning?cam=0' && x.opts?.method==='POST');assert.ok(saved);
 assert.deepEqual(JSON.parse(saved.opts.body),{version:1,bbox:{x:10,y:20,width:21,height:31}});
 assert.ok(!vm.runInContext('S.learning',context));
 vm.runInContext('renderResult({cam:0,ver:2,frame:"next",score:0,threshold:60,sensitivity:.5,raw_anomaly_score:.2,learned_normal_matches_count:1,verdict:"OK",zones:[],ms:1,learning_robust_enabled:true,learning_ms:1,learning_diagnostics:[{id:"test",status:"matched",method:"shadow",removed_pixels:10,shift:[20,8],shadow_correlation:.95}]})',context);
 assert.ok(document.getElementById('result').children.some(x=>x?.children?.some(y=>String(y.textContent).includes('test:'))),'per-case diagnostics rendered');
 console.log('UI startup and region selection checks passed (mock DOM)');
})().catch(e=>{console.error(e);process.exitCode=1;});
