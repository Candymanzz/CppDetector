'use strict';
const fs=require('fs'),os=require('os'),path=require('path'),cp=require('child_process'),net=require('net'),assert=require('assert/strict');
(async()=>{
 const [exe,frames,web,report]=process.argv.slice(2).map(x=>path.resolve(x));
 const data=fs.mkdtempSync(path.join(os.tmpdir(),'adaptive-roi-http-'));
 const listener=net.createServer();await new Promise(r=>listener.listen(0,'127.0.0.1',r));const port=listener.address().port;await new Promise(r=>listener.close(r));
 let child,output='';const base='http://127.0.0.1:'+port;
 const api=async(route,opts,status=200)=>{const response=await fetch(base+route,opts),json=await response.json();assert.equal(response.status,status,JSON.stringify(json));return json;};
 const post=(route,body,status)=>api(route,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)},status);
 async function start(){child=cp.spawn(exe,['--frames',frames,'--web',web,'--data',data,'--port',String(port)],{windowsHide:true});child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);for(let i=0;i<100;i++){try{const r=await fetch(base+'/api/state');if(r.ok)return;}catch{}await new Promise(r=>setTimeout(r,100));}throw Error(output);}
 async function stop(){if(child&&child.exitCode===null){const exit=new Promise(r=>child.once('exit',r));child.kill();await exit;}}
 try {
  await start();let state=await api('/api/state');
  for(let cam=0;cam<10;cam++) {
   assert.equal(state.cameras_info[cam].adaptive_roi_enabled,false);
   await post('/api/camera?cam='+cam,{adaptive_roi_enabled:true,adaptive_roi_min_coverage:.7,adaptive_roi_max_motion:96});
  }
  state=await api('/api/state');const before=state.cameras_info[0];
  await post('/api/camera?cam=0',{adaptive_roi_min_coverage:0,sensitivity:.8},400);
  assert.deepEqual((await api('/api/state')).cameras_info[0],before);
  const rows=[];
  for(const index of [0,1,10,40,79]) {
   const batch=await Promise.all(state.cameras_info.map(async(c,cam)=>{
    const frame=c.frames[Math.min(index,c.frames.length-1)];
    const r=await api('/api/inspect?cam='+cam+'&frame='+encodeURIComponent(frame),{method:'POST'});
    assert.equal(r.adaptive_roi.enabled,true);assert.equal(typeof r.control_complete,'boolean');
    if(!r.control_complete)assert.notEqual(r.verdict,'OK');
    if(r.adaptive_roi.valid)assert.ok(r.adaptive_roi.coverage>=.7);
    assert.equal((await fetch(base+'/api/overlay?cam='+cam+'&v='+r.ver)).status,200);
    return {cam,index,frame,verdict:r.verdict,score:r.score,ms:r.ms,adaptive:r.adaptive_roi};
   }));rows.push(...batch);
  }
  // Learning must be rejected by the server too, independent of the UI checkbox.
  const latest=await api('/api/status');await post('/api/learning?cam=0',{version:latest.last[0].ver,bbox:{x:10,y:10,width:20,height:20}},400);
  await stop();await start();state=await api('/api/state');
  assert.equal(state.cameras_info[0].adaptive_roi_enabled,true);assert.equal(state.cameras_info[0].adaptive_roi_min_coverage,.7);
  const batchTimes=[];await post('/api/global',{period_ms:1000});await api('/api/run?on=1',{method:'POST'});
  let last=0;for(let i=0;i<100&&batchTimes.length<4;i++){
   await new Promise(r=>setTimeout(r,100));const status=await api('/api/status');
   if(status.batch>last){last=status.batch;batchTimes.push(status.batch_ms);}
  }
  await api('/api/run?on=0',{method:'POST'});assert.equal(batchTimes.length,4);
  const summary={frames:rows.length,valid:rows.filter(r=>r.adaptive.valid).length,verdicts:rows.reduce((a,r)=>(a[r.verdict]=(a[r.verdict]||0)+1,a),{}),batchTimes,rows};
  fs.writeFileSync(report,JSON.stringify(summary,null,2));
  console.log(JSON.stringify({frames:summary.frames,valid:summary.valid,verdicts:summary.verdicts,batchTimes}));
  console.log('Adaptive ROI HTTP: diagnostics, overlays, invalid updates, learning guard, restart and ten-camera stream passed');
 } finally {await stop();if(path.dirname(data)!==os.tmpdir())throw Error('unsafe temporary cleanup');fs.rmSync(data,{recursive:true,force:true});}
})().catch(error=>{console.error(error);process.exitCode=1;});
