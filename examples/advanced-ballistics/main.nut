// All motion comes from CombatCarrierRuntime; rendering consumes owning states().
persist laboratories = []
persist labClock = 0.0
persist accumulator = 0.0
persist readyPrinted = false

persist labFontOwner = null
persist labFont = null

function rect(x,y,w,h,r,g,b,a=1.0) { gfx.drawSolidRect(x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),r,g,b,a); }
function label(text,x,y,size,r,g,b) {
    gfx.drawText(labFont,text,x.tofloat(),y.tofloat(),r,g,b,1.0,size*0.45);
}
function checked(result) {
    if (!result.ok) throw "ballistics: " + result.status.summary;
    return result.value;
}
function addLab(name,detail,motion,speed=6.0,position=null,direction=null,extraTriggers=null,extraImpacts=null) {
    motion.append("linear");
    local index = laboratories.len();
    local id = "laboratory:case" + index;
    local recipe = { id=id, lifetime=3.6, speed=speed, motion=motion,
        triggers=["onExpire"], impacts=[{kind="release",on="onExpire"}] };
    if (extraTriggers != null) foreach (t in extraTriggers) recipe.triggers.append(t);
    if (extraImpacts != null) foreach (i in extraImpacts) recipe.impacts.append(i);
    local pool = checked(eve.Weapon().newCarrierRuntime(24));
    checked(pool.configureSimulation(1.0/120.0,256,64,4096));
    checked(pool.registerRecipe(recipe));
    laboratories.append({name=name,detail=detail,id=id,pool=pool,
        position=position == null ? [-8.0,0.0,0.0] : position,
        direction=direction == null ? [1.0,0.0,0.0] : direction,
        target=(name=="HOMING" || name=="PREDICTIVE" || name=="ORBIT"),
        trails={},points=[],flashes=[],events=0,timer=index*0.07,serial=0});
}
eve_init = function() {
    gfx.setBackgroundColor(0.025,0.035,0.055,1.0);
    laboratories=[]; labClock=0.0; accumulator=0.0; readyPrinted=false;
    labFontOwner=eve.Font();
    local data=labFontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf",24);
    labFont=gfx.newFont(data," ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789");
    gfx.setFont(labFont);
    addLab("ACCELERATION","SIGNED SPEED RAMP",[{kind="accelerate",acceleration=2.0}],3.0);
    addLab("BALLISTIC","GRAVITY AND APEX",[{kind="gravity",gravity=5.0}],8.0,[-8.0,-2.0,0.0],[1.0,1.0,0.0],
        ["onApex"],[{kind="emitHit",on="onApex"}]);
    addLab("SINE WAVE","BOUNDED LATERAL ARC",[{kind="sway",amplitude=1.2,frequency=0.8}]);
    addLab("HELIX","TWO AXIS CORKSCREW",[{kind="helix",amplitude=1.0,frequency=0.8}]);
    addLab("HOMING","LIMITED TURN RATE",[{kind="homing",turnRate=100.0}]);
    addLab("PREDICTIVE","MOVING TARGET LEAD",[{kind="predictive",turnRate=130.0,predictionSeconds=0.7}]);
    addLab("ORBIT","TARGET RADIAL CONTROL",[{kind="orbit",orbitRadius=3.2,angularRateDegrees=100.0,turnRate=210.0,axisX=0.0,axisY=0.0,axisZ=1.0}],5.0,[3.2,0.0,0.0],[0.0,1.0,0.0]);
    addLab("CUSTOM PATH","SCRIPT POSITION FUNCTION",[],5.0,[-5.0,-2.0,0.0]);
    addLab("WANDER","SEEDED SMOOTH NOISE",[{kind="wander",turnRate=100.0,amplitude=1.1,frequency=2.0,randomSeed=73}]);
    addLab("CUSTOM SPEED","SCRIPT VELOCITY FUNCTION",[],5.0);
    addLab("RICOCHET","MULTI HIT REMAINING TIME",[],8.0,[-2.0,0.0,0.0],null,
        ["onHit"],[{kind="emitHit",on="onHit"},{kind="bounce",on="onHit",bounceCount=6,restitution=1.0},{kind="release",on="onHit"}]);
    addLab("TRIGGERS","DISTANCE AND INTERVAL",[{kind="drag",drag=0.3}],8.0,null,null,
        [{kind="onDistance",threshold=7.0},{kind="onInterval",interval=0.45}],
        [{kind="emitHit",on="onDistance"},{kind="emitHit",on="onInterval"}]);
    checked(laboratories[10].pool.addHitTarget(11,1,4.0,0.0,0.0,1.1));
    checked(laboratories[10].pool.addHitTarget(12,1,-4.0,0.0,0.0,1.1));
};
// Functions receive owning pre-step/proposed tables; no retained native pointers.
// Return velocity only: the engine integrates once, then checks hits and triggers.
function customSpeed(state, proposed, dt) {
    local speed = 4.0 + 3.5 * sin((state.age + dt) * 5.0);
    return {vx=speed, vy=0.0, vz=0.0};
}
// Return position only: the engine derives velocity from the swept displacement.
function customTrajectory(state, proposed, dt) {
    local t = state.age + dt;
    return {x=-5.0 + 5.0*sin(t*1.4), y=-2.0 + 3.0*(1.0-cos(t*1.4)), z=0.0};
}
function tickLab(lab,index,dt) {
    if (lab.target) {
        local tx = lab.name=="ORBIT" ? 0.0 : 4.0;
        local ty = lab.name=="ORBIT" ? 0.0 : sin(labClock*1.6)*2.4;
        checked(lab.pool.setTargetPosition(1,1,tx,ty,0.0));
    }
    lab.timer -= dt;
    if (lab.timer <= 0.0) {
        local p=lab.position,d=lab.direction;
        if (lab.target) checked(lab.pool.spawnAtTarget(lab.id,p[0],p[1],p[2],d[0],d[1],d[2],1,1));
        else checked(lab.pool.spawn(lab.id,p[0],p[1],p[2],d[0],d[1],d[2]));
        lab.timer=0.8;
    }
    local frame = lab.name == "CUSTOM PATH" ? checked(lab.pool.updateWithMotion(dt,customTrajectory)) :
        lab.name == "CUSTOM SPEED" ? checked(lab.pool.updateWithMotion(dt,customSpeed)) : checked(lab.pool.update(dt));
    foreach (event in frame.events) {
        lab.events++;
        lab.flashes.append({x=event.x,y=event.y,z=event.z,life=0.5});
    }
    foreach (flash in lab.flashes) flash.life-=dt;
    for (local i=lab.flashes.len()-1;i>=0;--i) if (lab.flashes[i].life<=0.0) lab.flashes.remove(i);
    lab.points=checked(lab.pool.states());
    local live={};
    foreach (state in lab.points) {
        local key=state.handle.slot+":"+state.handle.generation;
        live[key] <- true;
        if (!(key in lab.trails)) lab.trails[key] <- [];
        lab.trails[key].append([state.x,state.y,state.z]);
        if (lab.trails[key].len()>65) lab.trails[key].remove(0);
    }
    local retired=[];
    foreach (key,path in lab.trails) if (!(key in live)) retired.append(key);
    foreach (key in retired) delete lab.trails[key];
}
eve_update = function(dt) {
    accumulator += dt;
    // Fixed simulation timestep; retain all elapsed time, including slow frames.
    local steps = 0;
    while (accumulator >= 1.0/60.0 && steps < 4) {
        labClock += 1.0/60.0;
        foreach (index,lab in laboratories) tickLab(lab,index,1.0/60.0);
        accumulator -= 1.0/60.0;
        steps++;
    }
    if (!readyPrinted && labClock>0.25) {print("ADVANCED_BALLISTICS_READY cases=12 fixedHz=60\n");readyPrinted=true;}
};
function dot(lab,index,x,y,z,size,r,g,b,a=1.0) {
    local left=24+(index%4)*354, top=152+(index/4)*238;
    local px=left+175+x*14.0+z*2.5, py=top+132-y*14.0+z*10.0;
    if (px>left+12 && px<left+326 && py>top+58 && py<top+198) rect(px-size*0.5,py-size*0.5,size,size,r,g,b,a);
}
eve_render = function() {
    gfx.clear();
    rect(24,27,4,68,0.28,0.88,0.85);
    label("ADVANCED BALLISTICS",42,29,3.5,0.9,0.96,1.0);
    label("MOTION LABORATORY",43,70,1.5,0.35,0.73,0.8);
    label("12 LIVE SYSTEMS   60 HZ   SEEDED REPLAY",784,37,1.5,0.51,0.65,0.76);
    rect(24,113,1392,1,0.16,0.25,0.33);
    foreach (index,lab in laboratories) {
        local left=24+(index%4)*354, top=152+(index/4)*238;
        local colors=[[0.3,0.88,0.92],[1.0,0.67,0.3],[0.64,0.58,1.0],[0.4,0.9,0.64]];
        local c=colors[index%4];
        rect(left,top,330,216,0.045,0.063,0.09);
        rect(left,top,330,2,c[0],c[1],c[2],0.75);
        label(lab.name,left+15,top+16,1.65,0.87,0.92,0.98);
        label(lab.detail,left+15,top+38,1.0,0.38,0.51,0.64);
        for (local gx=0;gx<11;gx++) for(local gy=0;gy<5;gy++) rect(left+24+gx*28,top+70+gy*28,1,1,0.18,0.25,0.33);
        foreach (key,path in lab.trails) foreach (i,p in path) dot(lab,index,p[0],p[1],p[2],2.2,c[0],c[1],c[2],0.08+0.52*i/path.len());
        foreach (p in lab.points) {
            dot(lab,index,p.x,p.y,p.z,11,c[0],c[1],c[2],0.08);
            dot(lab,index,p.x,p.y,p.z,4.5,0.93,0.98,1.0);
        }
        if (lab.target) dot(lab,index,lab.name=="ORBIT"?0.0:4.0,lab.name=="ORBIT"?0.0:sin(labClock*1.6)*2.4,0.0,6,1.0,0.4,0.4);
        if (lab.name=="RICOCHET") {
            dot(lab,index,4.0,0.0,0.0,28,0.4,0.5,0.6,0.35);
            dot(lab,index,-4.0,0.0,0.0,28,0.4,0.5,0.6,0.35);
        }
        foreach (f in lab.flashes) dot(lab,index,f.x,f.y,f.z,6+20*(0.5-f.life),1.0,0.7,0.3,f.life);
        label("ACTIVE "+lab.points.len()+"   EVENTS "+lab.events,left+15,top+198,1.0,0.35,0.5,0.62);
    }
    label("WHITE  PROJECTILE     COLOR  TRAJECTORY     AMBER  TRIGGER     RED  TARGET",24,880,1.1,0.42,0.57,0.67);
};
