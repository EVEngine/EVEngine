// Fixed simulation tick, main thread. Events are polled after native stepping.
// Support transport uses the solver body's velocity; it never teleports the actor.
function resetCourse() {
    site.clock=0.0; site.remainder=0.0; site.yaw=0.0; site.facing=0.0;
    site.gateHeight=4.0; site.jumpBuffer=0.0; site.coyote=0.0;
    intent.x=0.0; intent.z=0.0; intent.jump=false;
    state={phase="ready",checkpoint=0,gateOpen=false,deaths=0,time=0.0,contacts=0,
        triggers=0,grounded=false,x=-26.0,y=0.0,z=-29.0,support=0,
        liftY=2.0,ferryZ=26.0,last="Enter to start / F to inspect the full facility",
        crateMoved=false,platformRidden=false,finished=false};
    foreach(r in site.bodies) if(r.type!="static") {
        r.body.setPosition(r.x,r.y,r.z); r.body.setRotation(0.0,0.0,0.0,1.0);
        r.body.setLinearVelocity(0.0,0.0,0.0); r.body.setAngularVelocity(0.0,0.0,0.0);
    }
    foreach(r in site.rollers) r.body.setLinearVelocity(0.0,0.0,-4.5);
    returnToCheckpoint(false);
    state.phase="ready";
}
function returnToCheckpoint(died) {
    local p=site.runner.body;
    local cp=state.checkpoint>0?site.checkpoints[state.checkpoint-1]:{x=-26.0,y=0.0,z=-29.0,yaw=0.0};
    p.setPosition(cp.x,cp.y+0.96,cp.z); p.setLinearVelocity(0.0,0.0,0.0);
    site.grounded=false; site.support=null; site.jumpBuffer=0.0; site.coyote=0.0;
    site.respawnPending=false; site.yaw=cp.yaw; site.facing=cp.yaw;
    if(died) { state.deaths++; state.last="Contact with machinery / returned to checkpoint "+state.checkpoint; }
    state.x=cp.x; state.y=cp.y; state.z=cp.z; state.grounded=false;
}
function driveKinematics(h) {
    local t=site.clock;
    foreach(m in site.movers) {
        local r=m.record, x=r.x,y=r.y,z=r.z,qy=0.0,qw=1.0;
        if(m.kind=="rotor") { local a=t*0.85+m.phase; qy=sin(a*0.5); qw=cos(a*0.5); }
        if(m.kind=="lift") { y=2.75-1.0*cos(t*0.75); state.liftY=y+0.25; }
        if(m.kind=="ferry") { z=26.0+7.0*sin(t*0.65); state.ferryZ=z; }
        if(m.kind=="press") y=8.0+3.0*sin(t*1.1+m.phase);
        r.body.setTargetTransform(x,y,z,0.0,qy,0.0,qw,h);
    }
    foreach(p in site.pendulums) {
        local angle=0.75*sin(t*1.15+p.phase);
        local x=6.0*sin(angle),y=11.0-6.0*cos(angle);
        p.record.body.setTargetTransform(x,y,p.z,0.0,0.0,0.0,1.0,h);
    }
    site.gateHeight+=clamp3((state.gateOpen?9.0:4.0)-site.gateHeight,-h*3.0,h*3.0);
    site.gate.body.setTargetTransform(-26.0,site.gateHeight,34.0,0.0,0.0,0.0,1.0,h);
}
function readIntent3() {
    local x=intent.x,z=intent.z;
    if(!site.overview) {
        local f=0.0,r=0.0;
        if(keyboard.isDown("w") || keyboard.isDown("up")) f+=1.0;
        if(keyboard.isDown("s") || keyboard.isDown("down")) f-=1.0;
        if(keyboard.isDown("d") || keyboard.isDown("right")) r+=1.0;
        if(keyboard.isDown("a") || keyboard.isDown("left")) r-=1.0;
        x+=sin(site.yaw)*f-cos(site.yaw)*r;
        z+=cos(site.yaw)*f+sin(site.yaw)*r;
    }
    local len=sqrt(x*x+z*z);
    if(len>1.0) { x/=len; z/=len; }
    return {x=x,z=z};
}
function moveRunner3(h) {
    local p=site.runner.body, v=readIntent3();
    if(state.phase=="ready" && !site.overview) state.phase="running";
    if(state.phase!="running") { v.x=0.0; v.z=0.0; }
    local sx=0.0,sz=0.0;
    if(site.grounded && site.support!=null && site.support.type=="kinematic") {
        sx=site.support.body.getLinearVelocityX(); sz=site.support.body.getLinearVelocityZ();
        if(site.support.tag==3) state.platformRidden=true;
    }
    local vx=p.getLinearVelocityX(), vy=p.getLinearVelocityY(), vz=p.getLinearVelocityZ();
    local acceleration=site.grounded?46.0:22.0;
    vx+=clamp3(v.x*6.3+sx-vx,-acceleration*h,acceleration*h);
    vz+=clamp3(v.z*6.3+sz-vz,-acceleration*h,acceleration*h);
    site.jumpBuffer=max3(0.0,site.jumpBuffer-h);
    site.coyote=site.grounded?0.09:max3(0.0,site.coyote-h);
    if(site.jumpBuffer>0.0 && site.coyote>0.0 && state.phase=="running") {
        vy=8.0; site.jumpBuffer=0.0; site.coyote=0.0; site.grounded=false;
    }
    p.setLinearVelocity(vx,vy,vz);
    if(v.x*v.x+v.z*v.z>0.05) site.facing=atan2(v.x,v.z);
}
function pollCourseEvents() {
    local w=site.world, id=site.runner.body.getId();
    for(local i=0;i<w.getBeginContactCount();i++) {
        local a=w.getBeginContactBodyAId(i),b=w.getBeginContactBodyBId(i);
        if(a!=id && b!=id) continue;
        local other=a==id?w.getBeginContactShapeBTag(i):w.getBeginContactShapeATag(i);
        state.contacts++; state.last="Rigid contact / shape "+other;
        if(other>=20 && other<=23 && state.phase=="running") site.respawnPending=true;
    }
    for(local i=0;i<w.getBeginTriggerCount();i++) {
        local tag=w.getBeginTriggerSensorShapeTag(i), visitor=w.getBeginTriggerVisitorBodyId(i);
        if(tag==40 && visitor==site.crate.body.getId()) {
            state.gateOpen=true; state.last="Cargo plate engaged / security gate opening";
        }
        if(visitor!=id) continue;
        state.triggers++;
        if(tag==101+state.checkpoint && tag<=106 && (tag!=101 || state.gateOpen)) {
            state.checkpoint++; state.last="Checkpoint "+state.checkpoint+" / route secured";
        }
        if(tag==200 && state.checkpoint==6 && state.gateOpen) {
            state.phase="complete"; state.finished=true; state.last="All eight sectors cleared";
            intent.x=0.0; intent.z=0.0;
        }
    }
}
function sampleGround3() {
    local p=site.runner.body,w=site.world;
    local feet=p.getY()-0.9;
    w.setQueryFilter(2,9); // Solids and cargo only; gameplay sensors are category 4.
    w.setQueryIgnoredBodyId(p.getId());
    local hit=w.rayCast(p.getX(),feet+0.15,p.getZ(),p.getX(),feet-0.22,p.getZ());
    site.grounded=hit>0 && w.getRayHitNormalY()>0.65 && p.getLinearVelocityY()<1.5;
    site.support=null;
    if(site.grounded) {
        local id=w.getRayHitBodyId();
        if(id in site.byId) site.support=site.byId[id];
    }
    w.clearQueryIgnores(); w.resetQueryFilter();
    state.grounded=site.grounded;
    state.support=site.support!=null?site.support.tag:0;
}
function simulateCourse(h) {
    site.clock+=h;
    if(state.phase=="running") state.time+=h;
    driveKinematics(h); moveRunner3(h);
    site.world.clearContactEvents(); site.world.update(h); pollCourseEvents();
    foreach(r in site.rollers) {
        if(r.body.getZ()<12.0 || r.body.getY()<1.0) {
            r.body.setPosition(r.x,5.2,30.0); r.body.setLinearVelocity(0.0,0.0,-4.5);
            r.body.setAngularVelocity(-4.5,0.0,0.0);
        }
    }
    local p=site.runner.body;
    if(p.getY()< -4.0) site.respawnPending=true;
    if(site.respawnPending) returnToCheckpoint(true);
    sampleGround3();
    state.x=p.getX(); state.y=p.getY()-0.9; state.z=p.getZ();
    state.crateMoved=abs(site.crate.body.getZ()-site.crate.z)>0.5;
}
