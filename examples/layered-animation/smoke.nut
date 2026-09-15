// Smoke: build the mixer, advance a few frames, and assert layered pose motion.
if (anim == null) anim = eve.Animation();
if (mixer == null) {
    // eve_init may not have run under eve_run_script; build on demand.
    dofile("main.nut");
    eve_init();
}
local samples = 0;
for (local i = 0; i < 8; ++i) {
    eve_update(1.0 / 30.0);
    local pose = mixer.getPose();
    pose.computeWorld(sk);
    if (pose.getBoneCount() != 3) throw "unexpected bone count";
    if (!isfinite(pose.getWorldPositionY(1))) throw "non-finite torso";
    samples += 1;
}
if (samples != 8) throw "smoke samples mismatch";
if (mixer.getLayerCount() != 2) throw "expected upper+recoil layers";
if (mixer.getLayerMode("recoil") != "additive") throw "recoil mode";
if (mixer.getLayerAdditiveReference("recoil") != "bind") throw "recoil reference";
print("layered-animation smoke ok\n");
