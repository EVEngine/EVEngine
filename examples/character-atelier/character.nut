// Downloaded mesh + authored morphs + added skin joints. Resources share this owner.
persist atelierCharacter = {model=null,skeleton=null,pose=null,parts=[],skins=[],materials=[],
    materialNames=[],bones={},bind=[],style=null,shader=null,offsetY=0.0,landmarks=[],time=0.0};
function arBone(name) { return atelierCharacter.bones[name]; }
function arQuatMul(a,b) {
    return [a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],
        a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],
        a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],
        a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]];
}
function arRotate(semantic,x,y,z) {
    if(!(semantic in atelierRigData.humanoid)) return;
    local i=arBone(atelierRigData.humanoid[semantic]),b=atelierCharacter.bind[i];
    local q=arQuatMul([sin(x*0.5),0.0,0.0,cos(x*0.5)],
        arQuatMul([0.0,sin(y*0.5),0.0,cos(y*0.5)],[0.0,0.0,sin(z*0.5),cos(z*0.5)]));
    q=arQuatMul(b.q,q);atelierCharacter.pose.setLocalRotation(i,q[0],q[1],q[2],q[3]);
}
function arScaleName(name,x,y,z) {
    local i=arBone(name),b=atelierCharacter.bind[i];atelierCharacter.pose.setLocalScale(i,b.s[0]*x,b.s[1]*y,b.s[2]*z);
}
function arScale(semantic,x,y,z) {arScaleName(atelierRigData.humanoid[semantic],x,y,z);}
function arMove(semantic,x,y,z) {
    local i=arBone(atelierRigData.humanoid[semantic]),b=atelierCharacter.bind[i];
    atelierCharacter.pose.setLocalPosition(i,b.p[0]+x,b.p[1]+y,b.p[2]+z);
}
function arInit() {
    atelierCharacter.model=model3d.newModelDataFromFile("assets/pixiv/female-rig.glb");
    atelierCharacter.skeleton=anim.newSkeletonFromModel(atelierCharacter.model);
    local sk=atelierCharacter.skeleton;
    atelierCharacter.pose=anim.newPose(sk.getBoneCount());
    for(local i=0;i<sk.getBoneCount();i++) {
        atelierCharacter.bones[sk.getBoneName(i)] <- i;
        atelierCharacter.bind.append({p=[sk.getBindPositionX(i),sk.getBindPositionY(i),sk.getBindPositionZ(i)],
            q=[sk.getBindRotationX(i),sk.getBindRotationY(i),sk.getBindRotationZ(i),sk.getBindRotationW(i)],
            s=[sk.getBindScaleX(i),sk.getBindScaleY(i),sk.getBindScaleZ(i)]});
    }
    atelierCharacter.style=eve.Stylize().newInstance("ysa");
    atelierCharacter.style.setFloat("ambientR",0.88);atelierCharacter.style.setFloat("ambientG",0.88);
    atelierCharacter.style.setFloat("ambientB",0.88);atelierCharacter.style.setFloat("shadowPower",0.12);
    atelierCharacter.style.setFloat("rimR",0.75);atelierCharacter.style.setFloat("rimG",0.84);atelierCharacter.style.setFloat("rimB",1.0);
    atelierCharacter.shader=atelierCharacter.style.newMeshShader(gfx);
    for(local i=0;i<atelierCharacter.model.getMeshCount();i++) {
        if(!atelierCharacter.model.hasBones(i)) throw "Unskinned primitive in the adapted character";
        local part=model3d.createRenderable(gfx,atelierCharacter.model,i);
        local imported=part.getMaterial(),surface=gfx.newMaterial();
        surface.setShader(atelierCharacter.shader);surface.setShadingModel("custom");
        local materialName=atelierCharacter.model.getMaterialName(atelierCharacter.model.getMaterialIndex(i));
        local info=atelierRigData.materials[materialName],texture=imported.getAlbedoTexture();
        if(texture==null || texture.getWidth()!=info.width || texture.getHeight()!=info.height)
            throw "Missing or mismatched original texture: "+materialName;
        surface.setAlbedoTexture(texture);surface.setTint(info.tint[0].tofloat(),info.tint[1].tofloat(),info.tint[2].tofloat(),info.tint[3].tofloat());
        surface.setDoubleSided(imported.getDoubleSided());surface.setReceiveLight(true);
        surface.setSurfaceMode(imported.getSurfaceMode());surface.setAlphaCutoff(imported.getAlphaCutoff());
        surface.setFloat("alphaClip",info.alphaMode=="MASK"?imported.getAlphaCutoff():0.0);
        atelierCharacter.materialNames.append(materialName);
        print("ATELIER_MATERIAL "+materialName+" "+texture.getWidth()+"x"+texture.getHeight()+" alpha="+info.alphaMode+"\n");
        part.setMaterial(surface);part.setTint(1.0,1.0,1.0,1.0);
        local skin=anim.newSkinFromModel(atelierCharacter.model,i,sk);
        if(!skin.bindGpuMesh(gfx,part.getMesh())) throw "GPU skin binding failed";
        atelierCharacter.parts.append(part);atelierCharacter.skins.append(skin);atelierCharacter.materials.append(surface);
    }
    foreach(c in atelierRigData.controls) if(!(c.bone in atelierCharacter.bones)) throw "Missing weighted facial joint: "+c.bone;
    print("ATELIER_RIG_READY joints="+sk.getBoneCount()+" facial="+atelierRigData.controls.len()+" renderer=ysa\n");
}
function arExpressions(values) {
    local weights={};
    foreach(name,e in atelierRigData.expressions) if(name in values) {
        local w=e.binary?(values[name]>0.5?1.0:0.0):values[name];
        foreach(b in e.binds) {if(!(b[0] in weights)) weights[b[0]] <- 0.0;weights[b[0]]+=b[1]*w;}
    }
    foreach(part in atelierCharacter.parts) {
        local mesh=part.getMesh();mesh.clearMorphWeights();
        foreach(name,w in weights) if(mesh.hasMorph(name)) mesh.setMorphWeight(name,acMin(1.0,w));
        if(mesh.isMorphDirty() && !gfx.bakeMeshMorph(mesh)) throw "Facial morph upload failed";
    }
}
function arPose(values,name,seconds) {
    local sk=atelierCharacter.skeleton,pose=atelierCharacter.pose;
    sk.applyBindPose(pose);
    arScaleName("Root",values.height,values.height,values.height);
    // Local shaping is compensated at the next trunk joint to preserve head/limb width.
    arScale("spine",values.waist,1.0,values.waist);
    arScale("chest",1.0/values.waist,1.0,1.0/values.waist);
    arScale("hips",values.hips,1.0,1.0);
    arScale("spine",values.waist/values.hips,1.0,values.waist);
    arScale("head",values.head,values.head,values.head);
    foreach(side in ["left","right"]) {
        local sign=side=="left"?1.0:-1.0;
        arScale(side+"UpperLeg",1.0/values.hips,values.legs,1.0);
        arMove(side+"Shoulder",sign*0.065*(values.shoulders-1.0),0.0,0.0);
        arScale(side+"UpperArm",values.arms,1.0,1.0);
        if(name!="tpose") arRotate(side+"UpperArm",0.0,0.0,-sign*1.18);
    }
    local motion=sin(seconds*3.2);
    if(name=="weight") {
        arRotate("hips",0.0,0.0,0.10);arRotate("spine",0.0,0.0,-0.10);
        arRotate("leftUpperLeg",0.0,0.0,-0.12);arRotate("rightLowerLeg",0.12,0.0,0.0);
        arRotate("head",0.0,0.0,-0.05);
    } else if(name=="hips") {
        arRotate("leftUpperArm",0.0,0.0,-0.65);arRotate("rightUpperArm",0.0,0.0,0.65);
        arRotate("leftLowerArm",0.0,-1.35,0.0);arRotate("rightLowerArm",0.0,1.35,0.0);
    } else if(name=="wave") {
        arRotate("rightUpperArm",0.0,-0.15,-0.35);arRotate("rightLowerArm",0.0,0.0,-1.25);
        arRotate("rightHand",0.0,0.20*motion,-0.15+0.20*motion);arRotate("head",0.0,0.0,0.05);
    } else if(name=="think") {
        arRotate("rightUpperArm",-0.65,0.0,0.55);arRotate("rightLowerArm",0.0,1.7,0.0);
        arRotate("head",0.07,0.10,-0.06);
    } else if(name=="idle") arRotate("chest",0.009*motion,0.0,0.0);
    foreach(c in atelierRigData.controls) {
        local i=arBone(c.bone),b=atelierCharacter.bind[i],prefix="face_"+c.id+"_";
        pose.setLocalPosition(i,b.p[0]+values[prefix+"x"],b.p[1]+values[prefix+"y"],b.p[2]+values[prefix+"z"]);
        pose.setLocalScale(i,values[prefix+"sx"],values[prefix+"sy"],1.0);
    }
    pose.computeWorld(sk);
    if(name=="hips" || name=="think") {
        local targets=[];
        if(name=="hips") {
            local h=arBone(atelierRigData.humanoid.hips);
            foreach(side in ["left","right"]) targets.append([side,
                pose.getWorldPositionX(h)+(side=="left"?1.0:-1.0)*0.15*values.height*values.hips,
                pose.getWorldPositionY(h)+0.015*values.height,
                pose.getWorldPositionZ(h)+0.10*values.height]);
        } else {
            local chin=arBone("Atelier_chin");
            targets.append(["right",pose.getWorldPositionX(chin)-0.035*values.height,
                pose.getWorldPositionY(chin)-0.045*values.height,pose.getWorldPositionZ(chin)+0.09*values.height]);
        }
        foreach(t in targets) if(!pose.solveTwoBoneIK(sk,arBone(atelierRigData.humanoid[t[0]+"UpperArm"]),
            arBone(atelierRigData.humanoid[t[0]+"LowerArm"]),arBone(atelierRigData.humanoid[t[0]+"Hand"]),t[1],t[2],t[3],1.0))
                throw "Pose arm IK failed";
    }
    local lf=arBone(atelierRigData.humanoid.leftFoot),rf=arBone(atelierRigData.humanoid.rightFoot);
    local restFoot=0.075;
    atelierCharacter.offsetY=restFoot-acMin(pose.getWorldPositionY(lf),pose.getWorldPositionY(rf));
    foreach(i,skin in atelierCharacter.skins) {
        if(!skin.updateGpuMesh(atelierCharacter.parts[i].getMesh(),pose)) throw "Skin palette update failed";
        atelierCharacter.parts[i].setPosition(0.0,atelierCharacter.offsetY,0.0);
    }
}
function arWorld(bone) {
    local p=atelierCharacter.pose,i=arBone(bone);
    return [p.getWorldPositionX(i),p.getWorldPositionY(i)+atelierCharacter.offsetY,p.getWorldPositionZ(i)];
}
