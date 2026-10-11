local retained=[];
local camera=null;
local fog=null;
local water=null;
local waterEntity=null;
local underwaterMaterialEntity=null;
local underwaterState=null;
local underwaterOutput=null;
local underwaterSettings=null;
local underwaterInput=null;
local depthGradient=null;
local timeGradient=null;
local surfaceFog=null;
local surfacePostFx=null;
local surfaceMaterial=null;
local postExposureCurve=null;
local postColorGradient=null;
local causticTexture=null;
local submergeDownSource=null;
local submergeUpSource=null;
local underwaterAmbienceSource=null;
local particleModule=null;
local underwaterParticleEmitter=null;
local transitionParticleEmitter=null;
local surfaceVfxEmitter=null;
local groundParticleEmitter=null;
local horizonEntity=null;
local triggerWorld=null;
local disableTriggerState=null;
local originShiftedParticles=0;
local terrainGravityActivated=false;
local followWaterApplied=false;
local seconds=0.0;

function retain(v){if(v==null)throw "pcg-underwater: null resource";retained.append(v);return v;}
function requireResult(r,name){if(!r.ok)throw "pcg-underwater: "+name+" failed";return r.value;}
function object(mesh,x,y,z,sx,sy,sz,r,g,b){
    local e=retain(eve.Renderable3D());e.setMesh(mesh);e.setPosition(x,y,z);e.setScale(sx,sy,sz);
    e.setTint(r,g,b,1.0);e.setRoughness(0.82);e.setCastShadow(true);return e;
}
function buildCausticCookie(){
    causticTexture=retain(gfx.newTextureFromFile("caustic-cookie.png"));
}
function setupUnderwaterAudio(){
    local sound=retain(eve.Sound());local audio=retain(eve.Audio());
    local downPcm=retain(sound.newSoundDataFromFile("submerge-down.wav"));
    local upPcm=retain(sound.newSoundDataFromFile("submerge-up.wav"));
    local ambiencePcm=retain(sound.newSoundDataFromFile("underwater-loop.wav"));
    submergeDownSource=retain(audio.newSource(downPcm));submergeDownSource.setRelative(true);
    submergeUpSource=retain(audio.newSource(upPcm));submergeUpSource.setRelative(true);
    underwaterAmbienceSource=retain(audio.newSource(ambiencePcm));underwaterAmbienceSource.setRelative(true);
    requireResult(eve.applyUnderwaterAudio(submergeDownSource,submergeUpSource,underwaterAmbienceSource,
                                           underwaterOutput.playSubmergeDown,underwaterOutput.playSubmergeUp,
                                           underwaterOutput.loopAudio,underwaterSettings.playbackVolume),"audio provider");
    if(!underwaterAmbienceSource.isLooping()||!underwaterAmbienceSource.isPlaying())
        throw "pcg-underwater: ambience source mismatch";
}
function setupUnderwaterVisualLifecycle(){
    particleModule=retain(eve.Particles());
    underwaterParticleEmitter=retain(particleModule.newEmitter(256));
    underwaterParticleEmitter.setPosition(480.0,520.0);underwaterParticleEmitter.setEmissionArea("rectangle",400.0,35.0);
    underwaterParticleEmitter.setEmissionRate(18.0);underwaterParticleEmitter.setParticleLifetime(2.0,4.5);
    underwaterParticleEmitter.setSpeed(18.0,42.0);underwaterParticleEmitter.setDirection(-1.5708);
    underwaterParticleEmitter.setSpread(0.32);underwaterParticleEmitter.setParticleSize(3.0,3.0);
    underwaterParticleEmitter.setColorStart(0.55,0.88,1.0,0.55);underwaterParticleEmitter.setColorEnd(0.35,0.72,1.0,0.0);
    transitionParticleEmitter=retain(particleModule.newEmitter(96));
    transitionParticleEmitter.setPosition(480.0,320.0);transitionParticleEmitter.setEmissionRate(90.0);
    transitionParticleEmitter.setEmitterLifetime(0.35);transitionParticleEmitter.setParticleLifetime(0.25,0.7);
    transitionParticleEmitter.setSpeed(45.0,110.0);transitionParticleEmitter.setSpread(6.283);
    transitionParticleEmitter.setParticleSize(5.0,5.0);transitionParticleEmitter.setColorStart(0.65,0.92,1.0,0.7);
    transitionParticleEmitter.setColorEnd(0.25,0.62,0.95,0.0);
    surfaceVfxEmitter=retain(particleModule.newEmitter(96));
    surfaceVfxEmitter.setPosition(480.0,40.0);surfaceVfxEmitter.setEmissionArea("rectangle",900.0,20.0);
    surfaceVfxEmitter.setEmissionRate(30.0);surfaceVfxEmitter.setParticleLifetime(1.0,2.0);
    surfaceVfxEmitter.setSpeed(80.0,120.0);surfaceVfxEmitter.setDirection(1.5708);
    surfaceVfxEmitter.setParticleSize(2.0,4.0);surfaceVfxEmitter.setColorStart(0.7,0.82,0.92,0.45);
    surfaceVfxEmitter.setColorEnd(0.55,0.68,0.80,0.0);
    requireResult(eve.applyUnderwaterParticles(underwaterParticleEmitter,transitionParticleEmitter,
                                               underwaterOutput.particles,underwaterOutput.transitionFx,
                                                underwaterOutput.entered,underwaterOutput.exited),"particle provider");
    requireResult(eve.applyUnderwaterSurfaceVfx(surfaceVfxEmitter,underwaterOutput.surfaceVfx),"surface vfx provider");
    requireResult(eve.applyWaterUnderwaterHorizon(horizonEntity,underwaterOutput),"horizon provider");
    if(!underwaterParticleEmitter.isActive()||!horizonEntity.getVisible()||
       !surfaceVfxEmitter.isStopped()||surfaceVfxEmitter.isVisible())
        throw "pcg-underwater: visual lifecycle mismatch";
}
function verifyDisableTriggerLifecycle(){
    local groundParticles=retain(eve.Particles());groundParticleEmitter=retain(groundParticles.newEmitter(64));
    groundParticleEmitter.stop();
    groundParticleEmitter.setSimulationSpace("world");groundParticleEmitter.setPosition(1200.0,0.0);
    groundParticleEmitter.emit(2);
    originShiftedParticles=requireResult(eve.shiftWorldSpaceParticles(groundParticleEmitter,1000.0,0.0),"particle origin shift");
    if(originShiftedParticles!=2||!groundParticleEmitter.isStopped())throw "pcg-underwater: particle origin shift mismatch";
    local physics=retain(eve.Physics());triggerWorld=retain(physics.newWorld3D(0.0,0.0,0.0,false));
    local zone=triggerWorld.newBody("static",0.0,0.0,0.0);local sensor=zone.newBoxShape(4.0,4.0,4.0,1.0,0.5,0.0);
    sensor.setTag(8101);sensor.setSensor(true);
    local visitor=triggerWorld.newBody("dynamic",0.0,0.0,0.0);local visitorShape=visitor.newSphereShape(0.5,1.0,0.5,0.0);
    local gravityWait=retain(eve.TerrainLoadGravityState());
    requireResult(eve.beginTerrainLoadGravity(gravityWait,visitor,true),"terrain gravity begin");
    if(visitor.getGravityScale()!=0.0)throw "pcg-underwater: terrain wait did not suppress gravity";
    requireResult(eve.advanceTerrainLoadGravity(gravityWait,visitor,false,0.5,2.0),"terrain gravity wait");
    requireResult(eve.advanceTerrainLoadGravity(gravityWait,visitor,true,0.75,2.0),"terrain gravity schedule");
    terrainGravityActivated=requireResult(eve.advanceTerrainLoadGravity(gravityWait,visitor,false,1.25,2.0),"terrain gravity activate");
    if(!terrainGravityActivated||visitor.getGravityScale()!=1.0)throw "pcg-underwater: terrain gravity did not restore";
    visitorShape.setTag(8102);triggerWorld.update(0.0166667);
    if(triggerWorld.getBeginTriggerCount()!=1)throw "pcg-underwater: missing disable trigger enter";
    disableTriggerState=retain(eve.WaterUnderwaterDisableTriggerState());local event=retain(eve.WaterUnderwaterTriggerEvent());
    event.sensorTag=triggerWorld.getBeginTriggerSensorShapeTag(0);event.visitorTag=triggerWorld.getBeginTriggerVisitorShapeTag(0);
    event.entered=true;requireResult(eve.advanceWaterUnderwaterDisableTrigger(disableTriggerState,event,8101,8102),"trigger enter");
    requireResult(eve.applyGroundParticleCulling(groundParticleEmitter,8102,event.visitorTag,true,false),"ground particle enter");
    if(!groundParticleEmitter.isActive())throw "pcg-underwater: ground particles did not start";
    if(disableTriggerState.effectsEnabled)throw "pcg-underwater: disable trigger did not suppress effects";
    visitor.setPosition(10.0,0.0,0.0);triggerWorld.update(0.0166667);
    if(triggerWorld.getEndTriggerCount()!=1)throw "pcg-underwater: missing disable trigger exit";
    event.entered=false;event.exited=true;event.sensorTag=triggerWorld.getEndTriggerSensorShapeTag(0);
    event.visitorTag=triggerWorld.getEndTriggerVisitorShapeTag(0);
    requireResult(eve.advanceWaterUnderwaterDisableTrigger(disableTriggerState,event,8101,8102),"trigger exit");
    requireResult(eve.applyGroundParticleCulling(groundParticleEmitter,8102,event.visitorTag,false,true),"ground particle exit");
    if(!groundParticleEmitter.isStopped())throw "pcg-underwater: ground particles did not stop";
    if(!disableTriggerState.effectsEnabled)throw "pcg-underwater: disable trigger did not restore effects";
}

eve_init=function(){
    verifyDisableTriggerLifecycle();
    buildCausticCookie();
    gfx.setBackgroundColor(0.015,0.06,0.10,1.0);
    gfx.setDirectionalLight(-0.35,0.75,0.45,0.46,0.68,0.82);
    gfx.getRenderControl().enable("gbuffer");
    camera=retain(eve.Camera3D());camera.setEye(0.0,4.21,12.0);camera.setTarget(0.0,4.5,-10.0);
    camera.setUp(0.0,1.0,0.0);camera.setFov(58.0);camera.setClipPlanes(0.1,120.0);
    camera.setAmbient(0.10,0.18,0.24);camera.setActive(true);

    local cube=retain(gfx.newMeshCube(1.0));
    object(cube,0.0,-2.6,-8.0,28.0,1.0,38.0,0.18,0.26,0.20);
    object(cube,-5.0,-0.7,-7.0,2.2,3.4,2.2,0.22,0.34,0.28);
    object(cube,4.0,-1.2,-13.0,3.0,2.4,3.0,0.16,0.29,0.25);
    object(cube,0.5,-0.4,-20.0,1.4,4.2,1.4,0.25,0.38,0.31);
    horizonEntity=object(cube,0.0,3.6,-31.0,42.0,0.7,1.0,0.04,0.19,0.30);
    horizonEntity.setCastShadow(false);horizonEntity.setVisible(false);

    water=retain(gfx.newWater());local mesh=eve.WaterMeshSettings();mesh.setType(0);
    mesh.sizeX=36.0;mesh.sizeZ=48.0;mesh.densityX=3.0;mesh.densityY=3.0;
    requireResult(water.createProceduralMesh(mesh),"water mesh");water.setWaveAmplitude(0.12);
    waterEntity=object(water.getMesh(),0.0,4.25,-8.0,1.0,1.0,1.0,0.12,0.54,0.72);
    local followSettings=retain(eve.FollowPlayerSettings());followSettings.waterObject=true;
    followSettings.useOffset=true;followSettings.offsetX=0.0;followSettings.offsetY=9.96;followSettings.offsetZ=20.0;
    local followInput=retain(eve.FollowPlayerInput());followInput.hasPlayer=true;
    followInput.playerX=0.0;followInput.playerY=4.21;followInput.playerZ=12.0;
    local followOutput=retain(eve.FollowPlayerOutput());
    requireResult(eve.evaluateFollowPlayer(followOutput,followSettings,followInput),"follow water");
    if(!followOutput.applyPosition)throw "pcg-underwater: follow water output missing";
    waterEntity.setPosition(followOutput.x,followOutput.y,followOutput.z);followWaterApplied=true;
    waterEntity.setShader(water.getShader());waterEntity.setCastShadow(false);waterEntity.setReceiveShadow(false);
    underwaterMaterialEntity=object(cube,0.0,4.25,-8.0,36.0,0.05,48.0,0.12,0.54,0.72);
    underwaterMaterialEntity.setShader(water.getShader());underwaterMaterialEntity.setCastShadow(false);
    underwaterMaterialEntity.setReceiveShadow(false);

    underwaterSettings=retain(eve.WaterUnderwaterSettings());underwaterSettings.causticTextureCount=16;
    underwaterSettings.enabled=disableTriggerState.effectsEnabled;
    underwaterSettings.fogDepth=20.0;underwaterSettings.fogDensity=0.052;underwaterSettings.fogDistance=42.0;
    underwaterSettings.setFogColorMultiplier(0.02,-1.0,0.0);
    underwaterInput=retain(eve.WaterUnderwaterInput());underwaterInput.seaLevel=4.25;
    underwaterInput.cameraY=6.0;underwaterInput.setMainLightColor(0.72,0.88,1.0);
    underwaterState=retain(eve.WaterUnderwaterState());underwaterOutput=retain(eve.WaterUnderwaterOutput());
    depthGradient=retain(eve.UnderwaterColorGradient());timeGradient=retain(eve.UnderwaterColorGradient());
    postExposureCurve=retain(eve.UnderwaterScalarCurve());postColorGradient=retain(eve.UnderwaterColorGradient());
    requireResult(depthGradient.addStop(0.0,0.08,0.48,0.62),"shallow fog");
    requireResult(depthGradient.addStop(0.5,0.025,0.20,0.34),"middle fog");
    requireResult(depthGradient.addStop(1.0,0.005,0.035,0.09),"deep fog");
    requireResult(timeGradient.addStop(0.0,0.03,0.12,0.22),"night fog");
    requireResult(timeGradient.addStop(1.0,0.09,0.42,0.55),"day fog");
    requireResult(postExposureCurve.addKey(0.0,-1.0),"night exposure");
    requireResult(postExposureCurve.addKey(1.0,-0.35),"day exposure");
    requireResult(postColorGradient.addStop(0.0,0.35,0.58,0.78),"night color filter");
    requireResult(postColorGradient.addStop(1.0,0.72,0.92,1.0),"day color filter");
    underwaterSettings.timeDrivenPostFx=true;underwaterInput.timeOfDay=0.5;
    requireResult(eve.advanceWaterUnderwaterEffects(underwaterState,underwaterOutput,underwaterSettings,
                                                    underwaterInput,depthGradient,timeGradient,
                                                    postExposureCurve,postColorGradient),"surface seed");
    underwaterInput.cameraY=4.21;underwaterInput.causticTicks=3;
    requireResult(eve.advanceWaterUnderwaterEffects(underwaterState,underwaterOutput,underwaterSettings,
                                                    underwaterInput,depthGradient,timeGradient,
                                                    postExposureCurve,postColorGradient),"submerge");
    if(!underwaterOutput.entered||!underwaterOutput.loopAudio||underwaterOutput.causticFrame!=3)
        throw "pcg-underwater: transition mismatch";
    surfaceFog=retain(eve.WaterSurfaceFogSnapshot());surfaceFog.setColor(0.55,0.58,0.62);
    fog=retain(gfx.newVolumetric());fog.setCamera(0.0,4.21,12.0,0.0,4.5,-10.0,0.0,1.0,0.0,58.0,1.5,0.1,120.0);
    fog.setFogNoise(0.10);requireResult(eve.applyWaterUnderwaterFog(fog,underwaterOutput,surfaceFog),"fog provider");
    surfacePostFx=retain(eve.WaterSurfacePostFxSnapshot());surfacePostFx.exposureEv=0.0;surfacePostFx.setColor(1.0,1.0,1.0);
    requireResult(eve.applyWaterUnderwaterPostFx(gfx,camera,underwaterOutput,surfacePostFx),"post fx provider");
    surfaceMaterial=retain(eve.WaterSurfaceMaterialSnapshot());surfaceMaterial.setColor(0.12,0.54,0.72);
    requireResult(eve.applyWaterUnderwaterMaterial(underwaterMaterialEntity,underwaterOutput,surfaceMaterial),"underwater material");
    if(abs(underwaterMaterialEntity.getTintR()-0.72)>0.001)
        throw "pcg-underwater: underwater material color mismatch";
    setupUnderwaterAudio();
    setupUnderwaterVisualLifecycle();
    print("PCG_UNDERWATER_READY depth="+underwaterOutput.depth01+" caustic="+underwaterOutput.causticFrame+" fog="+underwaterOutput.getFogB()+" transition="+underwaterOutput.transitionWeight+" audio=looping particles=active surfaceVfx=paused trigger=restored groundParticles=culled originParticles="+originShiftedParticles+" terrainGravity=activated followWater=applied material=mainLight horizon=visible\n");
};

eve_update=function(dt){seconds+=dt;water.update(dt);fog.setTime(seconds);};
eve_render=function(){
    gfx.clear();gfx.render3D();
    local depth=gfx.getRenderControl().getGBuffer().getDepthTexture();
    if(underwaterOutput.caustics)requireResult(fog.projectDirectionalCookie(gfx,depth,causticTexture,underwaterOutput.causticSize,0.85),"caustic projector");
    fog.applyFog(gfx,depth);
};
