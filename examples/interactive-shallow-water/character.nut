// Quaternius CC0 model and authored clips reused from character-motion-lab.
// Owners live together on the game thread; only computeWorld-owned values are read.
persist wader={source=null,model=null,sourceSkeleton=null,skeleton=null,profile=null,
    player=null,clips=[],parts=[],skins=[],textures=[],feet=[],ankles=[],knees=[],previous={},collisions=true,footHeights=[0.0,0.0],
    contact=[false,false],walking=false,yaw=0.0,phase=0.0,footfalls=0}
function pondRetargetProfile(target) {
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
        if (wader.sourceSkeleton.findBone(pair[0]) < 0 || target.findBone(pair[1]) < 0)
            throw "Missing humanoid mapping: " + pair[0] + " -> " + pair[1];
        p.addBoneMapping(pair[0], pair[1]);
    }
    return p;
}


function pondCharacterInit() {
    if(anim==null) anim=eve.Animation();if(model3d==null) model3d=eve.Model3D();
    wader.source=model3d.newModelDataFromFile("assets/character/AnimationLibrary.glb");
    wader.model=model3d.newModelDataFromFile("assets/character/Superhero_Male_FullBody.gltf");
    wader.sourceSkeleton=anim.newSkeletonFromModel(wader.source);
    wader.skeleton=anim.newSkeletonFromModel(wader.model);wader.profile=pondRetargetProfile(wader.skeleton);
    foreach(name in ["Idle_Loop","Walk_Loop"]) {
        local index=-1;for(local i=0;i<wader.source.getAnimationCount();++i)
            if(wader.source.getAnimationName(i)==name) index=i;
        if(index<0) throw "Missing wader clip: "+name;
        local clip=anim.newClipFromModel(wader.source,wader.sourceSkeleton,index);
        local target=clip.retargetWithProfile(wader.sourceSkeleton,wader.skeleton,wader.profile);
        target.setLoop(true);wader.clips.append(target);
    }
    foreach(name in ["ball_l","ball_r"]) {
        local bone=wader.skeleton.findBone(name);if(bone<0) throw "Missing wader foot: "+name;
        wader.feet.append(bone);
    }
    foreach(side in ["l","r"]) {
        wader.ankles.append(wader.skeleton.findBone("foot_"+side));wader.knees.append(wader.skeleton.findBone("calf_"+side));
        if(wader.ankles.top()<0 || wader.knees.top()<0) throw "Missing leg bones";
    }
    for(local i=0;i<wader.model.getMeshCount();++i) {
        if(!wader.model.hasBones(i)) continue;
        local part=model3d.createRenderable(gfx,wader.model,i),texture=null;
        local path=wader.model.getMaterialTexturePath(wader.model.getMaterialIndex(i),"base_color",0);
        if(path!="") {texture=gfx.newTextureFromFile("assets/character/"+path);wader.textures.append(texture);}
        local surface=gfx.newMaterial();surface.setShadingModel("pbr");surface.setMetallic(0.0);
        surface.setRoughness(0.66);surface.setReceiveLight(true);surface.setReceiveShadow(true);surface.setCastShadow(true);
        surface.setTint(1.0,1.0,1.0,1.0);if(texture!=null) {surface.setAlbedoTexture(texture);part.setTexture(texture);}
        part.setMaterial(surface);part.setTint(1.0,1.0,1.0,1.0);part.setCamera(cam);
        part.setReceiveLight(true);part.setReceiveShadow(true);part.setCastShadow(true);
        local skin=anim.newSkinFromModel(wader.model,i,wader.skeleton);
        if(!skin.bindGpuMesh(gfx,part.getMesh())) throw "Wader GPU skin binding failed";
        wader.parts.append(part);wader.skins.append(skin);
    }
    if(wader.parts.len()==0) throw "Wader has no skinned mesh";
    wader.player=anim.newPlayer(wader.skeleton);wader.player.play(wader.clips[0]);
}
function pondCharacterPose(dt) {
    local speed=sqrt(characterVx*characterVx+characterVz*characterVz),moving=speed>0.05;
    if(moving!=wader.walking) {wader.player.crossFade(wader.clips[moving?1:0],0.18);wader.walking=moving;}
    wader.player.setSpeed(moving?sceneClamp(speed/0.85,0.6,1.3):1.0);
    wader.player.update(dt);local pose=wader.player.getPose();pose.computeWorld(wader.skeleton);
    if(moving) {
        local turn=atan2(characterVx,characterVz)-wader.yaw;
        while(turn>3.14159265) turn-=6.2831853;while(turn< -3.14159265) turn+=6.2831853;
        wader.yaw+=sceneClamp(turn,-4.0*dt,4.0*dt);
    }
    local cy=cos(wader.yaw),sy=sin(wader.yaw),ground=pondBed(characterX,characterZ);
    foreach(i,part in wader.parts) {
        if(!wader.skins[i].updateGpuMesh(part.getMesh(),pose)) throw "Wader skin upload failed";
        part.setPosition(characterX,ground,characterZ);part.setYaw(wader.yaw);
    }
    foreach(i,bone in wader.feet) {
        local toe=pondBoneWorld(pose,bone,cy,sy,ground),ankle=pondBoneWorld(pose,wader.ankles[i],cy,sy,ground);
        local knee=pondBoneWorld(pose,wader.knees[i],cy,sy,ground);
        local py=toe[1]-ground;
        if(py>0.15) wader.contact[i]=false;
        if(moving && py<wader.footHeights[i] && py<0.105 && !wader.contact[i]) {
            wader.contact[i]=true;++footfallCount;
            if(wader.collisions && dt>0.0) {
                local impact=sceneClamp((wader.footHeights[i]-py)/dt,0.0,1.0);
                requireOk(sim.disturbAt(toe[0],toe[2],0.23,0.004*impact,0.025*impact),"gentle foot contact ring");
            }
        }
        wader.footHeights[i]=py;
        local shin=[(ankle[0]+knee[0])*0.5,(ankle[1]+knee[1])*0.5,(ankle[2]+knee[2])*0.5];
        local height=knee[1]-ankle[1];if(height<0.0) height=-height;
        local half=height*0.5+0.06;if(half<0.13) half=0.13;
        local speed=pondBoneCollider(i*3+1,"capsule",shin,0.10,half,0.10,dt);
        pondBoneCollider(i*3+2,"sphere",[ankle[0],ankle[1]-0.035,ankle[2]],0.11,0.11,0.11,dt);
        pondBoneCollider(i*3+3,"sphere",[toe[0],toe[1]-0.015,toe[2]],0.11,0.11,0.11,dt);
        if(i==0) pondPhysics.leftSpeed=speed;else pondPhysics.rightSpeed=speed;
    }
}
function pondBoneWorld(pose,bone,cy,sy,ground) {
    local x=pose.getWorldPositionX(bone),y=pose.getWorldPositionY(bone),z=pose.getWorldPositionZ(bone);
    return [characterX+x*cy+z*sy,ground+y,characterZ-x*sy+z*cy];
}
function pondBoneCollider(label,shape,position,rx,ry,rz,dt) {
    local velocity=[0.0,0.0,0.0];
    if(label in wader.previous && dt>0.0) for(local axis=0;axis<3;++axis) velocity[axis]=(position[axis]-wader.previous[label][axis])/dt;
    wader.previous[label]<-position;
    if(wader.collisions) pondCollider(label,shape,position[0],position[1],position[2],rx,ry,rz,velocity[0],velocity[1],velocity[2]);
    return sqrt(velocity[0]*velocity[0]+velocity[1]*velocity[1]+velocity[2]*velocity[2]);
}
