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
function oceanPart(mesh, scale, color, offset) {
    local r = eve.Renderable3D();
    r.setMesh(mesh);
    r.setScale(scale[0],scale[1],scale[2]);
    r.setTint(color[0],color[1],color[2],1.0);
    r.setRoughness(0.45);
    return { renderable=r, offset=offset };
}
function oceanHullMesh() {
    local top=[[-1.1,0.5,-3.0],[1.1,0.5,-3.0],[1.1,0.5,1.8],[0.0,0.5,3.5],[-1.1,0.5,1.8]];
    local bottom=[[-0.65,-0.55,-2.7],[0.65,-0.55,-2.7],[0.65,-0.55,1.6],[0.0,-0.30,2.8],[-0.65,-0.55,1.6]];
    local pos=[], normals=[], uv=[], indices=[], bevels=[];
    local triangle=function(a,b,c,mask=false) {
        local u=[b[0]-a[0],b[1]-a[1],b[2]-a[2]], v=[c[0]-a[0],c[1]-a[1],c[2]-a[2]];
        local n=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
        local length=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(mask) {
            local d=(n[0]*a[0]+n[1]*a[1]+n[2]*a[2])/length;
            local sign=d<0.0 ? -1.0 : 1.0;
            bevels.append([sign*n[0]/length,sign*n[1]/length,sign*n[2]/length,sign*d-0.01]);
        }
        foreach(p in [a,b,c]) {
            indices.append(indices.len());
            foreach(x in p) pos.append(x);
            foreach(x in n) normals.append(x/length);
            uv.append(0.0); uv.append(0.0);
        }
    };
    for(local i=0;i<5;++i) {
        local j=(i+1)%5;
        triangle(top[i],bottom[i],bottom[j],true); triangle(top[i],bottom[j],top[j],true);
        triangle([0.0,0.5,0.0],top[j],top[i]);
        triangle([0.0,-0.55,0.0],bottom[i],bottom[j]);
    }
    return {mesh=gfx.newMeshFromArrays(pos,normals,uv,indices.len(),indices,indices.len()),bevels=bevels};
}
function oceanPose() {
    local s=voyage, body=s.body;
    local x=body.getRotX(), y=body.getRotY(), z=body.getRotZ(), w=body.getRotW();
    local m=[1.0-2.0*(y*y+z*z),2.0*(x*y-z*w),2.0*(x*z+y*w),
             2.0*(x*y+z*w),1.0-2.0*(x*x+z*z),2.0*(y*z-x*w),
             2.0*(x*z-y*w),2.0*(y*z+x*w),1.0-2.0*(x*x+y*y)];
    local yaw=atan2(m[2],m[8]), pitch=asin(oceanClamp(-m[5],-1.0,1.0)), roll=atan2(m[3],m[4]);
    foreach(part in s.parts) {
        local p=part.offset;
        part.renderable.setPosition(body.getX()+m[0]*p[0]+m[1]*p[1]+m[2]*p[2],
                                    body.getY()+m[3]*p[0]+m[4]*p[1]+m[5]*p[2],
                                    body.getZ()+m[6]*p[0]+m[7]*p[1]+m[8]*p[2]);
        part.renderable.setRotation(yaw,pitch,roll);
    }
    s.camera.setEye(body.getX()-m[2]*15.0+5.0,body.getY()+6.5,body.getZ()-m[8]*15.0);
    s.camera.setTarget(body.getX()+m[2]*4.0,body.getY()+0.8,body.getZ()+m[8]*4.0);
    local vx=body.getLinearVelocityX(), vy=body.getLinearVelocityY(), vz=body.getLinearVelocityZ();
    // Read-only projection of native authority for Play Host observations.
    voyageTelemetry={sprayVisible=s.sprayVisible,spray=oceanRequire(s.spray.summary()),tick=s.tick,paused=s.paused,cruise=s.cruise,
                     boat={x=body.getX(),y=body.getY(),z=body.getZ(),speed=sqrt(vx*vx+vy*vy+vz*vz),
                           yaw=yaw,pitch=pitch,roll=roll}};
}

eve_init = function() {
    if (voyage != null) return;
    local sb=eve.OceanSpectrumBuilder(), spectrumSettings=oceanRequire(sb.settings());
    spectrumSettings.seed="3182026";
    spectrumSettings.significantWaveHeight=1.5;
    spectrumSettings.peakPeriod=4.5;
    spectrumSettings.windDirection=[0.8,0.6];
    spectrumSettings.choppiness=0.45;
    oceanRequire(sb.configure(spectrumSettings));
    local sea=oceanRequire(sb.build());
    local gpu=oceanRequire(eve.OceanGpuSpectrumBuilder().build(sea,gfx,gpgpu));
    local rb=eve.OceanRendererBuilder(), rs=oceanRequire(rb.settings());
    rs.segments=128; rs.rings=96;
    oceanRequire(rb.configure(rs));
    local renderer=oceanRequire(rb.build(gfx));
    local wb=eve.OceanWakeBuilder(), ws=oceanRequire(wb.settings());
    ws.resolution=64; ws.length=128.0; ws.absorptionWidth=16.0;
    oceanRequire(wb.configure(ws));
    local wake=oceanRequire(wb.build());
    local world=physics.newWorld3D(0.0,-9.81,0.0,false);
    local body=world.newBody("dynamic",0.0,0.0,0.0);
    if (body.newBoxShape(2.2,1.0,6.0,100.0,0.3,0.0)==null) throw "Boat shape creation failed";
    local bb=eve.OceanBoatBuilder(), hull=oceanRequire(bb.settings());
    hull.cells=[];
    foreach(x in [-0.75,0.75]) foreach(z in [-2.2,-0.75,0.75,2.2])
        hull.cells.append({center=[x,0.0,z],radius=0.6,volume=0.32});
    hull.propeller=[0.0,-0.4,-2.6]; hull.thrust=4500.0; hull.rudderCoefficient=900.0;
    oceanRequire(bb.configure(hull));
    local boat=oceanRequire(bb.build(body));
    local sprayBuilder=eve.OceanSprayBuilder(), spraySettings=oceanRequire(sprayBuilder.settings());
    spraySettings.seed="3182026"; spraySettings.capacity=512;
    spraySettings.radius=0.07; spraySettings.emissionPerVolumeSpeed=60.0;
    oceanRequire(sprayBuilder.configure(spraySettings));
    local spray=oceanRequire(sprayBuilder.build());
    local sprayRenderer=oceanRequire(eve.OceanSprayRendererBuilder().build(gfx));
    local cube=gfx.newMeshCube(1.0);
    local hullGeometry=oceanHullMesh();
    local parts=[oceanPart(hullGeometry.mesh,[1.0,1.0,1.0],[0.92,0.30,0.045],[0.0,0.0,0.0]),
                 oceanPart(cube,[1.35,1.0,1.6],[0.9,0.91,0.86],[0.0,1.0,-0.5]),
                 oceanPart(cube,[1.15,0.45,1.65],[0.055,0.13,0.18],[0.0,1.2,-0.5]),
                 oceanPart(cube,[1.5,0.14,1.9],[0.95,0.92,0.78],[0.0,1.58,-0.5]),
                 oceanPart(cube,[0.10,1.0,0.10],[0.18,0.20,0.22],[0.0,2.08,-0.6])];
    local camera=eve.Camera3D(); camera.setFov(58.0); camera.setClipPlanes(0.15,30000.0);
    camera.setAmbient(0.24,0.29,0.36);
    gfx.setBackgroundColor(0.35,0.55,0.72,1.0);
    daynight.init(gfx); daynight.setTimeOfDay(15.0); daynight.setPaused(true);
    daynight.setSkyboxEnabled(true); daynight.update(0.0,gfx);
    voyage={hullBevels=hullGeometry.bevels,sprayVisible=true,spray=spray,sprayRenderer=sprayRenderer,sea=sea,gpu=gpu,renderer=renderer,wake=wake,world=world,body=body,boat=boat,
            parts=parts,camera=camera,tick=0,accumulator=0.0,paused=false,cruise=true,
            frame=oceanRequire(oceanRequire(sea.frameBuilder(0,60)).build()),published=-1};
    oceanPose();
    print("Ocean Voyage: W/S throttle, A/D rudder; C automatic cruise; P pause; V spray visibility.\n");
};

eve_update = function(dt) {
    local s=voyage;
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
        local receipt=oceanRequire(s.boat.apply(s.frame,s.wake,60,throttle,rudder));
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
        local hullMask={origin=placement.origin,volumes=[{
            center=[body.getX(),body.getY(),body.getZ()],
            rotation=[body.getRotX(),body.getRotY(),body.getRotZ(),body.getRotW()],
            halfExtent=[1.1,0.5,3.5],bevels=s.hullBevels}]};
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
    if (command=="left") voyageSteering=-0.7;
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
