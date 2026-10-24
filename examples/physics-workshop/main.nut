// World units are metres, +Y is up. Physics owns every simulated position.
persist stage3 = null

function checked3(result) {
    if (!result.ok) throw "physics-workshop: " + result.status.summary;
    return result.value;
}
function couplingPoint(x, y, z) {
    local p = eve.CouplingPoint3D(); p.x=x; p.y=y; p.z=z; return p;
}
function object3(mesh, material, x, y, z, sx, sy, sz) {
    local e=eve.Renderable3D(); e.setMesh(mesh); e.setMaterial(material);
    e.setPosition(x,y,z); e.setScale(sx,sy,sz);
    e.setCastShadow(true); e.setReceiveShadow(true);
    stage3.entities.push(e); return e;
}
function material3(r,g,b,metal) {
    local m=gfx.newMaterial(); m.setTint(r,g,b,1.0);
    m.setMetallic(metal); m.setRoughness(0.58); stage3.materials.push(m); return m;
}
function box3(x,y,z,sx,sy,sz,material) {
    return object3(stage3.cube,material,x,y,z,sx,sy,sz);
}
// Unit-Y bars are transformed between world endpoints, never projected into 2D.
function segment3(e,ax,ay,az,bx,by,bz,radius) {
    local dx=bx-ax, dy=by-ay, dz=bz-az;
    local length=sqrt(dx*dx+dy*dy+dz*dz);
    if (length<0.00001) { e.setVisible(false); return; }
    e.setVisible(true); e.setPosition((ax+bx)*0.5,(ay+by)*0.5,(az+bz)*0.5);
    local c=dy/length; if(c>1.0)c=1.0; if(c< -1.0)c= -1.0;
    e.setRotation(atan2(dx,dz),acos(c),0.0); e.setScale(radius,length,radius);
}
function syncBody3(e,body) {
    e.setPosition(body.getX(),body.getY(),body.getZ());
    local x=body.getRotX(), y=body.getRotY(), z=body.getRotZ(), w=body.getRotW();
    local c=2.0*(x*w-y*z); if(c>1.0)c=1.0; if(c< -1.0)c= -1.0;
    e.setRotation(atan2(2.0*(x*z+y*w),1.0-2.0*(x*x+y*y)),asin(c),
        atan2(2.0*(x*y+z*w),1.0-2.0*(x*x+z*z)));
}
function makeStage3() {
    stage3={entities=[],materials=[],ropeBars=[],cables=[],tracers=[],world=null,coupling=null,
        rope=null,fabric=null,camera=null,a=null,b=null,clock=0.0,accumulator=0.0,
        steps=0,wind=1.0,paused=false,orbit=false,yaw=0.48,error=0.0};
    stage3.cube <- gfx.newMeshCube(1.0);
    stage3.cylinder <- gfx.newMeshCylinder(24,1,true);
    local steel=material3(0.22,0.30,0.39,0.65);
    local gold=material3(0.96,0.55,0.14,0.3);
    local floorMat=material3(0.09,0.14,0.20,0.05);
    local grid=material3(0.18,0.26,0.33,0.1);
    local blue=material3(0.23,0.49,0.95,0.25);
    local red=material3(0.95,0.31,0.20,0.25);
    local air=material3(0.20,0.67,0.77,0.1);
    box3(0.0,-0.25,0.0,19.0,0.5,10.0,floorMat);
    for(local i= -9;i<=9;i++) box3(i.tofloat(),0.008,0.0,0.014,0.014,10.0,grid);
    for(local i= -5;i<=5;i++) box3(0.0,0.008,i.tofloat(),19.0,0.014,0.014,grid);
    foreach(x in [-8.4,8.4]) {
        box3(x,3.4,0.0,0.22,6.8,0.28,steel);
        box3(x,0.1,0.0,1.2,0.2,1.8,steel);
    }
    box3(0.0,6.7,0.0,17.0,0.24,0.32,steel);
    // Three plinth stripes identify the native rope, cloth and pulley stations.
    box3(-5.2,0.025,0.3,4.8,0.05,3.0,gold);
    box3(0.0,0.025,0.3,4.2,0.05,3.0,air);
    box3(5.0,0.025,0.3,4.3,0.05,3.0,blue);
    foreach(x in [3.6,6.5]) {
        local w=object3(stage3.cylinder,gold,x,6.2,0.0,0.56,0.18,0.56);
        w.setRotation(0.0,1.5707963,0.0);
        box3(x,6.45,0.0,0.12,0.5,0.16,steel);
        box3(x,5.7,0.0,1.0,0.12,0.7,steel);
    }
    stage3.weightA <- box3(3.6,3.2,0.0,0.7,0.7,0.7,blue);
    stage3.weightB <- box3(6.5,3.2,0.0,0.7,0.7,0.7,red);
    for(local i=0;i<48;i++) stage3.ropeBars.push(box3(0.0,0.0,0.0,0.07,0.1,0.07,gold));
    for(local i=0;i<3;i++) stage3.cables.push(box3(0.0,0.0,0.0,0.04,1.0,0.04,gold));
    for(local i=0;i<18;i++) stage3.tracers.push(box3(0.0,0.0,0.0,0.4,0.025,0.025,air));
    stage3.rope=rope.newRope3D(32,-7.4,6.4,0.0,-3.2,6.4,0.0);
    stage3.rope.setGravity(0.0,-9.81,0.0); stage3.rope.setRadius(0.055);
    stage3.rope.setDamping(0.015); stage3.rope.setSelfCollision(true);
    stage3.fabric=cloth.newCloth3D(18,15,0.22,-1.9,6.3,-0.2);
    stage3.fabric.setGravity(0.0,-9.81,0.0); stage3.fabric.setDamping(0.015);
    stage3.fabric.setParticleMass(0.035); stage3.fabric.setParticleSize(0.06);
    stage3.fabric.setStretchCompliance(0.0001); stage3.fabric.setBendCompliance(0.003);
    stage3.fabric.setSelfCollision(true); stage3.fabric.setAerodynamics(1.225,1.1,0.15);
    stage3.fabric.setColor(0.10,0.75,0.78,1.0);
    stage3.camera=eve.Camera3D(); stage3.camera.setTarget(0.0,3.2,0.0);
    stage3.camera.setFov(48.0); stage3.camera.setAmbient(0.36,0.40,0.48);
    gfx.setDirectionalLight(-0.45,0.9,0.4,1.8,1.65,1.45);
}
function reset3() {
    // Keep the world and both borrowers alive across resets. Only the coupling
    // is recreated; body transforms and velocities are restored in place.
    if(stage3.coupling!=null) checked3(stage3.coupling.destroy());
    if(stage3.world==null) {
        stage3.world=physics.newWorld3D(0.0,-9.81,0.0,false);
        local ground=stage3.world.newBody("static",0.0,-0.25,0.0);
        ground.newBoxShape(19.0,0.5,10.0,1.0,0.6,0.05);
        foreach(x in [3.6,6.5]) {
            local stop=stage3.world.newBody("static",x,5.7,0.0);
            stop.newBoxShape(1.0,0.12,0.7,1.0,0.4,0.0);
        }
        stage3.a=stage3.world.newBody("dynamic",3.6,3.2,0.0);
        stage3.a.newBoxShape(0.7,0.7,0.7,4.0,0.5,0.05);
        stage3.b=stage3.world.newBody("dynamic",6.5,3.2,0.0);
        stage3.b.newBoxShape(0.7,0.7,0.7,4.5,0.5,0.05);
        stage3.a.setLinearDamping(0.12); stage3.b.setLinearDamping(0.12);
    }
    stage3.a.setPosition(3.6,3.2,0.0); stage3.b.setPosition(6.5,3.2,0.0);
    foreach(body in [stage3.a,stage3.b]) {
        body.setRotation(0.0,0.0,0.0,1.0);
        body.setLinearVelocity(0.0,0.0,0.0); body.setAngularVelocity(0.0,0.0,0.0);
    }
    local settings=eve.PulleyCoupling3DSettings();
    settings.groundA=couplingPoint(3.6,6.2,0.0); settings.groundB=couplingPoint(6.5,6.2,0.0);
    settings.localAnchorA=couplingPoint(0.0,0.0,0.0); settings.localAnchorB=couplingPoint(0.0,0.0,0.0);
    settings.ratio=1.0; settings.length=6.0; settings.tensionOnly=true;
    stage3.coupling=checked3(eve.createPulleyCoupling3D(stage3.world,stage3.a,stage3.b,settings));
    for(local i=0;i<32;i++) {
        local t=i.tofloat()/31.0;
        stage3.rope.attach(i,-7.4+4.2*t,6.4,0.0);
        if(i>0 && i<31) stage3.rope.detach(i);
    }
    stage3.rope.setRestLength(6.5); stage3.rope.setCollideWorld(stage3.world);
    stage3.fabric.reset();
    // The native grid starts in XZ; rotate its particle positions into a hanging XY sail.
    for(local row=0;row<15;row++) for(local col=0;col<18;col++)
        stage3.fabric.setParticlePosition(row*18+col,-1.9+col*0.22,6.3-row*0.22,-0.2);
    stage3.fabric.setCollideWorld(stage3.world);
    stage3.clock=0.0; stage3.accumulator=0.0; stage3.steps=0; stage3.paused=false; stage3.error=0.0;
}
function panel3() {
    ui.setScale(0.5); ui.setTheme("dark"); ui.beginBuild();
    ui.beginWindow("WINDWORKS 3D", "root");
    ui.text("WINDWORKS 3D | ROPE / SAIL / PULLEY", "title");
    ui.slider("Wind",1.0,-2.0,2.0,"wind");
    ui.button("Pause / resume","pause"); ui.button("Reset physics","reset");
    ui.button("Orbit camera on / off","orbit");
    ui.text("Native 3D physics | metres | fixed 120 Hz","status");
    ui.end(); ui.mountBuildAs("workshop"); ui.select("workshop");
    ui.setHostOverlay(true); ui.setHostPos(16.0,12.0,0.0,0.0);
}
eve_init = function() {
    gfx.setBackgroundColor(0.025,0.045,0.075,1.0);
    if("dev" in eve) { eve.dev.ai.setVisible(false); eve.dev.console.setVisible(false); }
    if(stage3==null) makeStage3();
    reset3(); panel3();
}
eve_update = function(dt) {
    local change=ui.consumeChange();
    while(change!="") {
        if(change=="workshop/wind") { ui.select("workshop"); stage3.wind=ui.getValue("wind"); }
        change=ui.consumeChange();
    }
    local click=ui.consumeClick();
    while(click!="") {
        if(click=="workshop/pause") stage3.paused=!stage3.paused;
        if(click=="workshop/reset") reset3();
        if(click=="workshop/orbit") stage3.orbit=!stage3.orbit;
        click=ui.consumeClick();
    }
    if(stage3.orbit) stage3.yaw+=dt*0.18;
    stage3.camera.setEye(23.0*sin(stage3.yaw),10.4,23.0*cos(stage3.yaw));
    if(!stage3.paused) {
        stage3.accumulator+=dt>0.05?0.05:dt;
        local h=1.0/120.0;
        while(stage3.accumulator>=h) {
            stage3.accumulator-=h; stage3.clock+=h;
            local wind=stage3.wind*(3.5+1.6*sin(stage3.clock*1.4));
            stage3.fabric.setWindVelocity(wind,0.0,wind*0.65);
            stage3.rope.applyForce(wind*0.22,0.0,wind*0.7);
            foreach(body in [stage3.a,stage3.b]) {
                body.applyForce(body.getMass()*wind*0.3,0.0,body.getMass()*wind*0.15);
            }
            stage3.world.update(h); stage3.rope.update(h); stage3.fabric.update(h); stage3.steps++;
            if(stage3.steps%120==0) {
                local sample=checked3(stage3.coupling.sample()); stage3.error=sample.error;
                if(stage3.steps==240) print("PHYSICS_WORKSHOP_3D_RUNNING steps=240 cloth="+stage3.fabric.getParticleCount()+" rope="+stage3.rope.getParticleCount()+" pulleyError="+stage3.error);
            }
        }
    }
    for(local i=0;i<48;i++) {
        local a=i.tofloat()/48.0, b=(i+1).tofloat()/48.0;
        segment3(stage3.ropeBars[i],stage3.rope.getSampleX(a),stage3.rope.getSampleY(a),stage3.rope.getSampleZ(a),
            stage3.rope.getSampleX(b),stage3.rope.getSampleY(b),stage3.rope.getSampleZ(b),0.065);
    }
    syncBody3(stage3.weightA,stage3.a); syncBody3(stage3.weightB,stage3.b);
    segment3(stage3.cables[0],3.6,6.2,0.0,stage3.a.getX(),stage3.a.getY(),stage3.a.getZ(),0.035);
    segment3(stage3.cables[1],6.5,6.2,0.0,stage3.b.getX(),stage3.b.getY(),stage3.b.getZ(),0.035);
    segment3(stage3.cables[2],3.6,6.47,0.0,6.5,6.47,0.0,0.035);
    for(local i=0;i<18;i++) {
        local x= -8.0+((i*1.7+stage3.clock*stage3.wind*1.5)%16.0);
        if(x< -8.0)x+=16.0;
        stage3.tracers[i].setPosition(x,1.2+(i%4)*1.1,-1.8+(i%3)*1.7);
    }
    ui.select("workshop");
    ui.setText("status",(stage3.paused?"PAUSED":"RUNNING")+" | 3D steps "+stage3.steps+" | wind "+stage3.wind);
}
eve_render = function() {
    gfx.clear(); gfx.render3D(); stage3.fabric.draw(gfx); ui.beginFrameAndRender();
}
