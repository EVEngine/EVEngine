dofile("dust.nut");
// Fixed-step bake once; reload on subsequent launches. No physics during playback.
persist world3 = null
persist instance = null
persist renderer = null
persist clip = null
persist playTime = 0.0
persist paused = true
persist font = null
persist orbit = 0.66
persist sunlight = null
persist studioGround = null
persist shadowsEnabled = true
persist aaEnabled = true
persist physics = null
persist destruction = null
persist destructionFx = null
persist destructionCook = null

function checked(r) {
    if (!r.ok) throw "collapse operation failed: " + r.status.summary;
}
function applyAntialiasing() {
    local control=gfx.getRenderControl();
    if(aaEnabled) {
        control.enable("aa");control.disable("taa");control.enable("msaa");gfx.setMsaaSamples(8);
    } else {
        control.disable("taa");control.disable("msaa");control.disable("aa");
    }
}
// Event controls work for physical keys and engine-owned input injection alike.
function listenControls() {
    asyncOnEvent("keypressed").then(function(key) {
        key=key.tolower();
        if (key=="space") paused=!paused;
        if (key=="r") {playTime=0.0;paused=false;}
        if (key=="1") {playTime=0.0;paused=true;}
        if (key=="2") {playTime=config.collapse.impacts[0].time+1.2;paused=true;}
        if (key=="3") {playTime=config.collapse.impacts[1].time+1.8;paused=true;}
        if (key=="4") {playTime=config.collapse.impacts[2].time+1.8;paused=true;}
        if (key=="5") {playTime=clip.duration()-0.1;paused=true;}
        if(key=="v") dustEnabled=!dustEnabled;
        if(key=="f") {aaEnabled=!aaEnabled;applyAntialiasing();}
        if(aaEnabled && (key=="1"||key=="2"||key=="3"||key=="4"||key=="5"||key=="r")) {
            gfx.getRenderControl().disable("taa");applyAntialiasing();
        }
        if(key=="s") {
            shadowsEnabled=!shadowsEnabled;
            if(shadowsEnabled)gfx.getRenderControl().enable("shadow");else gfx.getRenderControl().disable("shadow");
        }
        listenControls();
    });
}
function countBroken() {
    local count=0;for(local edge=0;edge<instance.edgeCount();edge++)if(instance.isEdgeBroken(edge))count++;
    return count;
}
// Cache identity follows the authored recipe, source and bake script, not wall time.
function bakePath(settings) {
    local text="house-structural-v2"+fs.readText("config.nut")+fs.readText("house.json")+fs.readText("main.nut");
    return settings.output+"-"+clip.contentKey(text)+".evecollapse";
}
function eve_init() {
    // This scene has few lights; select the ordinary forward lighting path.
    if (!gfx.getRenderControl().setLightingMode("forwardPlus")) throw "forward lighting unavailable";
    local renderControl=gfx.getRenderControl();
    renderControl.enable("forward");renderControl.enable("depthTest");renderControl.enable("gbuffer");
    renderControl.enable("shadow");renderControl.enable("aa");
    applyAntialiasing();
    renderControl.setPostProcessQuality("high");
    gfx.setShadowSchemeDirectionalEnabled(true);
    print("cottage render: forward lighting, explicit shadow-casting sun, spatial FXAA high and MSAA requested 8\n");
    gfx.setBackgroundColor(0.075,0.102,0.125,1.0);
    font=gfx.newFont(eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",24),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.-:+|[]");
    camera=eve.Camera3D();camera.setEye(14.0,11.0,18.0);camera.setTarget(0.0,3.5,0.0);
    camera.setUp(0.0,1.0,0.0);camera.setFov(39.0);camera.setAmbient(0.17,0.21,0.27);camera.setClipPlanes(0.2,65.0);camera.setActive(true);
    gfx.setDirectionalLight(-0.7,1.0,0.5,1.3,1.15,0.95);
    sunlight=eve.Light3D();sunlight.setType("dir");sunlight.setDirection(-0.7,1.0,0.5);
    sunlight.setColor(1.0,0.88,0.73,1.5);sunlight.setCastShadow(true);
    sunlight.setShadowBias(0.0);sunlight.setShadowStrength(1.0);
    // A real scene receiver also participates in depth/G-buffer preparation.
    studioGround=eve.Renderable3D();studioGround.setMesh(gfx.newMeshCube(1.0));
    studioGround.setPosition(0.0,-0.5,0.0);studioGround.setScale(40.0,0.1,40.0);
    studioGround.setTint(0.22,0.28,0.29,1.0);studioGround.setRoughness(0.95);
    studioGround.setReceiveLight(true);studioGround.setReceiveShadow(true);studioGround.setCastShadow(false);
    physics=eve.Physics();destruction=eve.Destruction();destructionFx=eve.DestructionFx();
    clip=eve.CollapseAnimation();
    local view=destructionFx.createAnimationRenderer();checked(view);renderer=view.value;
    renderer.setExteriorColor(0.75,0.58,0.40,1.0);
    renderer.setInteriorColor(0.42,0.25,0.14,1.0);
    local settings=config.collapse;
    foreach(group in settings.groups) checked(renderer.setGroupExteriorColor(group.id,group.color[0],group.color[1],group.color[2],1.0));
    if(settings.fixedHz<=0.0 || settings.seconds<=0.0) throw "collapse fixedHz and seconds must be positive";
    local output=settings.playbackAsset!="" ? settings.playbackAsset : bakePath(settings);
    print("house-collapse: asset="+output+"\n");
    local loaded=clip.load(output);
    if (!loaded.ok) {
        world3=physics.newWorld3D(0.0,-9.8,0.0,true);
        local ground=world3.newBody("static",0.0,-0.95,0.0);
        ground.newBoxShape(30.0,1.0,30.0,0.0,0.65,0.0);
        local asset=destruction.assetFromJson(fs.readText("house.json"));
        destructionCook=eve.DestructionCook();
        local fracture=eve.AssemblyFractureSettings();
        checked(fracture.setSeed(settings.seed));
        checked(fracture.setContactTolerance(settings.contactTolerance));
        checked(fracture.setMaximumBones(settings.maximumFragments));
        foreach(group in settings.groups) checked(fracture.setGroupPolicy(group.id,group.fragments,group.cohesion,group.connection));
        local cooked=destructionCook.fractureBoxes(asset,fracture);checked(cooked);
        local created=cooked.value.createInstance(world3,0.0,0.0,0.0);checked(created);instance=created.value;
        print("house-collapse: Voronoi fragments="+instance.boneCount()+"\n");
        instance.setStepBudget(settings.edgeBreaksPerStep,0);
        local dt=1.0/settings.fixedHz;
        local frames=(settings.seconds*settings.fixedHz).tointeger();
        local fired={};
        checked(clip.capture(instance,0.0));
        for(local frame=1;frame<=frames;frame++) {
            local seconds=frame*dt;
            foreach(index,impact in settings.impacts) if(!(index in fired) && seconds>=impact.time) {
                instance.applyStrainField(impact.x,impact.y,impact.z,impact.radius,impact.strain);
            }
            instance.step(frame,dt);
            foreach(index,impact in settings.impacts) if(!(index in fired) && seconds>=impact.time) {
                instance.applyImpulseField(impact.x,impact.y,impact.z,impact.radius,impact.impulse,impact.dx,impact.dy,impact.dz);
                fired[index]<-true;
            }
            world3.update(dt);
            checked(clip.capture(instance,seconds));
        }
        print("house-collapse: broken edges="+countBroken()+" of "+instance.edgeCount()+"\n");
        checked(clip.save(output));
        // Exercise disk decoding even on the first run.
        checked(clip.load(output));
        print("house-collapse: baked, saved and reloaded " + clip.frameCount() + " frames\n");
        instance.releaseBodies();instance=null;world3.destroy();world3=null;
    } else print("house-collapse: loaded saved animation without cook or physics world\n");
    checked(renderer.prepareAnimation(clip,gfx));
    initDust(output);
    listenControls();
    print("house-collapse: physics destroyed; Space pause, arrows scrub, R restart\n");
}
function eve_update(dt) {
    if (keyboard.isDown("a")) orbit-=dt*0.5;
    if (keyboard.isDown("d")) orbit+=dt*0.5;
    camera.setEye(sin(orbit)*23.0,11.0,cos(orbit)*23.0);
    if (keyboard.isDown("left")) {playTime-=dt*2;paused=true;}
    if (keyboard.isDown("right")) {playTime+=dt*2;paused=true;}
    if (!paused) playTime+=dt;
    if (playTime<0) playTime=0.0;
    if (playTime>clip.duration()) {playTime=clip.duration();paused=true;}
}
function eve_render() {
    gfx.clear();
    checked(renderer.submitAnimation(clip,playTime,0.0,0.0,0.0));
    gfx.render3D();
    drawDust();
    gfx.drawSolidRect(0.0,0.0,1280.0,112.0,0.035,0.052,0.066,0.97);
    gfx.drawSolidRect(32.0,30.0,4.0,49.0,0.76,0.38,0.19,1.0);
    gfx.drawText(font,"COTTAGE / FOUR-STAGE COLLAPSE",52.0,26.0,0.93,0.89,0.78,1.0,1.0);
    gfx.drawText(font,((shadowsEnabled ? "SUN / SHADOWS ON / " : "SUN / SHADOWS OFF / ")+(aaEnabled ? "MSAA + FXAA" : "AA OFF")),912.0,44.0,0.67,0.74,0.76,1.0,0.51);
    gfx.drawText(font,"Masonry walls  /  timber frame  /  tiled roof",52.0,62.0,0.56,0.65,0.68,1.0,0.64);
    gfx.drawSolidRect(0.0,744.0,1280.0,116.0,0.035,0.052,0.066,0.97);
    local phase="01  INTACT HOUSE";
    foreach(index,impact in config.collapse.impacts) if(playTime>=impact.time) phase=format("%02d  %s",index+2,impact.label);
    gfx.drawText(font,phase,36.0,760.0,0.89,0.82,0.65,1.0,0.82);
    gfx.drawText(font,format("%04.1f / %.1f sec",playTime,clip.duration()),1035.0,762.0,0.75,0.81,0.82,1.0,0.7);
    gfx.drawSolidRect(36.0,800.0,1208.0,3.0,0.20,0.27,0.29,1.0);
    gfx.drawSolidRect(36.0,800.0,1208.0*playTime/clip.duration(),3.0,0.76,0.38,0.19,1.0);
    gfx.drawText(font,"SPACE play/pause   1 intact   2 walls   3 roof   4 frame   5 final   A/D orbit   S shadows   F AA   V dust   R replay",36.0,818.0,0.55,0.64,0.68,1.0,0.59);
}
