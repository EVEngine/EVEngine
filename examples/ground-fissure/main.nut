dofile("geometry.nut");
local cells=[],debris=[],dust=[],cores=[],paths=[],cam=null,sun=null;
local topMaterial=null,wallMaterial=null,glowMaterial=null,ground=null,cube=null,rockMesh=null,card=null,dustTexture=null;
local geometrySeed=-1,geometryCache=null;
local lavaFloor=null,lavaMaterial=null;
local pendingActions=[];
local age=-0.6,paused=false,looping=true,radial=false,presetName="LAVA RIFT",replays=0;
local settings=clone config.fissure;
local knobs=[
    {id="seed",label="Seed",lo=1.0,hi=999.0},
    {id="length",label="Length / m",lo=5.0,hi=15.0},
    {id="width",label="Opening / m",lo=0.1,hi=2.5},
    {id="branches",label="Branches",lo=0.0,hi=4.0},
    {id="depth",label="Depth / m",lo=0.4,hi=3.0},
    {id="duration",label="Propagation / s",lo=0.4,hi=4.0},
    {id="lift",label="Rock uplift / m",lo=0.0,hi=1.5},
    {id="debris",label="Debris count",lo=0.0,hi=120.0},
    {id="impulse",label="Debris impulse",lo=0.5,hi=8.0},
    {id="dust",label="Dust amount",lo=0.0,hi=1.0},
    {id="glow",label="Core glow",lo=0.0,hi=2.0},
    {id="red",label="Glow red",lo=0.0,hi=1.0},
    {id="green",label="Glow green",lo=0.0,hi=1.0},
    {id="blue",label="Glow blue",lo=0.0,hi=1.0}
];
function object(mesh,material) {
    local e=eve.Renderable3D();e.setMesh(mesh);
    if(material!=null) e.setMaterial(material);
    e.setRoughness(0.95);return e;
}
function makeMaterial(texture,lit) {
    local m=gfx.newMaterial(); if(texture!=null) m.setAlbedoTexture(texture);
    m.setReceiveLight(lit);m.setRoughness(0.97);m.setDoubleSided(true);
    m.setSurfaceMode("opaque");m.setDepthWrite(true);m.setCastShadow(lit);m.setReceiveShadow(lit);
    return m;
}
function replay() {
    age=-0.6;paused=false;replays++;ui.setValue("timeline",0.0);
    print(format("FAULT_REPLAY seed=%d count=%d\n",settings.seed.tointeger(),replays));
}
function preset(which) {
    radial=which==2;presetName=which==0?"DRY FAULT":(which==1?"LAVA RIFT":"RADIAL SHATTER");
    settings=clone config.fissure;
    if(which==0) {settings.glow=0.0;settings.dust=0.8;settings.lift=0.4;settings.width=0.85;}
    if(which==2) {settings.length=12.0;settings.branches=3;settings.lift=1.0;settings.width=1.6;settings.duration=1.3;}
    foreach(k in knobs) ui.setValue(k.id,settings[k.id].tofloat());
    rebuild();
}
function rebuild() {
    paths=createFaultPaths(settings,radial);
    if(geometryCache==null||geometrySeed!=settings.seed.tointeger()) {
        geometryCache=makeCells(settings.seed.tointeger());geometrySeed=settings.seed.tointeger();
    }
    local geometry=geometryCache;
    foreach(i,c in geometry) {
        local meshes=polygonMeshes(c.poly,c.x,c.z,settings.depth),nearest=nearestFault(paths,c.x,c.z);
        local cell=cells[i];cell.topMesh=uploadMesh(cell.topMesh,meshes[0]);cell.wallMesh=uploadMesh(cell.wallMesh,meshes[1]);
        cell.top.setMesh(cell.topMesh);cell.wall.setMesh(cell.wallMesh);
        cell.x=c.x;cell.z=c.z;cell.hit=nearest;cell.random=c.random;
        cell.influence=exp(-nearest.d*nearest.d/5.0);
        cell.top.setTint(0.80+c.random*0.25,0.82+c.random*0.22,0.82+c.random*0.16,1.0);
        cell.wall.setTint(0.32+c.random*0.16,0.26+c.random*0.12,0.19+c.random*0.09,1.0);
    }
    local rng=randomStream(settings.seed.tointeger()+1907);
    foreach(i,d in debris) {
        local path=paths[i%paths.len()],p=path[(rng()*(path.len()-1)).tointeger()];
        local a=rng()*6.283185;d.x=p.x+(rng()-0.5)*0.6;d.z=p.z+(rng()-0.5)*0.6;
        d.delay=p.t*settings.duration;d.vx=cos(a)*(0.4+rng())*settings.impulse;
        d.vz=sin(a)*(0.4+rng())*settings.impulse;d.vy=(0.65+rng())*settings.impulse;
        d.size=0.05+rng()*0.20;d.spin=rng()*5.0;d.life=0.8+rng()*1.5;
    }
    foreach(i,d in dust) {
        local path=paths[i%paths.len()],p=path[(rng()*(path.len()-1)).tointeger()],a=rng()*6.283185;
        d.x=p.x;d.z=p.z;d.delay=p.t*settings.duration+rng()*0.22;
        d.vx=cos(a)*(0.4+rng());d.vz=sin(a)*(0.4+rng());
        d.size=0.65+rng()*0.75;d.life=1.3+rng()*1.6;d.rise=0.3+rng()*0.6;
    }
    local index=0;
    foreach(path in paths) for(local i=0;i<path.len()-1;i++) {
        local a=path[i],b=path[i+1],c=cores[index++],dx=b.x-a.x,dz=b.z-a.z;
        c.x=(a.x+b.x)*0.5;c.z=(a.z+b.z)*0.5;c.length=sqrt(dx*dx+dz*dz)+0.12;
        c.angle=atan2(-dz,dx);c.delay=a.t*settings.duration;
    }
    foreach(i,c in cores) c.used=i<index;
    ground.setPosition(0.0,-settings.depth-0.3,0.0);
    lavaFloor.setPosition(0.0,-settings.depth+0.03,0.0);
    print(format("FAULT_APPLY preset=%s seed=%d cells=%d paths=%d width=%.2f depth=%.2f\n",
        presetName,settings.seed.tointeger(),cells.len(),paths.len(),settings.width,settings.depth));
    replay();
}
function applyKnobs() {
    foreach(k in knobs) settings[k.id]=bounded(ui.getValue(k.id),k.lo,k.hi);
    settings.seed=settings.seed.tointeger();settings.branches=settings.branches.tointeger();settings.debris=settings.debris.tointeger();
    rebuild();
}
function buildUI() {
    ui.setTheme("dark");ui.setScale(0.78);ui.beginBuild();ui.beginWindow("FAULT / CONTROL","root");
    ui.text("F A U L T   /   0 1","title");ui.text("PARAMETRIC GROUND FISSURE","subtitle");
    ui.beginRow("presets",5.0);ui.button("Earth","earth");ui.button("Lava","lava");ui.button("Radial","radial");ui.end();
    ui.beginRow("transport",5.0);ui.button("Apply + replay","apply");ui.button("Replay","replay");ui.button("Pause","pause");ui.end();
    ui.text("Shape controls apply on Apply + replay","hint");
    foreach(k in knobs) ui.slider(k.label,settings[k.id].tofloat(),k.lo,k.hi,k.id);
    ui.separator("separator");ui.slider("Playback speed",1.0,0.1,2.0,"speed");
    ui.slider("Inspect time / s",0.0,0.0,8.0,"timeline");
    ui.slider("Camera orbit",0.52,-3.14,3.14,"orbit");
    ui.beginRow("seek",5.0);ui.button("Intact","intact");ui.button("Impact","impact");ui.button("Opened","opened");ui.button("Loop: ON","loop");ui.end();
    ui.beginRow("fine",5.0);ui.button("Step +1/16 s","step");ui.button("Width -","narrower");ui.button("Width +","wider");ui.end();
    ui.text("SPACE pause  /  R replay  /  1 2 3 preset","keys");ui.text("", "status");ui.end();
    ui.mountBuildAs("fault");ui.select("fault");ui.setHostPos(12.0,12.0,0.0,0.0);
    ui.setHostSize(302.0,776.0);ui.setHostOverlay(true);
}
function control(id) {
    if(id=="earth"||id=="1") preset(0);
    if(id=="lava"||id=="2") preset(1);
    if(id=="radial"||id=="3") preset(2);
    if(id=="apply") applyKnobs();
    if(id=="replay"||id=="r") replay();
    if(id=="pause"||id=="space") paused=!paused;
    if(id=="intact") {age=-0.05;paused=true;}
    if(id=="impact") {age=settings.duration*0.5;paused=true;}
    if(id=="opened") {age=settings.duration+2.4;paused=true;}
    if(id=="step") {age=bounded(age+0.0625,-0.6,8.0);paused=true;}
    if(id=="narrower"||id=="wider") {
        ui.setValue("width",bounded(ui.getValue("width")+(id=="wider"?0.25:-0.25),0.1,2.5));applyKnobs();
    }
    if(id=="loop") {looping=!looping;ui.setText("loop",looping?"Loop: ON":"Loop: OFF");}
}
function listenKeys() {
    asyncOnEvent("keypressed").then(function(key) {control(key.tolower());listenKeys();});
}
function eve_init() {
    if("dev" in eve) {eve.dev.console.setVisible(false);eve.dev.ai.setVisible(false);}
    gfx.setBackgroundColor(0.027,0.037,0.045,1.0);
    local rc=gfx.getRenderControl();rc.enable("forward");rc.enable("depthTest");rc.enable("shadow");rc.enable("aa");
    if(!rc.setLightingMode("forwardPlus")) throw "ground-fissure: forward lighting unavailable";
    cam=eve.Camera3D();cam.setUp(0.0,1.0,0.0);cam.setFov(43.0);cam.setClipPlanes(0.1,90.0);
    cam.setAmbient(0.32,0.37,0.44);cam.setActive(true);
    sun=eve.Light3D();sun.setType("dir");sun.setDirection(-0.6,1.0,0.4);sun.setColor(1.0,0.86,0.67,1.5);sun.setCastShadow(true);
    gfx.setDirectionalLight(-0.6,1.0,0.4,1.25,1.1,0.9);
    local texture=gfx.newTextureFromFileRepeated("assets/dirt_diffuse_1k.jpg",true,true);
    if(texture==null) throw "ground-fissure: missing bundled dirt texture";
    gfx.setTextureSampler(texture,"linear","linear",4.0,0.0);
    topMaterial=makeMaterial(texture,true);wallMaterial=makeMaterial(texture,true);
    topMaterial.setTint(0.48,0.46,0.42,1.0);wallMaterial.setTint(0.18,0.14,0.10,1.0);
    cube=gfx.newMeshCube(1.0);rockMesh=gfx.newMeshSphere(5,3);
    local neutral=makeMaterial(null,true);neutral.setTint(0.025,0.021,0.018,1.0);
    ground=object(cube,neutral);ground.setScale(19.5,0.5,17.5);ground.setTint(0.045,0.036,0.030,1.0);
    local lavaTexture=gfx.newTextureFromFile("assets/lava.tga");
    if(lavaTexture==null) throw "ground-fissure: missing bundled lava texture";
    lavaMaterial=makeMaterial(lavaTexture,false);
    lavaFloor=object(cube,lavaMaterial);lavaFloor.setScale(17.9,0.015,15.9);
    local baseMaterial=makeMaterial(null,true);baseMaterial.setTint(0.045,0.058,0.065,1.0);
    local pedestal=object(cube,baseMaterial);pedestal.setPosition(0.0,-3.9,0.0);pedestal.setScale(21.5,0.25,19.5);
    for(local i=0;i<120;i++) cells.push({top=object(cube,topMaterial),wall=object(cube,wallMaterial),topMesh=null,wallMesh=null,
        x=0.0,z=0.0,hit=null,influence=0.0,random=0.0});
    for(local i=0;i<120;i++) {
        local e=object(rockMesh,wallMaterial);e.setTint(0.7,0.55,0.39,1.0);
        debris.push({e=e,x=0.0,z=0.0,delay=0.0,vx=0.0,vz=0.0,vy=0.0,size=0.0,spin=0.0,life=0.0});
    }
    local quad=meshData();
    vertex(quad,-0.5,0.0,0.0,0.0,0.0,1.0,0.0,1.0);vertex(quad,0.5,0.0,0.0,0.0,0.0,1.0,1.0,1.0);
    vertex(quad,0.5,1.0,0.0,0.0,0.0,1.0,1.0,0.0);vertex(quad,-0.5,1.0,0.0,0.0,0.0,1.0,0.0,0.0);
    quad.idx=[0,1,2,0,2,3];card=uploadMesh(null,quad);
    dustTexture=gfx.newTextureFromFile("assets/dust.tga");
    if(dustTexture==null) throw "ground-fissure: missing bundled dust texture";
    for(local i=0;i<72;i++) {
        local m=makeMaterial(dustTexture,false);m.setSurfaceMode("transparent");m.setBlendMode("alpha");m.setDepthWrite(false);
        m.setCastShadow(false);m.setReceiveShadow(false);
        dust.push({e=object(card,m),m=m,x=0.0,z=0.0,delay=0.0,vx=0.0,vz=0.0,size=0.0,life=0.0,rise=0.0});
    }
    glowMaterial=makeMaterial(null,false);
    for(local i=0;i<80;i++) cores.push({e=object(cube,glowMaterial),x=0.0,z=0.0,length=0.0,angle=0.0,delay=0.0,used=false});
    buildUI();listenKeys();rebuild();
    print("GROUND_FISSURE_READY cells=120 debrisPool=120 dustPool=72 nativeMesh=true\n");
}
function eve_update(dt) {
    ui.select("fault");
    if(pendingActions.len()>0) {local next=pendingActions[0];pendingActions.remove(0);control(next);}
    local click=ui.consumeClick();
    while(click!="") {local slash=click.find("/");control(slash==null?click:click.slice(slash+1));click=ui.consumeClick();}
    local change=ui.consumeChange();
    while(change!="") {
        if(change=="fault/timeline"||change=="timeline") {age=ui.getValue("timeline");paused=true;}
        change=ui.consumeChange();
    }
    if(!paused) age+=bounded(dt,0.0,0.1)*ui.getValue("speed");
    if(looping&&age>settings.duration+4.0) replay();
    local yaw=ui.getValue("orbit"),dist=31.0;
    cam.setEye(-3.5+sin(yaw)*dist,22.0,1.0+cos(yaw)*dist);cam.setTarget(-3.5,-1.8,1.0);
    foreach(c in cells) {
        local t=age-c.hit.t*settings.duration,open=smooth(t/0.48),a=c.influence;
        local dx=c.hit.nx*settings.width*0.62*open*a,dz=c.hit.nz*settings.width*0.62*open*a;
        local kick=t>0.0?sin(bounded(t/0.7,0.0,1.0)*3.14159265)*0.3:0.0;
        local y=(settings.lift*(0.2+0.8*c.random)*open+kick)*a;
        local tilt=settings.lift*open*a*0.14;
        foreach(e in [c.top,c.wall]) {
            e.setPosition(c.x+dx,y,c.z+dz);e.setScale(1.0-open*a*0.055,1.0,1.0-open*a*0.055);
            e.setRotation(0.0,c.hit.nz*tilt,-c.hit.nx*tilt);
        }
    }
    foreach(i,d in debris) {
        local t=age-d.delay;d.e.setVisible(i<settings.debris&&t>=0.0&&t<d.life);
        if(i>=settings.debris||t<0.0||t>=d.life) continue;
        local y=0.12+d.vy*t-4.9*t*t;
        if(y<0.0) {d.e.setVisible(false);continue;}
        d.e.setPosition(d.x+d.vx*t,y,d.z+d.vz*t);d.e.setRotation(t*d.spin,t*d.spin*0.6,t*d.spin*0.4);
        local s=d.size*(1.0-smooth((t-d.life+0.25)/0.25));d.e.setScale(s,s*0.7,s*1.2);
    }
    foreach(i,d in dust) {
        local t=age-d.delay;d.e.setVisible(i<settings.dust*72&&t>=0.0&&t<d.life);
        if(i>=settings.dust*72||t<0.0||t>=d.life) continue;
        local fade=smooth(t/0.16)*(1.0-smooth((t/d.life-0.25)/0.75));
        local s=d.size*(0.7+t*1.1);d.e.setPosition(d.x+d.vx*t,0.15+t*d.rise,d.z+d.vz*t);
        d.e.setRotation(yaw,0.0,0.0);d.e.setScale(s,s*0.7,s);
        d.m.setTint(0.70,0.61,0.47,fade*0.65);
    }
    local pulse=(0.9+sin(age*4.0)*0.1)*settings.glow;
    glowMaterial.setTint(settings.red*pulse,settings.green*pulse,settings.blue*pulse,1.0);
    lavaFloor.setVisible(settings.glow>0.0&&age>=0.0);
    // A red-biased texture supplies mottled heat; user RGB still controls its hue.
    lavaMaterial.setTint(settings.red*pulse,settings.green*pulse*2.5,settings.blue*pulse*5.0,1.0);
    foreach(c in cores) {
        local open=smooth((age-c.delay)/0.55);c.e.setVisible(c.used&&settings.glow>0.0&&open>0.0);
        if(!c.used) continue;
        c.e.setPosition(c.x,-settings.depth+0.09,c.z);c.e.setRotation(c.angle,0.0,0.0);
        c.e.setScale(c.length,0.035,settings.width*0.64*open);
    }
    ui.setText("status",format("%s  |  %s  |  %.2f s",presetName,paused?"PAUSED":"PLAYING",age));
    ui.setText("pause",paused?"Play":"Pause");
    ui.setValue("timeline",bounded(age,0.0,8.0));
    faultState.time=age;faultState.paused=paused;faultState.seed=settings.seed;faultState.replays=replays;
    faultState.width=settings.width;faultState.depth=settings.depth;faultState.preset=presetName;
}
function eve_render() {
    gfx.clear();gfx.render3D();
    // Thin framing accents leave the scene itself unobstructed.
    gfx.drawSolidRect(336.0,28.0,4.0,40.0,0.88,0.39,0.12,1.0);
    gfx.drawSolidRect(336.0,770.0,916.0,1.0,0.15,0.19,0.20,1.0);
    ui.select("fault");ui.beginFrameAndRender();
}
// MCP may call actions while polling. Enqueue only; apply on the next update,
// exactly like UI input, so no render-resource mutation occurs inside tool dispatch.
faultState <- {time=0.0,paused=false,seed=37,replays=0,width=1.25,depth=1.6,preset="LAVA RIFT"};
faultActions <- {
    replay=function() {pendingActions.push("replay");},
    earth=function() {pendingActions.push("earth");},
    lava=function() {pendingActions.push("lava");},
    radial=function() {pendingActions.push("radial");},
    intact=function() {pendingActions.push("intact");},
    impact=function() {pendingActions.push("impact");},
    opened=function() {pendingActions.push("opened");},
    step=function() {pendingActions.push("step");},
    narrower=function() {pendingActions.push("narrower");},
    wider=function() {pendingActions.push("wider");}
};
