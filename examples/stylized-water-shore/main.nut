// Natural shoreline: authored bathymetry, coherent small waves and local foam.
dofile("scene.nut");
dofile("water_shader.nut");
persist shoreCamera=null
persist shoreWater=null
persist shoreTime=0.0
persist shorePaused=false
persist shoreView=1
persist shoreLayer=0
persist shoreTelemetry={time=0.0,paused=false,view=1,layer=0}
persist shoreFloaters=[]
persist shoreRockRecords=[]
persist shoreContacts=[]
persist shoreContactImageApi=null
persist shoreContactImage=null
persist shoreContactGradient=null

// One registry drives wood placement and the immutable contact atlas. World X/Z
// use two bytes each; dimensions/angle occupy a second row (16 contact capacity).
function shoreContactAtlas() {
    if(shoreContacts.len()>16) throw "shore contact atlas capacity exceeded";
    shoreContactImageApi=eve.Image();
    shoreContactImage=shoreContactImageApi.newEmptyImageData(16,16,"RGBA8");
    for(local y=0;y<16;++y) for(local x=0;x<16;++x) shoreContactImage.setPixel(x,y,0.0,0.0,0.0,0.0);
    foreach(i,c in shoreContacts) {
        local px=((c[0]+8.0)/16.0*65535.0+0.5).tointeger();
        local pz=((c[1]+8.0)/16.0*65535.0+0.5).tointeger();
        shoreContactImage.setPixel(i,0,(px/256).tofloat()/255.0,(px%256).tofloat()/255.0,
            (pz/256).tofloat()/255.0,(pz%256).tofloat()/255.0);
        shoreContactImage.setPixel(i,1,c[2],c[3],c[4]/6.2831853,c[5]/3.0);
    }
    if(shoreRockRecords.len()>16) throw "shore rock atlas capacity exceeded";
    foreach(i,r in shoreRockRecords) {
        local px=((r[0]+16.0)/32.0*65535.0+0.5).tointeger();
        local pz=((r[1]+16.0)/32.0*65535.0+0.5).tointeger();
        shoreContactImage.setPixel(i,2,(px/256).tofloat()/255.0,(px%256).tofloat()/255.0,
            (pz/256).tofloat()/255.0,(pz%256).tofloat()/255.0);
        shoreContactImage.setPixel(i,3,r[2]/2.0,r[4]/2.0,0.0,1.0);
    }
    shoreContactGradient=eve.WaterDepthGradient();
    local result=shoreContactGradient.addColorStop(0.0,0.0,0.0,0.0);
    if(!result.ok) throw result.status.summary;
    result=shoreContactGradient.addAlphaStop(0.0,1.0);
    if(!result.ok) throw result.status.summary;
    result=shoreWater.setDepthGradient(shoreContactGradient,16);
    if(!result.ok) throw result.status.summary;
    gfx.updateTextureFromImageData(shoreWater.getDepthGradientTexture(),shoreContactImage);
}
function shoreSmooth(a,b,x) {local v=(x-a)/(b-a);v=v<0.0?0.0:(v>1.0?1.0:v);return v*v*(3.0-2.0*v);}
// Same wave train as wave.glsl, sampled at each end for float heave and pitch.
function shoreFloatHeight(x,z) {
    local d=shoreWetDistance(x,z),t=shoreWater.getTime()*shoreWater.getWaveSpeed();
    t-=floor(t/628.3185307)*628.3185307;
    local phase=x*2.0+t*1.8+sin(z*0.65)*0.48+sin(z*1.7)*0.09+0.25*exp(-(d>0.0?d:0.0)*2.0);
    return (1.0-0.45*shoreSmooth(3.0,14.0,d))*shoreWater.getWaveAmplitude()*
        ((sin(phase)+0.27*sin(phase*2.0-0.65))*0.85+sin(x*6.3-z*2.1+t*2.3)*0.12)*shoreSmooth(-0.16,0.65,d);
}

function shoreBoundary(z) { return -3.8+0.7*sin(z*0.33)+0.22*sin(z*0.91); }
function shoreCoastDistance(x,z) { return (x-shoreBoundary(z))*0.96; }
function shoreBed(x,z) {
    local d=shoreCoastDistance(x,z);
    return d<0.0? -d*0.19+0.018*sceneNoise(x,z):-0.04-d*0.115+0.016*sceneNoise(x,z);
}
function shoreWetDistance(x,z) {
    local distance=shoreCoastDistance(x,z);
    foreach(r in shoreRockRecords) {
        local dx=(x-r[0])/r[2],dz=(z-r[1])/r[4];
        local rd=(sqrt(dx*dx+dz*dz)-1.0)*(r[2]<r[4]?r[2]:r[4]);
        if(rd<distance) distance=rd;
    }
    return distance;
}
function shoreSetView() {
    local eyes=[[8.0,5.1,10.5],[-5.1,3.5,6.0],[1.0,13.0,7.0],[-2.5,2.5,1.8]];
    local targets=[[-2.7,0.0,-2.2],[-1.0,0.0,-2.5],[-2.0,0.0,-1.0],[-0.8,0.0,-3.0]];
    local e=eyes[shoreView],t=targets[shoreView];
    shoreCamera.setEye(e[0],e[1],e[2]);shoreCamera.setTarget(t[0],t[1],t[2]);
}
local SHORE_STYLES=[
@"{""schema"": ""eve.graphics.stylized-water"", ""schemaVersion"": 1, ""unknownFields"": ""reject"", ""colors"": {""deep"": [0.012, 0.065, 0.10], ""shallow"": [0.10, 0.25, 0.21], ""foam"": [0.91, 0.95, 0.91], ""reflectionTint"": [0.93, 0.97, 1.0]}, ""waves"": {""speed"": 0.85, ""amplitude"": 0.10, ""scale"": 1.35, ""sharpness"": 1.65, ""rippleAmplitude"": 0.0, ""rippleCount"": 0, ""rippleInterval"": 1.4}, ""depth"": {""distance"": 1.8, ""opacity"": 0.52}, ""foam"": {""width"": 0.26, ""softness"": 0.09, ""strength"": 0.95}, ""lighting"": {""fresnelPower"": 5.0, ""reflectionIntensity"": 1.0, ""sunIntensity"": 1.15, ""screenSpaceReflection"": false, ""screenSpaceReflectionStrength"": 0.85}, ""refraction"": {""strength"": 0.015}, ""caustics"": {""strength"": 0.4, ""scale"": 3.0}}",
@"{""schema"": ""eve.graphics.stylized-water"", ""schemaVersion"": 1, ""unknownFields"": ""reject"", ""colors"": {""deep"": [0.012, 0.065, 0.10], ""shallow"": [0.10, 0.25, 0.21], ""foam"": [0.91, 0.95, 0.91], ""reflectionTint"": [0.93, 0.97, 1.0]}, ""waves"": {""speed"": 0.85, ""amplitude"": 0.10, ""scale"": 1.35, ""sharpness"": 1.65, ""rippleAmplitude"": 0.0, ""rippleCount"": 0, ""rippleInterval"": 1.4}, ""depth"": {""distance"": 1.8, ""opacity"": 0.52}, ""foam"": {""width"": 0.26, ""softness"": 0.09, ""strength"": 0.18}, ""lighting"": {""fresnelPower"": 5.0, ""reflectionIntensity"": 0.0, ""sunIntensity"": 0.12, ""screenSpaceReflection"": false, ""screenSpaceReflectionStrength"": 0.85}, ""refraction"": {""strength"": 0.015}, ""caustics"": {""strength"": 0.4, ""scale"": 3.0}}",
@"{""schema"": ""eve.graphics.stylized-water"", ""schemaVersion"": 1, ""unknownFields"": ""reject"", ""colors"": {""deep"": [0.012, 0.065, 0.10], ""shallow"": [0.10, 0.25, 0.21], ""foam"": [0.91, 0.95, 0.91], ""reflectionTint"": [0.93, 0.97, 1.0]}, ""waves"": {""speed"": 0.85, ""amplitude"": 0.10, ""scale"": 1.35, ""sharpness"": 1.65, ""rippleAmplitude"": 0.0, ""rippleCount"": 0, ""rippleInterval"": 1.4}, ""depth"": {""distance"": 1.8, ""opacity"": 0.52}, ""foam"": {""width"": 0.26, ""softness"": 0.09, ""strength"": 0.95}, ""lighting"": {""fresnelPower"": 5.0, ""reflectionIntensity"": 1.35, ""sunIntensity"": 1.15, ""screenSpaceReflection"": false, ""screenSpaceReflectionStrength"": 0.85}, ""refraction"": {""strength"": 0.005}, ""caustics"": {""strength"": 0.4, ""scale"": 3.0}}",
@"{""schema"": ""eve.graphics.stylized-water"", ""schemaVersion"": 1, ""unknownFields"": ""reject"", ""colors"": {""deep"": [0.012, 0.065, 0.10], ""shallow"": [0.10, 0.25, 0.21], ""foam"": [0.91, 0.95, 0.91], ""reflectionTint"": [0.93, 0.97, 1.0]}, ""waves"": {""speed"": 0.85, ""amplitude"": 0.10, ""scale"": 1.35, ""sharpness"": 1.65, ""rippleAmplitude"": 0.0, ""rippleCount"": 0, ""rippleInterval"": 1.4}, ""depth"": {""distance"": 1.8, ""opacity"": 0.52}, ""foam"": {""width"": 0.26, ""softness"": 0.09, ""strength"": 0.95}, ""lighting"": {""fresnelPower"": 5.0, ""reflectionIntensity"": 1.0, ""sunIntensity"": 1.15, ""screenSpaceReflection"": true, ""screenSpaceReflectionStrength"": 0.7}, ""refraction"": {""strength"": 0.015}, ""caustics"": {""strength"": 0.4, ""scale"": 3.0}}"
];

function shoreStyle() {
    local rc=gfx.getRenderControl();
    if(shoreLayer==3) {rc.enable("gbuffer");rc.enable("ssr");}
    else {rc.disable("ssr");rc.disable("gbuffer");}
    local result=shoreWater.applyConfigJson(SHORE_STYLES[shoreLayer]);
    if(!result.ok) throw "shore style: "+result.status.summary;
}
function shoreWaterMesh() {
    local pos=[],nrm=[],uv=[],idx=[],nx=192,nz=192;
    for(local z=0;z<=nz;++z) for(local x=0;x<=nx;++x) {
        local fx=x.tofloat()/nx,fz=z.tofloat()/nz*2.0-1.0,az=fz<0.0?-fz:fz;
        local wx=fx<0.68? -7.0+fx/0.68*21.0:14.0+pow((fx-0.68)/0.32,1.6)*110.0;
        local wz=(az<0.68? az/0.68*20.0:20.0+pow((az-0.68)/0.32,1.6)*100.0)*(fz<0.0?-1.0:1.0);
        pos.extend([wx,0.0,wz]);nrm.extend([0.0,1.0,0.0]);
        uv.extend([shoreWetDistance(wx,wz),-shoreBed(wx,wz)]);
        if(x<nx && z<nz) {local i=z*(nx+1)+x;idx.extend([i,i+nx+1,i+1,i+1,i+nx+1,i+nx+2]);}
    }
    if(!gfx.updateMeshVertices(shoreWater.getMesh(),pos,nrm,uv,pos.len()/3,idx,idx.len()))
        throw "shore water mesh upload failed";
}
function shoreRock(camera,seed,z,offset,rx,ry,rz) {
    local x=shoreBoundary(z)+offset,ground=shoreBed(x,z),cy=ground+ry*0.65;
    if(offset>0.0) cy=-ry*0.12;
    local rock=sceneProp(sceneBoulder(seed),"stone",[0.87,0.91,0.88],x,cy,z,rx,ry,rz,camera);
    rock.setRotation(seed*0.13,0.08*sin(seed),0.05*cos(seed));
    if(offset>-0.25) shoreRockRecords.append([x,z,rx*0.95,ry,rz*0.95]);
}
eve_init=function() {
    gfx.setBackgroundColor(0.12,0.24,0.39,1.0);
    local rc=gfx.getRenderControl();rc.disable("gbuffer");rc.enable("shadow");
    sceneLoadMaterials();shoreCamera=eve.Camera3D();shoreCamera.setActive(true);
    shoreCamera.setFov(48.0);shoreCamera.setClipPlanes(0.1,450.0);shoreCamera.setAmbient(0.16,0.19,0.22);shoreSetView();
    gfx.setDirectionalLight(-0.60,0.90,-0.80,1.12,1.02,0.86);sceneSunLight();
    sceneTerrain(shoreCamera,-14.0,22.0,-23.0,23.0,144,shoreCoastDistance,shoreBed,0.0);
    scenePebbleBed(shoreCamera,-6.5,4.0,-17.0,17.0,shoreCoastDistance,shoreBed);
    local specs=[[-6.5,0.10,0.72,0.50,0.56],[-3.9,-0.05,0.93,0.69,0.72],[-1.5,0.12,0.51,0.39,0.48],
        [1.1,0.36,0.83,0.55,0.63],[3.4,-0.14,0.62,0.40,0.53],[5.7,0.06,0.44,0.32,0.40],[-9.0,-1.1,1.05,0.76,0.87]];
    foreach(i,r in specs) shoreRock(shoreCamera,3.0+i,r[0],r[1],r[2],r[3],r[4]);
    for(local i=0;i<70;++i) {
        local z=-12.0+i*0.36,x=shoreBoundary(z)-0.55+sin(i*2.399)*0.6;
        local scale=0.035+0.045*(0.5+0.5*sin(i*5.1));
        sceneProp(sceneRockMeshes[i%sceneRockMeshes.len()],"stone",[0.75,0.74,0.66],x,shoreBed(x,z)+scale*0.45,z,
            scale,scale*0.6,scale*0.85,shoreCamera);
    }
    for(local i=0;i<11;++i) {
        local z=-11.0+i*2.0,x=shoreBoundary(z)-0.5;
        sceneReeds(shoreCamera,x,shoreBed(x,z)-0.02,z,0.95+0.18*sin(i*1.7),i.tofloat());
    }
    // Level weathered planks, supported by vertical posts; rotations are radians.
    local cube=gfx.newMeshCube(1.0),cylinder=gfx.newMeshCylinder(12,1,true),jettyZ=-4.8;
    for(local i=0;i<13;++i) {
        local x=-5.8+i*0.38;
        local plank=sceneProp(cube,"wood",[0.85,0.84,0.76],x,0.40+0.008*sin(i*3.0),jettyZ,0.35,0.085,1.35,shoreCamera);
        plank.setRotation(0.01*sin(i),0.0,0.012*cos(i));
    }
    for(local i=0;i<4;++i) for(local side=-1;side<=1;side+=2) {
        local x=-5.45+i*1.35,z=jettyZ+side*0.66;
        local post=sceneProp(cylinder,"wood",[0.62,0.64,0.59],x,0.10,z,0.10,0.575,0.10,shoreCamera);
        shoreContacts.append([x,z,0.10,0.0,0.0,1.0]);
        post.setRotation(0.0,side*0.025,0.02*sin(i));
    }
    local log=sceneProp(cylinder,"wood",[0.75,0.73,0.66],0.6,0.015,-2.3,0.11,0.525,0.11,shoreCamera);
    log.setRotation(0.4,0.0,1.5708);shoreFloaters.append([log,0.6,0.015,-2.3,0.25,1.5708]);
    local logFloat=shoreFloaters.top();
    shoreContacts.append([logFloat[1],logFloat[3],0.11,0.45,logFloat[4],2.0]);
    local plank=sceneProp(cube,"wood",[0.83,0.82,0.75],1.6,0.02,0.4,0.7,0.045,0.22,shoreCamera);
    plank.setRotation(0.2,0.0,0.01);shoreFloaters.append([plank,1.6,0.02,0.4,0.25,0.01]);
    local plankFloat=shoreFloaters.top();
    shoreContacts.append([plankFloat[1],plankFloat[3],0.11,0.35,plankFloat[4],3.0]);
    sceneTrees(shoreCamera,[[-9.0,1.1,-13.0],[-10.0,1.3,-8.0],[-9.2,1.1,-3.0],[-10.0,1.3,2.0],[-9.0,1.2,7.0],[-8.0,1.0,12.0]]);
    sceneDistantRidge(shoreCamera);sceneSky(shoreCamera);
    sceneEnvironment(shoreCamera,-1.0,0.6,0.0);
    shoreWater=gfx.newWater();shoreWater.createPlane(26.0,38.0,160,144);
    installWaterSurface(shoreWater.getShader());shoreWaterMesh();shoreStyle();shoreContactAtlas();
    print("stylized-water-shore ready: V views; Space pause; 1 full, 2 transmission, 3 reflection, 4 SSR\n");
};
eve_update=function(dt) {
    if(key_just_pressed("space")) shorePaused=!shorePaused;
    if(key_just_pressed("v")) {shoreView=(shoreView+1)%4;shoreSetView();}
    for(local i=0;i<4;++i) if(key_just_pressed((i+1).tostring())) {shoreLayer=i;shoreStyle();}
    if(!shorePaused) {
        shoreTime+=dt;shoreWater.update(dt);
        // Full animation period; retain elapsed telemetry separately from GPU phase time.
        local phaseSeconds=shoreWater.getTime(),period=739.1982714;
        if(phaseSeconds>=period) shoreWater.setTime(phaseSeconds-floor(phaseSeconds/period)*period);
    }
    shoreTelemetry.time=shoreTime;shoreTelemetry.paused=shorePaused;shoreTelemetry.view=shoreView;shoreTelemetry.layer=shoreLayer;
    foreach(f in shoreFloaters) {
        local h=shoreFloatHeight(f[1],f[3]);
        local slope=(shoreFloatHeight(f[1]+0.35,f[3])-shoreFloatHeight(f[1]-0.35,f[3]))/0.70;
        f[0].setPosition(f[1],f[2]+h,f[3]);
        f[0].setRotation(f[4],0.0,f[5]+atan(slope)*0.65);
    }
};
eve_render=function() {
    gfx.clear();gfx.render3D();shoreWater.setViewport(gfx.getWidth().tofloat(),gfx.getHeight().tofloat());shoreWater.draw();
};
