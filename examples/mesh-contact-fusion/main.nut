// Mesh Contact Fusion — static merge (± opt-in contact blend) + live MeshAdhereLive.
// Screen left → right (camera looks down -Z): blend OFF · blend ON · live adhere.

persist fusionObjects = []
persist fusionCamera = null
persist fusionLive = null
persist fusionLiveVisual = null
persist fusionSurfaceVisual = null
persist fusionUiReady = false
persist fusionFrame = 0
persist fusionScreenshotSaved = false
persist fusionPassPrinted = false

persist edgeRadius = 0.55
persist materialRadius = 0.55
persist strength = 1.0
persist softSnap = true
persist sourceLift = 0.78

function fusionRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function fusionRecipe(id, width, height, depth, detail) {
    local params = fusionRequire(procgen.newParams(), "new params").value;
    fusionRequire(procgen.applyMeshRecipeDefaults(id, params), "recipe defaults " + id);
    params.setFloat("width", width);
    params.setFloat("height", height);
    params.setFloat("depth", depth);
    params.setInt("detail", detail);
    return fusionRequire(procgen.buildMesh(id, params), "build " + id).value;
}

function fusionTransform(mesh, x, y, z) {
    local graph = fusionRequire(procgen.newMeshModifierGraph(), "new transform graph").value;
    fusionRequire(graph.addNode("source", "mesh.input"), "add transform source");
    fusionRequire(graph.addNode("move", "deform.transform"), "add transform");
    fusionRequire(graph.addNode("out", "mesh.output"), "add transform out");
    fusionRequire(graph.setNodeMesh("source", mesh), "bind transform source");
    fusionRequire(graph.setNodeFloat("move", "x", x), "set transform x");
    fusionRequire(graph.setNodeFloat("move", "y", y), "set transform y");
    fusionRequire(graph.setNodeFloat("move", "z", z), "set transform z");
    fusionRequire(graph.connect("source", "move", 0), "connect transform");
    fusionRequire(graph.connect("move", "out", 0), "connect transform out");
    return fusionRequire(graph.executeResult("out"), "execute transform").value;
}

function fusionAdd(mesh, x, y, tintR, tintG, tintB) {
    local uploaded = fusionRequire(procgen.uploadMesh(mesh, gfx), "upload mesh").value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(x, y, 0.0);
    object.setTint(tintR, tintG, tintB, 1.0);
    object.setRoughness(0.62);
    object.setMetallic(0.04);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    fusionObjects.append(object);
    return object;
}

function fusionMergePair(enableBlend) {
    // Two blocks meet on a face with a tiny overlap. Soft-snap stays OFF for static
    // multi-source blend — mutual closest-point snap on overlapping solids caves the joint.
    local left = fusionRecipe("prototype.cube", 1.1, 1.4, 1.1, 12);
    local right = fusionRecipe("prototype.cube", 1.1, 1.4, 1.1, 12);
    local plan = eve.MeshMergePlan();
    fusionRequire(plan.appendSource(left, -0.52, 0.7, 0.0, 0.0, 1.0, 1.0, 1.0, "matA"), "append left");
    fusionRequire(plan.appendSource(right, 0.52, 0.7, 0.0, 12.0, 1.0, 1.0, 1.0, "matB"), "append right");
    fusionRequire(plan.setPivotMode(1), "world pivot");
    fusionRequire(plan.setEnableContactBlend(enableBlend), "set blend enable");
    if (enableBlend) {
        // Normals + material weights only (softSnap=false). Readable as smoother seam lighting.
        fusionRequire(plan.setContactBlend(0.55, 0.55, 1.0, 1.0, 1.0, 0.0, false, "smooth"),
                      "set contact blend");
    }
    local merged = fusionRequire(eve.mergeStaticMeshes(plan),
                                 enableBlend ? "merge with blend" : "merge without blend");
    local mesh = merged.value;
    print("mesh-contact-fusion: static sources=" + plan.getSourceCount() +
          " blend=" + plan.getEnableContactBlend() +
          " verts=" + mesh.getVertexCount() +
          " colors=" + mesh.hasVertexColors() +
          " meta=" + mesh.getMeta("contactBlend.enabled", "?") + "\n");
    return mesh;
}

function fusionUploadLive() {
    if (fusionLive == null || fusionLiveVisual == null) return;
    if (fusionLive.isDirty())
        fusionRequire(fusionLive.evaluate(false), "evaluate live adhere");
    local snapshot = fusionRequire(fusionLive.derivedMeshResult(), "live derived snapshot").value;
    local uploaded = fusionRequire(procgen.uploadMesh(snapshot, gfx), "upload live derived").value;
    fusionLiveVisual.setMesh(uploaded);
}

function fusionRebuildLiveSource() {
    // Movable cube floats above a wide ground slab; soft-snap pulls A onto B.
    local cube = fusionRecipe("prototype.cube", 1.15, 1.15, 1.15, 14);
    local lifted = fusionTransform(cube, 0.0, sourceLift, 0.0);
    if (fusionLive == null || !fusionLive.isActive()) {
        local ground = fusionRecipe("prototype.ground", 3.6, 0.14, 3.6, 6);
        fusionLive = eve.MeshAdhereLive();
        fusionRequire(fusionLive.activate(lifted, ground), "activate live adhere");
        if (fusionSurfaceVisual == null)
            fusionSurfaceVisual = fusionAdd(ground, 5.5, 0.0, 0.42, 0.48, 0.55);
        if (fusionLiveVisual == null) {
            fusionLiveVisual = eve.Renderable3D();
            fusionLiveVisual.setPosition(5.5, 0.0, 0.0);
            fusionLiveVisual.setTint(0.95, 0.58, 0.28, 1.0);
            fusionLiveVisual.setRoughness(0.55);
            fusionLiveVisual.setMetallic(0.06);
            fusionLiveVisual.setCastShadow(true);
            fusionLiveVisual.setReceiveShadow(true);
            fusionObjects.append(fusionLiveVisual);
        }
    } else {
        fusionRequire(fusionLive.setSource(lifted), "replace live source");
    }
    fusionRequire(fusionLive.setParams(edgeRadius, materialRadius, strength, 1.0, 1.0, 0.02, softSnap, "smooth"),
                  "set live params");
    fusionUploadLive();
}

function fusionBuildStaticShowcase() {
    local plain = fusionMergePair(false);
    // Cool tint: hard intersection, no contact band.
    fusionAdd(plain, -5.5, 0.0, 0.55, 0.62, 0.72);
    local blended = fusionMergePair(true);
    // Warm tint: normals blended across the seam (softSnap intentionally off).
    fusionAdd(blended, 0.0, 0.0, 0.92, 0.62, 0.32);
}

function fusionBuildUi() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("CONTACT FUSION", "root");
    ui.text("LEFT  static merge  blend OFF", "staticOff");
    ui.text("CENTER  static merge  blend ON (normals, no soft-snap)", "staticOn");
    ui.text("RIGHT  MeshAdhereLive  A soft-snaps onto B", "liveLabel");
    ui.slider("Edge radius", edgeRadius, 0.05, 1.5, "edgeRadius");
    ui.slider("Material radius", materialRadius, 0.05, 1.5, "materialRadius");
    ui.slider("Strength", strength, 0.0, 1.0, "strength");
    ui.slider("Source lift", sourceLift, 0.15, 1.4, "sourceLift");
    ui.beginRow("actions", 8.0);
    ui.button(softSnap ? "Soft snap: ON" : "Soft snap: OFF", "softSnap");
    ui.button("Re-evaluate", "reeval");
    ui.button("Bake live", "bake");
    ui.end();
    ui.text("Sliders dirty the live session and re-upload derivedMeshResult.", "hint");
    ui.text("Ready", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(900.0, 24.0, 350.0, 460.0);
    fusionUiReady = true;
}

function fusionSetStatus(message) {
    if (!fusionUiReady) return;
    ui.select("lab");
    ui.setText("status", message);
}

function fusionBakeLive() {
    if (fusionLive == null || !fusionLive.isActive()) {
        fusionSetStatus("No active live session to bake");
        return;
    }
    local baked = fusionRequire(fusionLive.bakeToMesh(), "bake live").value;
    local uploaded = fusionRequire(procgen.uploadMesh(baked, gfx), "upload bake").value;
    fusionLiveVisual.setMesh(uploaded);
    fusionRebuildLiveSource();
    fusionSetStatus("Baked once, then re-activated  rev=" + fusionLive.getRevision());
}

function fusionBuildAll() {
    fusionBuildStaticShowcase();
    fusionRebuildLiveSource();
    if (!fusionUiReady) fusionBuildUi();
    fusionSetStatus("rev=" + fusionLive.getRevision() + "  softSnap=" + softSnap);
    if (!fusionPassPrinted) {
        print("MESH_CONTACT_FUSION_PASS static=blend-off+on(noSoftSnap) live=MeshAdhereLive " +
              "derivedMeshResult=upload setParams=realtime bake=optional\n");
        fusionPassPrinted = true;
    }
}

if (fusionCamera == null) {
    // Look down -Z so world -X is screen-left (matches LEFT/CENTER/RIGHT labels).
    fusionCamera = eve.Camera3D();
    fusionCamera.setEye(0.0, 5.4, 14.5);
    fusionCamera.setTarget(0.0, 0.55, 0.0);
    fusionCamera.setUp(0.0, 1.0, 0.0);
    fusionCamera.setFov(42.0);
    fusionCamera.setAmbient(0.28, 0.30, 0.34);
    fusionCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, 0.45, 1.45, 1.28, 1.12);
    gfx.setBackgroundColor(0.05, 0.065, 0.09, 1.0);
}

if (fusionObjects.len() == 0) fusionBuildAll();

function eve_update(dt) {
    fusionFrame += 1;
    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "lab/softSnap") {
            softSnap = !softSnap;
            ui.select("lab");
            ui.setText("softSnap", softSnap ? "Soft snap: ON" : "Soft snap: OFF");
            fusionRebuildLiveSource();
            fusionSetStatus("softSnap=" + softSnap + "  rev=" + fusionLive.getRevision());
        } else if (clicked == "lab/reeval") {
            fusionUploadLive();
            fusionSetStatus("force evaluate  rev=" + fusionLive.getRevision());
        } else if (clicked == "lab/bake") {
            fusionBakeLive();
        }
        clicked = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    local rebuild = false;
    while (changed != "") {
        ui.select("lab");
        if (changed == "lab/edgeRadius") { edgeRadius = ui.getValue("edgeRadius"); rebuild = true; }
        else if (changed == "lab/materialRadius") { materialRadius = ui.getValue("materialRadius"); rebuild = true; }
        else if (changed == "lab/strength") { strength = ui.getValue("strength"); rebuild = true; }
        else if (changed == "lab/sourceLift") { sourceLift = ui.getValue("sourceLift"); rebuild = true; }
        changed = ui.consumeChange();
    }
    if (rebuild) {
        fusionRebuildLiveSource();
        fusionSetStatus("rev=" + fusionLive.getRevision() + "  lift=" + sourceLift);
    }

    if (!fusionScreenshotSaved && fusionFrame > 14 && gfx.saveFramePng("mesh-contact-fusion.png")) {
        fusionScreenshotSaved = true;
        print("mesh-contact-fusion: screenshot saved\n");
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
}
