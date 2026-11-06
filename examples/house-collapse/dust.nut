// Fluid solver runs only during cache construction. Playback restores recorded gas samples.
persist dustModule = null
persist dustClip = null
persist dustFluid = null
persist dustFog = null
persist dustLastFrame = -1
persist dustLastOrbit = -100.0
persist dustEnabled = true
persist dustTimingCount = 0
function dustValue(result) { checked(result);return result.value; }
function dustDefinition() {
    local definition=dustValue(fluids.volumeDefaults());
    definition.settings.capacity=512;definition.settings.spacing=config.dust.spacing;
    definition.settings.minimum=[-18.0,-0.45,-18.0];definition.settings.maximum=[18.0,24.0,18.0];
    definition.settings.iterations=2;definition.particles=[];
    return definition;
}
function dustBurst(index) {
    local impact=config.collapse.impacts[index];local settings=config.dust;
    local prototype=dustValue(fluids.volumeEmissionDefaults()).description.prototype;
    local particles=[];local count=settings.counts[index];
    for(local i=0;i<count;++i) {
        local angle=i*2.399963+config.collapse.seed*0.1;
        local radius=sqrt((i+0.5)/count)*(index==0?1.4:4.4);
        local p=clone prototype;p.material=clone prototype.material;
        p.material.phase=1;p.material.buoyancy=-settings.updraft;p.material.drag=settings.drag;
        p.material.viscosity=0.05;p.material.cohesion=0.0;p.material.vorticity=settings.vorticity;
        p.position=[(index==0?impact.x:0.0)+cos(angle)*radius,
                    (index==1?3.2:0.35)+0.4*(1.0+sin(i*1.7)),
                    (index==0?impact.z:0.0)+sin(angle)*radius];
        p.velocity=[cos(angle)*settings.spread,0.5+0.35*cos(i*0.9),sin(angle)*settings.spread];
        p.color=[settings.color[0],settings.color[1],settings.color[2],settings.opacity];
        p.life=settings.lifetime;particles.append(p);
    }
    checked(dustFluid.emit(particles));
}
function initDust(bodyAsset) {
    dustModule=eve.DestructionDust();dustClip=eve.CollapseDust();
    local path=bodyAsset+"-"+clip.contentKey(fs.readText("dust.nut"))+".evedust";local loaded=dustClip.load(path);
    if(!loaded.ok) {
        if(!missingBakeAsset(loaded)) checked(loaded);
        dustFluid=dustValue(fluids.newVolumeSimulator(dustDefinition()));
        local dt=1.0/config.dust.fixedHz;local fired={};checked(dustClip.capture(dustFluid,0.0));
        for(local frame=1;frame<=(config.collapse.seconds*config.dust.fixedHz).tointeger();++frame) {
            local seconds=frame*dt;
            foreach(index,impact in config.collapse.impacts) if(!(index in fired)&&seconds>=impact.time) {
                dustBurst(index);fired[index]<-true;
            }
            checked(dustFluid.step(dt*0.5,3));checked(dustFluid.step(dt*0.5,3));checked(dustClip.capture(dustFluid,seconds));
        }
        checked(dustClip.save(path));checked(dustClip.load(path));
        print("house-collapse: fluid smoke baked, saved and reloaded "+dustClip.frameCount()+" frames\n");
    }else print("house-collapse: loaded saved smoke, no fluid simulation\n");
    dustFluid=null;
    dustFog=gfx.newVolumetric();dustFog.setMode("froxel");dustFog.setQuality("high");
    dustFog.configureFroxelGrid(config.dust.gridWidth,config.dust.gridHeight,config.dust.gridDepth,0.2,65.0);
    checked(dustClip.prepareFog(clip,config.dust.width,config.dust.height,
        config.dust.gridWidth,config.dust.gridHeight,config.dust.gridDepth));
    // Create the existing renderer's atlas once. Workers only return owning pixels.
    dustFog.integrateFroxel(1.10,1.02,0.86,1.0);dustFog.uploadFroxel(gfx);
}
function drawDust() {
    local playTime=playback.time();
    if(!dustEnabled||playTime<config.collapse.impacts[0].time)return;
    local seconds=(playTime*config.dust.displayHz).tointeger().tofloat()/config.dust.displayHz;
    dustFog.setCamera(sin(orbit)*23.0,11.0,cos(orbit)*23.0,0.0,3.5,0.0,0.0,1.0,0.0,39.0,1280.0/860.0,0.2,65.0);
    local start=clock();
    local ready=dustValue(dustClip.updateFog(dustFog,gfx,seconds,camera,config.dust.extinction,config.dust.radius));
    if(dustTimingCount<3&&ready==1) {print(format("dust main-thread poll/upload %.2f ms\n",(clock()-start)*1000.0));dustTimingCount++;}
    if(ready==1)dustFog.applyFroxel(gfx,gfx.getRenderControl().getGBuffer().getDepthTexture());
}
