persist finishMaterials = []
// ArchSpace authoring demo: build a real floor plan, bake mesh with door/window
// cutouts, upload to the GPU and prove the viewport path with a screenshot.

persist arch = {
    camera = null,
    object = null,
    frame = 0,
    screenshotSaved = false,
    summary = "",
}

function archRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function archBuildApartment() {
    local doc = eve.ArchSpaceDocument();
    archRequire(doc.bootstrap("site", "building", "level0", 3.0), "bootstrap");
    archRequire(doc.createRectRoom("level0", "living", "Living", 0.0, 0.0, 6.0, 4.5, 3.0, 0.2, 0.2), "living");
    archRequire(doc.createRectRoom("level0", "office", "Office", 6.0, 0.0, 4.0, 4.5, 3.0, 0.2, 0.2), "office");
    archRequire(doc.createWall("level0", "corridor.wall", "Corridor", 0.0, 4.5, 10.0, 4.5, 3.0, 0.18), "corridor");
    archRequire(doc.createOpening("living.wall.0", "living.door", "Entry", "door", 0.45, 1.0, 2.1, 0.0), "door");
    archRequire(doc.createOpening("office.wall.1", "office.window", "Window", "window", 0.5, 1.4, 1.4, 0.9),
                "window");
    archRequire(doc.placeItem("level0", "desk", "Desk", "furniture.desk", 7.2, 0.0, 2.0, 90.0), "desk");
    archRequire(doc.placeItem("level0", "sofa", "Sofa", "furniture.sofa", 2.0, 0.0, 2.2, 0.0), "sofa");
    archRequire(doc.placeItem("level0", "chair", "Chair", "furniture.chair", 7.2, 0.0, 1.1, 0.0), "chair");
    if (!doc.hasNode("living.zone") || !doc.hasNode("living.door") || !doc.hasNode("desk") ||
        !doc.hasNode("sofa") || !doc.hasNode("chair"))
        throw "ArchSpace document missing authored nodes";
    return doc;
}

function archUpload(doc) {
    local arrays = doc.bakeMeshArrays();
    if (arrays.triangleCount < 12) throw "ArchSpace bake produced too few triangles: " + arrays.triangleCount;
    local objects=[];
    local buckets=[{p=[],n=[],uv=[],idx=[]},{p=[],n=[],uv=[],idx=[]},{p=[],n=[],uv=[],idx=[]}];
    // The document bake emits eight vertices per slab/catalogue box/wall segment.
    // Classify whole solids, then expand triangle normals for crisp architectural edges.
    for(local i=0;i<arrays.indexCount;i+=3) {
        local first=(arrays.indices[i]/8).tointeger()*8;
        local lo=[10000.0,10000.0,10000.0],hi=[-10000.0,-10000.0,-10000.0];
        for(local v=first;v<first+8;++v) for(local axis=0;axis<3;++axis) {
            local value=arrays.positions[v*3+axis];
            if(value<lo[axis])lo[axis]=value;if(value>hi[axis])hi[axis]=value;
        }
        local floorSurface=hi[1]<=0.201;
        local wall=false;
        foreach(line in [0.0,6.0,10.0])
            if(hi[0]-lo[0]<=0.221 && abs((hi[0]+lo[0])*0.5-line)<0.05) wall=true;
        foreach(line in [0.0,4.5])
            if(hi[2]-lo[2]<=0.221 && abs((hi[2]+lo[2])*0.5-line)<0.05) wall=true;
        local bucket=buckets[floorSurface?0:(wall?1:2)];
        local points=[];
        for(local j=0;j<3;++j) {
            local v=arrays.indices[i+j];points.append([arrays.positions[v*3],arrays.positions[v*3+1],arrays.positions[v*3+2]]);
        }
        local ax=points[1][0]-points[0][0],ay=points[1][1]-points[0][1],az=points[1][2]-points[0][2];
        local bx=points[2][0]-points[0][0],by=points[2][1]-points[0][1],bz=points[2][2]-points[0][2];
        local nx=ay*bz-az*by,ny=az*bx-ax*bz,nz=ax*by-ay*bx;
        local length=sqrt(nx*nx+ny*ny+nz*nz);if(length<0.00001) continue;
        foreach(point in points) {
            bucket.idx.append(bucket.p.len()/3);bucket.p.extend(point);
            bucket.n.extend([nx/length,ny/length,nz/length]);bucket.uv.extend([point[0],point[2]]);
        }
    }
    local colors=[[0.20,0.24,0.24],[0.48,0.43,0.34],[0.08,0.24,0.27]];
    foreach(i,bucket in buckets) {
        if(bucket.idx.len()==0)continue;
        local mesh=gfx.newMeshFromArrays(bucket.p,bucket.n,bucket.uv,bucket.p.len()/3,bucket.idx,bucket.idx.len());
        if(mesh==null)throw "ArchSpace material upload failed";
        local material=gfx.newMaterial();local c=colors[i];material.setTint(c[0],c[1],c[2],1.0);
        material.setRoughness(0.82);finishMaterials.append(material);
        local object=eve.Renderable3D();object.setMesh(mesh);object.setMaterial(material);
        object.setCastShadow(true);object.setReceiveShadow(true);objects.append(object);
    }
    return {object=objects,summary=doc.summary()};
}

if (arch.camera == null) {
    arch.camera = eve.Camera3D();
    arch.camera.setEye(-3.0, 12.0, -7.5);
    arch.camera.setTarget(5.0, 0.5, 2.5);
    arch.camera.setUp(0.0, 1.0, 0.0);
    arch.camera.setFov(42.0);
    arch.camera.setAmbient(0.28, 0.30, 0.34);
    arch.camera.setExposure(0.82);
    archRequire(gfx.setSceneToneMapping("aces"), "tone mapping");
    arch.camera.setActive(true);
    gfx.setDirectionalLight(-0.45, 1.0, -0.35, 0.75, 0.70, 0.62);
    gfx.setBackgroundColor(0.04, 0.055, 0.08, 1.0);
}

if (arch.object == null) {
    local built = archUpload(archBuildApartment());
    arch.object = built.object;
    arch.summary = built.summary;
    print("ARCHSPACE_PASS " + arch.summary + "\n");
}


persist presentation = []
function finishBox(x,y,z,sx,sy,sz,r,g,b) {
    local m=gfx.newMaterial();m.setTint(r,g,b,1.0);m.setRoughness(0.88);finishMaterials.append(m);
    local e=eve.Renderable3D();e.setMesh(gfx.newMeshCube(1.0));e.setMaterial(m);
    e.setPosition(x,y,z);e.setScale(sx,sy,sz);e.setReceiveShadow(true);presentation.append(e);
}
if(presentation.len()==0) {
    finishBox(5.0,-0.32,4.0,12.0,0.30,10.0,0.12,0.18,0.21);
    foreach(room in [[3.0,2.25,5.6,4.1,0.42,0.31,0.20],[8.0,2.25,3.6,4.1,0.23,0.36,0.38]]) {
        finishBox(room[0],0.23,room[1],room[2],0.024,room[3],room[4],room[5],room[6]);
        for(local x=room[0]-room[2]*0.5+0.2;x<room[0]+room[2]*0.5;x+=0.2)
            finishBox(x,0.248,room[1],0.012,0.006,room[3],0.17,0.20,0.19);
    }
}

persist showWalls=true
persist wallKey=false
function eve_update(dt) {
    local down=keyboard.isDown("w");
    if(down && !wallKey) {showWalls=!showWalls;arch.object[1].setVisible(showWalls);}
    wallKey=down;
    arch.frame += 1;
    if (arch.object != null) foreach(object in arch.object) object.setYaw(0.0);
    if (!arch.screenshotSaved && arch.frame > 24 && gfx.saveFramePng("archspace-editor.png")) {
        arch.screenshotSaved = true;
        print("archspace-editor: screenshot saved\n");
    }
}

persist archFontOwner=null
persist archFont=null
if(archFont==null) {
    archFontOwner=eve.Font();local data=archFontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf",20);
    archFont=gfx.newFont(data," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/");
}
function eve_render() {
    gfx.clear();
    gfx.render3D();
    gfx.drawSolidRect(18.0,18.0,455.0,65.0,0.025,0.045,0.06,0.94);
    gfx.drawText(archFont,"ARCHSPACE / AUTHORING VIEW",32.0,27.0,0.89,0.93,0.91,1.0,0.9);
    gfx.drawText(archFont,"W walls / document openings / catalogue furniture",32.0,55.0,0.58,0.75,0.78,1.0,0.65);
}
