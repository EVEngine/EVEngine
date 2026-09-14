// Owners remain alive together; all players receive the same engine dt.
persist lab = { actors=[], source=null, skeleton=null, clips=[], camera=null,
    lights=[], ground=null, paused=false, selected=0, speed=1.0,
    yaw=0.0, dragging=false, mouseX=0.0,
    aaEnabled=true, aa=null, sceneCanvas=null, sceneWidth=0, sceneHeight=0 };
labActions <- ["Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop",
    "Crouch_Fwd_Loop", "Dance_Loop", "Punch_Jab", "Sword_Attack"];

function humanoidProfile(target) {
    local p = eve.AnimRetargetProfile();
    p.setNormalizedNameMatching(true); p.setUseSkeletonSpaceRotation(true);
    p.setRootBones("DEF-hips", "pelvis"); p.setAutoRootScale(true);
    local pairs = [["root","root"], ["DEF-hips","pelvis"],
        ["DEF-spine.001","spine_01"], ["DEF-spine.002","spine_02"],
        ["DEF-spine.003","spine_03"], ["DEF-neck","neck_01"], ["DEF-head","Head"]];
    foreach (side in ["l", "r"]) {
        local s = side == "l" ? "L" : "R";
        foreach (pair in [["shoulder","clavicle"], ["upper_arm","upperarm"],
            ["forearm","lowerarm"], ["hand","hand"], ["thigh","thigh"],
            ["shin","calf"], ["foot","foot"], ["toe","ball"]])
            pairs.push(["DEF-" + pair[0] + "." + s, pair[1] + "_" + side]);
        foreach (finger in ["index", "middle", "pinky", "ring", "thumb"])
            for (local j=1; j<=3; ++j)
                pairs.push(["DEF-" + (finger == "thumb" ? "" : "f_") + finger + ".0" + j + "." + s,
                    finger + "_0" + j + "_" + side]);
    }
    foreach (pair in pairs) {
        if (lab.skeleton.findBone(pair[0]) < 0 || target.findBone(pair[1]) < 0)
            throw "Missing humanoid mapping: " + pair[0] + " -> " + pair[1];
        p.addBoneMapping(pair[0], pair[1]);
    }
    return p;
}

// createRenderable enables the glTF extension PBR path whenever Assimp reports
// an alphaMode. That shader is meant for IBL/capture, not this UI viewport:
// albedo is decoded twice, linear ORM/normal maps are treated as sRGB, and the
// characters render as a flat red unlit mass. Rebuild a conventional Material
// so renderScene3DToCanvas uses mesh3d lighting like the original showcase.
function labPreviewMaterial(part, albedo) {
    local surface = gfx.newMaterial();
    surface.setShadingModel("pbr");
    surface.setMetallic(0.0);
    surface.setRoughness(0.78);
    surface.setReceiveLight(true);
    surface.setReceiveShadow(true);
    surface.setCastShadow(true);
    surface.setTint(part.getTintR(), part.getTintG(), part.getTintB(), 1.0);
    if (albedo != null) {
        surface.setAlbedoTexture(albedo);
        surface.setTint(1.0, 1.0, 1.0, 1.0);
        part.setTexture(albedo);
        part.setTint(1.0, 1.0, 1.0, 1.0);
    }
    part.setMaterial(surface);
    part.setMetallic(0.0);
    part.setRoughness(0.78);
    part.setCastShadow(true);
    part.setReceiveShadow(true);
    part.setReceiveLight(true);
}

function addLabActor(model, name, x, source) {
    local sk = source ? lab.skeleton : anim.newSkeletonFromModel(model);
    local a = { model=model, name=name, skeleton=sk, clips=[], parts=[], skins=[],
        player=anim.newPlayer(sk), profile=null, textures=[], coverage="Source rig" };
    if (!source) a.profile = humanoidProfile(sk);
    foreach (clip in lab.clips) {
        local converted = source ? clip : clip.retargetWithProfile(lab.skeleton, sk, a.profile);
        converted.setLoop(true); a.clips.push(converted);
    }
    if (!source) {
        a.coverage = a.profile.getMatchedBoneCount() + " mapped / " + sk.getBoneCount() + " bones";
        for (local i=0; i<a.profile.getUnmatchedBoneCount(); ++i)
            print("character-lab: " + name + " bind-only " + a.profile.getUnmatchedTargetBone(i) + "\n");
    }
    for (local i=0; i<model.getMeshCount(); ++i) {
        if (!model.hasBones(i)) continue;
        local part = model3d.createRenderable(gfx, model, i);
        part.setPosition(x, 0.0, 0.0);
        local albedo = null;
        local imported = part.getMaterial();
        if (imported != null) albedo = imported.getAlbedoTexture();
        local texturePath = model.getMaterialTexturePath(model.getMaterialIndex(i), "base_color", 0);
        if (texturePath != "") {
            albedo = gfx.newTextureFromFile("assets/quaternius/" + texturePath);
            a.textures.push(albedo);
        }
        labPreviewMaterial(part, albedo);
        a.parts.push(part);
        a.skins.push(anim.newSkinFromModel(model, i, sk));
    }
    if (a.parts.len() == 0) throw "No skinned meshes in " + name;
    lab.actors.push(a);
    print("character-lab: " + name + " " + a.coverage + " meshes=" + a.parts.len() + "\n");
}

function selectLabAction(index) {
    lab.selected = index;
    foreach (a in lab.actors) {
        if (lab.paused) a.player.play(a.clips[index]);
        else a.player.crossFade(a.clips[index], 0.22);
        a.player.setSpeed(lab.speed);
        if (lab.paused) a.player.pause();
    }
}
function labCamera() {
    lab.camera.setEye(7.8*sin(lab.yaw), 2.6, 7.8*cos(lab.yaw));
    lab.camera.setTarget(0.0, 1.0, 0.0);
}
eve_init = function() {
    if (anim == null) anim = eve.Animation();
    if (model3d == null) model3d = eve.Model3D();
    lab.source = model3d.newModelDataFromFile("assets/quaternius/AnimationLibrary.glb");
    lab.skeleton = anim.newSkeletonFromModel(lab.source);
    foreach (name in labActions) {
        local index = -1;
        for (local i=0; i<lab.source.getAnimationCount(); ++i)
            if (lab.source.getAnimationName(i) == name) index = i;
        if (index < 0) throw "Missing authored animation: " + name;
        lab.clips.push(anim.newClipFromModel(lab.source, lab.skeleton, index));
    }
    addLabActor(lab.source, "SOURCE / Mannequin", -2.5, true);
    addLabActor(model3d.newModelDataFromFile("assets/quaternius/Superhero_Female_FullBody.gltf"),
        "TARGET / Female", 0.0, false);
    addLabActor(model3d.newModelDataFromFile("assets/quaternius/Superhero_Male_FullBody.gltf"),
        "TARGET / Male", 2.5, false);
    lab.ground = eve.Renderable3D(); lab.ground.setMesh(gfx.newMeshCube(1.0));
    lab.ground.setPosition(0.0,-0.10,0.0); lab.ground.setScale(10.0,0.16,5.0);
    lab.ground.setTint(0.13,0.17,0.21,1.0); lab.ground.setRoughness(0.92);
    lab.ground.setReceiveShadow(true); lab.ground.setReceiveLight(true); lab.ground.setCastShadow(false);
    lab.camera = eve.Camera3D(); labCamera(); lab.camera.setUp(0.0,1.0,0.0);
    lab.camera.setFov(26.0); lab.camera.setAmbient(0.38,0.40,0.46); lab.camera.setActive(true);
    gfx.setDirectionalLight(-0.4, 1.0, 0.7, 1.60, 1.49, 1.34);
    local key = eve.Light3D(); key.setType("dir"); key.setDirection(-0.4,1.0,0.7);
    key.setColor(1.0,0.93,0.84,1.6); key.setCastShadow(true); key.setShadowStrength(0.85);
    lab.lights.push(key);
    local fill = eve.Light3D(); fill.setType("point");
    fill.setPosition(2.2, 2.6, 3.4); fill.setRadius(12.0);
    fill.setColor(0.55, 0.68, 0.92, 1.15); lab.lights.push(fill);
    gfx.setBackgroundColor(0.045,0.065,0.09,1.0);
    ui.setScale(1.0); ui.setTheme("dark"); ui.beginBuild(); ui.beginWindow("Character Motion Lab", "root");
    ui.text("CHARACTER MOTION LAB   /   Quaternius CC0", "title");
    ui.text("One authored performance, three characters. RMB drag to orbit.", "help");
    ui.beginRow("actions", 6.0);
    foreach (i,name in labActions) ui.button(name, "action-" + i);
    ui.end(); ui.beginRow("transport", 12.0);
    ui.button("Pause / Play", "pause"); ui.button("Restart", "restart");
    ui.checkbox("Smooth edges (2x SSAA)", lab.aaEnabled, "antialiasing");
    ui.end();
    ui.slider("Playback speed", 1.0, 0.1, 1.5, "speed");
    ui.slider("Cycle phase (scrub to pause)", 0.0, 0.0, 1.0, "phase");
    ui.text("", "status");
    ui.viewport("stage", config.width.tofloat()-36.0, config.height.tofloat()-330.0);
    ui.beginRow("actors", 32.0);
    foreach (i,a in lab.actors) ui.text(a.name + "  |  " + a.coverage, "actor-"+i);
    ui.end();
    ui.text("Retarget: bind-space rotation + proportional pelvis motion. Unmapped model nodes and leaf tips retain bind pose.", "mapping");
    ui.end(); ui.mountBuildAs("lab"); ui.select("lab");
    ui.setHostPos(0.0,0.0,0.0,0.0); ui.setHostSize(config.width.tofloat(),config.height.tofloat());
    ui.setHostOverlay(false);
    foreach (a in lab.actors) a.player.play(a.clips[0]);
};

eve_update = function(dt) {
    ui.select("lab"); local click = ui.consumeClick();
    while (click != "") {
        local slash = click.find("/"); local id = slash == null ? click : click.slice(slash+1);
        if (id == "pause") {
            lab.paused = !lab.paused;
            foreach (a in lab.actors) { if (lab.paused) a.player.pause(); else a.player["resume"](); }
        } else if (id == "restart") {
            foreach (a in lab.actors) {
                a.player.play(a.clips[lab.selected]);
                if (lab.paused) a.player.pause();
            }
        } else if (id.find("action-") == 0) selectLabAction(id.slice(7).tointeger());
        click = ui.consumeClick();
    }
    local change = ui.consumeChange();
    while (change != "") {
        if (change == "lab/antialiasing") {
            lab.aaEnabled = ui.getChecked("antialiasing");
        } else if (change == "lab/speed") {
            lab.speed = ui.getValue("speed"); foreach (a in lab.actors) a.player.setSpeed(lab.speed);
        } else if (change == "lab/phase") {
            lab.paused = true;
            foreach (a in lab.actors) {
                a.player.play(a.clips[lab.selected]);
                a.player.setTime(ui.getValue("phase") * a.clips[lab.selected].getDuration()); a.player.pause();
            }
        }
        change = ui.consumeChange();
    }
    local down = ui.viewportHovered("stage") && mouse.isDown(3);
    local mx = ui.viewportMouseX("stage");
    if (down && lab.dragging) { lab.yaw += (mx-lab.mouseX)*0.008; labCamera(); }
    lab.dragging = down; lab.mouseX = mx;
    foreach (a in lab.actors) {
        a.player.update(dt); local pose = a.player.getPose(); pose.computeWorld(a.skeleton);
        foreach (i,skin in a.skins)
            if (!skin.applyToMesh(gfx,a.parts[i].getMesh(),pose)) throw "Skin upload failed: " + a.name;
    }
    local player = lab.actors[0].player;
    local cycleTime = player.getTime() % lab.clips[lab.selected].getDuration();
    ui.setText("status", format("%s  |  %s  |  %.2f s  |  %.2fx", labActions[lab.selected],
        lab.paused ? "PAUSED" : "PLAYING", cycleTime, lab.speed));
    ui.setValue("phase",cycleTime/lab.clips[lab.selected].getDuration());
};
eve_render = function() {
    gfx.clear(); ui.select("lab"); local canvas = ui.viewportCanvas("stage");
    if (canvas != null) {
        if (lab.aaEnabled) {
            if (lab.aa == null) {
                lab.aa = gfx.newAntiAliasing();
                lab.aa.setMode("ssaa"); lab.aa.setQuality("medium");
            }
            // Use the actual viewport pixel size, including DPI/layout changes.
            // Retain only our source; the UI owns the borrowed destination canvas.
            local width = lab.aa.resolutionFor(canvas.getWidth());
            local height = lab.aa.resolutionFor(canvas.getHeight());
            if (lab.sceneCanvas == null || lab.sceneWidth != width || lab.sceneHeight != height) {
                lab.sceneCanvas = gfx.newCanvas(width, height);
                lab.sceneWidth = width; lab.sceneHeight = height;
            }
            gfx.renderScene3DToCanvas(lab.sceneCanvas,lab.camera);
            lab.aa.applyCanvasTo(gfx,lab.sceneCanvas,canvas);
        } else {
            gfx.renderScene3DToCanvas(canvas,lab.camera);
        }
    }
    ui.beginFrameAndRender();
};
