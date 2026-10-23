// A shared fixed clock drives the spectral sea, local wake and real World3D boat.
// Opaque native handles remain in this live VM; they are not snapshot values.
if (!("voyage" in getroottable())) voyage <- null;
persist voyageSteering = 0.0
persist voyageTelemetry = {}
if ("dev" in eve) {
    eve.dev.markStateRoot("voyageSteering");
    eve.dev.markStateRoot("voyageTelemetry");
}

function oceanRequire(r) {
    if (!r.ok) throw "Ocean Voyage: " + r.status.summary;
    return r.hasValue ? r.value : null;
}
function oceanClamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
dofile("type055.nut");
function oceanPose() {
    local s=voyage, body=s.body;
    local x=body.getRotX(), y=body.getRotY(), z=body.getRotZ(), w=body.getRotW();
    local m=[1.0-2.0*(y*y+z*z),2.0*(x*y-z*w),2.0*(x*z+y*w),
             2.0*(x*y+z*w),1.0-2.0*(x*x+z*z),2.0*(y*z-x*w),
             2.0*(x*z-y*w),2.0*(y*z+x*w),1.0-2.0*(x*x+y*y)];
    local yaw=atan2(m[2],m[8]), pitch=asin(oceanClamp(-m[5],-1.0,1.0)), roll=atan2(m[3],m[4]);
    // Compose body rotation with model-local -90 degrees about Y (+X bow -> +Z).
    // Adding a yaw offset alone would swap pitch/roll axes when the ship tilts.
    local modelYaw=atan2(-m[0],-m[6]), modelPitch=asin(oceanClamp(m[3],-1.0,1.0));
    local modelRoll=atan2(m[5],m[4]);
    foreach(part in s.parts) {
        part.setPosition(body.getX(),body.getY(),body.getZ());
        part.setRotation(modelYaw,modelPitch,modelRoll);
    }
    // Bow-quarter chase view includes the bow wave and the trail behind the stern.
    // Smooth on the same fixed clock as the boat; pause also freezes the camera.
    local desiredEye=[body.getX()+sin(yaw)*180.0+cos(yaw)*150.0,body.getY()+75.0,
                      body.getZ()+cos(yaw)*180.0-sin(yaw)*150.0];
    local desiredTarget=[body.getX()-sin(yaw)*40.0,body.getY()+1.0,body.getZ()-cos(yaw)*40.0];
    if(s.viewMode=="top") {
        desiredEye=[body.getX(),body.getY()+480.0,body.getZ()-50.0];
        desiredTarget=[body.getX(),body.getY(),body.getZ()-49.0];
    }
    local blend=1.0-exp(-(s.tick-s.cameraTick)/60.0/0.8);
    if(s.cameraTick<0) blend=1.0;
    for(local i=0;i<3;++i) {
        s.eye[i]+=(desiredEye[i]-s.eye[i])*blend;
        s.target[i]+=(desiredTarget[i]-s.target[i])*blend;
    }
    s.cameraTick=s.tick;
    s.camera.setEye(s.eye[0],s.eye[1],s.eye[2]);
    s.camera.setTarget(s.target[0],s.target[1],s.target[2]);
    local vx=body.getLinearVelocityX(), vy=body.getLinearVelocityY(), vz=body.getLinearVelocityZ();
    local wakeHeight=0.0, wakeFoam=0.0;
    foreach(probe in [[0.0,-115.0],[-14.0,70.0],[14.0,70.0]]) {
        local sample=oceanRequire(s.wake.sample(body.getX()+m[0]*probe[0]+m[2]*probe[1],
                                               body.getZ()+m[6]*probe[0]+m[8]*probe[1]));
        local height=fabs(sample.height);
        if(height>wakeHeight) wakeHeight=height;
        if(sample.foam>wakeFoam) wakeFoam=sample.foam;
    }
    // Read-only projection of native authority for Play Host observations.
    voyageTelemetry={sprayVisible=s.sprayVisible,spray=oceanRequire(s.spray.summary()),tick=s.tick,paused=s.paused,cruise=s.cruise,
                     boat={x=body.getX(),y=body.getY(),z=body.getZ(),speed=sqrt(vx*vx+vy*vy+vz*vz),
                           forwardSpeed=vx*m[2]+vy*m[5]+vz*m[8],yaw=yaw,pitch=pitch,roll=roll},
                     camera={x=s.eye[0],y=s.eye[1],z=s.eye[2]},wake={height=wakeHeight,foam=wakeFoam}};
}

eve_init = function() {
    if (voyage != null) return;
    local sb=eve.OceanSpectrumBuilder(), spectrumSettings=oceanRequire(sb.settings());
    spectrumSettings.seed="3182026";
    spectrumSettings.significantWaveHeight=0.65;
    spectrumSettings.peakPeriod=9.0;
    spectrumSettings.windDirection=[0.8,0.6];
    spectrumSettings.choppiness=0.45;
    oceanRequire(sb.configure(spectrumSettings));
    local sea=oceanRequire(sb.build());
    local gpu=oceanRequire(eve.OceanGpuSpectrumBuilder().build(sea,gfx,gpgpu));
    local rb=eve.OceanRendererBuilder(), rs=oceanRequire(rb.settings());
    rs.segments=256; rs.rings=192;
    oceanRequire(rb.configure(rs));
    local renderer=oceanRequire(rb.build(gfx));
    local wb=eve.OceanWakeBuilder(), ws=oceanRequire(wb.settings());
    ws.resolution=64; ws.length=384.0; ws.absorptionWidth=48.0;
    ws.damping=0.08; ws.foamDecay=0.22;
    oceanRequire(wb.configure(ws));
    local wake=oceanRequire(wb.build());
    local world=physics.newWorld3D(0.0,-9.81,0.0,false);
    local body=world.newBody("dynamic",0.0,0.0,0.0);
    // Gameplay mass/inertia proxy (~8,208 tonnes), not a naval engineering model.
    if (body.newBoxShape(20.0,6.0,180.0,380.0,0.3,0.0)==null) throw "Boat shape creation failed";
    // Start already underway; subsequent movement is integrated by World3D only.
    body.setLinearVelocity(0.0,0.0,12.0);
    local bb=eve.OceanBoatBuilder(), hull=oceanRequire(bb.settings());
    hull.cells=[];
    foreach(station in [[-70.0,6.0],[-50.0,7.0],[-30.0,7.0],[-10.0,7.0],
                        [10.0,6.5],[30.0,6.0],[50.0,4.0],[70.0,2.5]])
        foreach(side in [-1.0,1.0])
            hull.cells.append({center=[side*station[1]*0.65,0.0,station[0]],radius=3.0,volume=1000.0});
    hull.propeller=[0.0,-2.0,-78.0]; hull.thrust=6000000.0; hull.rudderCoefficient=100000.0;
    oceanRequire(bb.configure(hull));
    local boat=oceanRequire(bb.build(body));
    local sprayBuilder=eve.OceanSprayBuilder(), spraySettings=oceanRequire(sprayBuilder.settings());
    spraySettings.seed="3182026"; spraySettings.capacity=1024;
    // Native emission bounds each contact's volume contribution to one cubic metre.
    spraySettings.radius=0.15; spraySettings.emissionPerVolumeSpeed=15.0;
    oceanRequire(sprayBuilder.configure(spraySettings));
    local spray=oceanRequire(sprayBuilder.build());
    local sprayRenderer=oceanRequire(eve.OceanSprayRendererBuilder().build(gfx));
    local ship=oceanLoadType055();
    local camera=eve.Camera3D(); camera.setFov(48.0); camera.setClipPlanes(0.5,30000.0);
    camera.setAmbient(0.24,0.29,0.36);
    gfx.setBackgroundColor(0.35,0.55,0.72,1.0);
    daynight.init(gfx); daynight.setTimeOfDay(15.0); daynight.setPaused(true);
    daynight.setSkyboxEnabled(true); daynight.update(0.0,gfx);
    voyage={hullMasks=oceanType055Masks(),modelData=ship.data,sprayVisible=true,spray=spray,sprayRenderer=sprayRenderer,sea=sea,gpu=gpu,renderer=renderer,wake=wake,world=world,body=body,boat=boat,
            parts=ship.parts,camera=camera,viewMode="chase",eye=[0.0,0.0,0.0],target=[0.0,0.0,0.0],cameraTick=-1,
            tick=0,accumulator=0.0,paused=false,cruise=true,
            frame=oceanRequire(oceanRequire(sea.frameBuilder(0,60)).build()),published=-1};
    oceanPose();
    print("Ocean Voyage: W/S throttle, A/D rudder; C cruise; P pause; V spray; T overhead/chase view.\n");
};

eve_update = function(dt) {
    local s=voyage;
    if(key_just_pressed("t")) { s.viewMode=s.viewMode=="top" ? "chase" : "top"; s.cameraTick=-1; }
    if (key_just_pressed("v")) s.sprayVisible=!s.sprayVisible;
    if (key_just_pressed("p")) s.paused=!s.paused;
    if (key_just_pressed("c")) s.cruise=!s.cruise;
    if (!s.paused) s.accumulator+=dt;
    local throttle=s.cruise ? 0.7 : 0.0, rudder=voyageSteering;
    if (keyboard.isDown("w")) throttle=1.0;
    if (keyboard.isDown("s")) throttle=-0.6;
    if (keyboard.isDown("a")) rudder=-0.7;
    if (keyboard.isDown("d")) rudder=0.7;
    local steps=0;
    while (!s.paused && s.accumulator>=1.0/60.0 && steps<4) {
        // This bounded wake is a visual response, not a self-consistent pressure solver.
        // Feeding the ship's own impulses back into its buoyancy made the large hull
        // lift itself and build a stationary foam mound. The spectral sea drives forces.
        local receipt=oceanRequire(s.boat.apply(s.frame,null,60,throttle,rudder));
        oceanRequire(s.wake.injectReceipt(receipt,60));
        local sprayStep=oceanRequire(s.spray.stepBuilder(s.frame,s.wake,60));
        oceanRequire(sprayStep.addReceipt(1,receipt));
        local nextSpray=oceanRequire(sprayStep.build());
        local pending=oceanRequire(s.wake.prepareStep(s.frame,s.tick+1,60));
        s.world.update(1.0/60.0);
        ++s.tick; ++steps; s.accumulator-=1.0/60.0;
        oceanRequire(s.wake.commitStep(pending));
        s.spray=nextSpray;
        oceanRequire(s.wake.recenter(s.body.getX(),s.body.getZ()));
        s.frame=oceanRequire(oceanRequire(s.sea.frameBuilder(s.tick,60)).build());
    }
    oceanPose();
};

eve_render = function() {
    local s=voyage;
    if (s.published!=s.tick) {
        local token=oceanRequire(oceanRequire(s.gpu.frameBuilder(s.tick,60)).submit());
        local placement={center=[s.body.getX(),s.body.getZ()],origin=[0.0,0.0,0.0]};
        oceanRequire(oceanRequire(s.renderer.gpuFrameBuilder(s.gpu,token,s.wake,placement)).publish());
        local body=s.body;
        local hullMask={origin=placement.origin,volumes=[]};
        foreach(bevels in s.hullMasks) hullMask.volumes.append({
            center=[body.getX(),body.getY(),body.getZ()],
            rotation=[body.getRotX(),body.getRotY(),body.getRotZ(),body.getRotW()],
            halfExtent=[10.0,9.0,90.0],bevels=bevels});
        oceanRequire(oceanRequire(s.renderer.hullMaskBuilder(hullMask)).publish());
        local c=s.camera;
        local sprayView={eye=[c.getEyeX(),c.getEyeY(),c.getEyeZ()],
            forward=[c.getTargetX()-c.getEyeX(),c.getTargetY()-c.getEyeY(),c.getTargetZ()-c.getEyeZ()],
            up=[0.0,1.0,0.0],origin=[0.0,0.0,0.0]};
        oceanRequire(oceanRequire(s.sprayRenderer.frameBuilder(s.spray,sprayView)).publish());
        s.published=s.tick;
    }
    // DayNight owns the sky; the existing probe projection supplies its Graphics-owned texture.
    // Publish it to the camera because render3D restores camera environment state.
    if (!("skyProjection" in s)) s.skyProjection <- gfx.newReflectionProbeCapture();
    if (s.skyProjection==null) throw "Ocean sky projection allocation failed";
    daynight.applyReflectionProbeSky(s.skyProjection);
    s.camera.setEnvMap(s.skyProjection.getEnvironmentLighting());
    s.camera.setEnvIntensity(s.skyProjection.getEnvironmentLightingIntensity());
    gfx.clear(); gfx.render3D();
    oceanRequire(s.renderer.draw());
    if (s.sprayVisible) oceanRequire(s.sprayRenderer.draw());
};

// Official Play Host commands only alter driver input, never rigid-body state.
function oceanCommand(command) {
    if (command=="top-view") { voyage.viewMode="top"; voyage.cameraTick=-1; }
    else if (command=="chase-view") { voyage.viewMode="chase"; voyage.cameraTick=-1; }
    else if (command=="left") voyageSteering=-0.7;
    else if (command=="right") voyageSteering=0.7;
    else if (command=="straight") voyageSteering=0.0;
    else if (command=="forward") voyage.cruise=true;
    else if (command=="stop") { voyage.cruise=false; voyageSteering=0.0; }
    else if (command=="spray-on") voyage.sprayVisible=true;
    else if (command=="spray-off") voyage.sprayVisible=false;
    else if (command=="pause") voyage.paused=true;
    else if (command=="resume") voyage.paused=false;
    else throw "Unknown ocean driver command";
}
