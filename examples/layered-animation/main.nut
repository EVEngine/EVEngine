// Layered animation demo: locomotion base + upper override + recoil additive.
// No external assets — builds a tiny procedural skeleton and draws a stick figure
// with gfx primitive lines that are rebuilt each frame from the mixed pose.

persist anim = null
persist sk = null
persist mixer = null
persist basePlayer = null
persist upperPlayer = null
persist recoilPlayer = null
persist upperMask = null
persist walkClip = null
persist aimClip = null
persist recoilClip = null
persist camera = null
persist boneLines = []
persist hud = {
    upperWeight = 1.0,
    recoilWeight = 0.0,
    upperEnabled = true,
    additiveRef = "bind",
    time = 0.0,
}

function clearBoneLines() {
    foreach (line in boneLines) {
        if (line != null && !line.isStale()) line.remove();
    }
    boneLines = [];
}

function makeClips() {
    walkClip = anim.newClip("walk");
    walkClip.setDuration(1.0);
    walkClip.setLoop(true);
    // root sways in Z, torso keeps bind height with a light bob.
    walkClip.addPositionKey(0, 0.0, 0.0, 0.0, 0.0);
    walkClip.addPositionKey(0, 0.5, 0.0, 0.0, 0.35);
    walkClip.addPositionKey(0, 1.0, 0.0, 0.0, 0.0);
    walkClip.addPositionKey(1, 0.0, 0.0, 1.0, 0.0);
    walkClip.addPositionKey(1, 0.5, 0.0, 1.06, 0.0);
    walkClip.addPositionKey(1, 1.0, 0.0, 1.0, 0.0);

    aimClip = anim.newClip("aim");
    aimClip.setDuration(1.0);
    aimClip.setLoop(true);
    // Absolute upper pose: lean torso forward on local Z.
    aimClip.addPositionKey(1, 0.0, 0.0, 1.0, 0.25);
    aimClip.addPositionKey(1, 1.0, 0.0, 1.0, 0.25);
    aimClip.addPositionKey(2, 0.0, 0.0, 0.9, 0.35);
    aimClip.addPositionKey(2, 1.0, 0.0, 0.9, 0.35);

    recoilClip = anim.newClip("recoil");
    recoilClip.setDuration(0.35);
    recoilClip.setLoop(true);
    // Absolute torso kick used with bind-pose additive reference.
    recoilClip.addPositionKey(1, 0.0, 0.0, 1.0, 0.0);
    recoilClip.addPositionKey(1, 0.08, 0.0, 1.15, -0.2);
    recoilClip.addPositionKey(1, 0.35, 0.0, 1.0, 0.0);
}

function buildMixer() {
    sk = anim.newSkeleton();
    local root = sk.addBone("root", -1);
    sk.setBindPosition(root, 0.0, 0.0, 0.0);
    local torso = sk.addBone("torso", root);
    sk.setBindPosition(torso, 0.0, 1.0, 0.0);
    local head = sk.addBone("head", torso);
    sk.setBindPosition(head, 0.0, 0.9, 0.0);

    makeClips();

    basePlayer = anim.newPlayer(sk);
    upperPlayer = anim.newPlayer(sk);
    recoilPlayer = anim.newPlayer(sk);
    basePlayer.play(walkClip);
    upperPlayer.play(aimClip);
    recoilPlayer.play(recoilClip);

    upperMask = anim.newBoneMask(sk);
    upperMask.setBoneAndChildren("torso", 1.0);

    mixer = anim.newLayerMixer(sk);
    mixer.setBasePlayer(basePlayer);
    mixer.addLayer("upper", upperPlayer, upperMask, "override");
    mixer.addLayer("recoil", recoilPlayer, upperMask, "additive");
    mixer.setLayerAdditiveReference("recoil", "bind");
    mixer.setLayerWeight("upper", hud.upperWeight);
    mixer.setLayerWeight("recoil", hud.recoilWeight);
    mixer.setLayerEnabled("upper", hud.upperEnabled);
}

function drawStickFigure(pose) {
    clearBoneLines();
    pose.computeWorld(sk);
    local colors = [
        [0.35, 0.75, 1.0],
        [0.95, 0.75, 0.25],
        [0.95, 0.45, 0.55],
    ];
    for (local bone = 1; bone < sk.getBoneCount(); ++bone) {
        local parent = sk.getParent(bone);
        local c = colors[bone % colors.len()];
        local line = gfx.newPrimitiveLine3D(
            pose.getWorldPositionX(parent), pose.getWorldPositionY(parent), pose.getWorldPositionZ(parent),
            pose.getWorldPositionX(bone), pose.getWorldPositionY(bone), pose.getWorldPositionZ(bone),
            c[0], c[1], c[2], 1.0, 5.0);
        if (!line.ok) throw "bone line: " + line.status.summary;
        line.value.setDepthMode("ignore");
        boneLines.push(line.value);
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.07, 0.08, 0.12, 1.0);
    if (anim == null) anim = eve.Animation();
    if (camera == null) {
        camera = eve.Camera3D();
        camera.setEye(2.8, 1.6, 4.2);
        camera.setTarget(0.0, 1.1, 0.0);
        camera.setFov(50.0);
        camera.setAmbient(0.35, 0.38, 0.45);
    }
    if (mixer == null) buildMixer();
};

eve_update = function(dt) {
    hud.time += dt;
    // Triangle pulse in [0,1] without relying on sin/cos bindings.
    local cycle = hud.time - floor(hud.time);
    hud.recoilWeight = cycle < 0.5 ? (cycle * 2.0) : (2.0 - cycle * 2.0);
    mixer.setLayerWeight("recoil", hud.recoilWeight);
    mixer.update(dt);
    drawStickFigure(mixer.getPose());
};

eve_render = function() {
    gfx.clear();
};

// Manual toggles for interactive demos / MCP scripts.
toggle_upper <- function() {
    hud.upperEnabled = !hud.upperEnabled;
    mixer.setLayerEnabled("upper", hud.upperEnabled);
    return hud.upperEnabled;
};

set_upper_weight <- function(w) {
    hud.upperWeight = w;
    mixer.setLayerWeight("upper", w);
};

cycle_recoil_reference <- function() {
    hud.additiveRef = (hud.additiveRef == "bind") ? "identity" : "bind";
    mixer.setLayerAdditiveReference("recoil", hud.additiveRef);
    return hud.additiveRef;
};
