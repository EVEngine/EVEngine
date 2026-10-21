// Fixed architecture is compiled from scad/balcony.scad. All owners live in scene.
persist scene = { objects=[], models=[], plants=[], cats=[], lights=[], sky=null,
    camera=null, time=0.0, paused=false, orbit=0.0, rt=null, hwRT=false, reflection=true, natureMaterials=[], treeVariants=[], groundY=-3.0, clouds=null };

function checked(result, context) {
    if (!result.ok) throw context + ": " + ("error" in result ? result.error : result.diagnostics[0].message);
    return "value" in result ? result.value : null;
}
function keepMesh(mesh,x,y,z,sx,sy,sz,color,roughness=0.75,metallic=0.0) {
    local o=eve.Renderable3D(); o.setMesh(mesh);
    o.setPosition(x.tofloat(),y.tofloat(),z.tofloat()); o.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    o.setTint(color[0],color[1],color[2],1.0);
    o.setRoughness(roughness); o.setMetallic(metallic);
    o.setCastShadow(true); o.setReceiveShadow(true);
    scene.objects.push(o); return o;
}
function architecture(name,color,roughness,metallic=0.0) {
    local model=model3d.newModelDataFromFile("assets/architecture/"+name+".obj");
    scene.models.push(model);
    for(local i=0;i<model.getMeshCount();++i) {
        local o=model3d.createRenderable(gfx,model,i);
        o.setTint(color[0],color[1],color[2],name=="glass" ? 0.16 : 1.0);
        o.setRoughness(roughness); o.setMetallic(metallic);
        o.setCastShadow(name!="glass"); o.setReceiveShadow(true);
        local surface=gfx.newMaterial(); surface.setShadingModel("pbr");
        surface.setTint(color[0],color[1],color[2],1.0);
        surface.setRoughness(roughness); surface.setMetallic(metallic);
        surface.setCastShadow(name!="glass"); surface.setReceiveShadow(true); o.setMaterial(surface);
        if(name=="glass") {
            local m=gfx.newMaterial(); m.setShadingModel("pbr");
            m.setTint(color[0],color[1],color[2],0.16); m.setSurfaceMode("transparent"); m.setBlendMode("alpha");
            m.setDepthWrite(false); m.setRoughness(0.08); m.setMetallic(0.15); o.setMaterial(m);
        }
        scene.objects.push(o);
    }
}
function natureTexture(recipe,seed) {
    local p=checked(procgen.newParams(),"nature texture parameters");
    p.setSeed(seed); p.setSize(512,512); p.setFloat("scale",4.2);
    p.setInt("octaves",5); p.setInt("seamless",1); p.setInt("colors",12);
    return checked(procgen.generateTexture(recipe,p,gfx),"nature texture generation");
}
function treeMaterial(seed) {
    local atlas=natureTexture("tex.tree_atlas",seed);
    local mat=gfx.newMaterial(); mat.setAlbedoTexture(atlas);
    mat.setTint(1.0,1.0,1.0,1.0); mat.setRoughness(0.88);
    mat.setDoubleSided(true); mat.setSurfaceMode("masked");
    mat.setAlphaCutoff(0.32); mat.setAlphaTechnique("cutoff");
    mat.setCastShadow(true); mat.setReceiveShadow(true);
    scene.natureMaterials.push({texture=atlas,material=mat}); return mat;
}
function treeMesh(height,radius,seed) {
    local p=checked(procgen.newParams(),"plant parameters");
    p.setSeed(seed); p.setString("style","realistic");
    p.setString("branchAlgorithm","weberPenn"); p.setString("leafMode","clusters");
    p.setFloat("height",height); p.setFloat("crownRadius",radius);
    p.setFloat("trunkRadius",height*0.025); p.setFloat("leafSize",height*0.10);
    p.setFloat("foliageStart",0.28); p.setFloat("branchAngle",70.0);
    p.setInt("branchLevels",3); p.setInt("branchCount",9);
    p.setFloat("leafDensity",1.0); p.setFloat("clusterSize",0.38);
    p.setFloat("clusterSeparation",0.34); p.setFloat("clusterLeafScale",1.4);
    p.setInt("clusterPlanes",10); p.setInt("clusterLeaves",28); p.setInt("clusterLimit",120);
    return checked(procgen.generateMesh("mesh.tree",p,gfx),"plant generation");
}
function treeAt(x,y,z,height,radius,seed) {
    local o=keepMesh(treeMesh(height,radius,seed),x,y,z,1,1,1,[1.0,1.0,1.0]);
    o.setMaterial(treeMaterial(seed+3));
    scene.plants.push({object=o,x=x,y=y,z=z,phase=seed*0.1});
}
function gardenEnvironment() {
    // The balcony is one storey above a continuous garden, not a floating stage.
    local ground=keepMesh(gfx.newMeshCube(1.0),0.0,scene.groundY-0.35,-85.0,
        360.0,0.7,260.0,[0.14,0.22,0.09],0.97);
    local groundMaterial=gfx.newMaterial(); groundMaterial.setTint(0.17,0.25,0.10,1.0);
    groundMaterial.setRoughness(0.98); groundMaterial.setReceiveShadow(true); ground.setMaterial(groundMaterial);
    // A building facade and planted terrace edge anchor the slab in the garden.
    keepMesh(gfx.newMeshCube(1.0),-0.1,-1.65,0.1,8.05,3.0,5.85,[0.53,0.51,0.44]);
    local leafTexture=gfx.newTextureFromFile("assets/garden/leaf-spray.png");
    local leafMaterial=gfx.newMaterial(); leafMaterial.setAlbedoTexture(leafTexture);
    leafMaterial.setTint(0.68,0.78,0.60,1.0); leafMaterial.setRoughness(0.95);
    leafMaterial.setDoubleSided(true); leafMaterial.setSurfaceMode("masked"); leafMaterial.setAlphaCutoff(0.35);
    leafMaterial.setCastShadow(true); leafMaterial.setReceiveShadow(true);
    local barkTexture=natureTexture("tex.bark",904); local barkMaterial=gfx.newMaterial();
    barkMaterial.setAlbedoTexture(barkTexture); barkMaterial.setTint(0.46,0.40,0.32,1.0);
    barkMaterial.setRoughness(0.95); barkMaterial.setCastShadow(true); barkMaterial.setReceiveShadow(true);
    scene.natureMaterials.push({texture=leafTexture,material=leafMaterial});
    scene.natureMaterials.push({texture=barkTexture,material=barkMaterial});
    for(local i=0;i<3;++i) {
        local model=model3d.newModelDataFromFile("assets/garden/tree-"+i+".gltf"); scene.models.push(model);
        local parts=[];
        for(local j=0;j<model.getMeshCount();++j) {
            local source=model3d.createRenderable(gfx,model,j); source.setVisible(false); scene.objects.push(source);
            parts.push({mesh=source.getMesh(),material=model.getMaterialName(model.getMaterialIndex(j))=="leaves" ? leafMaterial : barkMaterial});
        }
        scene.treeVariants.push(parts);
    }
    for(local row=0;row<3;++row) {
        for(local i=0;i<9;++i) {
            local x=-30.0+i*7.3+sin(i*3.1+row)*1.7;
            local z=-12.0-row*12.0-cos(i*1.7)*2.0;
            local scale=0.92+0.10*sin(i*2.2+row);
            foreach(part in scene.treeVariants[(i+row)%3]) {
                local o=keepMesh(part.mesh,x,scene.groundY,z,scale,scale,scale,[1.0,1.0,1.0]);
                o.setMaterial(part.material); o.setYaw(i*1.72+row*0.6);
            }
        }
    }
}
function leafPlant(x,y,z,scale,seed,purple=false) {
    local leafMesh=gfx.newMeshSphere(7,4);
    for(local i=0;i<14;++i) {
        local a=i*2.39996+seed;
        local r=scale*(0.15+0.035*i);
        local h=scale*(0.35+0.025*i);
        local c=purple ? [0.34+0.01*i,0.09,0.19] : [0.22+0.009*i,0.38+0.012*i,0.12];
        local o=keepMesh(leafMesh,x+cos(a)*r,y+h,z+sin(a)*r,
            scale*0.13,scale*(0.3+0.014*i),scale*0.05,c);
        o.setRotation(a,0.3,0.35);
        scene.plants.push({object=o,x=x+cos(a)*r,y=y+h,z=z+sin(a)*r,phase=a});
    }
}
function addCat(path,clipName,targetHeight,x,y,z,walking) {
    local model=model3d.newModelDataFromFile(path);
    local sk=anim.newSkeletonFromModel(model);
    local clipIndex=-1;
    for(local i=0;i<model.getAnimationCount();++i)
        if(model.getAnimationName(i)==clipName) clipIndex=i;
    if(clipIndex<0) throw "Missing cat animation: "+clipName;
    local clip=anim.newClipFromModel(model,sk,clipIndex); clip.setLoop(true);
    local player=anim.newPlayer(sk); player.play(clip);
    local cat={model=model,skeleton=sk,clip=clip,player=player,parts=[],skins=[],
        scale=1.0,x=x,y=y,z=z,walking=walking,offsetX=0.0,offsetY=0.0,offsetZ=0.0};
    for(local i=0;i<model.getMeshCount();++i) {
        if(!model.hasBones(i)) continue;
        local o=model3d.createRenderable(gfx,model,i);
        // Preserve imported coat textures while using the standard lit shadow path.
        local imported=o.getMaterial(); local mat=gfx.newMaterial();
        mat.setShadingModel("pbr"); mat.setRoughness(0.82);
        local mi=model.getMaterialIndex(i);
        mat.setTint(model.getMaterialBaseColorR(mi),model.getMaterialBaseColorG(mi),model.getMaterialBaseColorB(mi),1.0);
        if(imported!=null && imported.getAlbedoTexture()!=null) mat.setAlbedoTexture(imported.getAlbedoTexture());
        mat.setCastShadow(true); mat.setReceiveShadow(true); mat.setReceiveLight(true);
        o.setMaterial(mat); o.setPosition(x,y,z);
        o.setCastShadow(true); o.setReceiveShadow(true);
        cat.parts.push(o); cat.skins.push(anim.newSkinFromModel(model,i,sk));
    }
    if(cat.parts.len()==0) throw "Cat contains no skinned meshes: "+path;
    // Measure the evaluated skin, including source root axes/scales, once at load.
    player.update(0.0); local initialPose=player.getPose(); initialPose.computeWorld(sk);
    local lo=[1e10,1e10,1e10]; local hi=[-1e10,-1e10,-1e10];
    foreach(skin in cat.skins) {
        if(!skin.updateSkinnedPositions(initialPose)) throw "Initial cat skin evaluation failed";
        for(local v=0;v<skin.getVertexCount();++v) {
            local p=[skin.getSkinnedPositionX(v),skin.getSkinnedPositionY(v),skin.getSkinnedPositionZ(v)];
            for(local k=0;k<3;++k) { if(p[k]<lo[k]) lo[k]=p[k]; if(p[k]>hi[k]) hi[k]=p[k]; }
        }
    }
    if(hi[1]-lo[1]<0.00001) throw "Invalid cat height: "+path;
    cat.scale=targetHeight/(hi[1]-lo[1]);
    cat.offsetX=-(lo[0]+hi[0])*0.5*cat.scale;
    cat.offsetY=-lo[1]*cat.scale; cat.offsetZ=-(lo[2]+hi[2])*0.5*cat.scale;
    foreach(o in cat.parts) o.setScale(cat.scale,cat.scale,cat.scale);
    print(format("modern-balcony: cat bounds %.3f %.3f %.3f -> %.3f %.3f %.3f scale=%.5f\n",lo[0],lo[1],lo[2],hi[0],hi[1],hi[2],cat.scale));
    scene.cats.push(cat);
    print("modern-balcony: cat "+path+" clip="+clipName+" skinned parts="+cat.parts.len()+"\n");
}
function cameraView() {
    scene.camera.setEye(8.5*sin(scene.orbit+0.31),2.85,8.5*cos(scene.orbit+0.31));
    scene.camera.setTarget(0.0,1.25,-1.1);
}
function reflections() {
    local rc=gfx.getRenderControl();
    rc.disable("rtx"); rc.disable("reflectionChain");
    if(scene.reflection) rc.enable(scene.hwRT ? "rtx" : "reflectionChain");
    rc.compile();
}
eve_init <- function() {
    if("dev" in eve) { eve.dev.ai.setVisible(false); eve.dev.console.setVisible(false); }
    scene.camera=eve.Camera3D(); scene.camera.setFov(57.0);
    scene.camera.setUp(0.0,1.0,0.0); scene.camera.setClipPlanes(0.1,500.0);
    scene.camera.setAmbient(0.22,0.27,0.30); scene.camera.setExposure(0.7);
    scene.camera.setBloom(0.1,1.25); scene.camera.setActive(true); cameraView();
    checked(gfx.setSceneToneMapping("filmic"),"tone mapping");
    scene.sky=checked(daynight.prepareSky(gfx,fs.readText("sky.json")),"sky preparation");
    checked(scene.sky.attach(),"sky attachment");
    scene.clouds=gfx.newVolumetric(); scene.clouds.setMode("cloud"); scene.clouds.setQuality("medium");
    scene.clouds.setCloudLayer(32.0,52.0); scene.clouds.setCloudCoverage(0.40);
    scene.clouds.setCloudDensity(0.65); scene.clouds.setCloudScale(60.0); scene.clouds.setCloudWind(0.4,0.1);
    local sun=eve.Light3D(); sun.setType("dir"); sun.setDirection(-0.5,0.8,0.4);
    sun.setColor(1.0,0.88,0.69,2.0); sun.setCastShadow(true);
    sun.setShadowMethod("csm"); sun.setShadowStrength(0.78); scene.lights.push(sun);
    gfx.setDirectionalLight(-0.5,0.8,0.4,0.75,0.68,0.55);
    gfx.setShadowSchemeDirectionalEnabled(true);
    local rc=gfx.getRenderControl(); rc.enable("shadow"); rc.enable("gbuffer");
    rc.setPostProcessQuality("high");
    scene.rt=eve.RayTracing(); scene.hwRT=scene.rt.isAvailable(); reflections();
    architecture("stone",[0.72,0.70,0.64],0.86);
    architecture("wood",[0.38,0.24,0.13],0.66);
    architecture("metal",[0.16,0.19,0.20],0.25,0.85);
    architecture("glass",[0.65,0.85,0.88],0.08,0.15);
    architecture("fabric",[0.79,0.79,0.69],0.95);
    architecture("ceramic",[0.62,0.39,0.28],0.7);
    architecture("soil",[0.08,0.055,0.035],1.0);
    treeAt(-3.05,0.77,-2.15,1.9,0.65,42);
    treeAt(3.05,0.62,-2.15,1.7,0.7,73);
    for(local i=0;i<9;++i) leafPlant(-3.3+i*0.8,0.49,-2.68,0.65,31+i,i%3==0);
    leafPlant(3.0,0.54,1.6,0.9,14); leafPlant(-3.2,0.43,1.7,0.65,15);
    leafPlant(0.75,0.9,-0.3,0.22,16);
    gardenEnvironment();
    local cube=gfx.newMeshCube(1.0); local cylinder=gfx.newMeshCylinder(24,1,true);
    // Woven rug, books, glazed cups, cushions, watering can and cat bowls.
    keepMesh(cube,0,0.028,0.55,3.4,0.025,2.1,[0.36,0.44,0.39]);
    for(local i=0;i<18;++i) keepMesh(cube,-1.55+i*0.18,0.044,0.55,0.025,0.006,2.0,[0.65,0.66,0.51]);
    local cushion=keepMesh(cube,-2.25,0.77,-1.2,0.55,0.35,0.55,[0.68,0.32,0.17]); cushion.setYaw(0.2);
    keepMesh(cube,-0.03,0.785,-0.3,0.42,0.05,0.32,[0.22,0.36,0.35]);
    keepMesh(cube,0.0,0.825,-0.31,0.38,0.03,0.29,[0.84,0.75,0.55]);
    keepMesh(cylinder,0.22,0.83,0.0,0.07,0.07,0.07,[0.91,0.88,0.77]);
    keepMesh(cylinder,0.48,0.83,0.0,0.07,0.07,0.07,[0.33,0.51,0.47]);
    keepMesh(cylinder,3.12,0.22,0.65,0.17,0.22,0.17,[0.23,0.35,0.31]);
    for(local i=0;i<2;++i) keepMesh(cylinder,-3.2+i*0.42,0.065,0.2,0.16,0.06,0.16,[0.50,0.57,0.57],0.2,0.65);
    for(local i=0;i<9;++i) {
        local x=-3.2+i*0.8; local y=2.9-0.35*sin(i*0.3927);
        keepMesh(gfx.newMeshSphere(10,6),x,y,-2.6,0.055,0.075,0.055,[1.0,0.75,0.35]);
        if(i%3==0) { local l=eve.Light3D(); l.setType("point"); l.setPosition(x,y,-2.6);
            l.setColor(1.0,0.69,0.35,0.5); l.setRadius(2.0); scene.lights.push(l); }
    }
    addCat("assets/cats/toon-cat.glb","Scene",0.52,-0.6,0.03,1.9,true);
    addCat("assets/cats/tuxedo.gltf","IdleSit",0.55,1.7,0.64,-1.1,false);
    ui.beginBuild(); ui.beginWindow("TERRACE / Afternoon garden","root");
    ui.text("A quiet afternoon. Two curious companions.","subtitle");
    ui.text(scene.hwRT ? "Ray tracing / Vulkan KHR" : "Portable reflections / RT unavailable","rt");
    ui.text("Left / Right: orbit    R: reset    Space: pause life    F: reflections","keys");
    ui.end(); ui.mountBuildAs("balcony-hud"); ui.select("balcony-hud");
    ui.setHostOverlay(true); ui.setHostPos(24.0,22.0,0.0,0.0);
    print("modern-balcony: ready; RT hardware="+scene.hwRT+"\n");
};
eve_update <- function(dt) {
    if(key_just_pressed("space")) scene.paused=!scene.paused;
    if(key_just_pressed("f")) { scene.reflection=!scene.reflection; reflections(); }
    if(key_just_pressed("left")) { scene.orbit-=0.15; cameraView(); }
    if(key_just_pressed("right")) { scene.orbit+=0.15; cameraView(); }
    if(key_just_pressed("r")) { scene.orbit=0.0; cameraView(); }
    if(scene.paused) return;
    scene.time+=dt;
    checked(scene.sky.advanceSeconds(dt),"sky advance");
    scene.clouds.setCamera(scene.camera.getEyeX(),scene.camera.getEyeY(),scene.camera.getEyeZ(),
        scene.camera.getTargetX(),scene.camera.getTargetY(),scene.camera.getTargetZ(),
        0.0,1.0,0.0,scene.camera.getFov(),1.6,0.1,500.0);
    scene.clouds.setLightDirection(-0.5,0.8,0.4); scene.clouds.setCloudLightColor(1.0,0.96,0.88);
    scene.clouds.setTime(scene.time*0.1);
    foreach(p in scene.plants) p.object.setPosition(p.x+sin(scene.time*0.8+p.phase)*0.012,p.y,p.z);
    foreach(c in scene.cats) {
        c.player.update(dt); local pose=c.player.getPose(); pose.computeWorld(c.skeleton);
        foreach(i,skin in c.skins)
            if(!skin.applyToMesh(gfx,c.parts[i].getMesh(),pose)) throw "Cat skin upload failed";
        local a=c.walking ? scene.time*0.16 : 0.0;
        local yaw=c.walking ? a+1.5708 : -0.5;
        local px=c.x+(c.walking ? 1.25*sin(a) : 0.0);
        local pz=c.z+(c.walking ? 0.45*cos(a) : 0.0);
        foreach(o in c.parts) {
            o.setPosition(px+cos(yaw)*c.offsetX+sin(yaw)*c.offsetZ,c.y+c.offsetY,
                pz-sin(yaw)*c.offsetX+cos(yaw)*c.offsetZ); o.setYaw(yaw);
        }
    }
};
eve_render <- function() {
    gfx.clear(); gfx.render3D();
    local gb=gfx.getRenderControl().getGBuffer();
    if(gb!=null && gb.isValid()) scene.clouds.renderClouds(gfx,gb.getDepthTexture());
    ui.beginFrameAndRender();
};
