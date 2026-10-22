// Generated shared scene helpers; edit stylized-water-shore/scene.nut.
// Scene-owned geometry. All positions and angles are world meters and radians.
persist sceneProps=[]
persist sceneTextures={}
persist sceneNormals={}
persist sceneRockMeshes=[]
persist sceneProbe=null
persist sceneReedMaterial=null
persist sceneReedMesh=null
persist sceneTerrainShader=null
persist sceneStoneShader=null
persist sceneModels=[]
persist sceneTreeMaterials=[]
persist sceneSun=null

function sceneClamp(x,a,b) { return x<a?a:(x>b?b:x); }
function sceneNoise(x,z) { return sin(x*1.37+z*0.91)*0.5+sin(x*3.17-z*2.23)*0.25; }
function sceneLoadMaterials() {
    foreach(name in ["sand","stone","grass","wood"]) {
        sceneTextures[name] <- gfx.newTextureFromFileRepeated(name=="sand"?"assets/gravel-albedo.png":"assets/"+name+".png",true,true);
        sceneNormals[name] <- gfx.newTextureFromFileRepeated("assets/"+name+"-normal.png",true,true);
        if(sceneTextures[name]==null || sceneNormals[name]==null) throw "Missing scene material: "+name;
        gfx.setTextureSampler(sceneTextures[name],"linear","linear",4.0,0.0);
        gfx.setTextureSampler(sceneNormals[name],"linear","linear",4.0,0.0);
    }

    local loaded=gfx.loadMeshShaderSpv("","shaders/stone.frag.spv");
    if(!loaded.ok) throw "stone shader: "+loaded.status.summary;
    sceneStoneShader=loaded.value;sceneStoneShader.declareFloat("waterLevel");
}
function sceneProp(mesh,material,color,x,y,z,sx,sy,sz,camera) {
    local prop=eve.Renderable3D();prop.setMesh(mesh);prop.setPosition(x,y,z);prop.setScale(sx,sy,sz);
    prop.setTint(color[0],color[1],color[2],1.0);prop.setMetallic(0.0);prop.setRoughness(0.86);
    prop.setCastShadow(true);prop.setReceiveShadow(true);prop.setCamera(camera);
    if(material!="") { prop.setTexture(sceneTextures[material]);prop.setNormalTexture(sceneNormals[material]); }
    if(material=="sand" || material=="grass") prop.setTexCellBomb(1.0,0.8,1.0);
    if(material=="stone") prop.setShader(sceneStoneShader);
    sceneProps.append(prop);return prop;
}
function sceneBoulder(seed) {
    local pos=[],nrm=[],uv=[],idx=[],rows=14,cols=24;
    for(local y=0;y<=rows;++y) for(local x=0;x<=cols;++x) {
        local a=x.tofloat()/cols*6.283185,b=y.tofloat()/rows*3.1415926;
        local nx=sin(b)*cos(a),ny=cos(b),nz=sin(b)*sin(a);
        local r=1.0+0.10*sin(nx*6.0+seed)*sin(nz*5.0-seed*0.7)+0.055*cos(ny*8.0+nx*3.0);
        pos.extend([nx*r,ny*r,nz*r]);nrm.extend([nx,ny,nz]);uv.extend([x.tofloat()/cols*2.0,y.tofloat()/rows]);
        if(y<rows && x<cols) { local i=y*(cols+1)+x;idx.extend([i,i+cols+1,i+1,i+1,i+cols+1,i+cols+2]); }
    }
    local mesh=gfx.newMeshFromArrays(pos,nrm,uv,pos.len()/3,idx,idx.len());
    sceneRockMeshes.append(mesh);return mesh;
}
function sceneTerrain(camera,minX,maxX,minZ,maxZ,steps,distanceFn,bedFn,waterY) {
    local pos=[],nrm=[],uv=[],indices=[],stride=steps+1;
    for(local z=0;z<=steps;++z) for(local x=0;x<=steps;++x) {
        local wx=minX+(maxX-minX)*x/steps,wz=minZ+(maxZ-minZ)*z/steps;
        local h=bedFn(wx,wz),eps=0.08;
        local dx=(bedFn(wx+eps,wz)-bedFn(wx-eps,wz))/(2.0*eps);
        local dz=(bedFn(wx,wz+eps)-bedFn(wx,wz-eps))/(2.0*eps);
        local len=sqrt(1.0+dx*dx+dz*dz);
        pos.extend([wx,h,wz]);nrm.extend([-dx/len,1.0/len,-dz/len]);uv.extend([wx*0.19,wz*0.19]);
    }
    for(local z=0;z<steps;++z) for(local x=0;x<steps;++x) {
        local i=z*stride+x;indices.extend([i,i+stride,i+1,i+1,i+stride,i+stride+1]);
    }
    local loaded=gfx.loadMeshShaderSpv("","shaders/terrain.frag.spv");
    if(!loaded.ok) throw "terrain shader: "+loaded.status.summary;
    sceneTerrainShader=loaded.value;sceneTerrainShader.declareFloat("waterLevel");sceneTerrainShader.sendFloat("waterLevel",waterY);
    sceneStoneShader.sendFloat("waterLevel",waterY);
    local mesh=gfx.newMeshFromArrays(pos,nrm,uv,pos.len()/3,indices,indices.len());
    local terrain=sceneProp(mesh,"sand",[1.0,1.0,1.0],0.0,0.0,0.0,1.0,1.0,1.0,camera);
    terrain.setShader(sceneTerrainShader);
}
function sceneReeds(camera,x,y,z,height,seed) {
    if(sceneReedMesh==null) {
        local pos=[-0.53,0.0,0.0,0.53,0.0,0.0,0.53,1.0,0.0,-0.53,1.0,0.0];
        local normal=[0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0];
        sceneReedMesh=gfx.newMeshFromArrays(pos,normal,[0.0,1.0,1.0,1.0,1.0,0.0,0.0,0.0],4,[0,1,2,0,2,3],6);
        local texture=gfx.newTextureFromFile("assets/reeds.png");
        if(texture==null) throw "Missing reed cutout";
        sceneReedMaterial=gfx.newMaterial();sceneReedMaterial.setAlbedoTexture(texture);
        sceneReedMaterial.setSurfaceMode("masked");sceneReedMaterial.setAlphaCutoff(0.4);
        sceneReedMaterial.setDoubleSided(true);sceneReedMaterial.setRoughness(0.95);
        sceneReedMaterial.setTint(0.58,0.63,0.50,1.0);sceneReedMaterial.setReceiveLight(false);
        sceneReedMaterial.setCastShadow(true);
    }
    for(local i=0;i<2;++i) {
        local prop=sceneProp(sceneReedMesh,"",[1.0,1.0,1.0],x,y-0.03,z,height,height,height,camera);
        prop.setMaterial(sceneReedMaterial);prop.setTint(0.65,0.63,0.46,1.0);prop.setYaw(seed*0.7+i*1.5708);
    }
}

function sceneEnvironment(camera,x,y,z) {
    local probe=gfx.newReflectionProbeCapture();probe.configure(x,y,z,128,0.1,100.0);
    local sky=[[0.52,0.64,0.75],[0.52,0.64,0.75],[0.25,0.45,0.69],[0.28,0.29,0.24],[0.64,0.71,0.76],[0.50,0.62,0.73]];
    foreach(i,c in sky) probe.setSkyFaceColor(i,c[0],c[1],c[2]);
    // Static opaque banks and rocks participate; the water is created afterwards.
    probe.setCaptureMask(-1);probe.requestCapture();
    for(local i=0;i<24;++i) {if(probe.update(6)) break;}
    if(!probe.isCaptureComplete() || !probe.stageCapturedFaces() || !probe.filterAndPublish(128))
        throw "Water scene environment capture failed";
    camera.setEnvMap(probe.getActiveCubemap());camera.setEnvIntensity(0.85);sceneProbe=probe;
}

function sceneTrees(camera,positions) {
    if(model3d==null) model3d=eve.Model3D();
    local leafTexture=gfx.newTextureFromFile("assets/garden/leaf-spray.png");
    local leaf=gfx.newMaterial();leaf.setAlbedoTexture(leafTexture);leaf.setSurfaceMode("masked");
    leaf.setAlphaCutoff(0.35);leaf.setDoubleSided(true);leaf.setTint(0.18,0.23,0.09,1.0);
    leaf.setRoughness(0.95);leaf.setReceiveLight(true);leaf.setCastShadow(true);leaf.setReceiveShadow(true);
    local bark=gfx.newMaterial();bark.setAlbedoTexture(sceneTextures.wood);bark.setTint(0.30,0.26,0.21,1.0);
    bark.setRoughness(0.95);bark.setReceiveLight(true);bark.setCastShadow(true);
    sceneTreeMaterials.extend([leafTexture,leaf,bark]);local variants=[];
    for(local i=0;i<3;++i) {
        local model=model3d.newModelDataFromFile("assets/garden/tree-"+i+".gltf");sceneModels.append(model);
        local parts=[];
        for(local j=0;j<model.getMeshCount();++j) {
            local owner=model3d.createRenderable(gfx,model,j);owner.setVisible(false);sceneProps.append(owner);
            parts.append([owner.getMesh(),model.getMaterialName(model.getMaterialIndex(j))=="leaves"?leaf:bark]);
        }
        variants.append(parts);
    }
    foreach(i,v in positions) {
        local scale=0.76+0.12*sin(i*1.7);
        foreach(part in variants[i%3]) {
            local tree=sceneProp(part[0],"",[1.0,1.0,1.0],v[0],v[1],v[2],scale,scale,scale,camera);
            tree.setMaterial(part[1]);tree.setTint(0.52,0.50,0.32,1.0);tree.setYaw(i*2.4);
        }
    }
}

function sceneSky(camera) {
    local pos=[],nrm=[],uv=[],idx=[],rows=16,cols=32;
    for(local y=0;y<=rows;++y) for(local x=0;x<=cols;++x) {
        local a=x.tofloat()/cols*6.283185,b=y.tofloat()/rows*3.1415926;
        local nx=sin(b)*cos(a),ny=cos(b),nz=sin(b)*sin(a);
        pos.extend([nx*300.0,ny*300.0,nz*300.0]);nrm.extend([-nx,-ny,-nz]);uv.extend([x.tofloat()/cols,y.tofloat()/rows]);
        if(y<rows && x<cols) {local i=y*(cols+1)+x;idx.extend([i,i+1,i+cols+1,i+1,i+cols+2,i+cols+1]);}
    }
    local loaded=gfx.loadMeshShaderSpv("","shaders/sky.frag.spv");if(!loaded.ok) throw "sky shader: "+loaded.status.summary;
    local mesh=gfx.newMeshFromArrays(pos,nrm,uv,pos.len()/3,idx,idx.len());
    local sky=sceneProp(mesh,"",[1.0,1.0,1.0],0.0,0.0,0.0,1.0,1.0,1.0,camera);
    sky.setShader(loaded.value);sky.setCastShadow(false);sky.setReceiveShadow(false);
}

// One merged mesh of real rounded pebbles, so refraction reveals shape and shading.
function scenePebbleBed(camera,minX,maxX,minZ,maxZ,distanceFn,bedFn) {
    local pos=[],nrm=[],uv=[],idx=[],cols=7,rows=4,spacing=0.28;
    local countX=((maxX-minX)/spacing).tointeger(),countZ=((maxZ-minZ)/spacing).tointeger();
    for(local z=0;z<countZ;++z) for(local x=0;x<countX;++x) {
        local seed=sin(x*127.1+z*311.7)*43758.5453,random=seed-floor(seed);
        if(random<0.50) continue;
        local wx=minX+(x+0.18+random*0.65)*spacing,wz=minZ+(z+0.5+0.28*sin(seed))*spacing;
        local distance=distanceFn(wx,wz);if(distance< -0.35 || distance>1.8) continue;
        local r=0.018+random*random*0.050,ground=bedFn(wx,wz),first=pos.len()/3;
        for(local j=0;j<=rows;++j) for(local k=0;k<=cols;++k) {
            local a=k.tofloat()/cols*6.283185,b=j.tofloat()/rows*3.1415926;
            local nx=sin(b)*cos(a),ny=cos(b),nz=sin(b)*sin(a);
            pos.extend([wx+nx*r,ground+ny*r*0.46+r*0.22,wz+nz*r*0.82]);
            nrm.extend([nx,ny,nz]);uv.extend([k.tofloat()/cols,j.tofloat()/rows]);
            if(j<rows && k<cols) {local i=first+j*(cols+1)+k;idx.extend([i,i+cols+1,i+1,i+1,i+cols+1,i+cols+2]);}
        }
    }
    local mesh=gfx.newMeshFromArrays(pos,nrm,uv,pos.len()/3,idx,idx.len());
    local stones=sceneProp(mesh,"stone",[1.0,0.96,0.85],0.0,0.0,0.0,1.0,1.0,1.0,camera);
    stones.setCastShadow(false);
}

function sceneSunLight() {
    sceneSun=eve.Light3D();sceneSun.setType("dir");sceneSun.setDirection(-0.60,0.90,-0.80);
    sceneSun.setColor(1.12,1.02,0.86,1.0);sceneSun.setCastShadow(true);sceneSun.setShadowStrength(0.85);
}

function sceneRidgeHeight(x,z) {
    local along=(z+110.0)/75.0,envelope=sin(sceneClamp(along,0.0,1.0)*3.1415926);
    local ridge=8.0+5.0*sin(x*0.048+0.6)+2.2*sin(x*0.13)+1.2*cos(x*0.35);
    return 0.15+envelope*ridge*0.48+envelope*(0.8*sin(x*1.7+z*0.8)+0.35*cos(x*3.2));
}
function sceneDistantRidge(camera) {
    local pos=[],nrm=[],uv=[],idx=[],nx=160,nz=32;
    for(local z=0;z<=nz;++z) for(local x=0;x<=nx;++x) {
        local wx=-65.0+x.tofloat()/nx*150.0,wz=-110.0+z.tofloat()/nz*75.0;
        local h=sceneRidgeHeight(wx,wz),dx=(sceneRidgeHeight(wx+0.2,wz)-sceneRidgeHeight(wx-0.2,wz))/0.4;
        local dz=(sceneRidgeHeight(wx,wz+0.2)-sceneRidgeHeight(wx,wz-0.2))/0.4,len=sqrt(dx*dx+dz*dz+1.0);
        pos.extend([wx,h,wz]);nrm.extend([-dx/len,1.0/len,-dz/len]);uv.extend([wx*0.1,wz*0.1]);
        if(x<nx && z<nz) {local i=z*(nx+1)+x;idx.extend([i,i+nx+1,i+1,i+1,i+nx+1,i+nx+2]);}
    }
    local loaded=gfx.loadMeshShaderSpv("","shaders/ridge.frag.spv");if(!loaded.ok) throw "ridge shader: "+loaded.status.summary;
    local mesh=gfx.newMeshFromArrays(pos,nrm,uv,pos.len()/3,idx,idx.len());
    local ridge=sceneProp(mesh,"",[1.0,1.0,1.0],0.0,0.0,0.0,1.0,1.0,1.0,camera);
    ridge.setShader(loaded.value);ridge.setCastShadow(false);
}
