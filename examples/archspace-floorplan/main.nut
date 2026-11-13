persist finishMaterials = []
// ArchSpace floor-plan demo: a compact apartment with several rooms, doors,
// windows and furniture. Bakes the document mesh (opening cutouts included),
// uploads it through gfx, and saves an engine-owned screenshot for evidence.

persist plan = {
    camera = null,
    object = null,
    frame = 0,
    screenshotSaved = false,
    summary = "",
}

function planRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function planBuildApartment() {
    local doc = eve.ArchSpaceDocument();
    planRequire(doc.bootstrap("site", "apartment", "level0", 2.8), "bootstrap");

    // Rooms (metres, X east / Z south). Adjacent rectangles share a wall line.
    //   [ bedroom 4x4 ][ living 6x5           ]
    //   [ bath 3x3    ][ living               ]
    //                  [ kitchen 4x3.5 | foyer ]
    planRequire(doc.createRectRoom("level0", "living", "Living", 4.0, 0.0, 6.0, 5.0, 2.8, 0.18, 0.18),
                "living");
    planRequire(doc.createRectRoom("level0", "bedroom", "Bedroom", 0.0, 0.0, 4.0, 4.0, 2.8, 0.18, 0.18),
                "bedroom");
    planRequire(doc.createRectRoom("level0", "bath", "Bath", 0.0, 4.0, 3.0, 3.0, 2.8, 0.16, 0.16),
                "bath");
    planRequire(doc.createRectRoom("level0", "kitchen", "Kitchen", 4.0, 5.0, 4.0, 3.5, 2.8, 0.18, 0.18),
                "kitchen");
    planRequire(doc.createRectRoom("level0", "foyer", "Foyer", 8.0, 5.0, 2.0, 3.5, 2.8, 0.16, 0.16),
                "foyer");

    // Doors (t along the host wall). wall.0 south, .1 east, .2 north, .3 west.
    // Foyer north wall faces the street; living north wall opens into foyer/kitchen.
    planRequire(doc.createOpening("foyer.wall.2", "entry.door", "Entry", "door", 0.5, 1.0, 2.1, 0.0),
                "entry.door");
    planRequire(doc.createOpening("living.wall.2", "living.foyer.door", "Living entry", "door", 0.22, 0.9, 2.1, 0.0),
                "living.foyer.door");
    planRequire(doc.createOpening("bedroom.wall.1", "bedroom.door", "Bedroom door", "door", 0.55, 0.9, 2.1, 0.0),
                "bedroom.door");
    planRequire(doc.createOpening("bath.wall.1", "bath.door", "Bath door", "door", 0.45, 0.8, 2.05, 0.0),
                "bath.door");
    planRequire(doc.createOpening("living.wall.2", "kitchen.door", "Kitchen door", "door", 0.65, 0.9, 2.1, 0.0),
                "kitchen.door");

    // Windows on exterior walls
    planRequire(doc.createOpening("living.wall.1", "living.window", "Living window", "window", 0.45, 1.6, 1.4, 0.9),
                "living.window");
    planRequire(doc.createOpening("bedroom.wall.3", "bedroom.window", "Bedroom window", "window", 0.5, 1.4, 1.3, 0.9),
                "bedroom.window");
    planRequire(doc.createOpening("kitchen.wall.0", "kitchen.window", "Kitchen window", "window", 0.55, 1.3, 1.2, 0.95),
                "kitchen.window");
    planRequire(doc.createOpening("bath.wall.3", "bath.window", "Bath window", "window", 0.5, 0.7, 0.7, 1.2),
                "bath.window");

    // Furniture markers from the built-in catalogue
    planRequire(doc.placeItem("level0", "sofa", "Sofa", "furniture.sofa", 6.5, 0.0, 2.2, 180.0), "sofa");
    planRequire(doc.placeItem("level0", "table", "Table", "furniture.table", 7.5, 0.0, 3.6, 0.0), "table");
    planRequire(doc.placeItem("level0", "bed", "Bed", "furniture.bed", 1.6, 0.0, 1.8, 0.0), "bed");
    planRequire(doc.placeItem("level0", "desk", "Desk", "furniture.desk", 2.8, 0.0, 3.2, 90.0), "desk");
    planRequire(doc.placeItem("level0", "chair", "Chair", "furniture.chair", 2.2, 0.0, 3.2, 270.0), "chair");
    planRequire(doc.placeItem("level0", "wardrobe", "Wardrobe", "furniture.wardrobe", 0.6, 0.0, 3.4, 90.0),
                "wardrobe");

    if (!doc.hasNode("living.zone") || !doc.hasNode("entry.door") || !doc.hasNode("living.window") ||
        !doc.hasNode("bedroom.door") || !doc.hasNode("sofa"))
        throw "ArchSpace floor plan missing authored nodes";

    local hit = doc.regionAt(7.0, 2.5, "level0");
    if (hit != "living.zone") throw "regionAt living failed: " + hit;
    return doc;
}

function planUpload(doc) {
    local arrays = doc.bakeMeshArrays();
    if (arrays.triangleCount < 24) throw "ArchSpace bake produced too few triangles: " + arrays.triangleCount;
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
        foreach(line in [0.0,3.0,4.0,8.0,10.0])
            if(hi[0]-lo[0]<=0.221 && abs((hi[0]+lo[0])*0.5-line)<0.05) wall=true;
        foreach(line in [0.0,4.0,5.0,7.0,8.5])
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

if (plan.camera == null) {
    // Elevated three-quarter view so rooms, doors and windows read clearly.
    plan.camera = eve.Camera3D();
    plan.camera.setEye(-2.5, 12.5, -6.0);
    plan.camera.setTarget(5.0, 0.6, 4.0);
    plan.camera.setUp(0.0, 1.0, 0.0);
    plan.camera.setFov(40.0);
    plan.camera.setAmbient(0.34, 0.36, 0.40);
    plan.camera.setExposure(0.82);
    planRequire(gfx.setSceneToneMapping("aces"), "tone mapping");
    plan.camera.setActive(true);
    gfx.setDirectionalLight(-0.4, 1.0, -0.3, 0.75, 0.70, 0.62);
    gfx.setBackgroundColor(0.05, 0.07, 0.10, 1.0);
}

if (plan.object == null) {
    local built = planUpload(planBuildApartment());
    plan.object = built.object;
    plan.summary = built.summary;
    print("ARCHSPACE_FLOORPLAN_PASS " + plan.summary + "\n");
}


persist presentation = []
function finishBox(x,y,z,sx,sy,sz,r,g,b) {
    local m=gfx.newMaterial();m.setTint(r,g,b,1.0);m.setRoughness(0.88);finishMaterials.append(m);
    local e=eve.Renderable3D();e.setMesh(gfx.newMeshCube(1.0));e.setMaterial(m);
    e.setPosition(x,y,z);e.setScale(sx,sy,sz);e.setReceiveShadow(true);presentation.append(e);
}
if(presentation.len()==0) {
    finishBox(5.0,-0.32,4.0,12.0,0.30,10.0,0.12,0.18,0.21);
    foreach(room in [[7.0,2.5,5.6,4.6,0.43,0.34,0.24],[2.0,2.0,3.6,3.6,0.36,0.28,0.21],[1.5,5.5,2.6,2.6,0.22,0.36,0.40],[6.0,6.75,3.6,3.1,0.35,0.39,0.31],[9.0,6.75,1.6,3.1,0.40,0.34,0.28]]) {
        finishBox(room[0],0.23,room[1],room[2],0.024,room[3],room[4],room[5],room[6]);
        for(local x=room[0]-room[2]*0.5+0.2;x<room[0]+room[2]*0.5;x+=0.2)
            finishBox(x,0.248,room[1],0.012,0.006,room[3],0.17,0.20,0.19);
    }
}

persist showWalls=true
persist wallKey=false
function eve_update(dt) {
    local down=keyboard.isDown("w");
    if(down && !wallKey) {showWalls=!showWalls;plan.object[1].setVisible(showWalls);}
    wallKey=down;
    plan.frame += 1;
    // Keep yaw fixed so the floor plan reads as a stable 户型图.
    if (plan.object != null) foreach(object in plan.object) object.setYaw(0.0);
    if (!plan.screenshotSaved && plan.frame > 36 && gfx.saveFramePng("archspace-floorplan.png")) {
        plan.screenshotSaved = true;
        print("archspace-floorplan: screenshot saved\n");
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
    gfx.drawText(archFont,"APARTMENT / FIVE ROOM PLAN",32.0,27.0,0.89,0.93,0.91,1.0,0.9);
    gfx.drawText(archFont,"W walls / document openings / catalogue furniture",32.0,55.0,0.58,0.75,0.78,1.0,0.65);
}
