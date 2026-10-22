// Native spline -> bounded GPU profile; native particles -> foreground debris.
local shader=null, path=null, debris=null, white=null, elapsed=0.0, paused=false;
local samples=[], plot=[];
local knobs=[
    {id="height",label="Height",value=7.0,lo=3.0,hi=10.0},
    {id="radius",label="Funnel width",value=1.8,lo=0.4,hi=3.0},
    {id="density",label="Optical density",value=1.5,lo=0.1,hi=3.0},
    {id="twist",label="Spiral pitch",value=1.2,lo=0.2,hi=3.0},
    {id="turbulence",label="Turbulence",value=0.7,lo=0.0,hi=1.8},
    {id="dust",label="Ground dust",value=0.9,lo=0.0,hi=2.0},
    {id="bend",label="Axis sway",value=0.65,lo=0.0,hi=2.0},
    {id="orbit",label="Camera orbit",value=0.3,lo=-3.14,hi=3.14},
    {id="steps",label="Ray samples",value=80.0,lo=40.0,hi=128.0},
    {id="speed",label="Time scale",value=1.0,lo=0.0,hi=2.0}
];
local radii=[0.16,0.30,0.55,1.35];
function need(r,context) { if(!r.ok) throw context+": "+r.status.summary; return r.value; }
function rebuildCurve() {
    path=need(procgen.newSplinePath(),"Create profile spline");
    need(path.setKind("catmullRom"),"Set profile interpolation");
    foreach(i,r in radii) need(path.addPoint(r,i/3.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0),"Add radius key");
    samples.clear();
    for(local i=0;i<8;++i) {
        local s=need(path.evaluateResult(i/7.0),"Sample radius profile");
        local r=s.getX(); samples.push(r<0.04?0.04:r);
        shader.sendFloat("profile"+i,samples[i]);
    }
    plot.clear();
    for(local i=0;i<64;++i) plot.push(need(path.evaluateResult(i/63.0),"Plot profile").getX());
}
function preset(wide) {
    radii=wide?[0.45,0.7,1.1,1.6]:[0.16,0.30,0.55,1.35];
    foreach(i,r in radii) ui.setValue("key"+i,r);
    rebuildCurve();
}
eve_init=function() {
    if("dev" in eve) { eve.dev.console.setVisible(false); eve.dev.ai.setVisible(false); }
    shader=gfx.newShaderFromSpvFile("shaders/tornado.frag.spv");
    white=gfx.newTexture(eve.Image().newImageDataFromFile("white.tga"),false,false);
    shader.declareFloat("time");
    for(local i=0;i<9;++i) shader.declareFloat(knobs[i].id);
    for(local i=0;i<8;++i) shader.declareFloat("profile"+i);
    rebuildCurve();
    ui.setScale(0.85); ui.setTheme("dark"); ui.beginBuild(); ui.beginWindow("TORNADO LAB","root");
    ui.text("VOLUMETRIC / 01","title");
    ui.text("TORNADO LABORATORY","subtitle");
    ui.beginRow("presets",6.0); ui.button("Slender","slender"); ui.button("Wedge","wedge"); ui.button("Pause","pause"); ui.end();
    foreach(k in knobs) ui.slider(k.label,k.value,k.lo,k.hi,k.id);
    ui.separator("sep"); ui.text("RADIUS / HEIGHT CURVE","curve-title");
    foreach(i,r in radii) ui.slider("Radius at "+(i*100/3)+"%",r,0.06,1.8,"key"+i);
    ui.text("Native Catmull-Rom / 4 editable keys","curve-help");
    ui.text("", "status"); ui.end(); ui.mountBuildAs("tornado"); ui.select("tornado");
    ui.setHostPos(8.0,8.0,0.0,0.0); ui.setHostSize(294.0,690.0); ui.setHostOverlay(true);
    debris=particles.newEmitter(180); debris.setRandomSeed(22026); debris.setAutoRandomSeed(false);
    debris.setEmissionRate(18.0); debris.setParticleLifetime(0.7,1.8);
    debris.setParticleSize(3.0,2.0); debris.setSizes(1.0,0.0); debris.setSpeed(30.0,90.0);
    debris.setSpread(6.283185); debris.setGravity(0.0,-26.0); debris.setDamping(0.3);
    debris.setBlendMode("alpha"); debris.setColorStart(0.42,0.35,0.23,0.65);
    debris.setColorEnd(0.24,0.25,0.23,0.0); debris.start();
    print("TORNADO_READY nativeSpline=catmullRom profileSamples=8 volumeLayers=3 particles=180\n");
};
eve_update=function(dt) {
    ui.select("tornado"); local click=ui.consumeClick();
    while(click!="") {
        if(click=="tornado/pause") paused=!paused;
        if(click=="tornado/slender") preset(false);
        if(click=="tornado/wedge") preset(true);
        click=ui.consumeClick();
    }
    local changed=ui.consumeChange();
    while(changed!="") {
        local dirty=false;
        foreach(i,r in radii) { local v=ui.getValue("key"+i); if(v!=r) { radii[i]=v; dirty=true; } }
        if(dirty) rebuildCurve();
        changed=ui.consumeChange();
    }
    local step=paused?0.0:dt*ui.getValue("speed"); elapsed+=step;
    shader.sendFloat("time",elapsed);
    for(local i=0;i<9;++i) shader.sendFloat(knobs[i].id,ui.getValue(knobs[i].id));
    debris.setEmissionRate(ui.getValue("dust")*18.0);
    local target=ui.getValue("height")*0.48;
    local groundY=400.0+700.0*15.0*target/(250.0-5.0*target);
    debris.setPosition(795.0+cos(elapsed*3.0)*55.0,groundY+sin(elapsed*3.0)*9.0);
    particles.update(step);
    ui.setText("status",format("%s / %.1f s",paused?"PAUSED":"LIVE",elapsed));
};
eve_render=function() {
    gfx.clear(); gfx.setShader(shader);
    gfx.drawTexturedRect(white,310.0,0.0,970.0,800.0,1.0,1.0,1.0,1.0);
    gfx.setShader(null); particles.render(gfx);
    // Live curve plot: x = radius, y = normalized height, sampled from the native spline.
    gfx.drawSolidRect(16.0,706.0,278.0,80.0,0.035,0.055,0.07,1.0);
    for(local i=0;i<64;++i) gfx.drawSolidRect(24.0+plot[i]*135.0,778.0-i,3.0,3.0,0.48,0.85,0.83,1.0);
    ui.select("tornado"); ui.beginFrameAndRender();
};
