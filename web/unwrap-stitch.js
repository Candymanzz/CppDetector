/* Reference-only feature calibration for developed side views. */
(function(root) {
  'use strict';
  function extract(cv,image,geometry,core) {
    image=core.rectify(image,geometry,700,520);
    const rgba=new cv.Mat(image.height,image.width,cv.CV_8UC4),gray=new cv.Mat(),enhanced=new cv.Mat(),mask=new cv.Mat(image.height,image.width,cv.CV_8UC1,new cv.Scalar(0));
    rgba.data.set(image.data);cv.cvtColor(rgba,gray,cv.COLOR_RGBA2GRAY);
    const clahe=new cv.CLAHE(2,new cv.Size(8,8));clahe.apply(gray,enhanced);
    cv.rectangle(mask,new cv.Point(16,20),new cv.Point(image.width-17,image.height-21),new cv.Scalar(255),-1);
    const orb=new cv.ORB();orb.setMaxFeatures(5000);orb.setFastThreshold(10);orb.setEdgeThreshold(16);
    const keypoints=new cv.KeyPointVector(),descriptors=new cv.Mat();orb.detectAndCompute(enhanced,mask,keypoints,descriptors);
    const points=[];for(let i=0;i<keypoints.size();i++){const p=keypoints.get(i).pt;points.push([p.x/image.width,p.y/image.height]);}
    for(const value of [rgba,gray,enhanced,mask,clahe,orb,keypoints])value.delete();
    return {id:geometry.id,width:700,height:520,points,descriptors};
  }

  function pair(cv,a,b) {
    const matcher=new cv.BFMatcher(cv.NORM_HAMMING,false),forward=new cv.DMatchVectorVector(),reverse=new cv.DMatchVectorVector();
    if(a.descriptors.rows<2||b.descriptors.rows<2){matcher.delete();forward.delete();reverse.delete();return {a:a.id,b:b.id,matches:[],count:0,candidates:0};}
    matcher.knnMatch(a.descriptors,b.descriptors,forward,2);matcher.knnMatch(b.descriptors,a.descriptors,reverse,2);
    const reciprocal=new Map();
    for(let i=0;i<reverse.size();i++){const row=reverse.get(i);if(row.size()>=2){const m=row.get(0),n=row.get(1);if(m.distance<.82*n.distance)reciprocal.set(m.queryIdx,m.trainIdx);}row.delete();}
    const pairs=[];
    for(let i=0;i<forward.size();i++) {
      const row=forward.get(i);if(row.size()<2){row.delete();continue;}const m=row.get(0),n=row.get(1),p=a.points[m.queryIdx],q=b.points[m.trainIdx];
      if(p&&q&&m.distance<.82*n.distance&&reciprocal.get(m.trainIdx)===m.queryIdx&&Math.abs(p[1]-q[1])<.45)pairs.push({p,q,distance:m.distance});
      row.delete();
    }
    for(const value of [matcher,forward,reverse])value.delete();
    if(pairs.length<6)return {a:a.id,b:b.id,matches:[],count:0,candidates:pairs.length};
    const src=cv.matFromArray(pairs.length,1,cv.CV_32FC2,pairs.flatMap(m=>[m.p[0]*a.width,m.p[1]*a.height]));
    const dst=cv.matFromArray(pairs.length,1,cv.CV_32FC2,pairs.flatMap(m=>[m.q[0]*b.width,m.q[1]*b.height]));
    cv.setRNGSeed(7919*a.id+13*b.id+17);
    const mask=new cv.Mat(),model=cv.estimateAffine2D(src,dst,mask,cv.RANSAC,3,5000,.995,20),matches=[];
    if(!model.empty())for(let i=0;i<pairs.length;i++)if(mask.data[i])matches.push(pairs[i]);
    const matrix=model.empty()?null:Array.from(model.data64F);
    for(const value of [src,dst,mask,model])value.delete();
    return {a:a.id,b:b.id,matches,count:matches.length,candidates:pairs.length,matrix};
  }

  function matches(cv,cameras,core) {
    const ordered=[...cameras].sort((a,b)=>a.center-b.center),features=[];
    try {
      for(const c of ordered)features.push(extract(cv,c.reference,c,core));
      const pairs=[];for(let i=0;i<features.length;i++)for(let j=i+1;j<features.length;j++){
        const forward=core.delta(ordered[j].center,ordered[i].center)>0;
        const p=pair(cv,forward?features[i]:features[j],forward?features[j]:features[i]);
        const m=p.matrix,spread=axis=>p.matches.length?Math.max(...p.matches.map(v=>v.p[axis]))-Math.min(...p.matches.map(v=>v.p[axis])):0;
        p.accepted=!!m&&p.count>=14&&p.count>=.15*p.candidates&&spread(0)>=.025&&spread(1)>=.2&&m[0]>=.35&&m[0]<=2&&m[3]>-.8&&m[3]<.8&&m[4]>=.5&&m[4]<=1.8&&Math.abs(m[1])<.6&&m[0]*m[4]-m[1]*m[3]>.3;
        pairs.push(p);
      }
      return {features:features.map(f=>({id:f.id,count:f.points.length})),pairs};
    } finally {features.forEach(f=>f.descriptors.delete());}
  }
  const api={matches};if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.UnwrapStitch=api;
})(typeof self!=='undefined'?self:globalThis);
