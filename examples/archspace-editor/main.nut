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
    if (!doc.hasNode("living.zone") || !doc.hasNode("living.door") || !doc.hasNode("desk"))
        throw "ArchSpace document missing authored nodes";
    return doc;
}

function archUpload(doc) {
    local arrays = doc.bakeMeshArrays();
    if (arrays.triangleCount < 12) throw "ArchSpace bake produced too few triangles: " + arrays.triangleCount;
    local mesh = gfx.newMeshFromArrays(arrays.positions, arrays.normals, arrays.uvs, arrays.vertexCount,
                                       arrays.indices, arrays.indexCount);
    if (mesh == null) throw "gfx.newMeshFromArrays failed for ArchSpace bake";
    local object = eve.Renderable3D();
    object.setMesh(mesh);
    object.setPosition(0.0, 0.0, 0.0);
    object.setTint(0.82, 0.78, 0.72, 1.0);
    object.setRoughness(0.55);
    object.setMetallic(0.05);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    return { object = object, summary = doc.summary() };
}

if (arch.camera == null) {
    arch.camera = eve.Camera3D();
    arch.camera.setEye(8.0, 11.0, -14.0);
    arch.camera.setTarget(5.0, 0.5, 2.5);
    arch.camera.setUp(0.0, 1.0, 0.0);
    arch.camera.setFov(42.0);
    arch.camera.setAmbient(0.28, 0.30, 0.34);
    arch.camera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.4, 1.3, 1.15);
    gfx.setBackgroundColor(0.04, 0.055, 0.08, 1.0);
}

if (arch.object == null) {
    local built = archUpload(archBuildApartment());
    arch.object = built.object;
    arch.summary = built.summary;
    print("ARCHSPACE_PASS " + arch.summary + "\n");
}

function eve_update(dt) {
    arch.frame += 1;
    if (arch.object != null) arch.object.setYaw(arch.frame * 0.15);
    if (!arch.screenshotSaved && arch.frame > 24 && gfx.saveFramePng("archspace-editor.png")) {
        arch.screenshotSaved = true;
        print("archspace-editor: screenshot saved\n");
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
