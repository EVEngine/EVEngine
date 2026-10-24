// Only bake functions advance simulation. Update/render consume frozen assets.
persist replay = null
const BODY_ID = "00000000-0000-0000-0000-000000000011"
const EMITTER_ID = "00000000-0000-0000-0000-000000000021"
function requireBake(result) {
    if (!result.ok) throw "physics-bake-replay: " + result.status.summary;
    return result.hasValue ? result.value : null;
}
function missingBake(result) {
    return !result.ok && result.diagnostics.len()>0 && result.diagnostics[0].code=="not_found";
}
function makeRecipe(kind) {
    local r=eve.BakeRecipe();
    requireBake(r.setCoordinates(kind=="particles" ? "pixels2d" : "metres3d"));
    local revision=r.producerBuildRevision();
    requireBake(r.setProducer("replay-workshop-v1:"+revision));
    requireBake(r.setBackend(kind=="rigid" ? r.box3DBackendRevision() : kind+":"+revision));
    // playbackOnly changes admission policy, not the producing simulation.
    requireBake(r.setParameters("hz="+config.bake.fixedHz+";seconds="+config.bake.seconds+";stride="+config.bake.stride));
    requireBake(r.setSource("scene",fs.readText("main.nut")));
    requireBake(r.setTiming(1.0/config.bake.fixedHz,kind=="particles" ? 1 : config.bake.stride));
    if(kind=="particles") requireBake(r.setRandomStream("burst","37"));
    return r;
}
function bakeRigid(recipe,path) {
    local world=physics.newWorld3DWithBackend("box3d",0.0,-9.81,0.0,false);
    local floor=world.newBody("static",0.0,-0.25,0.0);
    floor.newBoxShape(4.0,0.5,3.0,1.0,0.6,0.2);
    local body=world.newBody("dynamic",-0.5,3.2,0.0);
    body.newBoxShape(0.6,0.6,0.6,1.0,0.5,0.65);
    body.setLinearVelocity(0.7,0.0,0.0);
    local recorder=requireBake(physicsBakeRecording.beginRigid(recipe,replay.samples));
    requireBake(recorder.bindBody3D(world,body,BODY_ID));
    requireBake(recorder.capture3D(world,0.0));
    for(local tick=1;tick<=replay.ticks;tick++) {
        world.update(replay.step);
        if(tick%config.bake.stride==0) requireBake(recorder.capture3D(world,tick*replay.step));
    }
    world.destroy(); body=null; floor=null; world=null; collectgarbage();
    requireBake(recorder.save(path));
}
function bakeDeform(kind,recipe,path) {
    local source=kind=="cloth" ? cloth.newCloth3D(12,10,0.16,-0.88,3.0,0.0)
        : rope.newRope3D(20,-1.2,2.8,0.0,1.2,2.8,0.0);
    if(kind=="cloth") {
        source.setAerodynamics(1.225,1.1,0.3); source.setWindVelocity(0.0,0.0,5.0);
        // Vertical sheet. Preserve its top row pins; wind drives the drape.
        for(local row=0;row<10;row++) for(local col=0;col<12;col++)
            source.setParticlePosition(row*12+col,-0.88+col*0.16,3.0-row*0.16,0.25*sin(col*0.5));
    } else {
        if(!source.pin(0) || !source.pin(19)) throw "cannot pin replay rope";
        if(!source.setRestLength(3.2)) throw "cannot set rope slack";
    }
    local recorder=requireBake(physicsBakeRecording.beginDeform(recipe,replay.samples*source.getParticleCount()));
    if(kind=="cloth") requireBake(recorder.captureCloth(source,0.0));
    else requireBake(recorder.captureRope(source,0.0));
    for(local tick=1;tick<=replay.ticks;tick++) {
        source.update(replay.step);
        if(tick%config.bake.stride==0) {
            if(kind=="cloth") requireBake(recorder.captureCloth(source,tick*replay.step));
            else requireBake(recorder.captureRope(source,tick*replay.step));
        }
    }
    source=null; collectgarbage(); // Owning VM factory releases the native solver.
    requireBake(recorder.save(path));
}
function bakeParticles(recipe,path) {
    local emitter=particles.newEmitter(96);
    emitter.setGpuSimulation(false); emitter.setRenderMode("billboard",1.0); emitter.setSortMode("none");
    emitter.setRandomSeed(37); emitter.setPosition(220.0,680.0);
    emitter.setEmissionRate(0.0); emitter.setParticleLifetime(1.2,2.4);
    emitter.setDirection(-1.5707963); emitter.setSpread(1.8); emitter.setSpeed(70.0,160.0);
    emitter.setLinearAcceleration(0.0,85.0,0.0,85.0);
    emitter.setParticleSize(9.0,9.0); emitter.setSizes(1.0,0.1);
    emitter.setColorStart(1.0,0.65,0.15,1.0); emitter.setColorEnd(1.0,0.15,0.05,0.0);
    requireBake(particleBakeRecording.begin(emitter,20000)); emitter.emit(64);
    for(local tick=1;tick<=replay.ticks;tick++) particles.update(replay.step);
    local journal=requireBake(particleBakeRecording.finish(emitter));
    emitter.stop(); emitter.reset(); emitter=null; collectgarbage();
    // Emitter is ECS-owned; stop/reset retires its live particles. No update runs during replay.
    local archive=requireBake(particleBakeRecording.newArchive(recipe));
    requireBake(archive.addJournal(journal,EMITTER_ID));
    requireBake(archive.save(path));
}
function loadChannel(kind) {
    local recipe=makeRecipe(kind);
    local path=replay.directory+"/"+kind+"-"+requireBake(recipe.key())+".bake";
    local load=function() {
        if(kind=="rigid") return physicsBake.loadRigid(path,recipe);
        if(kind=="particles") return physicsBake.loadParticles(path,recipe);
        return physicsBake.loadDeform(path,recipe);
    };
    local loaded=load();
    if(!loaded.ok) {
        if(config.bake.playbackOnly || !missingBake(loaded)) requireBake(loaded);
        print("physics-bake-replay: recording "+kind+" at fixed step, please wait\n");
        if(kind=="rigid") bakeRigid(recipe,path);
        else if(kind=="particles") bakeParticles(recipe,path);
        else bakeDeform(kind,recipe,path);
        loaded=load(); requireBake(loaded);
        print("physics-bake-replay: baked-and-reloaded "+kind+" "+path+"\n");
    } else print("physics-bake-replay: cache-only "+kind+" "+path+"\n");
    return requireBake(loaded);
}
function meshObject(mesh,x,z,r,g,b) {
    local target=eve.Renderable3D(); target.setMesh(mesh); target.setPosition(x,0.0,z);
    target.setTint(r,g,b,1.0); target.setRoughness(0.7); target.setReceiveLight(true);
    replay.targets.push(target); return target;
}
function setPaused(paused) {
    replay.paused=paused;
    foreach(player in replay.players) player.setPaused(paused);
}
function setupControls() {
    ui.setScale(0.65); ui.setTheme("dark"); ui.beginBuild();
    ui.beginWindow("BAKED PHYSICS", "root");
    ui.text("SIMULATE ONCE / REPLAY TWICE", "title");
    ui.text("Rigid body | cloth | rope | particle burst", "channels");
    ui.text("Blue: 1x | Orange: 0.65x", "instances");
    ui.text("Playback: no solver steps", "mode");
    ui.text("", "clock");
    ui.button("Pause / resume", "pause"); ui.button("Restart", "restart");
    ui.button("Reverse direction", "reverse");
    ui.end(); ui.mountBuildAs("bakeReplay"); ui.select("bakeReplay");
    ui.setHostOverlay(true); ui.setHostPos(14.0,14.0,0.0,0.0);
}
eve_init = function() {
    if("dev" in eve) { eve.dev.ai.setVisible(false); eve.dev.console.setVisible(false); }
    replay={players=[],targets=[],deforms=[],rigids=[],bursts=[],materials=[],paused=false,direction=1.0};
    if(config.bake.fixedHz<=0.0 || config.bake.stride<=0 || config.bake.seconds<=0.0)
        throw "invalid fixed-step recording configuration";
    replay.step <- 1.0/config.bake.fixedHz;
    replay.ticks <- (config.bake.seconds*config.bake.fixedHz).tointeger();
    replay.samples <- replay.ticks/config.bake.stride+1;
    if(!fs.setIdentity("physics-bake-replay",false) || !fs.setupWriteDirectory())
        throw "cannot initialize replay save identity";
    replay.directory <- fs.getSaveDirectory()+"/clips";
    if(config.bake.fixedHz<=0.0 || config.bake.stride<=0 || replay.ticks%config.bake.stride!=0)
        throw "invalid fixed-step recording configuration";
    if(!fs.createDirectory("clips")) throw "cannot create replay asset directory";
    // Native asset IO uses a real path in the engine save directory.
    local rigid=loadChannel("rigid"), fabric=loadChannel("cloth"), cable=loadChannel("rope"), burst=loadChannel("particles");
    gfx.setBackgroundColor(0.025,0.04,0.065,1.0);
    replay.camera <- eve.Camera3D(); replay.camera.setEye(10.0,9.0,15.0);
    replay.camera.setTarget(0.0,1.0,0.0); replay.camera.setFov(48.0);
    replay.camera.setAmbient(0.45,0.48,0.55); replay.camera.setActive(true);
    gfx.setDirectionalLight(-0.5,1.0,0.4,1.5,1.4,1.25);
    local ground=meshObject(gfx.newMeshCube(1.0),0.0,0.0,0.08,0.12,0.18);
    ground.setPosition(0.0,-0.3,0.0); ground.setScale(13.0,0.1,8.0);
    for(local instance=0;instance<2;instance++) {
        local z=instance==0 ? -1.8 : 1.8;
        local color=instance==0 ? [0.25,0.65,1.0] : [1.0,0.55,0.2];
        local rate=instance==0 ? 1.0 : 0.65;
        local player=requireBake(rigid.copy()); requireBake(player.setPlacement(-4.0,0.0,z,0.0,0.0,0.0,1.0));
        local target=meshObject(gfx.newMeshCube(0.6),0.0,0.0,color[0],color[1],color[2]);
        replay.rigids.push({player=player,target=target}); replay.players.push(player);
        foreach(pair in [{source=fabric,x=0.0},{source=cable,x=4.0}]) {
            local view=requireBake(physicsBakeGraphics.prepareDeform(pair.source,0.045));
            requireBake(view.setPlacement(pair.x,0.0,z,0.0,0.0,0.0,1.0));
            local mesh=requireBake(view.createMesh(gfx));
            meshObject(mesh,0.0,0.0,color[0],color[1],color[2]);
            replay.deforms.push({player=view,mesh=mesh}); replay.players.push(view);
        }
        local view=requireBake(physicsBakeGraphics.prepareParticles(burst,[EMITTER_ID]));
        requireBake(view.setPlacement(instance*650.0,0.0,0.0,0.0,0.0,0.0,1.0));
        local material=eve.Sprite2D(); material.setVisible(false); material.setReceiveLight(false);
        material.setBlend("additive"); replay.materials.push(material);
        replay.bursts.push({player=view,materials=[material]}); replay.players.push(view);
        for(local i=instance*4;i<replay.players.len();i++) {
            requireBake(replay.players[i].setRate(rate)); requireBake(replay.players[i].setLoop(true));
            requireBake(replay.players[i].seek(instance*0.7));
        }
    }
    setupControls();
    print("physics-bake-replay: READY channels=4 instances=2 solver-updates-during-playback=0\n");
}
eve_update = function(dt) {
    ui.select("bakeReplay");
    local click=ui.consumeClick();
    while(click!="") {
        if(click=="bakeReplay/pause") setPaused(!replay.paused);
        if(click=="bakeReplay/restart") foreach(player in replay.players) requireBake(player.seek(0.0));
        if(click=="bakeReplay/reverse") {
            replay.direction=-replay.direction;
            foreach(player in replay.players) requireBake(player.setRate(-player.rate()));
        }
        click=ui.consumeClick();
    }
    foreach(player in replay.players) requireBake(player.advance(dt));
    ui.setText("clock", format("%s | Blue %.2fs (%+.2fx) | Orange %.2fs (%+.2fx)",
        replay.paused ? "Paused" : "Playing", replay.players[0].time(), replay.players[0].rate(),
        replay.players[4].time(), replay.players[4].rate()));
    foreach(item in replay.rigids) requireBake(physicsBakeGraphics.submitRigid3D(item.player,BODY_ID,item.target));
    foreach(item in replay.deforms) requireBake(item.player.upload(gfx,item.mesh));
}
eve_render = function() {
    gfx.clear(); gfx.render3D();
    foreach(item in replay.bursts) requireBake(item.player.draw(gfx,item.materials));
    ui.beginFrameAndRender();
}
