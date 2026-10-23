// Kinematic bone colliders transfer measured velocity into the shallow-water solver.
dofile("scene.nut");
dofile("water_shader.nut");
dofile("character.nut");
persist footfallCount=0
persist pondColliderDebug=false
persist pondColliderBoxes=[]
persist pondColliderSamples=[]
persist pondProbe=null
persist pondProbeObject=null
persist pondProbePrevious=null
persist pondPhysics={colliderCount=0,leftSpeed=0.0,rightSpeed=0.0,probeX=6.4,probeZ=6.4,flowX=0.0,flowZ=0.0,frontHeight=0.55,backHeight=0.55}
persist pondPerf={frame=0,frameMs=0.0,updateMs=0.0,renderMs=0.0,
    surfaceSyncMs=0.0,collisionMs=0.0,scanMs=0.0,characterMs=0.0,solverMs=0.0}
persist pondScopeStack=[]
function pondBegin(label) {pondScopeStack.append([label,timer.getTime()]);}
function pondEnd() {
    local item=pondScopeStack.pop();pondPerf[item[0]]+=(timer.getTime()-item[1])*1000.0;
}
persist fluids=null
persist imageApi=null
persist sim=null
persist waterFx=null
persist waterMesh=null
persist cam=null
persist fieldImage=null
persist fieldTex=null
persist pondGradient=null
persist pondBeds=[]
persist shoreTelemetry={tick=0,depth=0.0,speed=0.0,outwardFlow=0.0,surface=0.55,wetFront=0.0}
persist showFieldBoards=false
persist paused=false
persist autoWalk=true
persist pondShorePath=false
persist elapsed=0.0
persist fixedTick=0
persist accumulator=0.0
persist characterX=6.4
persist characterZ=6.4
persist characterVx=0.0
persist characterVz=0.0
persist prevMouse=false
persist pondView=1
persist pondEye=[16.0,9.8,18.0]
persist pondTarget=[6.4,0.25,6.4]
persist pondTelemetry={tick=0,time=0.0,paused=false,autowalk=true,backend="",centerHeight=0.55,sampledDeviation=0.0,character={x=6.4,z=6.4}}
const RES=128;
const CELL=0.1;
const LAKE=12.8;
const WATER_DEPTH=0.55;
const VISUAL_GAIN=1.8;
const MOVE_SPEED=0.85;
const FIXED_DT=0.016666667;
local POND_STYLE=@"{""schema"": ""eve.graphics.stylized-water"", ""schemaVersion"": 1, ""unknownFields"": ""reject"", ""colors"": {""deep"": [0.012, 0.055, 0.07], ""shallow"": [0.09, 0.24, 0.19], ""foam"": [0.91, 0.95, 0.91], ""reflectionTint"": [0.95, 0.98, 1.0]}, ""waves"": {""speed"": 0.65, ""amplitude"": 0.0, ""scale"": 1.0, ""sharpness"": 1.0, ""rippleAmplitude"": 0.0, ""rippleCount"": 0, ""rippleInterval"": 3.5}, ""depth"": {""distance"": 1.0, ""opacity"": 0.38}, ""foam"": {""width"": 0.12, ""softness"": 0.05, ""strength"": 0.95}, ""lighting"": {""fresnelPower"": 5.0, ""reflectionIntensity"": 1.6, ""sunIntensity"": 0.8, ""screenSpaceReflection"": false, ""screenSpaceReflectionStrength"": 0.85}, ""refraction"": {""strength"": 0.014}, ""caustics"": {""strength"": 0.45, ""scale"": 3.2}}";

function requireOk(result,label) {
    if(!result.ok) throw label+": "+result.status.summary;
    return result;
}
function pondDistance(x,z) {
    local dx=(x-6.4)/6.05,dz=(z-6.4)/5.55;
    return (1.0-sqrt(dx*dx+dz*dz))*5.6+0.08*sin(x*1.4+z*0.7);
}
function pondBed(x,z) {
    local distance=pondDistance(x,z);
    return WATER_DEPTH-sceneClamp(distance*0.18,-0.7,WATER_DEPTH)+0.008*sceneNoise(x,z);
}
function pondCamera() {
    if(pondView==0) {pondEye=[16.0,9.8,18.0];pondTarget=[6.4,0.25,6.4];}
    else if(pondView==1) {pondEye=[characterX+3.2,3.2,characterZ+4.4];pondTarget=[characterX,0.7,characterZ];}
    else if(pondView==2) {pondEye=[6.4,17.0,9.0];pondTarget=[6.4,0.0,6.4];}
    else {pondEye=[15.3,3.2,10.6];pondTarget=[10.9,0.65,6.8];}
    cam.setEye(pondEye[0],pondEye[1],pondEye[2]);cam.setTarget(pondTarget[0],pondTarget[1],pondTarget[2]);
}
function pondReset() {
    requireOk(sim.fillSurface(WATER_DEPTH),"level lake over shore bed");elapsed=0.0;fixedTick=0;accumulator=0.0;
    characterX=6.4;characterZ=6.4;pondShorePath=false;footfallCount=0;wader.previous.clear();pondProbe=null;pondProbePrevious=null;
    requireOk(sim.clearColliders(),"reset colliders");
}
function pondShoreWalk() {
    pondReset();characterX=10.6;characterZ=6.4;pondShorePath=true;autoWalk=true;paused=false;
    wader.previous.clear();pondView=3;pondCamera();
}
function pondSplash(x,z) {
    if(pondDistance(x,z)<0.2) return;
    pondProbe={x=x,z=z,age=0.0,vx=0.0,vz=0.0,plunge=true};pondProbePrevious=null;
}
function pondStartProbe(vx,vz) {
    pondReset();autoWalk=false;paused=true;wader.collisions=false;
    pondProbe={x=6.4,z=6.4,age=0.0,vx=vx,vz=vz,plunge=false};pondProbePrevious=null;
}
function pondPick(mx,my) {
    local fx=pondTarget[0]-pondEye[0],fy=pondTarget[1]-pondEye[1],fz=pondTarget[2]-pondEye[2];
    local len=sqrt(fx*fx+fy*fy+fz*fz);fx/=len;fy/=len;fz/=len;
    local rx=-fz,rz=fx;local rlen=sqrt(rx*rx+rz*rz);rx/=rlen;rz/=rlen;
    local ux=-rz*fy,uy=rz*fx-rx*fz,uz=rx*fy;
    local sx=(mx/gfx.getWidth().tofloat()*2.0-1.0)*gfx.getWidth().tofloat()/gfx.getHeight()*0.41421356;
    local sy=(1.0-my/gfx.getHeight().tofloat()*2.0)*0.41421356;
    local dx=fx+rx*sx+ux*sy,dy=fy+uy*sy,dz=fz+rz*sx+uz*sy;
    if(dy>=-0.001) return null;
    local travel=(WATER_DEPTH-pondEye[1])/dy;
    return [pondEye[0]+dx*travel,pondEye[2]+dz*travel];
}
function pondSyncMesh() {
    pondBegin("surfaceSyncMs");
    requireOk(sim.syncSurfaceMesh(gfx,waterMesh,VISUAL_GAIN),"native surface presentation");
    pondEnd();
    requireOk(sim.copyFieldToImage(fieldImage,1.5,0.0),"physical depth and flow");
    gfx.updateTextureFromImageData(fieldTex,fieldImage);
}
// Colliders are rebuilt as owned value samples each fixed tick before sim.step.
// Static immersion does not emit a source every tick; motion supplies momentum.
function pondCollider(label,shape,x,y,z,rx,ry,rz,vx,vy,vz) {
    local length=sqrt(vx*vx+vy*vy+vz*vz);
    if(length>4.0) {local scale=4.0/length;vx*=scale;vy*=scale;vz*=scale;}
    local surface=sim.surfaceHeightAt(x,z),fraction=sceneClamp((surface-(y-ry))/(ry*2.0),0.0,1.0);
    local entering=vy<0.0?-vy:0.0;
    local horizontal=sqrt(vx*vx+vz*vz);
    // Approximate the volume swept by the moving limb as well as downward entry.
    local displacement=sceneClamp(entering*0.45+horizontal*0.10,0.0,2.0);
    local drag=4.0*fraction;
    if(shape=="capsule") requireOk(sim.setColliderCapsule(label,x,y,z,ry-rx,rx,vx,vy,vz,displacement,drag),"bone capsule");
    else requireOk(sim.setColliderSphere(label,x,y,z,rx,vx,vy,vz,displacement,drag),"bone sphere");
    pondColliderSamples.append([x,y,z,rx,ry,rz,vx,vy,vz]);
}
function pondUpdateProbe(dt) {
    if(pondProbe==null) {if(pondProbeObject!=null) pondProbeObject.setVisible(false);return;}
    local p=pondProbe;p.age+=dt;
    if(p.plunge && p.age>0.65) {pondProbe=null;pondProbeObject.setVisible(false);return;}
    p.x+=p.vx*dt;p.z+=p.vz*dt;
    local y=WATER_DEPTH-0.10;
    if(p.plunge) y=WATER_DEPTH+0.30-(p.age<0.32?p.age:0.64-p.age)*2.0;
    local vy=0.0;if(pondProbePrevious!=null && dt>0.0) vy=(y-pondProbePrevious)/dt;
    pondProbePrevious=y;
    pondCollider(101,"sphere",p.x,y,p.z,0.19,0.19,0.19,p.vx,vy,p.vz);
    pondProbeObject.setVisible(true);pondProbeObject.setPosition(p.x,y,p.z);
    pondPhysics.probeX=p.x;pondPhysics.probeZ=p.z;
}
function pondDebugBoxes() {
    if(!pondColliderDebug) {foreach(box in pondColliderBoxes) box.setVisible(false);return;}
    local cube=null;if(pondColliderBoxes.len()<pondColliderSamples.len()*12) cube=gfx.newMeshCube(1.0);
    while(pondColliderBoxes.len()<pondColliderSamples.len()*12) {
        local box=sceneProp(cube,"",[1.0,0.3,0.04],0.0,0.0,0.0,0.01,0.01,0.01,cam);
        box.setCastShadow(false);box.setReceiveLight(false);box.setVisible(false);pondColliderBoxes.append(box);
    }
    foreach(i,box in pondColliderBoxes) box.setVisible(false);
    if(!pondColliderDebug) return;
    foreach(i,c in pondColliderSamples) {
        local edge=0;
        for(local axis=0;axis<3;++axis) for(local a=-1;a<=1;a+=2) for(local b=-1;b<=1;b+=2) {
            local u=(axis+1)%3,v=(axis+2)%3,pos=[c[0],c[1],c[2]],scale=[0.008,0.008,0.008];
            pos[u]+=a*c[u+3];pos[v]+=b*c[v+3];scale[axis]=c[axis+3]*2.0;
            local box=pondColliderBoxes[i*12+edge++];box.setPosition(pos[0],pos[1],pos[2]);box.setScale(scale[0],scale[1],scale[2]);box.setVisible(true);
        }
    }
}
function pondSamplePhysics() {
    pondPhysics.colliderCount=pondColliderSamples.len();
    shoreTelemetry.tick=fixedTick;shoreTelemetry.depth=sim.depthAt(12.05,6.4);shoreTelemetry.surface=sim.surfaceHeightAt(12.05,6.4);
    local ux=sim.velocityXAt(12.05,6.4),uz=sim.velocityZAt(12.05,6.4);shoreTelemetry.speed=sqrt(ux*ux+uz*uz);shoreTelemetry.outwardFlow=ux;
    shoreTelemetry.wetFront=0.0;
    for(local x=11.7;x<12.6;x+=0.05) if(sim.depthAt(x,6.4)>0.008) shoreTelemetry.wetFront=x;
    if(pondProbe==null) {pondPhysics.flowX=0.0;pondPhysics.flowZ=0.0;pondPhysics.frontHeight=WATER_DEPTH;pondPhysics.backHeight=WATER_DEPTH;return;}
    local p=pondProbe,speed=sqrt(p.vx*p.vx+p.vz*p.vz),dx=speed>0.001?p.vx/speed:1.0,dz=speed>0.001?p.vz/speed:0.0;
    pondPhysics.frontHeight=sim.surfaceHeightAt(p.x+dx*0.35,p.z+dz*0.35);
    pondPhysics.backHeight=sim.surfaceHeightAt(p.x-dx*0.35,p.z-dz*0.35);
    pondPhysics.flowX=sim.velocityXAt(p.x,p.z);pondPhysics.flowZ=sim.velocityZAt(p.x,p.z);
}
function pondAdvance(count) {
    for(local i=0;i<count;++i) pondStep();pondSyncMesh();pondSamplePhysics();pondDebugBoxes();
}
eve_init=function() {
    gfx.setBackgroundColor(0.12,0.24,0.39,1.0);local rc=gfx.getRenderControl();rc.disable("gbuffer");rc.enable("shadow");
    profiler.setEnabled(true);
    sceneLoadMaterials();cam=eve.Camera3D();cam.setFov(45.0);cam.setClipPlanes(0.1,450.0);
    cam.setAmbient(0.16,0.19,0.22);cam.setActive(true);pondCamera();
    gfx.setDirectionalLight(-0.60,0.90,-0.80,1.12,1.02,0.86);sceneSunLight();
    sceneTerrain(cam,-5.0,19.0,-5.0,19.0,120,pondDistance,pondBed,WATER_DEPTH);
    scenePebbleBed(cam,0.0,12.8,0.0,12.8,pondDistance,pondBed);
    local rocks=[[1.0,4.4,0.7],[2.3,1.8,0.8],[6.2,0.65,0.7],[10.2,1.8,0.55],[12.0,5.3,0.8],[10.4,10.8,0.75],[3.0,11.1,0.65]];
    foreach(i,r in rocks) {
        local scale=r[2];local rock=sceneProp(sceneBoulder(i+2.0),"stone",[0.86,0.89,0.85],r[0],pondBed(r[0],r[1])+scale*0.4,r[1],scale,scale*0.67,scale*0.8,cam);
        rock.setYaw(i*1.7);
    }
    for(local i=0;i<10;++i) {
        local a=i*0.6283185,x=6.4+6.4*cos(a),z=6.4+5.8*sin(a);
        sceneReeds(cam,x,pondBed(x,z),z,0.85,i.tofloat());
    }
    sceneTrees(cam,[[-0.5,1.0,-0.8],[4.2,1.0,-1.9],[10.0,1.0,-0.7],[-1.8,1.1,5.5]]);
    sceneSky(cam);
    sceneEnvironment(cam,6.4,1.0,6.4);
    fluids=eve.Fluids();imageApi=eve.Image();sim=fluids.newShallowWater(RES,RES,CELL,true);
    if(sim==null) throw "Shallow-water solver unavailable";
    for(local z=0;z<RES;++z) for(local x=0;x<RES;++x) pondBeds.append(pondBed((x+0.5)*CELL,(z+0.5)*CELL));
    requireOk(sim.setBed(pondBeds),"shared visible and physical shore bed");
    waterFx=gfx.newWater();waterFx.createPlane(LAKE,LAKE,RES-1,RES-1);
    installWaterSurface(waterFx.getShader());requireOk(waterFx.applyConfigJson(POND_STYLE),"pond water style");waterMesh=waterFx.getMesh();
    fieldImage=imageApi.newEmptyImageData(RES,RES,"RGBA8");
    pondGradient=eve.WaterDepthGradient();
    requireOk(pondGradient.addColorStop(0.0,0.0,0.0,0.0),"field allocation color");
    requireOk(pondGradient.addAlphaStop(0.0,1.0),"field allocation alpha");
    requireOk(waterFx.setDepthGradient(pondGradient,RES),"solver field texture allocation");
    fieldTex=waterFx.getDepthGradientTexture();
    gfx.setTextureSampler(fieldTex,"linear","linear",1.0,0.0);
    pondProbeObject=sceneProp(gfx.newMeshSphere(16,10),"wood",[0.8,0.65,0.35],6.4,0.5,6.4,0.19,0.19,0.19,cam);
    pondProbeObject.setVisible(false);pondCharacterInit();pondReset();pondCharacterPose(0.0);pondSyncMesh();
    pondTelemetry.backend=sim.usingGpu()?"GPU":(sim.usedBackendFallback()?"CPU-fallback":"CPU");
    print("interactive-shallow-water ready: "+pondTelemetry.backend+"; WASD move, LMB splash, P auto walk, V view, Space pause, R reset, F data\n");
};
function pondStep() {
    characterVx=0.0;characterVz=0.0;
    if(keyboard.isDown("a")) characterVx-=MOVE_SPEED;if(keyboard.isDown("d")) characterVx+=MOVE_SPEED;
    if(keyboard.isDown("w")) characterVz-=MOVE_SPEED;if(keyboard.isDown("s")) characterVz+=MOVE_SPEED;
    if(characterVx==0.0 && characterVz==0.0 && autoWalk) {
        if(pondShorePath) {characterVx=0.08*cos(elapsed*0.4);characterVz=0.65*cos(elapsed*0.6);}
        else {characterVx=0.42*cos(elapsed*0.22);characterVz=0.30*sin(elapsed*0.22);}
    }
    local oldX=characterX,oldZ=characterZ,nextX=characterX+characterVx*FIXED_DT,nextZ=characterZ+characterVz*FIXED_DT;
    if(pondDistance(nextX,nextZ)>1.0) {characterX=nextX;characterZ=nextZ;}
    characterVx=(characterX-oldX)/FIXED_DT;characterVz=(characterZ-oldZ)/FIXED_DT;
    pondColliderSamples.clear();requireOk(sim.clearColliders(),"tick collider set");
    elapsed+=FIXED_DT;++fixedTick;pondBegin("characterMs");pondCharacterPose(FIXED_DT);pondEnd();
    pondBegin("collisionMs");pondUpdateProbe(FIXED_DT);pondEnd();
    pondBegin("solverMs");requireOk(sim.step(FIXED_DT),"shallow water step");pondEnd();waterFx.update(FIXED_DT);
}
eve_update=function(dt) {
    local updateStart=timer.getTime();++pondPerf.frame;pondPerf.frameMs=dt*1000.0;
    foreach(key in ["surfaceSyncMs","collisionMs","scanMs","characterMs","solverMs"]) pondPerf[key]=0.0;
    if(key_just_pressed("space")) paused=!paused;if(key_just_pressed("r")) pondReset();
    if(key_just_pressed("h")) pondShoreWalk();
    if(key_just_pressed("p")) autoWalk=!autoWalk;if(key_just_pressed("f")) showFieldBoards=!showFieldBoards;
    if(key_just_pressed("v")) {pondView=(pondView+1)%4;pondCamera();}
    if(key_just_pressed("c")) {pondColliderDebug=!pondColliderDebug;pondDebugBoxes();}
    if(key_just_pressed("b")) {pondView=pondView==3?1:3;pondCamera();}
    if(key_just_pressed("g")) print("shallow-water backend: "+pondTelemetry.backend+"\n");
    local down=mouse.isDown(1);
    if(down && !prevMouse && !paused) {local hit=pondPick(mouse.getX(),mouse.getY());if(hit!=null) pondSplash(hit[0],hit[1]);}
    prevMouse=down;
    if(!paused) {
        accumulator+=dt>0.1?0.1:dt;
        local steps=0;while(accumulator>=FIXED_DT && steps<6) {pondStep();accumulator-=FIXED_DT;++steps;}
        if(steps>0) pondSyncMesh();
        pondSamplePhysics();
    }
    if(pondView==1) pondCamera();
    if(pondColliderDebug) pondDebugBoxes();
    pondTelemetry.backend=sim.usingGpu()?"GPU":(sim.usedBackendFallback()?"CPU-fallback":"CPU");
    pondTelemetry.tick=fixedTick;pondTelemetry.time=elapsed;pondTelemetry.paused=paused;pondTelemetry.autowalk=autoWalk;
    pondTelemetry.footfalls<-footfallCount;
    pondBegin("scanMs");local sampled=0.0;
    foreach(offset in [[0.0,0.0],[-0.35,0.0],[0.35,0.0],[0.0,-0.35],[0.0,0.35]]) {
        local deviation=sim.surfaceHeightAt(characterX+offset[0],characterZ+offset[1])-WATER_DEPTH;
        if(deviation<0.0) deviation=-deviation;if(deviation>sampled) sampled=deviation;
    }
    pondTelemetry.sampledDeviation=sampled*VISUAL_GAIN;pondEnd();
    pondTelemetry.centerHeight=sim.surfaceHeightAt(6.4,6.4);pondTelemetry.character.x=characterX;pondTelemetry.character.z=characterZ;
    pondPerf.updateMs=(timer.getTime()-updateStart)*1000.0;
};
eve_render=function() {
    local renderStart=timer.getTime();
    gfx.clear();gfx.render3D();waterFx.setViewport(gfx.getWidth().tofloat(),gfx.getHeight().tofloat());waterFx.draw();
    if(showFieldBoards) gfx.drawTexturedRect(fieldTex,20.0,20.0,192.0,192.0,1.0,1.0,1.0,1.0);
    pondPerf.renderMs=(timer.getTime()-renderStart)*1000.0;
};
