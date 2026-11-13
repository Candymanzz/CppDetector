'use strict';
const fs=require('fs'),os=require('os'),path=require('path'),cp=require('child_process'),net=require('net'),zlib=require('zlib'),assert=require('assert/strict');
function png(pixels,width,height){
 const crc=b=>{let c=0xffffffff;for(const byte of b){c^=byte;for(let k=0;k<8;k++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;};
 const chunk=(name,b)=>{const type=Buffer.from(name),length=Buffer.alloc(4),check=Buffer.alloc(4);length.writeUInt32BE(b.length);check.writeUInt32BE(crc(Buffer.concat([type,b])));return Buffer.concat([length,type,b,check]);};
 const header=Buffer.alloc(13);header.writeUInt32BE(width);header.writeUInt32BE(height,4);header[8]=8;header[9]=2;
 const rows=Buffer.alloc((width*3+1)*height);for(let y=0;y<height;y++)pixels.copy(rows,y*(width*3+1)+1,y*width*3,(y+1)*width*3);
 return Buffer.concat([Buffer.from([137,80,78,71,13,10,26,10]),chunk('IHDR',header),chunk('IDAT',zlib.deflateSync(rows)),chunk('IEND',Buffer.alloc(0))]);
}
(async()=>{
 const [exe,frames,web]=process.argv.slice(2).map(x=>path.resolve(x));
 const data=fs.mkdtempSync(path.join(os.tmpdir(),'cpp-auto-learning-'));
 const listener=net.createServer();await new Promise(r=>listener.listen(0,'127.0.0.1',r));const port=listener.address().port;await new Promise(r=>listener.close(r));
 let child,output='';const base='http://127.0.0.1:'+port;
 const api=async(route,opts,status=200)=>{const r=await fetch(base+route,opts),j=await r.json();assert.equal(r.status,status,JSON.stringify(j));return j;};
 const post=(route,body,status)=>api(route,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)},status);
 const upload=(route,pixels)=>{const form=new FormData();form.append('file',new Blob([png(pixels,384,320)],{type:'image/png'}),'frame.png');return api(route,{method:'POST',body:form});};
 async function start(){child=cp.spawn(exe,['--frames',frames,'--web',web,'--data',data,'--port',String(port)],{windowsHide:true});child.stdout.on('data',b=>output+=b);child.stderr.on('data',b=>output+=b);for(let i=0;i<100;i++){try{const r=await fetch(base+'/api/state');if(r.ok)return;}catch{}await new Promise(r=>setTimeout(r,100));}throw Error(output);}
 async function stop(){if(child&&child.exitCode===null){const exit=new Promise(r=>child.once('exit',r));child.kill();await exit;}}
 try{
  const ref=Buffer.alloc(384*320*3);let random=42;for(let i=0;i<ref.length;i+=3){random=(Math.imul(random,1664525)+1013904223)>>>0;const value=50+(random%130);ref.fill(value,i,i+3);}
  const normal=Buffer.from(ref);for(let y=100;y<160;y++)for(let x=100;x<160;x++)normal.fill(240,(y*384+x)*3,(y*384+x)*3+3);
  await start();await upload('/api/reference_upload?cam=0',ref);await post('/api/camera?cam=0',{reject_threshold:10,min_blob_area:3});
  const clean=await upload('/api/inspect_upload?cam=0',ref);assert.equal(clean.learning_bbox,null);
  await post('/api/learning?cam=0',{version:clean.ver},400);assert.equal((await api('/api/learning?cam=0')).cases.length,0);
  const result=await upload('/api/inspect_upload?cam=0',normal);assert.equal(result.reject,true);assert.ok(result.learning_bbox);
  const body={version:result.ver};const added=await post('/api/learning?cam=0',body),item=added.accepted_cases[0];
  assert.deepEqual(item.bbox,result.learning_bbox,'server crops the raw inspected image using its own heatmap bounds');
  assert.equal((await post('/api/learning?cam=0',body)).accepted_cases[0].id,item.id,'automatic save is idempotent');
  const accepted=await upload('/api/inspect_upload?cam=0',normal);assert.equal(accepted.reject,false);assert.ok(accepted.raw_anomaly_score>accepted.anomaly_score);
  await post('/api/learning?cam=0',body,400);
  const defect=Buffer.from(normal);for(let y=115;y<145;y++)for(let x=115;x<145;x++)defect.fill(0,(y*384+x)*3,(y*384+x)*3+3);
  assert.equal((await upload('/api/inspect_upload?cam=0',defect)).reject,true,'new defect remains rejected after automatic learning');
  const injected=await upload('/api/inspect_upload?cam=0&defect=1',normal);await post('/api/learning?cam=0',{version:injected.ver},400);
  const invalidated=await upload('/api/inspect_upload?cam=0',normal);await post('/api/camera?cam=0',{sensitivity:.5});await post('/api/learning?cam=0',{version:invalidated.ver},400);
  await stop();await start();assert.equal((await api('/api/learning?cam=0')).cases[0].id,item.id);assert.equal((await upload('/api/inspect_upload?cam=0',normal)).reject,false);
  console.log('Automatic HTTP learning passed: no bbox, empty map, idempotency, stale/injected/settings guards, new defect and restart');
 }finally{await stop();assert.equal(path.dirname(path.resolve(data)),path.resolve(os.tmpdir()));assert.ok(path.basename(data).startsWith('cpp-auto-learning-'));fs.rmSync(data,{recursive:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
