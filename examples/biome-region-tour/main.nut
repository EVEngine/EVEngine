import { tourContent } from "./schemes.nut"
import { makeWorld, groundHeight } from "./world.nut"

// Root observation is a projection of the native binding / camera, for eve_play.
persist tour_state = { ready=false, region="", visited=0, paused=false, revision=0, trees=0, objects=0, weather="", cameraX=0.0 }
local content=tourContent(), regions=content.regions;
local world=null, camera=null, controller=null, fog=null;
local active=-1, lastRevision=-1, poll=0.0, elapsed=0.0, residentTime=0.0;
local captured={}, seen={}, notice="Follow the trail through four living regions";
local environment={sky=[0.46,0.65,0.79],color=[0.65,0.78,0.73],density=0.003};
local targetWeather=null, targetFog=null;

function need(result) {
    if(!result.ok) throw result.status.summary;
    return result.value;
}

function refreshBinding() {
    local r=regions[active];
    local conf=need(r.binding.configuration());
    if(conf.revision==lastRevision) return;
    lastRevision=conf.revision;
    targetWeather=content.assets[conf.schemes.weather];
    targetFog=content.assets[conf.schemes.fog];
    weather.setPreset(targetWeather.preset);
    weather.setIntensity(targetWeather.intensity);
    weather.setWindSpeed(targetWeather.wind);
    weather.setWindDirection(25.0);
    weather.setLightningEnabled(targetWeather.preset=="storm");
    tour_state.revision=conf.revision;
    tour_state.weather=targetWeather.preset;
    ui.select("tour");
    ui.setText("region",r.name);
    ui.setText("description",r.description);
    ui.setText("binding","Region -> "+conf.biome+" / rev "+conf.revision);
    ui.setText("weather","Weather: "+targetWeather.preset+" / fog "+format("%.3f",targetFog.density));
    ui.setText("population",r.trees+" trees / 5 animals / 2 guardians");
    print("BIOME_TOUR_ENTER region="+r.id+" revision="+conf.revision+" weather="+targetWeather.preset+"\n");
}

function tourJump(index) {
    if(index<0 || index>=regions.len()) throw "Region index outside map";
    controller.stopSequence();
    if(!controller.switchTo(regions[index].id+".arrival",0.0)) throw "Missing tour camera view";
    controller.snap();
    controller.update(0.001);
    if(!tour_state.paused) controller.playSequence(10.0);
    notice="Jumped to "+regions[index].id;
}

function tourPause() { tour_state.paused=!tour_state.paused; }
function tourStorm() {
    if(active<0) return;
    need(regions[active].document.setScheme("weather","project://tour/shared/storm"));
    refreshBinding(); notice="Edited biome weather; the bound region followed automatically";
}
function tourUndo() {
    if(active<0) return;
    local result=regions[active].document.undo();
    notice=result.ok?"Undo restored the region's inherited weather":result.status.summary;
    refreshBinding();
}

function buildHud() {
    ui.setTheme("dark"); ui.beginBuild();
    ui.beginWindow("FIELD NOTES / BIOME EXPEDITION","root");
    ui.text("","region"); ui.text("","description");
    ui.separator("line"); ui.text("","binding"); ui.text("","weather");
    ui.text("","population"); ui.text("","progress");
    ui.end(); ui.mountBuildAs("tour"); ui.select("tour");
    ui.setHostPos(18.0,18.0,0.0,0.0); ui.setHostSize(455.0,245.0);
    ui.setHostOverlay(true); ui.setHostMovable(false); ui.setHostResizable(false);
    ui.beginBuild(); ui.beginWindow("EXPEDITION CONTROLS","root");
    ui.text("Space pause | 1-4 jump | R restart tour | V storm | Z undo","keys");
    ui.text(notice,"notice"); ui.end(); ui.mountBuildAs("controls"); ui.select("controls");
    ui.setHostPos(18.0,606.0,0.0,0.0); ui.setHostSize(800.0,96.0);
    ui.setHostOverlay(true); ui.setHostMovable(false); ui.setHostResizable(false);
}

eve_init=function() {
    tour_state.ready=false; tour_state.visited=0;
    foreach(i,r in regions) {
        r.minX <- -64.0+i*32.0; r.maxX <- r.minX+32.0; r.center <- r.minX+16.0;
        r.document <- need(eve.BiomeEditorModule().create("tour."+r.id));
        foreach(kind in ["monsters","animals","plants","surface","weather","fog"])
            need(r.document.setScheme(kind,"project://tour/"+r.id+"/"+kind));
        r.binding <- need(r.document.bindRegion("region."+r.id,"project://tour/regions/"+r.id,4100+i*97));
    }
    world=makeWorld(regions,content.assets);
    camera=eve.Camera3D(); camera.setEye(-48.0,8.0,18.0); camera.setTarget(-48.0,1.5,-3.0);
    camera.setFov(58.0); camera.setClipPlanes(0.1,220.0); camera.setAmbient(0.36,0.41,0.46);
    camera.setActive(true);
    controller=eve.CameraController(); controller.setCamera(camera); controller.setMode("cinematic");
    controller.setFov(58.0); controller.setSmooth(4.0);
    foreach(r in regions) {
        controller.addView(r.id+".arrival",r.center-4.0,8.0,18.0,r.center,1.8,-3.0);
        controller.addView(r.id+".close",r.center+5.0,4.5,11.5,r.center,1.0,-4.0);
    }
    // Native controller owns easing and the eight-view loop, including the return journey.
    if(!controller.switchTo("meadow.arrival",5.0)) throw "Missing initial camera view";
    controller.update(5.0); controller.playSequence(10.0);
    weather.init(gfx); weather.setEnvironmentEnabled(false);
    local rc=gfx.getRenderControl(); rc.enable("gbuffer"); rc.compile();
    fog=gfx.newVolumetric(); fog.setMode("fog"); fog.setQuality("medium");
    fog.setFogStart(7.0); fog.setFogEnd(110.0); fog.setFogHeight(1.5);
    fog.setFogHeightFalloff(0.20); fog.setFogNoise(0.23);
    buildHud();
    active=0; tour_state.region=regions[0].id; seen[regions[0].id] <- true;
    tour_state.visited=1; tour_state.objects=world.count;
    refreshBinding(); tour_state.ready=true;
    print("BIOME_TOUR_READY regions=4 nativeRules=true nativeRegionBindings=true objects="+world.count+"\n");
};

eve_update=function(dt) {
    elapsed+=dt;
    if(key_just_pressed("space")) tourPause();
    for(local i=0;i<4;++i) if(key_just_pressed((i+1).tostring())) tourJump(i);
    if(key_just_pressed("r")) { tour_state.paused=false; tourJump(0); }
    if(key_just_pressed("v")) tourStorm();
    if(key_just_pressed("z")) tourUndo();
    if(!tour_state.paused) controller.update(dt);
    local index=((camera.getEyeX()+64.0)/32.0).tointeger();
    if(index<0) index=0;
    if(index>3) index=3;
    if(index!=active) {
        active=index; lastRevision=-1; residentTime=0.0; tour_state.region=regions[index].id;
        if(!(tour_state.region in seen)) { seen[tour_state.region] <- true; ++tour_state.visited; }
        refreshBinding();
    }
    residentTime+=dt; poll+=dt;
    if(poll>=0.25) {
        poll=0.0; refreshBinding();
        tour_state.trees=regions[active].trees;
        tour_state.cameraX=camera.getEyeX();
        ui.select("tour"); ui.setText("progress",(tour_state.paused?"PAUSED":"AUTO TOUR")+" / visited "+tour_state.visited+" of 4");
        ui.select("controls"); ui.setText("notice",notice);
    }
    local blend=1.0-exp(-dt*1.4);
    for(local k=0;k<3;++k) {
        environment.sky[k]+=(targetWeather.sky[k]-environment.sky[k])*blend;
        environment.color[k]+=(targetFog.color[k]-environment.color[k])*blend;
    }
    environment.density+=(targetFog.density-environment.density)*blend;
    gfx.setBackgroundColor(environment.sky[0],environment.sky[1],environment.sky[2],1.0);
    gfx.setDirectionalLight(-0.45,0.85,0.35,1.35,1.23,1.05);
    fog.setDensity(environment.density); fog.setFogColor(environment.color[0],environment.color[1],environment.color[2]);
    fog.setCamera(camera.getEyeX(),camera.getEyeY(),camera.getEyeZ(),camera.getTargetX(),camera.getTargetY(),camera.getTargetZ(),
        0.0,1.0,0.0,58.0,1280.0/720.0,0.1,220.0);
    weather.update(dt,gfx);
    foreach(f in world.fauna) {
        local x=f.x+(f.animal?sin(elapsed*0.32+f.phase)*0.6:0.0);
        local z=f.z+(f.animal?cos(elapsed*0.32+f.phase)*0.3:0.0);
        foreach(p in f.parts) p.o.setPosition(x+p.x,groundHeight(x,z)+p.y+sin(elapsed*2.0+f.phase)*0.025,z+p.z);
    }
};

eve_render=function() {
    gfx.clear(); gfx.render3D();
    fog.applyFog(gfx,gfx.getRenderControl().getGBuffer().getDepthTexture());
    // Spatial minimap: four adjacent regions, route, boundaries and actual camera position.
    gfx.drawSolidRect(934.0,596.0,330.0,108.0,0.035,0.05,0.06,0.94);
    foreach(i,r in regions) {
        local c=r.ground;
        gfx.drawSolidRect(944.0+i*77.0,606.0,75.0,86.0,c[0],c[1],c[2],1.0);
        if(i==active) gfx.drawSolidRect(944.0+i*77.0,688.0,75.0,4.0,1.0,0.85,0.35,1.0);
    }
    gfx.drawSolidRect(944.0,665.0,306.0,2.0,0.9,0.82,0.6,1.0);
    local mx=944.0+(camera.getEyeX()+64.0)/128.0*306.0;
    local my=606.0+(camera.getEyeZ()+22.0)/44.0*86.0;
    gfx.drawSolidRect(mx-4.0,my-4.0,8.0,8.0,1.0,0.95,0.6,1.0);
    ui.beginFrameAndRender();
    local id=regions[active].id;
    if(residentTime>7.0 && !(id in captured)) {
        // First request enables GPU readback; retry on later presented frames until ready.
        if(gfx.saveFramePng("capture-"+id+".png")) {
            captured[id] <- true;
            print("BIOME_TOUR_CAPTURE region="+id+"\n");
        }
    }
};
