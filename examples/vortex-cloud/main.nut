local shader=null,camera=null,elapsed=0.0,paused=false,showControls=false;
local objects=[],materials=[],rockShader=null,noiseTexture=null;
local knobs=[
    {id="speed",label="Speed",value=0.045,lo=-0.2,hi=0.2},
    {id="twist",label="Curl",value=2.0,lo=0.0,hi=6.0},
    {id="cover",label="Density",value=1.5,lo=0.4,hi=3.0},
    {id="eye",label="Eye",value=2.5,lo=0.3,hi=3.0},
    {id="detail",label="Detail",value=0.65,lo=0.3,hi=1.2},
    {id="pitch",label="Look up",value=0.32,lo=0.12,hi=0.65},
    {id="steps",label="Samples",value=192.0,lo=64.0,hi=256.0},
    {id="flare",label="Wall flare",value=0.85,lo=0.3,hi=1.1}
];
function part(mesh,x,y,z,sx,sy,sz,color) {
    local e=eve.Renderable3D(); e.setMesh(mesh);
    e.setPosition(x.tofloat(),y.tofloat(),z.tofloat()); e.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    e.setTint(color[0],color[1],color[2],1.0); e.setRoughness(0.95); e.setCastShadow(false);
    objects.push(e); return e;
}
function buildScene() {
    local cube=gfx.newMeshCube(1.0),sphere=gfx.newMeshSphere(12,8),rock=gfx.newMeshSphere(7,4);
    local sky=part(cube,0,0,0,80,80,80,[1.0,1.0,1.0]);
    local mat=gfx.newMaterial(); mat.setShader(shader); mat.setShadingModel("custom");
    noiseTexture=gfx.newTexture(eve.Image().newImageDataFromFile("cloud-noise.tga"),false,false);
    mat.setAlbedoTexture(noiseTexture);
    mat.setDoubleSided(true); mat.setReceiveLight(false); mat.setReceiveShadow(false); mat.setCastShadow(false);
    materials.push(mat); sky.setMaterial(mat);
    // Fixed-seed analytic layout; no per-frame creation or global RNG use.
    local rockResult=gfx.loadMeshShaderSpv("","shaders/rock.frag.spv");
    if(!rockResult.ok) throw "Rock shader: "+rockResult.status.summary;
    rockShader=rockResult.value;
    part(rock,0,-1.6,-1.0,9,1.7,7,[0.035,0.047,0.052]).setShader(rockShader);
    for(local i=0;i<70;++i) {
        local a=i*2.39996,r=1.3+(i%13)*0.44;
        local x=cos(a)*r,z=-1.0+sin(a)*r;
        local s=0.28+(sin(i*72.13)*0.5+0.5)*0.65;
        local e=part(rock,x,-0.37-s*0.25,z,s*1.7,s*0.65,s,[0.045,0.058,0.064]);
        e.setRotation(i*0.13,i*0.37,i*0.23);
        e.setShader(rockShader);
    }
    // Back-facing traveller: boots, separated legs, coat, arms and hood.
    local dark=[0.025,0.032,0.036],cloth=[0.042,0.049,0.051];
    foreach(x in [-0.13,0.13]) {
        part(cube,x,0.06,-2.0,0.14,0.14,0.29,dark);
        part(sphere,x,0.42,-2.0,0.095,0.36,0.11,dark);
    }
    part(sphere,0,1.04,-2.0,0.25,0.40,0.15,cloth);
    part(sphere,0,1.51,-2.04,0.14,0.18,0.14,dark);
    part(sphere,-0.30,1.01,-2.0,0.075,0.32,0.09,cloth).setRotation(0.0,0.0,-0.14);
    part(sphere,0.30,1.02,-2.0,0.075,0.31,0.09,cloth).setRotation(0.0,0.0,0.18);
    // A folded cloak is a native mesh, readable against the luminous sky.
    local pos=[],norm=[],uv=[],idx=[];
    for(local j=0;j<=8;++j) for(local i=0;i<=12;++i) {
        local u=i/12.0,v=j/8.0,w=0.20+v*0.24;
        pos.extend([(u*2-1)*w-0.11*v*v,1.32-v*0.97,-1.82+0.13*v+sin(u*25.0)*0.035*v]);
        norm.extend([0.0,0.0,1.0]); uv.extend([u,v]);
        if(i<12&&j<8) { local a=j*13+i; idx.extend([a,a+13,a+1,a+1,a+13,a+14]); }
    }
    local cloak=part(gfx.newMeshFromArrays(pos,norm,uv,pos.len()/3,idx,idx.len()),0,0,0,1,1,1,cloth);
    local cloakMat=gfx.newMaterial(); cloakMat.setDoubleSided(true);
    cloakMat.setTint(0.025,0.032,0.036,1.0); materials.push(cloakMat); cloak.setMaterial(cloakMat);
    camera=eve.Camera3D(); camera.setEye(0.0,1.2,8.0); camera.setUp(0.0,1.0,0.0);
    camera.setFov(60.0); camera.setAmbient(0.20,0.24,0.29); camera.setActive(true);
    gfx.setDirectionalLight(0.25,0.8,-0.5,0.75,0.61,0.40);
}
eve_init=function() {
    if("dev" in eve) { eve.dev.console.setVisible(false); eve.dev.ai.setVisible(false); }
    if(!gfx.getRenderControl().setLightingMode("forwardPlus")) throw "Forward lighting unavailable";
    local result=gfx.loadMeshShaderSpv("","shaders/vortex.frag.spv");
    if(!result.ok) throw "Sky shader: "+result.status.summary;
    shader=result.value; shader.declareFloat("time"); foreach(k in knobs) shader.declareFloat(k.id);
    buildScene();
    ui.setScale(0.8); ui.setTheme("dark"); ui.beginBuild(); ui.beginWindow("SKY VORTEX","root");
    ui.text("SKY VORTEX / H to hide","title");
    foreach(k in knobs) ui.slider(k.label,k.value,k.lo,k.hi,k.id);
    ui.button("Pause / resume","pause"); ui.button("Reset","reset");
    ui.end(); ui.mountBuildAs("vortex"); ui.select("vortex");
    ui.setHostPos(14.0,18.0,0.0,0.0); ui.setHostSize(290.0,470.0); ui.setHostOverlay(true);
    ui.beginBuild(); ui.beginWindow("","root"); ui.button("Controls [H]","toggle"); ui.end(); ui.mountBuildAs("vortex-hud");
    ui.select("vortex-hud"); ui.setHostPos(14.0,750.0,0.0,0.0);
    ui.setHostSize(170.0,38.0); ui.setHostOverlay(true);
    print("VORTEX_CLOUD_READY volume=1 nativeForeground=1 controls=H\n");
};
eve_update=function(dt) {
    if(key_just_pressed("h","H")) showControls=!showControls;
    ui.select("vortex"); local click=ui.consumeClick();
    while(click!="") {
        if(click=="vortex-hud/toggle") showControls=!showControls;
        if(click=="vortex/pause") paused=!paused;
        if(click=="vortex/reset") { elapsed=0.0; foreach(k in knobs) ui.setValue(k.id,k.value); }
        click=ui.consumeClick();
    }
    ui.setHostVisible(showControls);
    if(!paused) elapsed+=dt*ui.getValue("speed");
    shader.sendFloat("time",elapsed); foreach(k in knobs) shader.sendFloat(k.id,ui.getValue(k.id));
    local pitch=ui.getValue("pitch");
    camera.setTarget(0.0,1.2+sin(pitch)*20.0,8.0-cos(pitch)*20.0);
};
eve_render=function() {
    gfx.clear(); gfx.render3D();
    ui.select("vortex-hud"); ui.beginFrameAndRender();
};
