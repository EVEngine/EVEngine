local whirlwind=loadfile("fx/whirlwind.nut")(), styles=loadfile("fx/styles.nut")();
local pools=[], camera=null, cube=null, sphere=null, sword=[], hero=[], depthColumn=null;
local time=0.0, autoClock=0.7, attackAge=10.0, attackId=0, lastSpace=false;
local selected=0, paused=false, automatic=true, orbit=0.65;
local shotCount=0, lastResult="ready", closed=false;
function clamp(v,a,b) { return v<a?a:(v>b?b:v); }
function part(mesh,x,y,z,sx,sy,sz,color,lit=true) {
    local e=eve.Renderable3D(); e.setMesh(mesh); e.setPosition(x.tofloat(),y.tofloat(),z.tofloat());
    e.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    e.setTint(color[0].tofloat(),color[1].tofloat(),color[2].tofloat(),1.0);
    e.setReceiveLight(lit); e.setRoughness(0.65); e.setCastShadow(lit); return e;
}
function buildScene() {
    cube=gfx.newMeshCube(1.0); sphere=gfx.newMeshSphere(20,12);
    local cylinder=gfx.newMeshCylinder(80,1,true);
    part(cylinder,0,-0.2,0,6.7,0.28,6.7,[0.055,0.085,0.12]);
    part(cylinder,0,-0.02,0,5.7,0.03,5.7,[0.075,0.12,0.16]);
    for(local i=-5;i<=5;++i) {
        part(cube,i,0.005,0,0.012,0.012,10,[0.12,0.20,0.25],false);
        part(cube,0,0.006,i,10,0.012,0.012,[0.12,0.20,0.25],false);
    }
    // A low ruined gate provides an opaque occluder for depth verification.
    for(local i=0;i<5;++i) {
        local a=i*0.55+0.3;
        part(cube,cos(a)*5.0,0.65,sin(a)*5.0,0.42,1.3,0.42,[0.13,0.20,0.26]);
        part(cube,cos(a)*5.0,1.34,sin(a)*5.0,0.62,0.09,0.62,[0.27,0.42,0.45]);
    }
    // Graphic armoured duellist, native geometry only.
    hero.push(part(cube,-3.1,1.1,0,0.55,0.72,0.37,[0.12,0.23,0.29]));
    hero.push(part(sphere,-3.1,1.67,0,0.23,0.27,0.23,[0.22,0.32,0.35]));
    part(cube,-3.1,1.72,0.21,0.32,0.055,0.05,[0.3,0.9,0.75],false);
    for(local i=0;i<2;++i) {
        part(cube,-3.25+i*0.32,0.45,i*0.12,0.2,0.75,0.22,[0.10,0.16,0.21]);
        part(cube,-3.48+i*0.76,1.18,0,0.2,0.58,0.22,[0.16,0.28,0.32]);
    }
    // Sword parts keep local offsets; attachment is updated from the same swing clock.
    sword.push({e=part(cube,0,0,0,0.11,1.85,0.045,[0.73,0.91,0.92]),y=1.25});
    sword.push({e=part(cube,0,0,0,0.035,1.78,0.052,[0.3,0.94,0.75],false),y=1.25});
    sword.push({e=part(cube,0,0,0,0.55,0.09,0.16,[0.82,0.64,0.27]),y=0.27});
    sword.push({e=part(cube,0,0,0,0.1,0.42,0.1,[0.15,0.23,0.26]),y=0.0});
    camera=eve.Camera3D(); camera.setTarget(-0.4,1.3,0.0); camera.setUp(0.0,1.0,0.0);
    depthColumn=part(cube,1.0,1.4,1.2,0.58,2.8,0.58,[0.015,0.023,0.03]); depthColumn.setVisible(false);
    camera.setFov(43.0); camera.setAmbient(0.12,0.16,0.22); camera.setActive(true);
    gfx.setDirectionalLight(-0.5,0.9,0.4,0.45,0.55,0.65);
    gfx.setBackgroundColor(0.018,0.029,0.045,1.0);
}
function poseFor(xOffset=0.0,zOffset=0.0) {
    local tilt=ui.getValue("tilt"), heading=ui.getValue("heading");
    return {x=ui.getValue("x")+xOffset,y=ui.getValue("y"),z=ui.getValue("z")+zOffset,
        dx=sin(tilt)*cos(heading),dy=cos(tilt),dz=sin(tilt)*sin(heading)};
}
function controls(instance) {
    instance.setControls(ui.getValue("height"),ui.getValue("radius"),ui.getValue("spin"),ui.getValue("travel"));
}
function triggerAttack(pair=false) {
    if(closed) return;
    local count=pair?2:1;
    for(local i=0;i<count;++i) {
        local instance=pools[selected].instance(i);
        controls(instance); ++attackId;
        local result=instance.trigger(attackId,poseFor(i*2.7,i*0.3),ui.getValue("duration"));
        lastResult=result;
        if(result=="started") { ++shotCount; attackAge=0.0; }
    }
}
function runContracts() {
    local f=pools[0].instance(0), g=pools[0].instance(1);
    local p={x=0.0,y=0.0,z=0.0,dx=1.0,dy=0.0,dz=0.0};
    if(f.trigger(0,p,1.0)!="started" || f.trigger(0,p,1.0)!="duplicate" || f.trigger(1,p,1.0)!="busy") throw "trigger contract";
    f.advance(0.25); local age=f.getAge(); f.advance(0.0); if(f.getAge()!=age) throw "freeze contract";
    p.x=99.0; if(f.getPosition().x>1.0) throw "pose copy contract";
    if(g.getState()!="idle") throw "instance isolation contract";
    local rejected=false;
    try { f.setPose({x=0,y=0,z=0,dx=0,dy=0,dz=0}); } catch(error) { rejected=true; }
    if(!rejected || f.getPosition().x>1.0) throw "invalid pose atomicity contract";
    rejected=false; try { f.advance(-0.1); } catch(error) { rejected=true; }
    if(!rejected || f.getAge()!=age) throw "negative dt contract";
    f.cancel(); f.advance(0.1); if(f.getState()!="fading") throw "fade contract";
    f.advance(100.0); if(f.isVisible() || f.getState()!="finished") throw "expiry contract";
    // One large dt and many small dt must produce the same active travel distance.
    local probe={x=0.0,y=0.0,z=0.0,dx=1.0,dy=0.0,dz=0.0};
    f.setControls(3.5,1.6,8.5,1.3); g.setControls(3.5,1.6,8.5,1.3);
    if(f.trigger(1,probe,1.0)!="started" || g.trigger(0,probe,1.0)!="started") throw "partition setup";
    f.advance(1.0); for(local i=0;i<100;++i) g.advance(0.01);
    if(fabs(f.getPosition().x-g.getPosition().x)>0.0001) throw "dt partition contract";
    local before=f.getPosition().x; f.setControls(3.5,1.6,8.5,2.0); f.advance(0.0);
    if(fabs(f.getPosition().x-before)>0.00001) throw "live speed teleport contract";
    f.stop(); g.stop();
    if(f.trigger(2,{x=0,y=0,z=0,dx=0,dy=1,dz=0},0.5)!="started") throw "reuse contract";
    f.advance(10.0); f.stop(); g.stop();
    // Failed preparation must not publish visible entities or alter existing instances.
    local bad=clone styles[0]; bad.schema="invalid"; rejected=false;
    try { whirlwind.prepare(gfx,procgen,bad,1); } catch(error) { rejected=true; }
    if(!rejected || f.isVisible()) throw "schema contract";
    local terminal=whirlwind.prepare(gfx,procgen,styles[0],1), c=terminal.instance(0);
    c.close(); c.close(); c.advance(1.0);
    if(c.getState()!="closed" || c.isVisible() || c.trigger(0,probe,1.0)!="closed") throw "closed instance contract";
    terminal.close(); terminal.close();
    print("WHIRLWIND_CONTRACTS_PASS trigger busy duplicate freeze copy isolation invalidPose negativeDt fade expiry partition liveSpeed reuse schema close\n");
}
eve_init=function() {
    if("dev" in eve) { eve.dev.console.setVisible(false); eve.dev.ai.setVisible(false); }
    // Explicit forward lighting for the scene and transparent blade meshes.
    if(!gfx.getRenderControl().setLightingMode("forwardPlus")) throw "Forward lighting unavailable";
    foreach(style in styles) pools.push(whirlwind.prepare(gfx,procgen,style,2));
    runContracts(); buildScene();
    ui.setTheme("dark"); ui.setScale(0.9); ui.beginBuild(); ui.beginWindow("GALE / BLADE VFX","root");
    ui.text("GALE / BLADE WHIRLWIND","title");
    ui.text("WORLD SPACE    /    WEAPON VFX","subtitle");
    ui.beginRow("styles",6.0); ui.button("Jade gale","jade"); ui.button("Amber ink","amber"); ui.end();
    ui.beginRow("attack-buttons",6.0); ui.button("Attack [SPACE]","attack"); ui.button("Dual cast","dual"); ui.end();
    ui.beginRow("transport",6.0); ui.button("Pause","pause"); ui.button("Fade out","cancel"); ui.button("Clear","clear"); ui.end();
    ui.button("Auto attack: ON","auto");
    ui.beginRow("spatial",6.0); ui.button("Depth check","depth"); ui.button("Forward cast","forward"); ui.end();
    ui.slider("Height",3.5,0.5,6.0,"height"); ui.slider("Radius",1.6,0.3,3.0,"radius");
    ui.slider("Spin",8.5,-16.0,16.0,"spin"); ui.slider("Travel",0.0,0.0,4.0,"travel");
    ui.slider("Duration x",1.0,0.25,3.0,"duration");
    ui.slider("Origin X",0.0,-2.0,3.0,"x"); ui.slider("Origin Y",0.12,0.0,3.5,"y");
    ui.slider("Origin Z",0.0,-2.0,2.0,"z");
    ui.slider("Axis tilt",0.0,0.0,1.57,"tilt"); ui.slider("Heading",0.0,-3.14,3.14,"heading");
    ui.slider("Orbit",orbit,-3.14,3.14,"orbit");
    ui.separator("s1"); ui.text("Native mesh / depth test / alpha blend","tech");
    ui.text("", "status"); ui.text("", "phase");
    ui.textWrapped("Attack emits an independent pose snapshot. Four pooled instances, no allocation on trigger. Style buttons replace mask, palette and shape.",280.0,"help");
    ui.end(); ui.mountBuildAs("gale"); ui.select("gale");
    ui.setHostPos(10.0,10.0,0.0,0.0); ui.setHostSize(320.0,780.0); ui.setHostOverlay(true);
    print("BLADE_WHIRLWIND_READY styles=2 pooledInstances=4 worldSpace=1 depthTest=1\n");
};
eve_update=function(dt) {
    ui.select("gale"); local click=ui.consumeClick();
    while(click!="") {
        if(click=="gale/attack") triggerAttack();
        else if(click=="gale/dual") triggerAttack(true);
        else if(click=="gale/pause") paused=!paused;
        else if(click=="gale/auto") { automatic=!automatic; ui.setText("auto",automatic?"Auto attack: ON":"Auto attack: OFF"); }
        else if(click=="gale/depth") depthColumn.setVisible(!depthColumn.getVisible());
        else if(click=="gale/forward") {
            ui.setValue("tilt",1.30); ui.setValue("heading",0.0); ui.setValue("travel",1.6); ui.setValue("y",1.8);
            for(local i=0;i<2;++i) pools[selected].instance(i).stop();
            triggerAttack();
        }
        else if(click=="gale/cancel") foreach(p in pools) for(local i=0;i<2;++i) p.instance(i).cancel();
        else if(click=="gale/clear") foreach(p in pools) for(local i=0;i<2;++i) p.instance(i).stop();
        else if(click=="gale/jade" || click=="gale/amber") {
            selected=click=="gale/jade"?0:1;
            ui.setValue("height",styles[selected].height); ui.setValue("radius",styles[selected].radius);
            ui.setValue("spin",styles[selected].spin); ui.setValue("travel",styles[selected].travel);
            triggerAttack();
        }
        click=ui.consumeClick();
    }
    local space=keyboard.isDown("SPACE"); if(space&&!lastSpace) triggerAttack(); lastSpace=space;
    local step=paused?0.0:dt;
    time+=step; attackAge+=step; autoClock-=step;
    foreach(pool in pools) pool.advance(step);
    for(local i=0;i<2;++i) controls(pools[selected].instance(i));
    if(automatic && autoClock<=0.0) { triggerAttack(); autoClock=3.8; }
    local swing=attackAge<0.6?sin(clamp(attackAge/0.6,0.0,1.0)*3.14159):0.0;
    local roll=-0.35-swing*1.7;
    foreach(p in sword) {
        p.e.setPosition(-2.7-sin(roll)*p.y,1.15+cos(roll)*p.y,0.25);
        p.e.setRotation(0.0,0.0,roll);
    }
    orbit=ui.getValue("orbit"); camera.setEye(sin(orbit)*12.0,6.3,cos(orbit)*12.0);
    ui.setText("status",format("%s / %s / attacks %d",styles[selected].id,lastResult,shotCount));
    ui.setText("phase",format("%s  |  %s  |  %.2f s",paused?"PAUSED":"LIVE",pools[selected].instance(0).getState(),pools[selected].instance(0).getAge()));
};
eve_render=function() { gfx.clear(); gfx.render3D(); ui.select("gale"); ui.beginFrameAndRender(); };
eve_before_restart <- function() { foreach(pool in pools) pool.close(); closed=true; };
