// Example-owned geometry. All uploads are on the main/render thread, only on Apply.
// Ground is a finite Voronoi patch: no stencil or runtime terrain Boolean required.
function bounded(v,a,b) { return v<a?a:(v>b?b:v); }
function smooth(v) { v=bounded(v,0.0,1.0); return v*v*(3.0-2.0*v); }
function randomStream(seed) {
    local state=seed.tointeger()&0x7fffffff;
    return function() { state=(state*1664525+1013904223)&0x7fffffff; return state/2147483648.0; };
}
function clipCell(poly,nx,nz,c) {
    local out=[];
    foreach(i,b in poly) {
        local a=poly[(i+poly.len()-1)%poly.len()];
        local da=a[0]*nx+a[1]*nz-c, db=b[0]*nx+b[1]*nz-c;
        if((da<=0.0)!=(db<=0.0)) {
            local t=da/(da-db); out.push([a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t]);
        }
        if(db<=0.0) out.push(b);
    }
    return out;
}
function meshData() { return {p=[],n=[],uv=[],idx=[]}; }
function vertex(m,x,y,z,nx,ny,nz,u,v) {
    m.p.extend([x.tofloat(),y.tofloat(),z.tofloat()]);
    m.n.extend([nx.tofloat(),ny.tofloat(),nz.tofloat()]); m.uv.extend([u.tofloat(),v.tofloat()]);
}
function polygonMeshes(poly,cx,cz,depth) {
    local top=meshData(), wall=meshData();
    foreach(p in poly) vertex(top,p[0]-cx,0.0,p[1]-cz,0.0,1.0,0.0,p[0]*0.4,p[1]*0.4);
    for(local i=1;i<poly.len()-1;i++) top.idx.extend([0,i+1,i]);
    foreach(i,a in poly) {
        local b=poly[(i+1)%poly.len()], dx=b[0]-a[0], dz=b[1]-a[1];
        local len=sqrt(dx*dx+dz*dz); if(len<0.00001) continue;
        local nx=dz/len,nz=-dx/len,first=wall.p.len()/3;
        vertex(wall,a[0]-cx,0.0,a[1]-cz,nx,0.0,nz,0.0,0.0);
        vertex(wall,b[0]-cx,0.0,b[1]-cz,nx,0.0,nz,len*0.5,0.0);
        vertex(wall,b[0]-cx,-depth,b[1]-cz,nx,0.0,nz,len*0.5,depth*0.5);
        vertex(wall,a[0]-cx,-depth,a[1]-cz,nx,0.0,nz,0.0,depth*0.5);
        wall.idx.extend([first,first+1,first+2,first,first+2,first+3]);
    }
    return [top,wall];
}
function uploadMesh(old,m) {
    if(old==null) {
        local mesh=gfx.newMeshFromArrays(m.p,m.n,m.uv,m.p.len()/3,m.idx,m.idx.len());
        if(mesh==null) throw "ground-fissure: mesh upload failed";
        return mesh;
    }
    if(!gfx.updateMeshVertices(old,m.p,m.n,m.uv,m.p.len()/3,m.idx,m.idx.len()))
        throw "ground-fissure: mesh update failed";
    return old;
}
function createFaultPaths(p,radial) {
    local rng=randomStream(p.seed+701),paths=[],length=p.length;
    local addPath=function(x,z,dx,dz,count,start,span) {
        local path=[];
        for(local i=0;i<=count;i++) {
            local t=i.tofloat()/count,offset=(rng()-0.5)*0.85;
            path.push({x=x+dx*t-dz/length*offset,z=z+dz*t+dx/length*offset,t=start+t*span});
        }
        paths.push(path); return path;
    };
    if(radial) {
        local arms=3+p.branches.tointeger();
        for(local i=0;i<arms;i++) {
            local a=i*6.283185/arms+0.2;
            addPath(0.0,0.0,cos(a)*length*0.48,sin(a)*length*0.48,7,0.05,0.85);
        }
    } else {
        local main=addPath(0.0,length*0.5,0.0,-length,16,0.0,1.0);
        for(local i=0;i<p.branches.tointeger();i++) {
            local key=4+i*3,root=main[key],sign=i%2==0?1.0:-1.0;
            addPath(root.x,root.z,sign*(2.0+rng()*2.8),-1.4-rng()*2.0,5,root.t,0.24);
        }
    }
    return paths;
}
function nearestFault(paths,x,z) {
    local best={d=1000.0,x=0.0,z=0.0,nx=1.0,nz=0.0,t=0.0};
    foreach(path in paths) for(local i=0;i<path.len()-1;i++) {
        local a=path[i],b=path[i+1],dx=b.x-a.x,dz=b.z-a.z;
        local t=bounded(((x-a.x)*dx+(z-a.z)*dz)/(dx*dx+dz*dz),0.0,1.0);
        local px=a.x+dx*t,pz=a.z+dz*t,ox=x-px,oz=z-pz,d=sqrt(ox*ox+oz*oz);
        if(d<best.d) best={d=d,x=px,z=pz,nx=ox/(d+0.00001),nz=oz/(d+0.00001),t=a.t+(b.t-a.t)*t};
    }
    return best;
}
function makeCells(seed) {
    local rng=randomStream(seed+31),sites=[],out=[];
    for(local z=0;z<10;z++) for(local x=0;x<12;x++)
        sites.push([-9.0+(x+0.5+(rng()-0.5)*0.8)*1.5,-8.0+(z+0.5+(rng()-0.5)*0.8)*1.6]);
    foreach(i,a in sites) {
        local poly=[[-9.0,-8.0],[9.0,-8.0],[9.0,8.0],[-9.0,8.0]];
        // Nearby sites first bound the cell. Reject redundant half-planes exactly
        // using the polygon support, rather than allocating a copy for every site.
        for(local pass=0;pass<2;pass++) foreach(j,b in sites) if(i!=j && poly.len()>0) {
            local dx=b[0]-a[0],dz=b[1]-a[1],near=dx*dx+dz*dz<12.0;
            if((pass==0)!=near) continue;
            local cut=(b[0]*b[0]+b[1]*b[1]-a[0]*a[0]-a[1]*a[1])*0.5,needed=false;
            foreach(p in poly) if(p[0]*dx+p[1]*dz>cut) {needed=true;break;}
            if(needed) poly=clipCell(poly,dx,dz,cut);
        }
        if(poly.len()<3) throw "ground-fissure: empty cell";
        local cx=0.0,cz=0.0; foreach(p in poly) {cx+=p[0];cz+=p[1];}
        out.push({poly=poly,x=cx/poly.len(),z=cz/poly.len(),random=rng()});
    }
    return out;
}
