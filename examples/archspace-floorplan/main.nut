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
    local mesh = gfx.newMeshFromArrays(arrays.positions, arrays.normals, arrays.uvs, arrays.vertexCount,
                                       arrays.indices, arrays.indexCount);
    if (mesh == null) throw "gfx.newMeshFromArrays failed for ArchSpace floor plan";
    local object = eve.Renderable3D();
    object.setMesh(mesh);
    object.setPosition(0.0, 0.0, 0.0);
    object.setTint(0.86, 0.82, 0.76, 1.0);
    object.setRoughness(0.6);
    object.setMetallic(0.04);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    return { object = object, summary = doc.summary() };
}

if (plan.camera == null) {
    // Elevated three-quarter view so rooms, doors and windows read clearly.
    plan.camera = eve.Camera3D();
    plan.camera.setEye(-2.5, 12.5, -6.0);
    plan.camera.setTarget(5.0, 0.6, 4.0);
    plan.camera.setUp(0.0, 1.0, 0.0);
    plan.camera.setFov(40.0);
    plan.camera.setAmbient(0.34, 0.36, 0.40);
    plan.camera.setActive(true);
    gfx.setDirectionalLight(-0.4, -1.0, -0.3, 1.4, 1.3, 1.18);
    gfx.setBackgroundColor(0.05, 0.07, 0.10, 1.0);
}

if (plan.object == null) {
    local built = planUpload(planBuildApartment());
    plan.object = built.object;
    plan.summary = built.summary;
    print("ARCHSPACE_FLOORPLAN_PASS " + plan.summary + "\n");
}

function eve_update(dt) {
    plan.frame += 1;
    // Keep yaw fixed so the floor plan reads as a stable 户型图.
    if (plan.object != null) plan.object.setYaw(0.0);
    if (!plan.screenshotSaved && plan.frame > 36 && gfx.saveFramePng("archspace-floorplan.png")) {
        plan.screenshotSaved = true;
        print("archspace-floorplan: screenshot saved\n");
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
