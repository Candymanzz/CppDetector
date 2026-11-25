/* Reference correspondence fitting. Geometry is frozen before inspecting current images. */
(function(root){
  'use strict';
  const identity=()=>[1,0,0,0,1,0];
  function solve(a,b) {
    const n=b.length,m=a.map((r,i)=>[...r,b[i]]);
    for(let k=0;k<n;k++) {
      let best=k;for(let i=k+1;i<n;i++)if(Math.abs(m[i][k])>Math.abs(m[best][k]))best=i;
      if(Math.abs(m[best][k])<1e-10)return null;[m[k],m[best]]=[m[best],m[k]];
      const d=m[k][k];for(let j=k;j<=n;j++)m[k][j]/=d;
      for(let i=0;i<n;i++)if(i!==k){const f=m[i][k];for(let j=k;j<=n;j++)m[i][j]-=f*m[k][j];}
    }
    return m.map(r=>r[n]);
  }
  function leastSquares(rows,values,n,priors=[]) {
    const a=Array.from({length:n},()=>Array(n).fill(0)),b=Array(n).fill(0);
    rows.forEach((r,index)=>{for(let i=0;i<n;i++){b[i]+=r[i]*values[index];for(let j=0;j<n;j++)a[i][j]+=r[i]*r[j];}});
    for(let i=0;i<n;i++)a[i][i]+=(priors[i]||.00001)**2;
    return solve(a,b);
  }
  function inverse(core,g,target) {
    let u=.5,v=.5;
    for(let it=0;it<20;it++) {
      const q=core.point(g,u,v),a=core.point(g,u+.0001,v),b=core.point(g,u,v+.0001),ex=q[0]-target[0],ey=q[1]-target[1];
      if(Math.hypot(ex,ey)<.000001)break;
      const ax=(a[0]-q[0])*10000,ay=(a[1]-q[1])*10000,bx=(b[0]-q[0])*10000,by=(b[1]-q[1])*10000,det=ax*by-ay*bx;
      if(Math.abs(det)<1e-8)return null;
      u-=Math.max(-.3,Math.min(.3,(by*ex-bx*ey)/det));v-=Math.max(-.3,Math.min(.3,(-ay*ex+ax*ey)/det));
    }
    const q=core.point(g,u,v);return u>=0&&u<=1&&v>=0&&v<=1&&Math.hypot(q[0]-target[0],q[1]-target[1])<.001?[u,v]:null;
  }
  function affine(matches) {
    const rows=matches.map(m=>[m.p[0],m.p[1],1]),x=leastSquares(rows,matches.map(m=>m.q[0]),3),y=leastSquares(rows,matches.map(m=>m.q[1]),3);
    return x&&y?[...x,...y]:null;
  }
  const residual=(t,m)=>Math.hypot((t[0]*m.p[0]+t[1]*m.p[1]+t[2]-m.q[0])*700,(t[3]*m.p[0]+t[4]*m.p[1]+t[5]-m.q[1])*520);
  function robustPair(core,a,b,raw) {
    const candidates=raw.map(m=>({p:inverse(core,a,m.p),q:inverse(core,b,m.q)})).filter(m=>m.p&&m.q&&m.p[0]-m.q[0]>.05&&Math.abs(m.p[1]-m.q[1])<.4);
    let seed=9137+a.id*101+b.id,best=[],bestError=Infinity;
    const random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
    for(let it=0;it<1800&&candidates.length>=6;it++) {
      const sample=[];while(sample.length<3){const p=candidates[Math.floor(random()*candidates.length)];if(!sample.includes(p))sample.push(p);}
      const t=affine(sample);if(!t||t[0]<.35||t[0]>2||t[4]<.5||t[4]>1.8||Math.abs(t[1])>.6||Math.abs(t[3])>.8||t[0]*t[4]-t[1]*t[3]<.3)continue;
      const inliers=candidates.filter(m=>residual(t,m)<4),error=inliers.reduce((s,m)=>s+residual(t,m),0)/Math.max(1,inliers.length);
      if(inliers.length>best.length||inliers.length===best.length&&error<bestError){best=inliers;bestError=error;}
    }
    let model=best.length>=6?affine(best):null;
    if(model)best=candidates.filter(m=>residual(model,m)<4);
    const spread=axis=>best.length?Math.max(...best.map(m=>m.p[axis]))-Math.min(...best.map(m=>m.p[axis])):0;
    const accepted=best.length>=10&&best.length>=.15*candidates.length&&spread(0)>=.025&&spread(1)>=.2;
    return {a:a.id,b:b.id,candidates:candidates.length,count:best.length,error:bestError,accepted,matches:accepted?best:[],model};
  }
  function fit(core,cameras,pairs) {
    const links=pairs.filter(p=>p.accepted),byId=new Map(cameras.map(c=>[c.id,c])),transforms=Object.fromEntries(cameras.map(c=>[c.id,identity()])),components=[];
    const visited=new Set();
    for(const initial of cameras) {
      if(visited.has(initial.id))continue;
      const group=[initial.id];visited.add(initial.id);
      for(let k=0;k<group.length;k++)for(const e of links){const other=e.a===group[k]?e.b:e.b===group[k]?e.a:null;if(other!==null&&!visited.has(other)){visited.add(other);group.push(other);}}
      const anchor=group[0],unknown=group.slice(1),n=unknown.length*3;
      if(!n){components.push({cameras:group,accepted:false});continue;}
      const rowsX=[],rhsX=[],rowsY=[],rhsY=[];
      for(const e of links.filter(e=>group.includes(e.a)&&group.includes(e.b)))for(const m of e.matches) {
        const a=byId.get(e.a),b=byId.get(e.b),weight=1/Math.sqrt(e.matches.length),rx=Array(n).fill(0),ry=Array(n).fill(0);
        for(const [g,p,sign] of [[a,m.p,1],[b,m.q,-1]]) {
          const index=unknown.indexOf(g.id);if(index<0)continue;
          for(let j=0;j<3;j++){const value=j===0?p[0]-.5:j===1?p[1]-.5:1;rx[index*3+j]=sign*g.fov*value*weight;ry[index*3+j]=sign*value*weight;}
        }
        rowsX.push(rx);rhsX.push(-core.delta(a.center+a.fov*(m.p[0]-.5),b.center+b.fov*(m.q[0]-.5))*weight);
        rowsY.push(ry);rhsY.push((m.q[1]-m.p[1])*weight);
      }
      const x=leastSquares(rowsX,rhsX,n,unknown.flatMap(()=>[2.5,10,1.67])),y=leastSquares(rowsY,rhsY,n,unknown.flatMap(()=>[.083,.042,.033]));
      let valid=!!(x&&y);
      if(valid)unknown.forEach((id,k)=>{const t=[1+x[k*3],x[k*3+1],x[k*3+2],y[k*3],1+y[k*3+1],y[k*3+2]];if(!validTransform(t)||byId.get(id).fov*(Math.abs(t[2])+(Math.abs(t[0])+Math.abs(t[1]))/2)>=179)valid=false;transforms[id]=t;});
      let before=0,after=0,samples=0;
      for(const edge of links.filter(e=>group.includes(e.a)&&group.includes(e.b)))for(const m of edge.matches) {
        const a=byId.get(edge.a),b=byId.get(edge.b),p0=core.toAtlas(a,m.p[0],m.p[1]),q0=core.toAtlas(b,m.q[0],m.q[1]);
        const p1=core.toAtlas({...a,stitch:transforms[a.id]},m.p[0],m.p[1]),q1=core.toAtlas({...b,stitch:transforms[b.id]},m.q[0],m.q[1]);
        before+=(core.delta(p0.angle,q0.angle)*1200/360)**2+((p0.v-q0.v)*312)**2;after+=(core.delta(p1.angle,q1.angle)*1200/360)**2+((p1.v-q1.v)*312)**2;samples++;
      }
      before=Math.sqrt(before/Math.max(1,samples));after=Math.sqrt(after/Math.max(1,samples));
      if(before>2&&after>before*.95)valid=false;
      if(!valid)group.forEach(id=>{transforms[id]=identity();});
      components.push({cameras:group,anchor,accepted:valid,beforeError:before,afterError:after,samples});
    }
    const count=components.filter(c=>c.accepted).reduce((s,c)=>s+c.cameras.length,0);
    return {version:1,transforms,count,complete:count===cameras.length,pairs:pairs.map(({matches,model,...p})=>p),components};
  }
  function validTransform(t) {
    return Array.isArray(t)&&t.length===6&&t.every(Number.isFinite)&&t[0]>=.55&&t[0]<=1.6&&Math.abs(t[1])<=.25&&Math.abs(t[2])<=.5&&Math.abs(t[3])<=.22&&t[4]>=.65&&t[4]<=1.4&&Math.abs(t[5])<=.25&&t[0]*t[4]-t[1]*t[3]>=.4;
  }
  function calibrate(core,cameras,rawPairs) {
    const byId=new Map(cameras.map(c=>[c.id,c]));
    const pairs=rawPairs.map(p=>robustPair(core,byId.get(p.a),byId.get(p.b),p.matches));return fit(core,cameras,pairs);
  }
  function signature(cameras) {
    let hash=2166136261;const feed=n=>{hash=Math.imul(hash^(n&255),16777619)>>>0;};
    for(const c of [...cameras].sort((a,b)=>a.id-b.id)) {
      const s=JSON.stringify([c.id,c.center,c.fov,c.projection,c.flip,c.enabled,c.points,c.reference.width,c.reference.height]);for(let i=0;i<s.length;i++)feed(s.charCodeAt(i));
      const data=c.reference.data;for(let i=0;i<data.length;i++)feed(data[i]);
    }
    return hash.toString(16).padStart(8,'0');
  }
  function validate(calibration,ids) {
    if(!calibration||calibration.version!==1||typeof calibration.signature!=='string'||!/^[a-f0-9]{8}$/.test(calibration.signature)||!calibration.transforms||!Array.isArray(calibration.pairs)||!Number.isInteger(calibration.count)||calibration.count<0||calibration.count>ids.length)throw new Error('Некорректная схема совмещения');
    for(const id of ids)if(!validTransform(calibration.transforms[id]))throw new Error('Некорректная подгонка камеры '+id);
    return calibration;
  }
  const api={identity,solve,leastSquares,inverse,robustPair,fit,calibrate,validTransform,signature,validate};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;else root.UnwrapStitching=api;
})(typeof self!=='undefined'?self:globalThis);
