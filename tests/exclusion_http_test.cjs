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
  const ref=Buffer.alloc(384*320*3);let random=42;for(let i=0;i<ref.length;i+=3){random=(Math.imul(random,1664525)+1013904223)>>>0;ref.fill(40+(random%170),i,i+3);}
  const inside=Buffer.from(ref);for(let y=115;y<165;y++)for(let x=115;x<165;x++)inside.fill(0,(y*384+x)*3,(y*384+x)*3+3);
  const outside=Buffer.from(inside);for(let y=220;y<265;y++)for(let x=240;x<285;x++)outside.fill(0,(y*384+x)*3,(y*384+x)*3+3);
  const exclusions=[[[90,90],[190,90],[190,190],[90,190]]];
  await start();await upload('/api/reference_upload?cam=0',ref);await post('/api/camera?cam=0',{reject_threshold:10,min_blob_area:3});
  assert.equal((await upload('/api/inspect_upload?cam=0',inside)).reject,true,'negative control before exclusion');
  await post('/api/camera?cam=0',{exclusions});
  const accepted=await upload('/api/inspect_upload?cam=0',inside);assert.equal(accepted.reject,false);assert.equal(accepted.score,0);assert.equal(accepted.learning_bbox,null);assert.deepEqual(accepted.exclusions,exclusions);
  assert.equal((await upload('/api/inspect_upload?cam=0',outside)).reject,true,'outside defect still rejects');
  const state=await api('/api/state');assert.deepEqual(state.cameras_info[0].exclusions,exclusions);assert.deepEqual(state.cameras_info[1].exclusions,[]);
  await post('/api/camera?cam=0',{exclusions:[[[0,0],[1,1]]],sensitivity:.8},400);assert.deepEqual((await api('/api/state')).cameras_info[0],state.cameras_info[0]);
  const page=await fetch(base+'/');assert.match(await page.text(),/exclusions.js/);const script=await fetch(base+'/exclusions.js');assert.equal(script.status,200);
  await stop();await start();assert.deepEqual((await api('/api/state')).cameras_info[0].exclusions,exclusions);assert.equal((await upload('/api/inspect_upload?cam=0',inside)).reject,false);
  await post('/api/camera?cam=0',{exclusions:[]});assert.equal((await upload('/api/inspect_upload?cam=0',inside)).reject,true,'deletion restores inspection');
  console.log('Exclusion HTTP passed: inside/outside defects, camera isolation, validation, result/log settings, static UI, restart and deletion');
 }finally{await stop();assert.equal(path.dirname(path.resolve(data)),path.resolve(os.tmpdir()));assert.ok(path.basename(data).startsWith('cpp-auto-learning-'));fs.rmSync(data,{recursive:true});}
})().catch(e=>{console.error(e);process.exitCode=1;});
