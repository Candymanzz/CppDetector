'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const web=path.resolve(process.argv[2]||path.join(__dirname,'../web'));
const elements=new Map(),requests=[],rectangles=[],pendingImages=[];
let delayImages=false,failLearning=false,failExclusion=false;
function element(){return {value:'',textContent:'',style:{},dataset:{},width:384,height:320,children:[],listeners:{},classList:{toggle(){},contains(){return false;}},
 addEventListener(type,fn,capture){(this.listeners[type]??=[]).push({fn,capture});},append(...v){this.children.push(...v);},replaceChildren(...v){this.children=v;},replaceChild(){},
 querySelector(){return element();},getContext(){return new Proxy({},{get:(_,key)=>key==='strokeRect'?(...args)=>rectangles.push(args):()=>{}});},getBoundingClientRect(){return {left:0,top:0,width:384,height:320};},setPointerCapture(){}};}
const document={getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},querySelectorAll(){return [];},createElement:element,createTextNode:t=>t,documentElement:element()};
const camera={frames:['first','next'],reference:'first',width:384,height:320,roi:[],seam:{enabled:false,pts:[],skip:[]},sensitivity:.5,reject_threshold:60,shift_tolerance_x:3,shift_tolerance_y:3,max_alignment_shift_x:12,max_alignment_shift_y:12,horizontal_shift_tolerance_mm:3,vertical_shift_tolerance_mm:3.96,downscale:1,min_blob_area:20,learning_robust_enabled:true,learning_shadow_enabled:true,learning_search_radius:24};
const state={inspection_engine:'cpp-detector-20909141',engine_source_commit:'2090914128af2822c5a94720a89555c02238642b',cameras_info:Array.from({length:10},()=>structuredClone(camera))};
const context=vm.createContext({document,console,window:{addEventListener(){}},location:{hash:''},setTimeout(){return 1;},clearTimeout(){},setInterval(){return 1;},getComputedStyle(){return {getPropertyValue(){return '';}};},
 Image:class{constructor(){this.naturalWidth=384;this.naturalHeight=320;}set src(s){if(delayImages)pendingImages.push(this);else this.onload?.();}},
 async fetch(route,opts){requests.push({route,opts});const failed=(failLearning&&route==='/api/learning?cam=0'||failExclusion&&route==='/api/camera?cam=0')&&opts?.method==='POST';const data=failed?{error:'Inspection changed; inspect the frame again'}:route==='/api/state'?state:route.startsWith('/api/status')?{last:[],running:false,period_ms:1000,batch:0,budget_ms:500}:route==='/api/testfiles'?[]:{cases:[],saved:true};return {ok:!failed,status:failed?400:200,async json(){return data;}};}});

(async()=>{
 for(const file of ['app.js','learning.js','exclusions.js']) vm.runInContext(fs.readFileSync(path.join(web,file),'utf8'),context);
 for(let i=0;i<4;i++)await new Promise(setImmediate);
 const e=id=>document.getElementById(id),read=expression=>JSON.parse(vm.runInContext('JSON.stringify('+expression+')',context));
 const click=(x,y,button=0)=>{for(const {fn} of e('canvas').listeners.pointerdown)fn({clientX:x,clientY:y,button,pointerId:1});};
 const posts=()=>requests.filter(x=>x.route.startsWith('/api/camera?')&&x.opts?.method==='POST');
 assert.equal(e('saveExclusion').disabled,true);e('addExclusion').onclick();
 assert.equal(read('S.mode'),'ref');assert.equal(e('autoRoi').disabled,true);
 click(90,90);click(190,90);assert.equal(e('saveExclusion').disabled,true);click(190,190);click(90,190);
 assert.deepEqual(read('S.roi'),[],'drawing exclusion must not edit ROI');
 assert.equal(e('saveExclusion').disabled,false);await e('saveExclusion').onclick();
 const polygon=[[90,90],[190,90],[190,190],[90,190]];
 assert.deepEqual(JSON.parse(posts().at(-1).opts.body),{exclusions:[polygon]});
 assert.deepEqual(read('S.exclusions'),[polygon]);assert.equal(read('S.exclusionDraft'),null);
 assert.equal(e('exclusionList').children.length,1);assert.equal(e('autoRoi').disabled,false);
 vm.runInContext('selectCam(1)',context);assert.deepEqual(read('S.exclusions'),[],'per-camera zones');
 vm.runInContext('selectCam(0)',context);assert.deepEqual(read('S.exclusions'),[polygon]);
 e('addExclusion').onclick();click(30,30);e('undoExclusion').onclick();assert.deepEqual(read('S.exclusionDraft'),[]);
 click(30,30);e('cancelExclusion').onclick();assert.equal(read('S.exclusionDraft'),null);assert.equal(posts().length,1,'cancel does not save');
 e('addExclusion').onclick();click(30,30);vm.runInContext('setMode("frame")',context);assert.equal(read('S.exclusionDraft'),null,'view switch cancels draft');
 e('addExclusion').onclick();click(30,30);click(70,30);click(50,70);failExclusion=true;await e('saveExclusion').onclick();failExclusion=false;
 assert.equal(read('S.exclusionDraft.length'),3,'server failure keeps editable draft');assert.deepEqual(read('S.exclusions'),[polygon]);
 await e('saveExclusion').onclick();assert.equal(read('S.exclusions.length'),2,'retry succeeds');
 await e('exclusionList').children[0].children[1].onclick();assert.equal(read('S.exclusions.length'),1,'remove selected zone');
 await e('clearExclusions').onclick();assert.deepEqual(read('S.exclusions'),[]);
 assert.deepEqual(JSON.parse(posts().at(-1).opts.body),{exclusions:[]});
 e('addExclusion').onclick();click(20,20);vm.runInContext('selectCam(1)',context);assert.equal(read('S.exclusionDraft'),null,'camera switch cancels draft');
 console.log('Exclusion UI passed: polygon drawing, ROI isolation, camera isolation, cancel, undo, failed save/retry and deletion (mock DOM)');
})().catch(e=>{console.error(e);process.exitCode=1;});
