// Tests an isolated copy of existing profiles; never writes to the source data directory.
'use strict';
const fs=require('fs'),os=require('os'),path=require('path'),cp=require('child_process'),net=require('net'),assert=require('assert/strict');
(async()=>{
 const [exe,source,frames,web]=process.argv.slice(2).map(x=>path.resolve(x));
 const data=fs.mkdtempSync(path.join(os.tmpdir(),'cpp-profile-migration-'));
 fs.cpSync(path.join(source,'config'),path.join(data,'config'),{recursive:true});
 const file=path.join(data,'config','config_'+path.basename(frames)+'_52aa31ea.json');
 const original=JSON.parse(fs.readFileSync(file,'utf8'));
 let baseline;
 const server=net.createServer();await new Promise(r=>server.listen(0,'127.0.0.1',r));const port=server.address().port;await new Promise(r=>server.close(r));
 const base='http://127.0.0.1:'+port;let child,output='';
 const sleep=ms=>new Promise(r=>setTimeout(r,ms));
 async function api(route,opts){const r=await fetch(base+route,opts);const j=await r.json();assert.equal(r.status,200,JSON.stringify(j));return j;}
 async function start(binary=exe){child=cp.spawn(binary,['--frames',frames,'--web',web,'--data',data,'--port',String(port)],{windowsHide:true});child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);for(let i=0;i<100;i++){try{const r=await fetch(base+'/api/state');if(r.ok)return;}catch{}await sleep(100);}throw Error(output);}
 async function stop(){if(child&&child.exitCode===null){const exit=new Promise(r=>child.once('exit',r));child.kill();await exit;}}
 function compare(st){for(let i=0;i<original.cameras.length;i++){const old=baseline.cameras_info[i],now=st.cameras_info[i];for(const key of ['reference','roi','seam','sensitivity','reject_threshold','shift_tolerance_x','shift_tolerance_y','downscale','min_blob_area','learning_robust_enabled','learning_shadow_enabled','learning_search_radius'])if(Object.hasOwn(old,key))assert.deepEqual(now[key],old[key],i+': '+key);assert.ok(Math.abs(now.max_alignment_shift_x-old.max_alignment_shift_x)<1e-9);assert.ok(Math.abs(now.max_alignment_shift_y-old.max_alignment_shift_y)<1e-9);}}
 try{
  await start(path.join(source,'build','Release','cppdetector.exe'));baseline=await api('/api/state');
  const cases=[];for(let cam=0;cam<10;cam++)cases.push((await api('/api/learning?cam='+cam)).cases);
  await stop();await start();let st=await api('/api/state');compare(st);
  for(let cam=0;cam<10;cam++)assert.deepEqual((await api('/api/learning?cam='+cam)).cases,cases[cam],'existing learning: camera '+cam);
  await api('/api/camera?cam=0',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({sensitivity:original.cameras[0].sensitivity})});
  await stop();await start();st=await api('/api/state');compare(st);
  for(let cam=0;cam<10;cam++)assert.deepEqual((await api('/api/learning?cam='+cam)).cases,cases[cam],'learning survives migration: camera '+cam);
  const saved=JSON.parse(fs.readFileSync(file,'utf8'));
  assert.equal(saved.engine_source_commit,'2090914128af2822c5a94720a89555c02238642b');
  assert.ok(saved.cameras.every(c=>typeof c.horizontal_shift_tolerance_mm==='number'));
  console.log('Existing profile migration passed: 10 cameras, references/ROI/settings and '+cases.flat().length+' learned cases retained through save/restart');
  const missing=original.cameras.filter((c,i)=>c.reference!=='upload'&&!baseline.cameras_info[i].frames.includes(c.reference)).length;
  if(missing)console.log('Pre-existing data condition: '+missing+' configured reference names are absent from the dataset; baseline and updated app use the same fallback frames');
 }finally{await stop();assert.equal(path.dirname(path.resolve(data)),path.resolve(os.tmpdir()));assert.ok(path.basename(data).startsWith('cpp-profile-migration-'));fs.rmSync(data,{recursive:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
