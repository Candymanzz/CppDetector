'use strict';
importScripts('unwrap-core.js?v=stitch-20261119','unwrap-stitch.js?v=stitch-20261119','unwrap-stitching.js?v=stitch-20261119','vendor/opencv-4.12.0.js');
// This OpenCV build exposes a self-resolving thenable; resolve readiness without returning cv.
const ready=new Promise(resolve=>{if(self.cv.Mat)resolve();else self.cv.onRuntimeInitialized=()=>resolve();});
self.onmessage=async event=>{
  try {
    await ready;
    const cameras=event.data.cameras;
    if(!Array.isArray(cameras)||cameras.length!==4)throw new Error('Нужны четыре эталонных ракурса');
    cameras.forEach(c=>{UnwrapCore.validateGeometry(c);if(!c.reference||c.reference.data.length!==c.reference.width*c.reference.height*4)throw new Error('Нет эталона камеры '+c.id);});
    const found=UnwrapStitch.matches(self.cv,cameras,UnwrapCore),result=UnwrapStitching.fit(UnwrapCore,cameras,found.pairs);
    result.signature=event.data.signature;result.features=found.features;result.createdAt=new Date().toISOString();
    self.postMessage(result);
  } catch(error){self.postMessage({error:error.message||String(error)});}
};
