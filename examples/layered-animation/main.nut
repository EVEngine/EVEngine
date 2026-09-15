// Real KayKit clips: show three sources independently, then the layered mix.
//   1) Walking_A
//   2) Melee_1H_Attack_Chop  (upper override source)
//   3) Hit_A                 (additive source)
//   4) AnimLayerMixer = walk base + spine-masked attack + additive hit
//
// Bootstrap assets with: ./fetch_assets.sh

persist anim = null
persist model3d = null
persist skeleton = null
persist knight = null
persist knightTexture = null
persist walkClip = null
persist attackClip = null
persist hitClip = null
persist upperMask = null
persist mixer = null
persist basePlayer = null
persist upperPlayer = null
persist hitPlayer = null
persist slots = []
persist camera = null
persist ground = null
persist lights = []
persist hudFont = null
persist frameCount = 0
persist screenshotSaved = false
persist hud = {
    upperWeight = 1.0,
    hitWeight = 0.55,
    upperEnabled = true,
    additiveRef = "bind",
    time = 0.0,
}

function findAnimation(model, name) {
    for (local i = 0; i < model.getAnimationCount(); ++i)
        if (model.getAnimationName(i) == name) return i;
    throw "animation not found: " + name;
}

function loadClip(path, name, looped) {
    local model = model3d.newModelDataFromFile(path);
    local clip = anim.newClipFromModel(model, skeleton, findAnimation(model, name));
    clip.setLoop(looped);
    return clip;
}

function configureMaterial(part) {
    part.setTint(1.0, 1.0, 1.0, 1.0);
    if (knightTexture != null) part.setTexture(knightTexture);
    part.setReceiveLight(true);
    part.setReceiveShadow(true);
    part.setCastShadow(true);
    local material = part.getMaterial();
    if (material != null) {
        material.setShadingModel("pbr");
        if (knightTexture != null) material.setAlbedoTexture(knightTexture);
        material.setTint(1.0, 1.0, 1.0, 1.0);
        material.setMetallic(0.0);
        material.setRoughness(0.72);
        material.setReceiveLight(true);
        material.setReceiveShadow(true);
    }
}

function spawnVisuals(x) {
    local parts = [];
    local skins = [];
    for (local mesh = 0; mesh < knight.getMeshCount(); ++mesh) {
        local part = model3d.createRenderable(gfx, knight, mesh);
        part.setPosition(x, 0.02, 0.0);
        part.setYaw(-0.20);
        configureMaterial(part);
        parts.push(part);
        if (knight.hasBones(mesh))
            skins.push({ skin = anim.newSkinFromModel(knight, mesh, skeleton), part = part });
    }
    if (parts.len() == 0) throw "Knight.glb has no meshes";
    return { parts = parts, skins = skins };
}

function makePedestal(x, r, g, b) {
    local ped = eve.Renderable3D();
    ped.setMesh(gfx.newMeshCube(1.0));
    ped.setPosition(x, -0.06, 0.0);
    ped.setScale(1.35, 0.10, 1.35);
    ped.setTint(r, g, b, 1.0);
    ped.setRoughness(0.9);
    ped.setReceiveShadow(true);
    ped.setCastShadow(false);
    return ped;
}

function makeSoloSlot(label, x, clip, tint) {
    local visuals = spawnVisuals(x);
    local player = anim.newPlayer(skeleton);
    player.setLoop(true);
    player.play(clip);
    return {
        label = label,
        kind = "solo",
        player = player,
        parts = visuals.parts,
        skins = visuals.skins,
        pedestal = makePedestal(x, tint[0], tint[1], tint[2]),
        x = x,
    };
}

function makeLayeredSlot(label, x, tint) {
    local visuals = spawnVisuals(x);

    basePlayer = anim.newPlayer(skeleton);
    upperPlayer = anim.newPlayer(skeleton);
    hitPlayer = anim.newPlayer(skeleton);
    basePlayer.setLoop(true);
    upperPlayer.setLoop(true);
    hitPlayer.setLoop(true);
    basePlayer.play(walkClip);
    upperPlayer.play(attackClip);
    hitPlayer.play(hitClip);

    upperMask = anim.newBoneMask(skeleton);
    upperMask.setAll(0.0);
    if (!upperMask.setBoneAndChildren("spine", 1.0))
        throw "KayKit skeleton missing spine bone for upper mask";

    mixer = anim.newLayerMixer(skeleton);
    mixer.setBasePlayer(basePlayer);
    mixer.addLayer("upper", upperPlayer, upperMask, "override");
    mixer.addLayer("hit", hitPlayer, upperMask, "additive");
    mixer.setLayerAdditiveReference("hit", hud.additiveRef);
    mixer.setLayerWeight("upper", hud.upperWeight);
    mixer.setLayerWeight("hit", hud.hitWeight);
    mixer.setLayerEnabled("upper", hud.upperEnabled);

    return {
        label = label,
        kind = "mixer",
        mixer = mixer,
        parts = visuals.parts,
        skins = visuals.skins,
        pedestal = makePedestal(x, tint[0], tint[1], tint[2]),
        x = x,
    };
}

function applyPose(slot, pose) {
    pose.computeWorld(skeleton);
    foreach (binding in slot.skins)
        binding.skin.applyToMesh(gfx, binding.part.getMesh(), pose);
}

function updateSlot(slot, dt) {
    if (slot.kind == "solo") {
        slot.player.update(dt);
        applyPose(slot, slot.player.getPose());
        return;
    }
    local cycle = hud.time - floor(hud.time / 1.2) * 1.2;
    local pulse = cycle < 0.6 ? (cycle / 0.6) : (1.0 - (cycle - 0.6) / 0.6);
    mixer.setLayerWeight("hit", hud.hitWeight * pulse);
    mixer.update(dt);
    applyPose(slot, mixer.getPose());
}

function buildScene() {
    if (anim == null) anim = eve.Animation();
    if (model3d == null) model3d = eve.Model3D();

    knight = model3d.newModelDataFromFile("assets/kaykit/Knight.glb");
    knightTexture = gfx.newTextureFromFile("assets/kaykit/knight_texture.png");
    skeleton = anim.newSkeletonFromModel(knight);

    walkClip = loadClip("assets/kaykit/Rig_Medium_MovementBasic.glb", "Walking_A", true);
    attackClip = loadClip("assets/kaykit/Rig_Medium_CombatMelee.glb", "Melee_1H_Attack_Chop", true);
    hitClip = loadClip("assets/kaykit/Rig_Medium_General.glb", "Hit_A", true);

    slots = [
        makeSoloSlot("1 Walk", -4.5, walkClip, [0.25, 0.55, 0.95]),
        makeSoloSlot("2 Attack", -1.5, attackClip, [0.95, 0.55, 0.20]),
        makeSoloSlot("3 Hit", 1.5, hitClip, [0.90, 0.30, 0.45]),
        makeLayeredSlot("4 Layered mix", 4.5, [0.25, 0.85, 0.45]),
    ];

    ground = eve.Renderable3D();
    ground.setMesh(gfx.newMeshCube(1.0));
    ground.setPosition(0.0, -0.14, 0.0);
    ground.setScale(14.0, 0.12, 5.0);
    ground.setTint(0.10, 0.13, 0.17, 1.0);
    ground.setRoughness(0.92);
    ground.setReceiveShadow(true);
    ground.setCastShadow(false);

    camera = eve.Camera3D();
    camera.setEye(0.0, 3.4, 9.5);
    camera.setTarget(0.0, 1.0, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(36.0);
    camera.setAmbient(0.30, 0.33, 0.40);
    camera.setActive(true);

    gfx.setDirectionalLight(-0.40, 1.0, 0.55, 1.55, 1.42, 1.28);
    local key = eve.Light3D();
    key.setType("dir");
    key.setDirection(-0.40, 1.0, 0.55);
    key.setColor(1.0, 0.93, 0.84, 1.55);
    key.setCastShadow(true);
    lights.push(key);
    local fill = eve.Light3D();
    fill.setType("point");
    fill.setPosition(0.0, 3.2, 4.0);
    fill.setRadius(14.0);
    fill.setColor(0.45, 0.62, 0.95, 1.1);
    lights.push(fill);
}

function ensureFont() {
    if (hudFont != null) return;
    local data = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 22);
    hudFont = gfx.newFont(data,
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]()/|-:+',.!?");
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.07, 0.11, 1.0);
    buildScene();
    ensureFont();
    foreach (slot in slots) updateSlot(slot, 0.0);
    print("layered-animation: Walk | Attack | Hit | Layered mix\n");
};

eve_update = function(dt) {
    hud.time += dt;
    frameCount += 1;
    foreach (slot in slots) updateSlot(slot, dt);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ensureFont();
    gfx.setFont(hudFont);
    gfx.drawSolidRect(18.0, 16.0, 980.0, 96.0, 0.02, 0.035, 0.06, 0.88);
    gfx.print("LAYERED ANIMATION  |  KayKit CC0  |  3 real clips + final mix",
              32.0, 28.0, 0.92, 0.95, 1.0, 1.0, 0.70);
    gfx.print("1 Walk    2 Attack (upper)    3 Hit (additive)    4 Layered = walk + masked attack + hit",
              32.0, 56.0, 0.65, 0.78, 0.95, 1.0, 0.52);
    gfx.print("toggle_upper()   set_upper_weight(w)   cycle_hit_reference()",
              32.0, 82.0, 0.50, 0.62, 0.75, 1.0, 0.46);
    if (!screenshotSaved && frameCount > 45 && gfx.saveFramePng("layered-animation.png")) {
        screenshotSaved = true;
        print("layered-animation: screenshot saved\n");
    }
};

toggle_upper <- function() {
    hud.upperEnabled = !hud.upperEnabled;
    mixer.setLayerEnabled("upper", hud.upperEnabled);
    return hud.upperEnabled;
};

set_upper_weight <- function(w) {
    hud.upperWeight = w;
    mixer.setLayerWeight("upper", w);
};

cycle_hit_reference <- function() {
    hud.additiveRef = (hud.additiveRef == "bind") ? "identity" : "bind";
    mixer.setLayerAdditiveReference("hit", hud.additiveRef);
    return hud.additiveRef;
};
