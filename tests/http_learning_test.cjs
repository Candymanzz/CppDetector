// node tests/http_learning_test.cjs <exe> <frames36> <web>
'use strict';
const fs=require('fs'), os=require('os'), path=require('path'), cp=require('child_process'), net=require('net'),assert=require('assert/strict');
(async()=>{
 const [exe,frames,web]=process.argv.slice(2).map(x=>path.resolve(x));
 const data=fs.mkdtempSync(path.join(os.tmpdir(),'cpp-learning-http-'));
 const server=net.createServer(); await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;await new Promise(r=>server.close(r));
 let child,output='';const base='http://127.0.0.1:'+port;
 const sleep=ms=>new Promise(r=>setTimeout(r,ms));
 async function start(){
  child=cp.spawn(exe,['--frames',frames,'--web',web,'--data',data,'--port',String(port)],{windowsHide:true});
  child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);
  for(let n=0;n<100;n++){try{const r=await fetch(base+'/api/state');if(r.ok)return;}catch{}await sleep(100);}
  throw Error('server failed to start: '+output);
 }
 async function stop(){if(child && child.exitCode===null){const exited=new Promise(r=>child.once('exit',r));child.kill();await exited;}}
 async function api(route,opts){const r=await fetch(base+route,opts);const j=await r.json();assert.equal(r.status,200,JSON.stringify(j));return j;}
 const post=(route,body)=>api(route,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 try{
  await start();const state=await api('/api/state');const name=state.cameras_info[0].frames[1];
  assert.equal(state.cameras_info[0].reject_threshold,60);assert.equal(state.cameras_info[0].max_alignment_shift_x,12);
  assert.equal(state.cameras_info[0].learning_robust_enabled,true);
  assert.equal(state.cameras_info[0].learning_shadow_enabled,true);
  await post('/api/camera?cam=0',{learning_search_radius:20});
  const result=await api('/api/inspect?cam=0&frame='+encodeURIComponent(name),{method:'POST'});
  const body={version:result.ver,bbox:{x:100,y:100,width:64,height:64}};
  const added=await post('/api/learning?cam=0',body);const id=added.accepted_cases[0].id;
  assert.equal((await post('/api/learning?cam=0',body)).accepted_cases[0].id,id);
  let cases=(await api('/api/learning?cam=0')).cases;assert.equal(cases.length,1);assert.equal(cases[0].active,true);
  assert.equal((await fetch(base+'/api/learning/image?cam=0&id='+id)).status,200);
  assert.equal((await fetch(base+'/api/learning/image?cam=1&id='+id)).status,404);
  assert.equal((await fetch(base+'/api/inspected?cam=0&version='+result.ver)).status,200);
  assert.equal((await fetch(base+'/api/learning?cam=0',{method:'POST',headers:{'Content-Type':'application/json'},body:'{bad'})).status,400);
  const next=await api('/api/inspect?cam=0&frame='+encodeURIComponent(name),{method:'POST'});
  assert.equal(typeof next.raw_anomaly_score,'number');assert.ok(Array.isArray(next.matched_accepted_case_ids));
  assert.equal(next.learning_engine,'local-robust-v2');assert.ok(Array.isArray(next.learning_diagnostics));
  assert.equal(next.learning_diagnostics[0].id,id);assert.ok(typeof next.learning_diagnostics[0].status==='string');
  await post('/api/camera?cam=0',{learning_robust_enabled:false});
  const oldMode=await api('/api/inspect?cam=0&frame='+encodeURIComponent(name),{method:'POST'});
  assert.equal(oldMode.learning_engine,'native-52aa31ea');
  await post('/api/camera?cam=0',{learning_robust_enabled:true});
  assert.equal((await fetch(base+'/api/learning?cam=0',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})).status,400);
  await stop();await start();assert.equal((await api('/api/state')).cameras_info[0].learning_search_radius,20);cases=(await api('/api/learning?cam=0')).cases;assert.equal(cases[0].id,id);assert.equal(cases[0].active,true);
  assert.equal((await api('/api/learning?cam=0&id='+id,{method:'DELETE'})).deleted,true);
  assert.equal((await api('/api/learning?cam=0')).cases.length,0);
  console.log('HTTP learning checks passed (save/retry/list/image/stale/malformed/restart/delete)');
 }finally{
  await stop();
  assert.equal(path.dirname(path.resolve(data)),path.resolve(os.tmpdir()));assert.ok(path.basename(data).startsWith('cpp-learning-http-'));
  fs.rmSync(data,{recursive:true});
 }
})().catch(e=>{console.error(e);process.exitCode=1;});
