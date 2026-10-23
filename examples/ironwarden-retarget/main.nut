dofile("assets/ironwarden/model_info.nut");
// All model/skeleton/clip/skin owners live together. Engine dt is the only clock.
persist ward = { actors=[], source=null, sourceRig=null, sourceClips=[],
    camera=null, lights=[], stage=[], paused=false, selected=0, speed=0.8,
    yaw=0.28, dragging=false, mouseX=0.0, showBones=false,
    renderControl=null, probe=null, quality=true, uiBuilt=false,
    fps=0.0, fpsTime=0.0, fpsFrames=0 };
dofile("rendering.nut");
wardActions <- ["Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop",
    "Crouch_Fwd_Loop", "Dance_Loop", "Punch_Jab", "Sword_Attack"];

function wardProfile(skeleton) {
    local profile=eve.AnimRetargetProfile();
    profile.setNormalizedNameMatching(false);
    profile.setUseSkeletonSpaceRotation(true);
    profile.setRootBones("DEF-hips", "IW_pelvis");
    profile.setAutoRootScale(true);
    local pairs=[["root","IW_root"], ["DEF-hips","IW_pelvis"],
        ["DEF-spine.001","IW_spine_01"], ["DEF-spine.002","IW_spine_02"],
        ["DEF-spine.003","IW_chest"], ["DEF-neck","IW_neck"], ["DEF-head","IW_head"]];
    foreach (side in ["L","R"])
        foreach (pair in [["shoulder","clavicle"], ["upper_arm","upperarm"],
            ["forearm","forearm"], ["hand","hand"], ["thigh","thigh"],
            ["shin","calf"], ["foot","foot"], ["toe","toe"]])
            pairs.push(["DEF-"+pair[0]+"."+side,"IW_"+pair[1]+"."+side]);
    foreach (pair in pairs) {
        if (ward.sourceRig.findBone(pair[0])<0 || skeleton.findBone(pair[1])<0)
            throw "Missing retarget bone: "+pair[0]+" -> "+pair[1];
        profile.addBoneMapping(pair[0],pair[1]);
    }
    return profile;
}

function wardSurface(part, mech, materialName) {
    // Conventional lit PBR materials now run through the full HDR scene graph.
    local imported=part.getMaterial();
    local albedo=imported==null ? null : imported.getAlbedoTexture();
    local normal=imported==null ? null : imported.getNormalTexture();
    local metal=imported==null ? 0.1 : imported.getMetallic();
    local rough=imported==null ? 0.7 : imported.getRoughness();
    if (mech) {
        // Factors of a texture-driven glTF material are multipliers (usually 1),
        // not average metal/rough values for this simpler viewport shader.
        metal=(materialName=="steel" || materialName=="chrome" || materialName=="brass") ? 0.78 : 0.06;
        rough=materialName=="chrome" ? 0.24 : materialName=="steel" ? 0.38 :
            materialName=="white" ? 0.46 : 0.54;
    }
    local surface=gfx.newMaterial(); surface.setShadingModel("pbr");
    surface.setMetallic(mech ? metal : 0.0); surface.setRoughness(mech ? rough : 0.68);
    surface.setReceiveLight(true); surface.setReceiveShadow(true); surface.setCastShadow(true);
    if (albedo!=null) { surface.setAlbedoTexture(albedo); surface.setTint(1.0,1.0,1.0,1.0); }
    else surface.setTint(mech ? part.getTintR() : 0.48,mech ? part.getTintG() : 0.63,
        mech ? part.getTintB() : 0.72,1.0);
    if (normal!=null) surface.setNormalTexture(normal);
    part.setMaterial(surface); part.setMetallic(mech ? metal : 0.0);
    part.setRoughness(mech ? rough : 0.68);
    part.setCastShadow(true); part.setReceiveShadow(true); part.setReceiveLight(true);
    return surface;
}

function wardActor(model, label, x, mech) {
    local rig=mech ? anim.newSkeletonFromModel(model) : ward.sourceRig;
    local actor={ model=model, name=label, rig=rig, x=x, profile=null, clips=[],
        player=anim.newPlayer(rig), parts=[], skins=[], surfaces=[], bones=[] };
    if (mech) actor.profile=wardProfile(rig);
    foreach (clip in ward.sourceClips) {
        local result=mech ? clip.retargetWithProfile(ward.sourceRig,rig,actor.profile) : clip;
        result.setLoop(true); actor.clips.push(result);
    }
    if (mech && actor.profile.getMatchedBoneCount()!=23) throw "Incomplete humanoid mapping";
    for (local i=0;i<model.getMeshCount();++i) {
        if (!model.hasBones(i)) continue;
        local part=model3d.createRenderable(gfx,model,i); part.setPosition(x,0.0,0.0);
        actor.surfaces.push(wardSurface(part,mech,model.getMaterialName(model.getMaterialIndex(i))));
        local skin=anim.newSkinFromModel(model,i,rig);
        if (!skin.bindGpuMesh(gfx,part.getMesh())) throw "GPU skin binding: "+label;
        actor.parts.push(part); actor.skins.push(skin);
    }
    if (actor.parts.len()==0) throw "No skinned meshes: "+label;
    foreach (boneName in ["DEF-hips","DEF-head","DEF-upper_arm.L","DEF-forearm.L","DEF-hand.L",
        "DEF-upper_arm.R","DEF-forearm.R","DEF-hand.R","DEF-thigh.L","DEF-shin.L","DEF-foot.L",
        "DEF-thigh.R","DEF-shin.R","DEF-foot.R"]) {
        local target=boneName;
        if (mech) {
            local names={ ["DEF-hips"]="IW_pelvis", ["DEF-head"]="IW_head" };
            if (boneName in names) target=names[boneName];
            else {
                target="IW_"+boneName.slice(4);
                if (target.find("upper_arm")!=null) target="IW_upperarm"+boneName.slice(-2);
                if (target.find("shin")!=null) target="IW_calf"+boneName.slice(-2);
            }
        }
        local index=rig.findBone(target);
        if (index<0) throw "Missing visualization joint: "+target;
        local marker=eve.Renderable3D(); marker.setMesh(gfx.newMeshCube(0.035));
        marker.setTint(0.1,0.9,1.0,1.0); marker.setReceiveLight(false); marker.setVisible(false);
        actor.bones.push({index=index, renderable=marker});
    }
    actor.player.setSpeed(ward.speed); actor.player.play(actor.clips[0]);
    ward.actors.push(actor);
    print("ironwarden: "+label+" meshes="+actor.parts.len()+" bones="+rig.getBoneCount()+"\n");
}

function wardSelect(index) {
    ward.selected=index;
    foreach (a in ward.actors) {
        if (ward.paused) { a.player.play(a.clips[index]); a.player.pause(); }
        else a.player.crossFade(a.clips[index],0.22);
        a.player.setSpeed(ward.speed);
    }
}
function wardCamera() {
    ward.camera.setEye(7.2*sin(ward.yaw),3.0,7.2*cos(ward.yaw));
    ward.camera.setTarget(0.0,1.65,0.0);
}
function wardSkin() {
    foreach (a in ward.actors) {
        local pose=a.player.getPose(); pose.computeWorld(a.rig);
        foreach (i,skin in a.skins)
            if (!skin.updateGpuMesh(a.parts[i].getMesh(),pose)) throw "GPU skin palette upload: "+a.name;
        foreach (joint in a.bones) {
            joint.renderable.setVisible(ward.showBones);
            if (ward.showBones) joint.renderable.setPosition(a.x+pose.getWorldPositionX(joint.index),
                pose.getWorldPositionY(joint.index),pose.getWorldPositionZ(joint.index));
        }
    }
}

eve_init=function() {
    if ("dev" in eve) { eve.dev.console.setVisible(false); eve.dev.ai.setVisible(false); }
    if (anim==null) anim=eve.Animation();
    if (model3d==null) model3d=eve.Model3D();
    ward.source=model3d.newModelDataFromFile("assets/quaternius/AnimationLibrary.glb");
    ward.sourceRig=anim.newSkeletonFromModel(ward.source);
    foreach (name in wardActions) {
        local index=-1;
        for (local i=0;i<ward.source.getAnimationCount();++i)
            if (ward.source.getAnimationName(i)==name) index=i;
        if (index<0) throw "Missing source clip: "+name;
        ward.sourceClips.push(anim.newClipFromModel(ward.source,ward.sourceRig,index));
    }
    wardActor(ward.source,"HUMAN / SOURCE",-1.2,false);
    wardActor(model3d.newModelDataFromFile("assets/ironwarden/ironwarden.glb"),"IRONWARDEN / TARGET",1.1,true);
    ward.camera=eve.Camera3D(); wardCamera(); ward.camera.setUp(0.0,1.0,0.0);
    ward.camera.setFov(33.0); ward.camera.setActive(true);
    wardInitRendering();
    ui.setScale(1.0); ui.setTheme("dark"); ui.beginBuild(); ui.beginWindow("IRONWARDEN", "root");
    ui.text("IRONWARDEN   /   RX-78 REFERENCE STUDY + HUMAN MOTION", "title");
    ui.text("Imported human skeleton -> 23 mapped mech joints -> rigid armour skin. RMB drag to orbit.","help");
    ui.beginRow("actions",6.0); foreach (i,name in wardActions) ui.button(name,"action-"+i); ui.end();
    ui.beginRow("transport",12.0); ui.button("Pause / Play","pause"); ui.button("Restart","restart");
    ui.checkbox("Show joint markers",false,"bones"); ui.button("Quality: HIGH","quality"); ui.end();
    ui.slider("Playback speed",ward.speed,0.1,1.5,"speed");
    ui.slider("Cycle phase (scrub to pause)",0.0,0.0,1.0,"phase");
    ui.text("", "status");
    ui.text("8x MSAA requested + FXAA high | CSM shadows | AO + GI + IBL | SSR | ACES + bloom + vignette","rendering");
    ui.text("LEFT: Quaternius human source     |     RIGHT: SCAD mech / 23 mapped bones / "+wardPartCount+" rigid parts", "actors");
    ui.end(); ui.mountBuildAs("ward"); ui.select("ward");
    ui.setHostPos(0.0,0.0,0.0,0.0); ui.setHostSize(config.width.tofloat(),300.0); ui.setHostOverlay(true);
    ward.uiBuilt=true;
    wardSkin();
    print("IRONWARDEN_READY actions=8 mapped=23 parts="+wardPartCount+"\n");
};

eve_update=function(dt) {
    ward.fpsTime+=dt; ward.fpsFrames++;
    if (ward.fpsTime>=1.0) { ward.fps=ward.fpsFrames/ward.fpsTime; ward.fpsTime=0.0; ward.fpsFrames=0; }
    ui.select("ward"); local click=ui.consumeClick();
    while (click!="") {
        local slash=click.find("/"); local id=slash==null ? click : click.slice(slash+1);
        if (id=="pause") {
            ward.paused=!ward.paused;
            foreach (a in ward.actors) { if (ward.paused) a.player.pause(); else a.player["resume"](); }
        } else if (id=="quality") {
            wardSetQuality(!ward.quality);
        } else if (id=="restart") {
            foreach (a in ward.actors) { a.player.play(a.clips[ward.selected]); if (ward.paused) a.player.pause(); }
        } else if (id.find("action-")==0) wardSelect(id.slice(7).tointeger());
        click=ui.consumeClick();
    }
    local change=ui.consumeChange();
    while (change!="") {
        if (change=="ward/bones") ward.showBones=ui.getChecked("bones");
        else if (change=="ward/speed") {
            ward.speed=ui.getValue("speed"); foreach (a in ward.actors) a.player.setSpeed(ward.speed);
        } else if (change=="ward/phase") {
            ward.paused=true;
            foreach (a in ward.actors) {
                a.player.play(a.clips[ward.selected]);
                a.player.setTime(ui.getValue("phase")*a.clips[ward.selected].getDuration()); a.player.pause();
            }
        }
        change=ui.consumeChange();
    }
    local down=mouse.getY()>300.0 && mouse.isDown(3);
    local mx=mouse.getX();
    if (down && ward.dragging) { ward.yaw+=(mx-ward.mouseX)*0.008; wardCamera(); }
    ward.dragging=down; ward.mouseX=mx;
    foreach (a in ward.actors) a.player.update(dt);
    wardSkin();
    local t=ward.actors[0].player.getTime()%ward.sourceClips[ward.selected].getDuration();
    ui.setText("status",format("%s | %s | %.2f s | %.2fx | %.0f FPS",wardActions[ward.selected],ward.paused ? "PAUSED" : "PLAYING",t,ward.speed,ward.fps));
    ui.setValue("phase",t/ward.sourceClips[ward.selected].getDuration());
};

eve_render=function() {
    gfx.clear(); gfx.render3D(); ui.select("ward");
    ui.beginFrameAndRender();
};
