// Smoke: load three KayKit clips, advance solos + layered mix, assert finite poses.
if (anim == null) anim = eve.Animation();
if (model3d == null) model3d = eve.Model3D();
if (slots == null || slots.len() == 0) {
    dofile("main.nut");
    eve_init();
}
if (slots.len() != 4) throw "expected 4 slots (3 solos + layered)";
if (mixer == null) throw "layered mixer missing";
if (mixer.getLayerCount() != 2) throw "expected upper+hit layers";
if (mixer.getLayerMode("hit") != "additive") throw "hit mode";
if (mixer.getLayerAdditiveReference("hit") != "bind") throw "hit reference";

local samples = 0;
for (local i = 0; i < 12; ++i) {
    eve_update(1.0 / 30.0);
    foreach (slot in slots) {
        local pose = slot.kind == "solo" ? slot.player.getPose() : mixer.getPose();
        pose.computeWorld(skeleton);
        if (pose.getBoneCount() < 8) throw "pose bone count too small";
        if (!isfinite(pose.getWorldPositionY(1))) throw "non-finite pose in " + slot.label;
    }
    samples += 1;
}
if (samples != 12) throw "smoke samples mismatch";
print("layered-animation smoke ok (3 clips + mix)\n");
