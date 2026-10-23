// Script-owned visual simulation; metres, seconds, deterministic local RNG.
// All meshes/materials are pooled once. No resources are loaded in render().
persist rail = null

function rgClamp(x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }
function rgRand() {
    rail.seed = (rail.seed * 16807) % 2147483647;
    return rail.seed.tofloat() / 2147483647.0;
}
function rgObject(mesh, blend) {
    local m = gfx.newMaterial();
    m.setShadingModel("unlit"); m.setSurfaceMode("transparent");
    m.setBlendMode(blend); m.setDepthWrite(false); m.setDoubleSided(true);
    m.setCastShadow(false); m.setReceiveShadow(false);
    local e = eve.Renderable3D(); e.setMesh(mesh); e.setMaterial(m);
    e.setCastShadow(false); e.setReceiveShadow(false); e.setVisible(false);
    return {e=e, m=m};
}
function rgBar(o, a, b, radius) {
    local dx=b[0]-a[0], dy=b[1]-a[1], dz=b[2]-a[2];
    local length=sqrt(dx*dx+dy*dy+dz*dz);
    if(length<0.00001) { o.e.setVisible(false); return; }
    o.e.setPosition((a[0]+b[0])*0.5,(a[1]+b[1])*0.5,(a[2]+b[2])*0.5);
    o.e.setRotation(atan2(dx,dz),acos(dy/length),0.0);
    o.e.setScale(radius,length,radius);
}
// Coordinates in the orthonormal shot frame: longitudinal / radial U / radial V.
function rgPoint(s,u,v) {
    local p=[];
    for(local k=0;k<3;k++) p.append(rail.a[k]+rail.dir[k]*s+rail.u[k]*u+rail.v[k]*v);
    return p;
}
// One immutable mesh and draw per family; GPU evaluates the original formulas.
function rgBatchShader(name) {
    local loaded=gfx.loadMeshShaderSpv("assets/"+name+".vert.spv","assets/"+name+".frag.spv");
    if(!loaded.ok) throw "railgun batch shader: "+loaded.status.summary;
    local shader=loaded.value;
    shader.declareFloat("age");
    foreach(name in ["shotA","axis","radialU","radialV"]) shader.declareVec4(name);
    shader.sendVec4("shotA",rail.a[0],rail.a[1],rail.a[2],0.0);
    shader.sendVec4("axis",rail.dir[0],rail.dir[1],rail.dir[2],rail.length);
    shader.sendVec4("radialU",rail.u[0],rail.u[1],rail.u[2],rail.flight);
    shader.sendVec4("radialV",rail.v[0],rail.v[1],rail.v[2],rail.cooling);
    local configured=gfx.configureMeshShaderSurface(shader,name=="smoke" ? "alpha" : "additive",false,true);
    if(!configured.ok) throw "railgun batch: "+configured.status.summary;
    return shader;
}
function rgBatchCreate(spark) {
    local pos=[], seed=[], uv=[], indices=[];
    local count=0;
    if(!spark) {
        for(local s=rail.spacing;s<rail.length;s+=rail.spacing) for(local j=0;j<40;j++) {
            local phase=rgRand()*6.2831853, jitter=rgRand(), speed=0.7+rgRand()*0.6;
            foreach(corner in [[0.0,0.0],[1.0,0.0],[1.0,1.0],[0.0,1.0]]) {
                pos.extend([s,j*6.2831853/40.0,phase]); seed.extend([jitter,speed,0.0]); uv.extend(corner);
            }
            foreach(index in [0,1,2,0,2,3]) indices.append(count+index);
            count+=4;
        }
    } else {
        for(local i=0;i<320;i++) {
            local s=rgRand()*rail.length, angle=rgRand()*6.2831853, speed=0.4+rgRand()*2.5;
            local life=2.4+rgRand()*2.0, size=0.018+rgRand()*0.045;
            for(local y=0;y<=5;y++) for(local x=0;x<=8;x++) {
                local phi=y*3.14159265/5.0, theta=x*6.2831853/8.0;
                pos.extend([sin(phi)*cos(theta),cos(phi),sin(phi)*sin(theta)]);
                seed.extend([s,angle,speed]); uv.extend([life,size]);
            }
            for(local y=0;y<5;y++) for(local x=0;x<8;x++) {
                local a=count+y*9+x, b=a+9;
                if(y>0) indices.extend([a,a+1,b]);
                if(y<4) indices.extend([a+1,b+1,b]);
            }
            count+=54;
        }
    }
    local mesh=gfx.newMeshFromArrays(pos,seed,uv,count,indices,indices.len());
    local o=rgObject(mesh,spark ? "additive" : "alpha");
    o.shader <- rgBatchShader(spark ? "spark" : "smoke"); o.m.setShader(o.shader);
    if(!spark) o.m.setAlbedoTexture(rail.smokeTexture);
    o.m.setTint(1.0,1.0,1.0,1.0); o.e.setTint(1.0,1.0,1.0,1.0);
    o.e.setVisible(true); return o;
}

function rgBeamBatch() {
    local pos=[], seed=[], uv=[], indices=[];
    for(local i=0;i<48;i++) for(local j=0;j<12;j++) {
        local s=rail.length*i/48.0;
        local a=j*6.2831853/12.0, b=(j+1)*6.2831853/12.0;
        local points=[[cos(a),-1.0,sin(a)],[cos(b),-1.0,sin(b)],
            [cos(b),1.0,sin(b)],[cos(a),1.0,sin(a)],
            [0.0,-1.0,0.0],[0.0,1.0,0.0]];
        local first=pos.len()/3;
        foreach(p in points) { pos.extend(p); seed.extend([s,0.0,0.0]); uv.extend([0.0,0.0]); }
        foreach(k in [0,1,2,0,2,3,4,1,0,5,3,2]) indices.append(first+k);
    }
    local mesh=gfx.newMeshFromArrays(pos,seed,uv,pos.len()/3,indices,indices.len());
    local o=rgObject(mesh,"additive"); o.shader <- rgBatchShader("beam");
    o.m.setShader(o.shader); o.e.setVisible(true); return o;
}

function rgFire() {
    rail.age=0.0; rail.seed=7193;
    print("RAILGUN_FIRE length="+rail.length+" spacing="+rail.spacing+"\n");
}
function eve_init() {
    if(rail!=null) return;
    rail={a=[-10.0,2.2,0.0], b=[10.0,3.4,0.0], spacing=2.0,
        flight=0.085, cooling=3.6, age=0.0, seed=7193, auto=true, orbit=false,
        yaw=0.0};
    local d=[]; local l=0.0;
    for(local k=0;k<3;k++) { d.append(rail.b[k]-rail.a[k]); l+=d[k]*d[k]; }
    if(l<0.000001 || rail.spacing<=0.0 || rail.flight<=0.0 || rail.cooling<=0.0)
        throw "railgun-3d: distinct endpoints and positive spacing/timing required";
    rail.length <- sqrt(l); rail.dir <- [];
    for(local k=0;k<3;k++) rail.dir.append(d[k]/rail.length);
    // Choose a reference that cannot be parallel to the firing axis.
    local ref=fabs(rail.dir[1])<0.95 ? [0.0,1.0,0.0] : [1.0,0.0,0.0];
    local n=rail.dir;
    local u=[n[1]*ref[2]-n[2]*ref[1],n[2]*ref[0]-n[0]*ref[2],n[0]*ref[1]-n[1]*ref[0]];
    local ul=sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);
    for(local k=0;k<3;k++) u[k]/=ul;
    rail.u <- u; rail.v <- [n[1]*u[2]-n[2]*u[1],n[2]*u[0]-n[0]*u[2],n[0]*u[1]-n[1]*u[0]];
    rail.cylinder <- gfx.newMeshCylinder(12,1,true);
    rail.smokeTexture <- gfx.newTextureFromFile("assets/smoke.png");
    gfx.setTextureSampler(rail.smokeTexture,"linear","none",1.0,0.0);
    rail.beamBatch <- rgBeamBatch();
    rail.flash <- rgObject(rail.cylinder,"additive");
    rail.smokeBatch <- rgBatchCreate(false);
    rail.sparkBatch <- rgBatchCreate(true);
    rail.camera <- eve.Camera3D(); rail.camera.setEye(15.0,10.0,23.0);
    rail.camera.setTarget(0.0,2.8,0.0); rail.camera.setUp(0.0,1.0,0.0);
    rail.camera.setFov(46.0); rail.camera.setAmbient(0.3,0.35,0.45); rail.camera.setActive(true);
    rail.camera.setBloom(0.65,0.85);
    local bloom=gfx.setBloomFilter("gaussianPyramid",0.78,6,48.0);
    if(!bloom.ok) throw "railgun bloom: "+bloom.status.summary;
    local control=gfx.getRenderControl(); control.enable("bloom");
    // This unlit effect does not need opaque-scene lighting passes.
    foreach(feature in ["ao","gi","shadow","msaa","clustered","atmosphere"])
        control.disable(feature);
    control.compile();
    gfx.setBackgroundColor(0.006,0.012,0.025,1.0);
    // Dim runway gives the rings a visible spatial reference without hiding particles.
    local cube=gfx.newMeshCube(1.0);
    for(local i= -12;i<=12;i+=2) {
        local o=rgObject(cube,"alpha"); o.m.setTint(0.10,0.20,0.30,0.35);
        o.e.setPosition(i.tofloat(),0.0,0.0); o.e.setScale(0.018,0.018,9.0); o.e.setVisible(true);
    }
    rail.profiler <- eve.Profiler(); rail.profiler.setEnabled(true);
    rgFire();
}
// Local froxel medium and emissive proxies: actual depth-composited volume.
function rgVolumeUpdate(dt) {
    if(!("volume" in rail)) {
        rail.volume <- gfx.newVolumetric(); rail.volume.setMode("froxel");
        rail.volume.setQuality("low");
        rail.volume.configureFroxelGrid(40,23,24,0.1,65.0);
        rail.volumeMedia <- []; rail.volumeClock <- 1.0; rail.volumeOn <- true;
        for(local i=0;i<6;i++) {
            local fog=eve.FogVolume(); fog.setShape("sphere");
            fog.setSize(5.5,2.4,2.4); fog.setAlbedo(0.75,0.80,0.88);
            fog.setExtinction(0.055); fog.setEdgeFalloff(0.85);
            fog.setNoise(0.35,1.6,71+i); fog.setAnisotropy(0.25);
            rail.volumeMedia.append(fog);
        }
        local rc=gfx.getRenderControl(); rc.enable("gbuffer");
        rc.enable("volumetricFog"); rc.compile();
    }
    if(key_just_pressed("V")) rail.volumeOn=!rail.volumeOn;
    if(!rail.volumeOn || rail.age>2.6) return;
    rail.volumeClock+=dt;
    if(rail.volumeClock<0.12) return;
    rail.volumeClock=0.0;
    local v=rail.volume;
    v.setCamera(rail.camera.getEyeX(),rail.camera.getEyeY(),rail.camera.getEyeZ(),
        rail.camera.getTargetX(),rail.camera.getTargetY(),rail.camera.getTargetZ(),
        0.0,1.0,0.0,rail.camera.getFov(),1280.0/720.0,0.1,65.0);
    v.clearFroxelGrid();
    for(local i=0;i<rail.volumeMedia.len();i++) {
        local distance=rail.length*i/5.0;
        local t=rail.age-distance/rail.length*rail.flight;
        local fade=rgClamp(1.0-t/2.5);
        if(t<0.0 || fade<=0.0 || !rail.volumeOn) continue;
        local point=rgPoint(distance,0.0,0.0);
        local fog=rail.volumeMedia[i]; fog.setPosition(point[0],point[1],point[2]);
        fog.setExtinction(0.055*fade);
        v.injectFroxelLocalVolume(fog);
        local yellow=rgClamp(1.0-t/0.85);
        local light=v.injectEmissiveLightProxy(point[0],point[1],point[2],
            1.0,0.12+0.65*yellow,0.025+0.18*yellow,3.4,1.8*fade*fade);
        if(!light.ok) throw "railgun volume light: "+light.status.summary;
    }
    local integrated=v.integrateFroxelFromSceneLights(0.015,0.02,0.03,
        -14.0,-2.0,-6.0,14.0,9.0,6.0,6);
    if(!integrated.ok) throw "railgun volume integration: "+integrated.status.summary;
    v.uploadFroxel(gfx);
}
function eve_update(dt) {
    if(key_just_pressed("Space")) rgFire();
    if(key_just_pressed("A")) rail.auto=!rail.auto;
    if(key_just_pressed("O")) rail.orbit=!rail.orbit;
    rail.age+=dt;
    if(rail.auto && rail.age>5.2) rgFire();
    if(rail.orbit) {
        rail.yaw+=dt*0.22;
        rail.camera.setEye(26.0*sin(rail.yaw+0.58),10.0,26.0*cos(rail.yaw+0.58));
    }
    rgVolumeUpdate(dt);
    local age=rail.age;
    local front=rgClamp(age/rail.flight)*rail.length;
    rail.flash.e.setVisible(age<rail.flight+0.065);
    rgBar(rail.flash,rail.a,rgPoint(front,0.0,0.0),0.11);
    rail.flash.m.setTint(22.0,18.0,9.0,rgClamp(1.0-age/0.16));
    rail.beamBatch.shader.sendFloat("age",age);
    rail.beamBatch.e.setVisible(age<rail.cooling+rail.flight);
    rail.smokeBatch.shader.sendFloat("age",age);
    rail.sparkBatch.shader.sendFloat("age",age);
    rail.smokeBatch.e.setVisible(age<4.4);
    rail.sparkBatch.e.setVisible(age<4.5);
}
function eve_render() {
    gfx.clear(); gfx.render3D();
    if(("volume" in rail) && rail.volumeOn && rail.age<2.6) {
        local gb=gfx.getRenderControl().getGBuffer();
        if(gb!=null && gb.isValid()) {
            local depth=gb.getDepthTexture();
            if(depth!=null) rail.volume.applyFroxel(gfx,depth);
        }
    }
}
