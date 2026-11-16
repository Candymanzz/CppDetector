'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const web=path.resolve(process.argv[2]||path.join(__dirname,'../web'));
const elements=new Map(),requests=[],windowEvents=new Map();
function element(){const classes=new Set();return {value:'',textContent:'',style:{},dataset:{},disabled:false,children:[],listeners:{},
 classList:{toggle(key,on){on?classes.add(key):classes.delete(key);},contains:key=>classes.has(key)},
 addEventListener(type,fn){(this.listeners[type]??=[]).push(fn);},append(...items){this.children.push(...items);},replaceChildren(...items){this.children=items;},replaceChild(){},
 querySelector(){return element();},getContext(){return new Proxy({},{get:()=>()=>{}});},getBoundingClientRect(){return {left:0,top:0,width:640,height:480};},setPointerCapture(){}};}
const document={getElementById(id){if(!elements.has(id))elements.set(id,element());return elements.get(id);},querySelectorAll(){return [];},querySelector(){return element();},createElement:element,createTextNode:text=>text,documentElement:element()};
const camera={frames:['reference','one','two'],reference:'reference',width:640,height:480,roi:[],seam:{enabled:false,pts:[],skip:[]},sensitivity:.5,reject_threshold:60,
 shift_tolerance_x:3,shift_tolerance_y:3,learning_search_radius:24,downscale:1,min_blob_area:20};
const state={inspection_engine:'cpp-detector-eefefce4',engine_source_commit:'eefefce455ea93a2184c8d4e9178a0f1eb45847a',cameras_info:Array.from({length:10},()=>structuredClone(camera))};
let batchReply=async()=>[],singleReply=async()=>({cam:0,ver:7,frame:'one',score:0,verdict:'OK',reject:false,zones:[]});
const context=vm.createContext({document,console,window:{addEventListener(type,fn){(windowEvents.get(type)||windowEvents.set(type,[]).get(type)).push(fn);}},location:{hash:''},
 setTimeout(){return 1;},clearTimeout(){},setInterval(){return 1;},getComputedStyle(){return {getPropertyValue(){return '';}};},
 Image:class{constructor(){this.naturalWidth=640;this.naturalHeight=480;}set src(value){this.onload?.();}},
 async fetch(route,opts){requests.push({route,opts});let data,ok=true;
  if(route.startsWith('/api/inspect_all?')){const reply=await batchReply();data=reply.data??reply;ok=reply.ok!==false;}
  else if(route.startsWith('/api/inspect?'))data=await singleReply();
  else data=route==='/api/state'?state:route.startsWith('/api/status')?{last:[],running:false,period_ms:1000,batch:0,budget_ms:500}:route==='/api/testfiles'||route.startsWith('/api/log?')?[]:{cases:[],saved:true};
  return {ok,status:ok?200:500,async json(){return data;}};
 }});
const read=code=>vm.runInContext(code,context),e=id=>document.getElementById(id),flush=async()=>{for(let i=0;i<4;i++)await new Promise(setImmediate);};
const inspections=()=>requests.filter(r=>/^\/api\/inspect(?:_all)?\?/.test(r.route));
const press=async(options={})=>{const event={key:'Enter',ctrlKey:true,prevented:false,preventDefault(){this.prevented=true;},...options};for(const fn of windowEvents.get('keydown')||[])fn(event);await flush();return event;};
(async()=>{
 const html=fs.readFileSync(path.join(web,'index.html'),'utf8'),header=html.match(/<header\b[\s\S]*?<\/header>/)[0];
 for(const id of ['runOne','runAll','inspectionSummary']){assert.ok(header.includes(`id="${id}"`),id+' belongs to the page header');assert.equal(html.split(`id="${id}"`).length-1,1,id+' must be unique');}
 for(const file of ['app.js','learning.js'])read(fs.readFileSync(path.join(web,file),'utf8'));
 await flush();read('showTab("cam")');assert.equal(e('inspectionHeader').hidden,false);
 assert.match(e('inspectionSummary').textContent,/ещё не проверялись/);
 let resolve;batchReply=()=>new Promise(r=>resolve=r);
 const running=read('inspectCameraFrames()');
 assert.equal(e('runOne').disabled,true);assert.equal(e('runAll').disabled,true);
 assert.match(e('inspectionSummary').textContent,/проверяем все кадры/);
 await read('inspectSelectedFrame()');await read('inspectCameraFrames()');assert.equal(inspections().length,1,'pending requests cannot be duplicated');
 resolve([{frame:'one',score:81,verdict:'REJECT'},{frame:'two',score:0,verdict:'OK'},
  {frame:'three',score:95,verdict:'REJECT'},{frame:'four',score:0,verdict:'NOT_INSPECTED'},
  {frame:'five',score:0,verdict:'NO_REFERENCE'}]);await running;
 assert.match(e('inspectionSummary').textContent,/Дефектных кадров: 2 из 5/);
 assert.match(e('inspectionSummary').textContent,/Без контроля: 1/);assert.match(e('inspectionSummary').textContent,/Ошибок: 1/);
 assert.equal(e('runOne').disabled,false);assert.equal(e('allResults').children[1].children.length,6);
 read('selectCam(1)');assert.match(e('inspectionSummary').textContent,/ещё не проверялись/);
 read('selectCam(0)');assert.match(e('inspectionSummary').textContent,/2 из 5/);assert.equal(e('allResults').children[1].children.length,6);
 batchReply=()=>new Promise(r=>resolve=r);const switched=read('inspectCameraFrames()');read('selectCam(1)');
 resolve([{frame:'one',score:0,verdict:'OK'}]);await switched;
 assert.equal(e('inspectionCamera').textContent,'Камера 1');assert.equal(e('allResults').children.length,0,'late batch does not replace another camera results');
 read('selectCam(0)');assert.match(e('inspectionSummary').textContent,/0 из 1/);
 batchReply=async()=>({ok:false,data:{error:'fixture unavailable'}});await read('inspectCameraFrames()');
 assert.match(e('inspectionSummary').textContent,/не завершена/);assert.equal(e('runAll').disabled,false,'failure permits retry');
 batchReply=async()=>[];await read('inspectCameraFrames()');assert.match(e('inspectionSummary').textContent,/Нет кадров/);
 batchReply=async()=>({unexpected:true});await read('inspectCameraFrames()');assert.match(e('inspectionSummary').textContent,/не завершена/,'malformed response cannot imply zero defects');
 batchReply=async()=>[{frame:'one',score:0,verdict:'OK'}];
 const before=inspections().length;assert.equal((await press()).prevented,true);assert.equal(inspections().length,before+1);
 assert.ok(inspections().at(-1).route.startsWith('/api/inspect?cam=0&frame=one'));
 await press({shiftKey:true});assert.ok(inspections().at(-1).route.startsWith('/api/inspect_all?cam=0'));
 await press({ctrlKey:false,metaKey:true});assert.ok(inspections().at(-1).route.startsWith('/api/inspect?'));
 const count=inspections().length;
 for(const options of [{repeat:true},{isComposing:true},{altKey:true},{defaultPrevented:true},{ctrlKey:false},{target:{isContentEditable:true}},{target:{closest(){return {};}}}]){
  assert.equal((await press(options)).prevented,false,'editing, native keys and held shortcuts are ignored');
 }
 read('showTab("grid")');assert.equal(e('inspectionHeader').hidden,true);await press();
 read('showTab("log")');await press();assert.equal(inspections().length,count,'shortcuts only inspect in the camera tab');
 read('showTab("cam");selectCam(0)');singleReply=()=>new Promise(r=>resolve=r);
 const one=read('inspectSelectedFrame()');read('selectCam(1)');resolve({cam:0,ver:99,frame:'one',score:0,verdict:'OK',reject:false,zones:[]});await one;
 assert.equal(read('S.learningResult'),null,'late single result cannot attach to another camera');
 assert.equal(e('runOne').disabled,false);
 console.log('Inspection header UI passed: counts, incomplete control, per-camera history, pending/failure/retry, late responses, header buttons and keyboard safeguards');
})().catch(error=>{console.error(error);process.exitCode=1;});
