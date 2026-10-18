import { initAssets, shape, createBattlefieldModel, poseAsset } from "./assets.nut"
import { selection } from "./selection.nut"
local model=null,camera=null,t=0.0,chosen=null,restElevation=0.0,rearShown=false;
eve_init=function() {
    chosen=selection(); initAssets();
    if(!gfx.getRenderControl().setLightingMode("forwardPlus")) throw "ForwardPlus unavailable";
    gfx.setBackgroundColor(0.055,0.075,0.10,1.0);
    camera=eve.Camera3D(); camera.setEye(chosen.size*0.83,chosen.height+chosen.size*0.48,chosen.size*1.12);
    camera.setTarget(0.0,chosen.height,0.5); camera.setUp(0.0,1.0,0.0);
    if(chosen.view=="top") camera.setEye(chosen.size*0.25,chosen.height+chosen.size*0.95,chosen.size*0.85);
    if(chosen.view=="rear") camera.setEye(chosen.size*0.83,chosen.height+chosen.size*0.36,-chosen.size*1.12);
    camera.setFov(39.0); camera.setAmbient(0.40,0.46,0.53); camera.setActive(true);
    gfx.setDirectionalLight(-0.65,-1.0,-0.4,1.05,1.0,0.88);
    shape(0,-0.16,0,180,0.2,180,[0.10,0.13,0.16]);
    for(local i=-12;i<=12;i++) {
        shape(i*2,-0.049,0,0.016,0.005,48,[0.14,0.18,0.22],false,true);
        shape(0,-0.048,i*2,48,0.005,0.016,[0.14,0.18,0.22],false,true);
    }
    model=createBattlefieldModel(chosen.kind,0.0,chosen.air?2.1:0.0,0.0);
    model.turret=0.19; restElevation=model.elevation;
    poseAsset(model);
    print("INSPECTION READY: "+chosen.kind+"\n");
};
eve_update=function(dt) {
    t+=dt;
    // Stable inspection pose first; animate joints after the opening capture.
    if(t>3.0) {
        if(chosen.stationary&&!rearShown) {
            model.yaw=3.14159265; rearShown=true;
            print("INSPECTION REAR READY: "+chosen.kind+"\n");
        }
        model.turret=0.19+sin((t-3)*0.7)*0.35;
        model.elevation=restElevation+sin((t-3)*0.8)*0.10;
        model.rotor=(t-3)*13; model.recoil=0.20*exp(-((t-3)%3.0)*13);
    }
    poseAsset(model);
};
eve_render=function() { gfx.clear(); gfx.render3D(); };
