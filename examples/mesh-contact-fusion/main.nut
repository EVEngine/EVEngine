// Mesh Contact Fusion — static merge (± opt-in contact blend) + live MeshAdhereLive.

persist fusionObjects = []
persist fusionCamera = null
persist fusionLive = null
persist fusionLiveVisual = null
persist fusionSurfaceVisual = null
persist fusionUiReady = false
persist fusionFrame = 0
persist fusionScreenshotSaved = false
persist fusionPassPrinted = false

persist edgeRadius = 0.85
persist materialRadius = 0.85
persist strength = 1.0
persist softSnap = true
persist sourceLift = 0.35

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

function fusionAdd(mesh, x, tintR, tintG, tintB) {
    local uploaded = fusionRequire(procgen.uploadMesh(mesh, gfx), "upload mesh").value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(x, 0.0, 0.0);
    object.setTint(tintR, tintG, tintB, 1.0);
    object.setRoughness(0.7);
    object.setMetallic(0.05);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    fusionObjects.append(object);
    return object;
}

function fusionMergePair(enableBlend) {
    local left = fusionRecipe("prototype.cube", 1.2, 1.2, 1.2, 8);
    local right = fusionRecipe("prototype.cube", 1.2, 1.2, 1.2, 8);
    local plan = eve.MeshMergePlan();
    fusionRequire(plan.appendSource(left, -0.45, 0.6, 0.0, 0.0, 1.0, 1.0, 1.0, "matA"), "append left");
    fusionRequire(plan.appendSource(right, 0.45, 0.6, 0.0, 18.0, 1.0, 1.0, 1.0, "matB"), "append right");
    fusionRequire(plan.setPivotMode(1), "world pivot");
    fusionRequire(plan.setEnableContactBlend(enableBlend), "set blend enable");
    if (enableBlend) {
        fusionRequire(plan.setContactBlend(0.9, 0.9, 1.0, 1.0, 1.0, 0.0, true, "smooth"),
                      "set contact blend");
    }
    local merged = fusionRequire(eve.mergeStaticMeshes(plan),
                                 enableBlend ? "merge with blend" : "merge without blend");
    print("mesh-contact-fusion: static sources=" + plan.getSourceCount() +
          " blend=" + plan.getEnableContactBlend() +
          " verts=" + merged.value.getVertexCount() +
          " meta=" + merged.value.getMeta("contactBlend.enabled", "?") + "\n");
    return merged.value;
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
    local cube = fusionRecipe("prototype.cube", 1.0, 1.0, 1.0, 10);
    local lifted = fusionTransform(cube, 0.0, sourceLift, 0.0);
    if (fusionLive == null || !fusionLive.isActive()) {
        local ground = fusionRecipe("prototype.cube", 4.5, 0.12, 4.5, 4);
        ground = fusionTransform(ground, 0.0, 0.0, 0.0);
        fusionLive = eve.MeshAdhereLive();
        fusionRequire(fusionLive.activate(lifted, ground), "activate live adhere");
        if (fusionSurfaceVisual == null) {
            fusionSurfaceVisual = fusionAdd(ground, 6.0, 0.35, 0.42, 0.48);
            fusionSurfaceVisual.setPosition(6.0, 0.0, 0.0);
        }
        if (fusionLiveVisual == null) {
            fusionLiveVisual = eve.Renderable3D();
            fusionLiveVisual.setPosition(6.0, 0.0, 0.0);
            fusionLiveVisual.setTint(0.92, 0.55, 0.28, 1.0);
            fusionLiveVisual.setRoughness(0.62);
            fusionLiveVisual.setMetallic(0.08);
            fusionLiveVisual.setCastShadow(true);
            fusionLiveVisual.setReceiveShadow(true);
            fusionObjects.append(fusionLiveVisual);
        }
    } else {
        fusionRequire(fusionLive.setSource(lifted), "replace live source");
    }
    fusionRequire(fusionLive.setParams(edgeRadius, materialRadius, strength, 1.0, 1.0, 0.0, softSnap, "smooth"),
                  "set live params");
    fusionUploadLive();
}

function fusionBuildStaticShowcase() {
    local plain = fusionMergePair(false);
    fusionAdd(plain, -6.0, 0.62, 0.68, 0.78);
    local blended = fusionMergePair(true);
    fusionAdd(blended, 0.0, 0.78, 0.52, 0.34);
}

function fusionBuildUi() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("CONTACT FUSION", "root");
    ui.text("STATIC  left=blend off  ·  center=blend on", "staticLabel");
    ui.text("LIVE     right=MeshAdhereLive (A on B)", "liveLabel");
    ui.slider("Edge radius", edgeRadius, 0.05, 2.0, "edgeRadius");
    ui.slider("Material radius", materialRadius, 0.05, 2.0, "materialRadius");
    ui.slider("Strength", strength, 0.0, 1.0, "strength");
    ui.slider("Source lift", sourceLift, 0.05, 1.2, "sourceLift");
    ui.beginRow("actions", 8.0);
    ui.button(softSnap ? "Soft snap: ON" : "Soft snap: OFF", "softSnap");
    ui.button("Re-evaluate", "reeval");
    ui.button("Bake live", "bake");
    ui.end();
    ui.text("Drag sliders to dirty + evaluate the live session.", "hint");
    ui.text("Ready", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(920.0, 28.0, 330.0, 420.0);
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
    fusionSetStatus("Baked revision frozen; session inactive");
    // Re-activate so sliders keep working after bake demo.
    fusionRebuildLiveSource();
    fusionSetStatus("Baked once, then re-activated  rev=" + fusionLive.getRevision());
}

function fusionBuildAll() {
    fusionBuildStaticShowcase();
    fusionRebuildLiveSource();
    if (!fusionUiReady) fusionBuildUi();
    fusionSetStatus("rev=" + fusionLive.getRevision() + "  softSnap=" + softSnap);
    if (!fusionPassPrinted) {
        print("MESH_CONTACT_FUSION_PASS static=blend-off+on live=MeshAdhereLive " +
              "derivedMeshResult=upload setParams=realtime bake=optional\n");
        fusionPassPrinted = true;
    }
}

if (fusionCamera == null) {
    fusionCamera = eve.Camera3D();
    fusionCamera.setEye(0.0, 4.8, -14.5);
    fusionCamera.setTarget(0.0, 0.6, 0.0);
    fusionCamera.setUp(0.0, 1.0, 0.0);
    fusionCamera.setFov(46.0);
    fusionCamera.setAmbient(0.22, 0.25, 0.3);
    fusionCamera.setActive(true);
    gfx.setDirectionalLight(-0.4, -1.0, -0.3, 1.4, 1.25, 1.1);
    gfx.setBackgroundColor(0.045, 0.055, 0.08, 1.0);
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
