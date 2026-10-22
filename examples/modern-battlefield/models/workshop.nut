// Geometry authoring helpers. Dimensions are metres; shapes are baked by material
// and articulated joint into meshes once, rather than one draw per bolt/tread.
local PI=3.14159265358979;
local function rotation(y,p,r) {
    local cy=cos(y),sy=sin(y),cp=cos(p),sp=sin(p),cr=cos(r),sr=sin(r);
    return [cy*cr+sy*sp*sr,-cy*sr+sy*sp*cr,sy*cp,
        cp*sr,cp*cr,-sp,-sy*cr+cy*sp*sr,sy*sr+cy*sp*cr,cy*cp];
}
local function vector(m,v) {
    return [m[0]*v[0]+m[1]*v[1]+m[2]*v[2],m[3]*v[0]+m[4]*v[1]+m[5]*v[2],m[6]*v[0]+m[7]*v[1]+m[8]*v[2]];
}
local function multiply(a,b) {
    local m=[];
    for(local r=0;r<3;r++) for(local c=0;c<3;c++)
        m.push(a[r*3]*b[c]+a[r*3+1]*b[c+3]+a[r*3+2]*b[c+6]);
    return m;
}
local Workshop = class {
    groups=null; owner=null; nodes=null;
    constructor(a) {
        owner=a; groups={};
        nodes={body={parent=null,p=[0.0,0.0,0.0]},turret={parent="body",p=[0.0,0.0,0.0]},
            barrel={parent="turret",p=[0.0,0.0,0.0]},rotor={parent="body",p=[0.0,0.0,0.0]},
            tailrotor={parent="body",p=[0.0,0.0,0.0]}};
        for(local i=0;i<4;i++) nodes["prop"+i]<-{parent="body",p=[0.0,0.0,0.0],spin=1.0+i*0.07,phase=i*0.45};
    }
    function setJointPivot(j,x,y,z) { nodes[j].p=[x.tofloat(),y.tofloat(),z.tofloat()]; }
    function bakeFaces(v,faces,c,j="body",x=0.0,y=0.0,z=0.0,yaw=0.0,pitch=0.0,roll=0.0) {
        local key=j+":"+c[0]+":"+c[1]+":"+c[2];
        if(!(key in groups)) groups[key]<-{pos=[],nrm=[],uv=[],idx=[],c=c,j=j};
        local g=groups[key], mat=rotation(yaw,pitch,roll), verts=[];
        foreach(q in v) { local w=vector(mat,q); verts.push([w[0]+x,w[1]+y,w[2]+z]); }
        foreach(f in faces) {
            for(local k=1;k<f.len()-1;k++) {
                local a=verts[f[0]],b=verts[f[k]],d=verts[f[k+1]];
                local u=[b[0]-a[0],b[1]-a[1],b[2]-a[2]],w=[d[0]-a[0],d[1]-a[1],d[2]-a[2]];
                local n=[u[1]*w[2]-u[2]*w[1],u[2]*w[0]-u[0]*w[2],u[0]*w[1]-u[1]*w[0]];
                local len=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]); if(len<0.0000001) continue;
                foreach(pt in [a,b,d]) {
                    g.idx.push(g.pos.len()/3);
                    foreach(t in pt) g.pos.push(t.tofloat());
                    foreach(t in n) g.nrm.push((t/len).tofloat());
                    g.uv.push(0.0); g.uv.push(0.0);
                }
            }
        }
    }
    function armorBox(x,y,z,w,h,l,c,j="body",yaw=0.0,pitch=0.0,roll=0.0) {
        local a=w*0.5,b=h*0.5,d=l*0.5;
        bakeFaces([[-a,-b,-d],[a,-b,-d],[a,b,-d],[-a,b,-d],[-a,-b,d],[a,-b,d],[a,b,d],[-a,b,d]],
            [[0,3,2,1],[4,5,6,7],[0,4,7,3],[1,2,6,5],[3,7,6,2],[0,1,5,4]],c,j,x,y,z,yaw,pitch,roll);
    }
    // Bevelled polygonal cross sections [z, half-width, bottom-y, top-y].
    function hullLoft(sections,c,j="body",bevel=0.15) {
        local v=[],f=[];
        foreach(s in sections) {
            local z=s[0],w=s[1],b=s[2],t=s[3],q=(t-b)*bevel;
            foreach(p in [[-w*0.78,b],[w*0.78,b],[w,b+q],[w,t-q],[w*0.78,t],[-w*0.78,t],[-w,t-q],[-w,b+q]])
                v.push([p[0],p[1],z]);
        }
        f.push([7,6,5,4,3,2,1,0]); local end=(sections.len()-1)*8;
        f.push([end,end+1,end+2,end+3,end+4,end+5,end+6,end+7]);
        for(local s=0;s<sections.len()-1;s++) for(local k=0;k<8;k++) {
            local n=(k+1)%8; f.push([s*8+k,s*8+n,(s+1)*8+n,(s+1)*8+k]);
        }
        bakeFaces(v,f,c,j);
    }
    // Z-axis lathe with 24-sided rings [z, radius]; orientation can be baked.
    function profileTube(x,y,z,rings,c,j="body",yaw=0.0,pitch=0.0,roll=0.0,sides=24) {
        local v=[],f=[];
        foreach(r in rings) for(local k=0;k<sides;k++) {
            local a=k*2*PI/sides; v.push([cos(a)*r[1],sin(a)*r[1],r[0]]);
        }
        local first=[],last=[],offset=(rings.len()-1)*sides;
        for(local k=0;k<sides;k++) { first.push(sides-1-k); last.push(offset+k); }
        f.push(first); f.push(last);
        for(local i=0;i<rings.len()-1;i++) for(local k=0;k<sides;k++) {
            local n=(k+1)%sides; f.push([i*sides+k,i*sides+n,(i+1)*sides+n,(i+1)*sides+k]);
        }
        bakeFaces(v,f,c,j,x,y,z,yaw,pitch,roll);
    }
    function roundTube(x,y,z,r,length,c,j="body",yaw=0.0,pitch=0.0) {
        profileTube(x,y,z,[[-length*0.5,r],[length*0.5,r]],c,j,yaw,pitch);
    }
    // Extruded polygon in X/Z, for swept wings, tail surfaces and split trails.
    function wingPanel(points,y,thickness,c,j="body") {
        local area=0.0;
        for(local i=0;i<points.len();i++) { local k=(i+1)%points.len(); area+=points[i][0]*points[k][1]-points[k][0]*points[i][1]; }
        if(area<0) { local reversed=[]; for(local i=points.len()-1;i>=0;i--) reversed.push(points[i]); points=reversed; }
        local v=[],f=[],n=points.len(),bot=[],top=[];
        foreach(h in [y-thickness*0.5,y+thickness*0.5]) foreach(p in points) v.push([p[0],h,p[1]]);
        for(local i=0;i<n;i++) { bot.push(i); top.push(n+n-1-i); local k=(i+1)%n; f.push([i,n+i,n+k,k]); }
        f.push(bot); f.push(top); bakeFaces(v,f,c,j);
    }
    function finPlate(points,x,thickness,c,j="body") {
        local v=[],f=[],n=points.len(),left=[],right=[];
        foreach(xx in [x-thickness*0.5,x+thickness*0.5]) foreach(p in points) v.push([xx,p[1],p[0]]);
        for(local i=0;i<n;i++) { left.push(n-1-i); right.push(n+i); local k=(i+1)%n; f.push([i,k,n+k,n+i]); }
        f.push(left); f.push(right); bakeFaces(v,f,c,j);
    }
    function braceBeam(a,b,width,c,j="body") {
        local dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2],len=sqrt(dx*dx+dy*dy+dz*dz);
        armorBox((a[0]+b[0])*0.5,(a[1]+b[1])*0.5,(a[2]+b[2])*0.5,width,width,len,c,j,
            atan2(dx,dz),-atan2(dy,sqrt(dx*dx+dz*dz)));
    }
    function trackedRunningGear(length,width,count,paint) {
        local metal=[0.17,0.19,0.18],tire=[0.055,0.065,0.06];
        foreach(s in [-1,1]) {
            armorBox(s*width,0.63,0,0.62,0.84,length,tire);
            for(local i=0;i<count;i++) {
                local z=-length*0.39+i*length*0.78/(count-1);
                roundTube(s*(width+0.16),0.62,z,0.39,0.32,metal,"body",1.5708);
                roundTube(s*(width+0.34),0.62,z,0.27,0.045,paint,"body",1.5708);
                roundTube(s*(width+0.37),0.62,z,0.09,0.06,metal,"body",1.5708);
                for(local k=0;k<6;k++) { local t=k*1.0472;
                    roundTube(s*(width+0.37),0.62+sin(t)*0.19,z+cos(t)*0.19,0.027,0.04,metal,"body",1.5708); }
            }
            local steps=(length/0.20).tointeger();
            for(local i=0;i<=steps;i++) foreach(y in [0.18,1.08])
                armorBox(s*width,y,-length*0.5+i*length/steps,0.69,0.07,0.14,metal);
            foreach(z in [-length*0.5,length*0.5]) for(local k=0;k<6;k++)
                armorBox(s*width,0.24+k*0.15,z,0.69,0.09,0.12,metal);
        }
    }
    // Soft filled cloth: pinched ends, compressed underside and a sewn rim.
    function filledBag(x,y,z,w,h,d,c,yaw=0.0) {
        local v=[],f=[],sections=[[-0.50,0.22],[-0.46,0.65],[-0.32,0.95],[0.0,1.0],[0.32,0.96],[0.46,0.65],[0.50,0.22]];
        foreach(s in sections) for(local k=0;k<12;k++) {
            local t=k*PI/6,cy=cos(t),sz=sin(t);
            local yy=(cy<0?-1:1)*pow(fabs(cy),0.65)*h*0.52*s[1];
            if(yy< -h*0.44) yy=-h*0.44;
            v.push([s[0]*w,yy,(sz<0?-1:1)*pow(fabs(sz),0.65)*d*0.5*s[1]]);
        }
        local cap1=[],cap2=[];
        for(local k=0;k<12;k++) { cap1.push(11-k);cap2.push(72+k); }
        f.push(cap1);f.push(cap2);
        for(local i=0;i<6;i++) for(local k=0;k<12;k++) {
            local n=(k+1)%12;f.push([i*12+k,i*12+n,(i+1)*12+n,(i+1)*12+k]);
        }
        bakeFaces(v,f,c,"body",x,y,z,yaw);
        local seam=[c[0]*0.66,c[1]*0.66,c[2]*0.66];
        foreach(s in [-1,1]) for(local i=0;i<6;i++) {
            local xx=(-0.43+i*0.143)*w,zz=s*d*(i==0||i==5?0.36:0.505);
            armorBox(x+xx*cos(yaw)+zz*sin(yaw),y+0.025,z-xx*sin(yaw)+zz*cos(yaw),0.11*w,0.012,0.013,seam,"body",yaw);
        }
        armorBox(x+w*0.50*cos(yaw),y,z-w*0.50*sin(yaw),0.09,0.06,0.11,c,"body",yaw,0,0.2);
    }
    function roundRod(a,b,r,c,sides=8) {
        local dx=b[0]-a[0],dy=b[1]-a[1],dz=b[2]-a[2],len=sqrt(dx*dx+dy*dy+dz*dz);
        profileTube((a[0]+b[0])*0.5,(a[1]+b[1])*0.5,(a[2]+b[2])*0.5,[[-len*0.5,r],[len*0.5,r]],c,"body",
            atan2(dx,dz),-atan2(dy,sqrt(dx*dx+dz*dz)),0,sides);
    }
    function structuralIbeam(x,y,z,length,yaw,pitch,roll,c) {
        local r=rotation(yaw,pitch,roll);
        armorBox(x,y,z,0.055,0.30,length,c,"body",yaw,pitch,roll);
        foreach(s in [-1,1]) {
            local offset=vector(r,[0,s*0.15,0]);
            armorBox(x+offset[0],y+offset[1],z+offset[2],0.35,0.045,length,c,"body",yaw,pitch,roll);
            foreach(end in [-1,1]) {
                local q=vector(r,[0,s*0.177,end*(length*0.5-0.11)]);
                armorBox(x+q[0],y+q[1],z+q[2],0.30,0.012,0.20,[0.32,0.24,0.15],"body",yaw,pitch,roll);
            }
        }
    }
    // Small solid stencil glyphs are geometry, requiring no font/texture service.
    function stencilGlyphs(text,x,y,z,cell,c,yaw=0.0,pitch=0.0) {
        local glyphs={
            A=["01110","10001","10001","11111","10001","10001","10001"],
            M=["10001","11011","10101","10101","10001","10001","10001"],
            O=["01110","10001","10001","10001","10001","10001","01110"],
            E=["11111","10000","10000","11110","10000","10000","11111"],
            D=["11110","10001","10001","10001","10001","10001","11110"],
            G=["01111","10000","10000","10111","10001","10001","01111"],
            P=["11110","10001","10001","11110","10000","10000","10000"],
            B=["11110","10001","10001","11110","10001","10001","11110"]
        };
        local r=rotation(yaw,pitch,0);
        for(local k=0;k<text.len();k++) {
            local ch=text.slice(k,k+1); if(!(ch in glyphs)) throw "Unsupported model stencil glyph: "+ch;
            local rows=glyphs[ch];
            for(local row=0;row<7;row++) for(local col=0;col<5;col++) if(rows[row][col]==49) {
                local offset=vector(r,[(k*6+col-(text.len()*6-2)*0.5)*cell,(3-row)*cell,0]);
                armorBox(x+offset[0],y+offset[1],z+offset[2],cell*0.86,cell*0.86,0.003,c,"body",yaw,pitch);
            }
        }
    }
    function finishModel() {
        local parts=[];
        foreach(k,g in groups) {
            local m=gfx.newMeshFromArrays(g.pos,g.nrm,g.uv,g.pos.len()/3,g.idx,g.idx.len());
            if(m==null) throw "Model upload failed: "+owner.kind+" / "+k;
            local e=eve.Renderable3D(); e.setMesh(m); e.setTint(g.c[0].tofloat(),g.c[1].tofloat(),g.c[2].tofloat(),1.0);
            e.setReceiveLight(true); e.setRoughness(0.72);
            parts.push({e=e,mesh=m,j=g.j});
        }
        return {parts=parts,nodes=nodes};
    }
};
export function newWorkshop(a) { return Workshop(a); }
export function poseDetailed(a) {
    local transforms={};
    foreach(j in ["body","turret","barrel","rotor","tailrotor","prop0","prop1","prop2","prop3"]) {
        local node=a.rig.nodes[j],p=node.p;
        local yaw=j=="body"?a.yaw:(j=="turret"?a.turret:(j=="rotor"?a.rotor:0.0));
        local pitch=j=="barrel"?-a.elevation:(j=="tailrotor"?a.rotor*1.7:0.0),roll=0.0;
        if(j=="body"&&("bank" in a)) roll=a.bank;
        if("spin" in node) roll=a.rotor*node.spin+node.phase;
        local r=rotation(yaw,pitch,roll),pos=[p[0],p[1],p[2]-(j=="barrel"?a.recoil:0.0)];
        if(node.parent!=null) {
            local parent=transforms[node.parent],v=vector(parent.r,pos);
            pos=[parent.p[0]+v[0],parent.p[1]+v[1],parent.p[2]+v[2]]; r=multiply(parent.r,r);
        } else pos=[a.x+p[0],a.y+p[1],a.z+p[2]];
        transforms[j]<-{r=r,p=pos};
    }
    if(a.armed) {
        local b=transforms.barrel;
        a.muzzle <- {x=b.p[0]+b.r[2]*a.barrel,y=b.p[1]+b.r[5]*a.barrel,z=b.p[2]+b.r[8]*a.barrel};
    }
    foreach(part in a.rig.parts) {
        local t=transforms[part.j],r=t.r,p=t.p;
        local v=r[5]< -1?-1.0:(r[5]>1?1.0:r[5]);
        local pitch=asin(-v),yaw=atan2(r[2],r[8]),roll=atan2(r[3],r[4]);
        part.e.setPosition(p[0].tofloat(),p[1].tofloat(),p[2].tofloat());
        part.e.setRotation(yaw.tofloat(),pitch.tofloat(),roll.tofloat());
    }
}
