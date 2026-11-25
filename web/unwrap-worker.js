'use strict';
importScripts('unwrap-core.js?v=quality-20261119','unwrap-inspection.js?v=quality-20261119','unwrap-source.js?v=quality-20261119');
self.onmessage = event => {
  try {
    const sources=UnwrapSource.prepare(event.data.cameras,UnwrapInspection.settings(event.data.inspection||{}),UnwrapInspection);
    const result = UnwrapCore.render(event.data);
    const issues=event.data.cameras.filter(c=>!c.enabled||!c.current||!c.reference||c.comparable===false)
      .map(c=>'Камера '+c.id+': '+(!c.enabled?'выключена':!c.current?'нет кадра':!c.reference?'нет эталона':'размеры кадра и эталона различаются'));
    result.inspection=UnwrapInspection.inspect({width:result.width,height:result.height,current:result.atlasCurrent,reference:result.atlasReference,lower:result.atlasLower,upper:result.atlasUpper,issues,prealigned:true,sources},event.data.inspection||{});
    delete result.atlasCurrent;delete result.atlasReference;delete result.atlasLower;delete result.atlasUpper;
    const buffers = ['current','reference','diff','coverage','owner','atlasOwner'].map(k => result[k].buffer);
    Object.values(result.inspection.rasters).forEach(r=>buffers.push(r.data.buffer));
    result.strips.forEach(s => buffers.push(s.data.buffer));
    self.postMessage(result, buffers);
  } catch (error) { self.postMessage({error: error.message}); }
};
