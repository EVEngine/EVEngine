// Deterministic real-asset smoke: 8 actions x 3 actors x 5 phases, CPU skinning.
// Run through eve_run_script after the lab has initialized.
local sampled = 0;
foreach (a in lab.actors) {
    if (a.profile != null && a.profile.getMatchedBoneCount() != 53)
        throw "Unexpected mapping coverage: " + a.name;
    foreach (clip in a.clips) {
        if (clip.getDuration() <= 0.0) throw "Empty clip";
        foreach (phase in [0.0, 0.2, 0.4, 0.6, 0.8]) {
            a.player.play(clip); a.player.setTime(phase * clip.getDuration());
            local pose = a.player.getPose(); pose.computeWorld(a.skeleton);
            foreach (skin in a.skins) {
                if (!skin.updateSkinnedPositions(pose)) throw "Skin sampling failed";
                for (local v=0; v<skin.getVertexCount(); ++v) {
                    local x=skin.getSkinnedPositionX(v), y=skin.getSkinnedPositionY(v), z=skin.getSkinnedPositionZ(v);
                    if (!(x>=-5.0 && x<=5.0 && y>=-2.0 && y<=5.0 && z>=-5.0 && z<=5.0))
                        throw "Nonfinite or exploded mesh: " + a.name + " / " + clip.getName();
                }
            }
            sampled++;
        }
    }
    a.player.play(a.clips[lab.selected]);
    if (lab.paused) a.player.pause();
}
if (sampled != 120) throw "Incomplete sample matrix";
print("character-motion-lab smoke PASS: 120 poses, all skinned vertices finite and bounded\n");
