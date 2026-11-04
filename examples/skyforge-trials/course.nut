// All geometry is authored in metres. No external models or texture assets.
function material3(r,g,b,metal=0.0) {
    local m=gfx.newMaterial(); m.setTint(r,g,b,1.0); m.setMetallic(metal); m.setRoughness(0.62);
    site.materials.append(m); return m;
}
function visual3(mesh,mat,x,y,z,sx,sy,sz) {
    local e=eve.Renderable3D(); e.setMesh(mesh); e.setMaterial(mat);
    e.setPosition(x,y,z); e.setScale(sx,sy,sz);
    e.setCastShadow(true); e.setReceiveShadow(true); site.visuals.append(e); return e;
}
function cube3(mat,x,y,z,sx,sy,sz) { return visual3(site.cube,mat,x,y,z,sx,sy,sz); }
function rod3(mat,ax,ay,az,bx,by,bz,radius) {
    local e=visual3(site.cylinder,mat,0.0,0.0,0.0,1.0,1.0,1.0);
    segment3(e,ax,ay,az,bx,by,bz,radius); return e;
}
function segment3(e,ax,ay,az,bx,by,bz,radius) {
    local dx=bx-ax,dy=by-ay,dz=bz-az, length=sqrt(dx*dx+dy*dy+dz*dz);
    if(length<0.0001) return;
    e.setPosition((ax+bx)*0.5,(ay+by)*0.5,(az+bz)*0.5);
    e.setRotation(atan2(dx,dz),acos(clamp3(dy/length,-1.0,1.0)),0.0);
    e.setScale(radius,length,radius);
}
function physical3(x,y,z,sx,sy,sz,tag,mat,type="static",density=1.0) {
    local body=site.world.newBody(type,x,y,z);
    local shape=body.newBoxShape(sx,sy,sz,density,0.65,0.03); shape.setTag(tag);
    local v=cube3(mat,x,y,z,sx,sy,sz);
    local record={body=body,shape=shape,visual=v,x=x,y=y,z=z,tag=tag,type=type};
    site.bodies.append(record); site.byId[body.getId()] <- record; return record;
}
function trigger3(x,y,z,sx,sy,sz,tag) {
    local body=site.world.newBody("static",x,y,z);
    local s=body.newBoxShape(sx,sy,sz,1.0,0.0,0.0);
    s.setTag(tag); s.setSensor(true); s.setCategoryBits(4); s.setMaskBits(tag==40?8:2);
    site.triggers.append(body); return body;
}
function deck3(x,z,sx,sz,height,rails=true) {
    physical3(x,height-0.45,z,sx,0.9,sz,1,site.concrete);
    cube3(site.navy,x,height-1.05,z,sx+0.35,0.35,sz+0.35);
    foreach(side in [-1.0,1.0]) {
        cube3(site.cyan,x+side*(sx/2.0-0.22),height+0.035,z,0.10,0.06,sz-0.5);
        if(rails) {
            physical3(x+side*(sx/2.0+0.1),height+0.65,z,0.22,1.3,sz,1,site.navy);
            cube3(site.gold,x+side*(sx/2.0+0.1),height+1.34,z,0.28,0.1,sz);
        }
    }
    // Visible load-bearing trusses connect each island to the foundation.
    foreach(dx in [-1.0,1.0]) foreach(dz in [-1.0,1.0]) {
        local px=x+dx*(sx/2.0-1.0), pz=z+dz*(sz/2.0-1.0);
        cube3(site.navy,px,(height-7.0)*0.5,pz,0.7,height+7.0,0.7);
        cube3(site.steel,px,-6.5,pz,1.4,0.6,1.4);
    }
    for(local zz=z-sz/2.0+2.0;zz<z+sz/2.0;zz+=3.0)
        cube3(site.white,x,height+0.025,zz,0.15,0.04,1.0);
}
function portal3(x,z,y,number,mat) {
    foreach(dx in [-5.8,5.8]) {
        physical3(x+dx,y+2.5,z,0.6,5.0,0.8,1,site.navy);
        cube3(mat,x+dx,y+2.5,z-0.43,0.14,4.0,0.08);
    }
    physical3(x,y+5.0,z,12.2,0.8,0.8,1,site.navy);
    cube3(mat,x,y+4.9,z-0.44,8.0,0.12,0.1);
    // Large physical stroke numerals remain readable in an overview.
    local patterns=["111101101101111","010110010010111","111001111100111","111001111001111",
        "101101111001001","111100111001111","111100111101111","111001010010010","111101111101111"];
    local p=patterns[number];
    for(local row=0;row<5;row++) for(local col=0;col<3;col++)
        if(p.slice(row*3+col,row*3+col+1)=="1")
            cube3(mat,x+(col-1)*0.25,y+5.55+(2-row)*0.23,z-0.45,0.2,0.18,0.08);
}
function checkpoint3(index,x,y,z,yaw) {
    site.checkpoints.append({x=x,y=y,z=z,yaw=yaw});
    trigger3(x,y+1.2,z,9.0,2.5,2.0,100+index);
    cube3(site.cyan,x,y+0.06,z,10.0,0.10,0.6);
    foreach(dx in [-5.0,5.0]) {
        cube3(site.steel,x+dx,y+0.7,z,0.4,1.4,0.4);
        cube3(site.cyan,x+dx,y+1.5,z,0.34,0.3,0.34);
    }
}
function sphereBody3(x,y,z,r,tag,mat,type="dynamic",density=2.0) {
    local b=site.world.newBody(type,x,y,z);
    local s=b.newSphereShape(r,density,0.5,0.1); s.setTag(tag);
    local v=visual3(site.sphere,mat,x,y,z,r*2.0,r*2.0,r*2.0);
    local rec={body=b,shape=s,visual=v,x=x,y=y,z=z,tag=tag,type=type};
    site.bodies.append(rec); site.byId[b.getId()] <- rec; return rec;
}
function createCourse() {
    site={world=physics.newWorld3D(0.0,-20.0,0.0,true),visuals=[],materials=[],bodies=[],byId={},
        triggers=[],movers=[],pendulums=[],rollers=[],checkpoints=[],runner=TrialRunner.create(),
        clock=0.0,remainder=0.0,jumpBuffer=0.0,coyote=0.0,grounded=false,support=null,
        overview=true,yaw=0.0,facing=0.0,gate=null,crate=null,gateHeight=4.0,
        respawnPending=false,camera=null,parts=[],plateVisual=null,beacon=null};
    site.cube <- gfx.newMeshCube(1.0); site.sphere <- gfx.newMeshSphere(20,12);
    site.cylinder <- gfx.newMeshCylinder(16,1,true);
    site.concrete <- material3(0.55,0.64,0.66);
    site.navy <- material3(0.055,0.105,0.15,0.4);
    site.steel <- material3(0.24,0.33,0.39,0.55);
    site.white <- material3(0.87,0.91,0.88);
    site.gold <- material3(1.0,0.52,0.08,0.25);
    site.cyan <- material3(0.08,0.85,0.88,0.25);
    site.red <- material3(0.96,0.14,0.08,0.2);
    site.blue <- material3(0.08,0.35,0.55,0.4);
    site.dark <- material3(0.07,0.15,0.19);
    // Foundation, recessed cooling basin, perimeter bastions and distant skyline.
    cube3(site.navy,0.0,-8.5,10.0,104.0,3.0,116.0);
    cube3(site.blue,0.0,-6.95,10.0,100.0,0.12,112.0);
    for(local xx=-48.0;xx<=48.0;xx+=8.0) cube3(site.cyan,xx,-6.84,10.0,0.06,0.04,110.0);
    for(local zz=-44.0;zz<=64.0;zz+=8.0) cube3(site.cyan,0.0,-6.84,zz,100.0,0.04,0.06);
    foreach(x in [-50.0,50.0]) foreach(z in [-45.0,-9.0,27.0,63.0]) {
        cube3(site.steel,x,0.0,z,3.0,16.0,3.0);
        cube3(site.gold,x,8.5,z,3.4,1.0,3.4);
        cube3(site.navy,x,11.5,z,2.2,5.0,2.2);
        cube3(site.cyan,x,14.0,z,2.4,0.2,2.4);
    }
    foreach(x in [-51.0,51.0]) cube3(site.steel,x,-4.0,9.0,1.0,5.0,114.0);
    foreach(z in [-46.0,66.0]) cube3(site.steel,0.0,-4.0,z,104.0,5.0,1.0);
    for(local i=0;i<18;i++) {
        local x=(i%2==0?-1.0:1.0)*(68.0+(i%4)*11.0), z=-48.0+i*9.0;
        local h=12.0+(i%5)*5.0;
        cube3(site.dark,x,h/2.0-8.0,z,8.0,h,8.0);
        cube3(site.steel,x,h-7.8,z,8.5,0.35,8.5);
    }
    // 01: arrival and a broad ascending staircase.
    deck3(-26.0,-28.0,14.0,16.0,0.0);
    for(local i=0;i<5;i++) {
        local top=(i+1)*0.4;
        physical3(-26.0,top/2.0,-19.0+i*2.0,14.0,top,2.0,1,site.concrete);
        cube3(site.gold,-26.0,top+0.025,-19.9+i*2.0,13.8,0.04,0.12);
    }
    portal3(-26.0,-23.0,0.0,1,site.cyan);
    deck3(-26.0,16.0,14.0,54.0,2.0);
    // 02: two real rotating sweeper beams; low enough to jump.
    foreach(z in [0.0,12.0]) {
        cube3(site.steel,-26.0,2.2,z,1.2,0.4,1.2);
        local beam=physical3(-26.0,2.55,z,12.0,0.55,0.65,20,site.red,"kinematic");
        site.movers.append({record=beam,kind="rotor",phase=z*0.13});
        visual3(site.cylinder,site.gold,-26.0,2.7,z,1.3,0.7,1.3);
    }
    portal3(-26.0,-7.0,2.0,2,site.gold);
    // 03: heavy cargo must overlap the plate; player sensors cannot unlock it.
    portal3(-26.0,21.0,2.0,3,site.gold);
    site.crate=physical3(-30.0,2.65,23.0,1.25,1.25,1.25,10,site.gold,"dynamic",2.0);
    site.crate.shape.setCategoryBits(8); site.crate.body.setLinearDamping(0.3);
    site.plateVisual=cube3(site.cyan,-30.0,2.045,27.0,3.0,0.09,3.0);
    trigger3(-30.0,2.22,27.0,3.0,0.45,3.0,40);
    site.gate=physical3(-26.0,4.0,34.0,14.0,4.0,0.65,5,site.blue,"kinematic");
    foreach(dx in [-7.0,7.0]) cube3(site.steel,-26.0+dx,5.0,34.0,0.8,6.0,1.3);
    cube3(site.steel,-26.0,8.0,34.0,15.0,0.5,1.3);
    checkpoint3(1,-26.0,2.0,39.0,1.5707963);
    // 04: turn east, board the lift, then ride the ferry south.
    deck3(-25.0,46.0,16.0,8.0,2.0,false);
    deck3(0.0,46.0,14.0,8.0,4.0,false);
    local lift=physical3(-12.0,2.1,46.0,6.0,0.5,7.0,3,site.cyan,"kinematic");
    site.movers.append({record=lift,kind="lift",phase=0.0});
    foreach(x in [-15.5,-8.5]) {
        cube3(site.steel,x,3.5,49.5,0.25,7.0,0.25);
        cube3(site.gold,x,7.0,49.5,0.8,0.25,0.8);
    }
    deck3(0.0,39.0,14.0,14.0,4.0);
    checkpoint3(2,0.0,4.0,42.0,3.14159265);
    portal3(0.0,39.0,4.0,4,site.cyan);
    local ferry=physical3(0.0,3.75,26.0,7.0,0.5,5.0,3,site.cyan,"kinematic");
    site.movers.append({record=ferry,kind="ferry",phase=0.0});
    foreach(x in [-4.2,4.2]) rod3(site.steel,x,2.0,17.0,x,2.0,35.0,0.12);
    // 05: hanging hammers, with native spherical colliders and timed swept motion.
    deck3(0.0,2.0,14.0,34.0,4.0);
    checkpoint3(3,0.0,4.0,15.0,3.14159265);
    portal3(0.0,14.0,4.0,5,site.gold);
    foreach(i,z in [8.0,0.0,-8.0]) {
        foreach(x in [-7.3,7.3]) cube3(site.steel,x,7.4,z,0.5,7.0,0.7);
        cube3(site.steel,0.0,11.0,z,15.0,0.6,0.8);
        local bob=sphereBody3(0.0,5.0,z,1.15,21,site.gold,"kinematic");
        local cable=rod3(site.navy,0.0,11.0,z,0.0,5.0,z,0.14);
        site.pendulums.append({record=bob,cable=cable,z=z,phase=i*1.7});
    }
    checkpoint3(4,0.0,4.0,-12.0,1.5707963);
    deck3(0.0,-18.0,14.0,6.0,4.0);
    deck3(13.0,-22.0,40.0,8.0,4.0,false);
    // 06: press hall. Red collision bodies cycle vertically; visible overhead frames.
    deck3(26.0,-5.0,14.0,36.0,4.0);
    portal3(26.0,-18.0,4.0,6,site.gold);
    foreach(i,z in [-11.0,-3.0,5.0]) {
        foreach(x in [18.5,33.5]) cube3(site.steel,x,8.0,z,0.65,8.0,0.8);
        physical3(26.0,12.0,z,16.0,0.8,3.0,1,site.navy);
        local press=physical3(26.0,8.0,z,12.0,2.0,2.6,22,site.red,"kinematic");
        site.movers.append({record=press,kind="press",phase=i*1.8});
        cube3(site.gold,26.0,4.035,z,12.0,0.06,2.8);
    }
    checkpoint3(5,28.3,4.0,11.0,0.0);
    // 07: rolling foundry balls are dynamic, impart impulses and recycle at the far gutter.
    deck3(26.0,23.0,14.0,20.0,4.0);
    portal3(26.0,15.0,4.0,7,site.gold);
    foreach(i,x in [22.0,26.0,30.0]) {
        local ball=sphereBody3(x,5.2,29.0-i*4.0,1.0,23,site.steel,"dynamic",3.0);
        site.rollers.append(ball);
        cube3(site.navy,x,8.0,30.0,2.6,0.6,3.0);
        cube3(site.gold,x,8.35,30.0,2.8,0.12,3.2);
        foreach(dx in [-1.15,1.15]) cube3(site.steel,x+dx,6.0,30.0,0.15,4.0,0.15);
    }
    checkpoint3(6,28.3,4.0,30.0,0.0);
    // 08: offset islands over the basin, followed by the finish pavilion.
    foreach(p in [{x=24.0,z=35.0,y=4.0},{x=28.0,z=38.5,y=4.5},{x=25.0,z=42.0,y=4.0}]) {
        physical3(p.x,p.y-0.3,p.z,3.0,0.6,3.0,1,site.white);
        cube3(site.cyan,p.x,p.y+0.02,p.z,2.5,0.04,0.2);
        cube3(site.navy,p.x,-1.0,p.z,0.6,10.0,0.6);
    }
    deck3(26.0,50.0,16.0,12.0,4.0);
    portal3(26.0,48.0,4.0,8,site.cyan);
    trigger3(26.0,5.5,49.0,11.0,3.0,2.0,200);
    cube3(site.cyan,26.0,4.07,49.0,12.0,0.12,2.0);
    foreach(dx in [-6.0,6.0]) {
        cube3(site.white,26.0+dx,6.0,54.0,1.2,4.0,1.2);
        visual3(site.sphere,site.gold,26.0+dx,8.5,54.0,1.4,1.4,1.4);
    }
    // Central command spire, suspended cross-braces and cooling stacks fill the facility.
    cube3(site.navy,13.0,2.0,20.0,5.0,18.0,7.0);
    cube3(site.steel,13.0,12.0,20.0,8.0,3.0,10.0);
    cube3(site.cyan,13.0,12.4,14.95,6.0,1.0,0.12);
    cube3(site.gold,13.0,14.0,20.0,8.5,0.3,10.5);
    rod3(site.steel,13.0,14.0,20.0,13.0,25.0,20.0,0.2);
    visual3(site.sphere,site.cyan,13.0,25.0,20.0,0.8,0.8,0.8);
    foreach(x in [-42.0,42.0]) foreach(z in [-30.0,0.0,30.0,55.0]) {
        visual3(site.cylinder,site.steel,x,-1.0,z,3.8,11.0,3.8);
        for(local j=0;j<3;j++) visual3(site.cylinder,site.navy,x,2.5+j*0.7,z,4.2,0.15,4.2);
        visual3(site.cylinder,site.gold,x,4.6,z,4.4,0.25,4.4);
    }
    foreach(z in [-33.0,59.0]) {
        rod3(site.steel,-49.0,7.0,z,49.0,7.0,z,0.18);
        rod3(site.steel,-49.0,7.0,z,0.0,18.0,z,0.12);
        rod3(site.steel,49.0,7.0,z,0.0,18.0,z,0.12);
    }
    // Capsule total height 1.8 m; visual humanoid follows its feet, never drives physics.
    local b=site.world.newBody("dynamic",-26.0,1.0,-29.0);
    local capsule=b.newCapsuleShape(1.04,0.38,75.0,0.0,0.0);
    capsule.setTag(2); capsule.setCategoryBits(2);
    b.setFixedRotation(true); b.setBullet(true); site.runner.body=b;
    foreach(i in [0,1,2,3,4,5,6,7]) site.parts.append(cube3(i==1?site.cyan:site.white,0.0,0.0,0.0,1.0,1.0,1.0));
    site.parts[0].setMaterial(site.gold);
    foreach(i in [3,4,7]) site.parts[i].setMaterial(site.navy);
    foreach(i in [5,6]) site.parts[i].setMaterial(site.gold);
    site.beacon=cube3(site.cyan,-26.0,5.0,39.0,0.18,6.0,0.18);
    site.camera=eve.Camera3D(); site.camera.setFov(54.0); site.camera.setClipPlanes(0.1,420.0);
    site.camera.setAmbient(0.45,0.52,0.58); site.camera.setActive(true);
    gfx.setDirectionalLight(-0.45,0.85,-0.35,1.6,1.45,1.2);
}
