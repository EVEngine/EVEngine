// Developer verification only; gameplay controls remain main.nut + real UI events.
local poses=0, vertices=0, rotationChecks=0, moving=0;
local pairs=[["DEF-hips","IW_pelvis"],["DEF-head","IW_head"]];
foreach (s in ["L","R"])
    foreach (p in [["upper_arm","upperarm"],["forearm","forearm"],["thigh","thigh"],["shin","calf"]])
        pairs.push(["DEF-"+p[0]+"."+s,"IW_"+p[1]+"."+s]);
local source=ward.actors[0], target=ward.actors[1];
if (target.profile.getMatchedBoneCount()!=23) throw "Incomplete retarget profile";
foreach (action,name in wardActions) {
    local first=null;
    foreach (phase in [0.0,0.2,0.4,0.6,0.8]) {
        foreach (a in ward.actors) {
            local clip=a.clips[action];
            if (clip.getDuration()<=0.0) throw "Empty animation: "+name;
            a.player.play(clip); a.player.setTime(phase*clip.getDuration());
            local pose=a.player.getPose(); pose.computeWorld(a.rig);
            foreach (skin in a.skins) {
                if (!skin.updateSkinnedPositions(pose)) throw "CPU skin sample failed";
                // Bounded VM/native calls: the offline checker covers every vertex
                // and influence; sample each material mesh throughout all poses here.
                local stride=skin.getVertexCount()>128 ? skin.getVertexCount()/128 : 1;
                for (local v=0;v<skin.getVertexCount();v+=stride) {
                    local x=skin.getSkinnedPositionX(v),y=skin.getSkinnedPositionY(v),z=skin.getSkinnedPositionZ(v);
                    if (!(x>=-5.0 && x<=5.0 && y>=-3.0 && y<=5.0 && z>=-5.0 && z<=5.0))
                        throw "Nonfinite or exploded vertex: "+name+" / "+a.name;
                    vertices++;
                }
            }
            poses++;
        }
        local sp=source.player.getPose(),tp=target.player.getPose();
        foreach (p in pairs) {
            local s=source.rig.findBone(p[0]),t=target.rig.findBone(p[1]);
            local dot=sp.getWorldRotationX(s)*tp.getWorldRotationX(t)+sp.getWorldRotationY(s)*tp.getWorldRotationY(t)+
                sp.getWorldRotationZ(s)*tp.getWorldRotationZ(t)+sp.getWorldRotationW(s)*tp.getWorldRotationW(t);
            if (fabs(dot)<0.999) throw "Rest-axis mismatch: "+name+" / "+p[1]+" dot="+dot;
            rotationChecks++;
        }
        local hand=target.rig.findBone("IW_hand.L");
        local x=tp.getWorldPositionX(hand),y=tp.getWorldPositionY(hand),z=tp.getWorldPositionZ(hand);
        if (first==null) first=[x,y,z];
        else if (fabs(x-first[0])+fabs(y-first[1])+fabs(z-first[2])>0.001) moving++;
    }
}
if (poses!=80 || rotationChecks!=400 || moving<20) throw "Incomplete or static motion matrix";
foreach (a in ward.actors) {
    a.player.play(a.clips[ward.selected]); a.player.setSpeed(ward.speed);
    if (ward.paused) a.player.pause();
}
wardSkin();
print("IRONWARDEN_SMOKE_PASS poses="+poses+" vertexSamples="+vertices+" rotations="+rotationChecks+" moving="+moving+"\n");
