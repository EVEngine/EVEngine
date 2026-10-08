// Mesh Contact Fusion — contact-band soft-snap that changes silhouette.
// Cubes across a gap only "clap shut"; spheres form a welded neck (fusion look).
// Screen left → right (camera looks down −Z):
//   two spheres, gap, blend OFF · same gap + soft-snap neck · live melt + ghost

persist fusionObjects = []
persist fusionCamera = null
persist fusionLive = null
persist fusionLiveVisual = null
persist fusionGhostVisual = null
persist fusionSurfaceVisual = null
persist fusionUiReady = false
persist fusionFrame = 0
persist fusionScreenshotSaved = false
persist fusionPassPrinted = false

persist edgeRadius = 1.55
persist materialRadius = 1.55
persist strength = 1.0
persist softSnap = true
persist sourceLift = 0.28

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

function fusionSubdivide(mesh, levels) {
    local graph = fusionRequire(procgen.newMeshModifierGraph(), "subdivide graph").value;
    fusionRequire(graph.addNode("source", "mesh.input"), "add subdiv source");
    fusionRequire(graph.addNode("subdivide", "mesh.subdivide"), "add subdiv");
    fusionRequire(graph.addNode("weld", "mesh.weld"), "add weld");
    fusionRequire(graph.addNode("out", "mesh.output"), "add subdiv out");
    fusionRequire(graph.setNodeMesh("source", mesh), "bind subdiv source");
    fusionRequire(graph.setNodeInt("subdivide", "levels", levels), "set subdiv levels");
    fusionRequire(graph.setNodeFloat("weld", "tolerance", 0.0001), "set weld");
    fusionRequire(graph.connect("source", "subdivide", 0), "connect subdiv");
    fusionRequire(graph.connect("subdivide", "weld", 0), "connect weld");
    fusionRequire(graph.connect("weld", "out", 0), "connect subdiv out");
    return fusionRequire(graph.executeResult("out"), "execute subdiv").value;
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

function fusionPaintContactBand(mesh) {
    if (!mesh.hasVertexColors()) return mesh;
    for (local v = 0; v < mesh.getVertexCount(); ++v) {
        local w = mesh.getColor(v, 3);
        // Very light warm tint — heavy contact paint was masking continuous shading.
        fusionRequire(mesh.setColor(v, 0.78 + 0.14 * w, 0.80 + 0.02 * w, 0.84 - 0.10 * w, 1.0),
                      "paint contact " + v);
    }
    return mesh;
}

function fusionAdd(mesh, x, y, tintR, tintG, tintB, roughness) {
    local uploaded = fusionRequire(procgen.uploadMesh(mesh, gfx), "upload mesh").value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(x, y, 0.0);
    object.setTint(tintR, tintG, tintB, 1.0);
    object.setRoughness(roughness);
    object.setMetallic(0.04);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    fusionObjects.append(object);
    return object;
}

function fusionRelax(mesh, strength, iterations) {
    // Light Laplacian smooth after soft-snap/weld so the neck shades continuously.
    local graph = fusionRequire(procgen.newMeshModifierGraph(), "relax graph").value;
    fusionRequire(graph.addNode("source", "mesh.input"), "add relax source");
    fusionRequire(graph.addNode("smooth", "deform.smooth"), "add relax smooth");
    fusionRequire(graph.addNode("out", "mesh.output"), "add relax out");
    fusionRequire(graph.setNodeMesh("source", mesh), "bind relax source");
    fusionRequire(graph.setNodeFloat("smooth", "strength", strength), "set relax strength");
    fusionRequire(graph.setNodeInt("smooth", "iterations", iterations), "set relax iters");
    fusionRequire(graph.connect("source", "smooth", 0), "connect relax");
    fusionRequire(graph.connect("smooth", "out", 0), "connect relax out");
    return fusionRequire(graph.executeResult("out"), "execute relax").value;
}

function fusionMergePair(mode) {
    // Two spheres, same layout for hard vs fuse — only soft-snap differs.
    // Diameter 1.7 → radius 0.85. Centers at ±0.94 → air gap ≈0.18 (obvious on left).
    // Partial soft-snap + post-blend weld + heavy Laplacian → continuous peanut neck
    // (full mutual snap pinches a hard crease even after normal rebuild).
    local dx = 0.94;
    local size = 1.7;
    local enableBlend = false;
    local soft = false;
    local radius = 0.7;
    local normals = 0.0;
    local weldTol = 0.0;
    if (mode == "fuse") {
        enableBlend = true;
        soft = true;
        radius = 1.4;
        normals = 1.0;
        weldTol = 0.12;
    }

    local left = fusionSubdivide(fusionRecipe("prototype.sphere", size, size, size, 28), 1);
    local right = fusionSubdivide(fusionRecipe("prototype.sphere", size, size, size, 28), 1);
    local plan = eve.MeshMergePlan();
    fusionRequire(plan.appendSource(left, -dx, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, "matA"), "append left");
    fusionRequire(plan.appendSource(right, dx, 0.0, 0.0, 8.0, 1.0, 1.0, 1.0, "matB"), "append right");
    fusionRequire(plan.setPivotMode(1), "world pivot");
    fusionRequire(plan.setEnableContactBlend(enableBlend), "set blend enable");
    if (enableBlend) {
        // Partial snap + small offset: bridge the gap without mutual cave-in.
        fusionRequire(plan.setContactBlend(radius, radius, 0.55, normals, 1.0, 0.03, soft, "smooth"),
                      "set contact blend");
    }
    if (weldTol > 0.0) fusionRequire(plan.setWeldTolerance(weldTol), "set post-blend weld");
    local merged = fusionRequire(eve.mergeStaticMeshes(plan), "merge " + mode);
    local mesh = merged.value;
    if (enableBlend) {
        mesh = fusionRelax(mesh, 0.7, 8);
        mesh = fusionPaintContactBand(mesh);
    }
    print("mesh-contact-fusion: mode=" + mode + " dx=" + dx + " soft=" + soft +
          " weld=" + weldTol +
          " verts=" + mesh.getVertexCount() +
          " colors=" + mesh.hasVertexColors() +
          " meta=" + mesh.getMeta("contactBlend.enabled", "?") +
          " normals=" + mesh.getMeta("contactBlend.normals", "?") + "\n");
    return mesh;
}

function fusionUploadLive() {
    if (fusionLive == null || fusionLiveVisual == null) return;
    if (fusionLive.isDirty())
        fusionRequire(fusionLive.evaluate(false), "evaluate live adhere");
    local snapshot = fusionRequire(fusionLive.derivedMeshResult(), "live derived snapshot").value;
    // Soft-snap already recalculates triangle normals; relax softens the melt rim crease.
    snapshot = fusionRelax(snapshot, 0.4, 3);
    snapshot = fusionPaintContactBand(snapshot);
    local uploaded = fusionRequire(procgen.uploadMesh(snapshot, gfx), "upload live derived").value;
    fusionLiveVisual.setMesh(uploaded);
}

function fusionRebuildLiveSource() {
    // Subdivided sphere floats above ground; soft-snap flattens a wide contact pancake.
    local ball = fusionSubdivide(fusionRecipe("prototype.sphere", 1.85, 1.85, 1.85, 28), 1);
    local lifted = fusionTransform(ball, 0.0, sourceLift, 0.0);
    if (fusionLive == null || !fusionLive.isActive()) {
        local ground = fusionRecipe("prototype.ground", 4.8, 0.14, 4.8, 10);
        fusionLive = eve.MeshAdhereLive();
        fusionRequire(fusionLive.activate(lifted, ground), "activate live adhere");
        if (fusionSurfaceVisual == null)
            fusionSurfaceVisual = fusionAdd(ground, 4.8, 0.0, 0.34, 0.40, 0.46, 0.88);
        if (fusionGhostVisual == null) {
            // Ghost = pre-adhere pose, parked beside the melt (not composited over it).
            fusionGhostVisual = fusionAdd(lifted, 7.4, 0.0, 0.40, 0.46, 0.52, 0.95);
            fusionGhostVisual.setCastShadow(false);
        }
        if (fusionLiveVisual == null) {
            fusionLiveVisual = eve.Renderable3D();
            fusionLiveVisual.setPosition(4.8, 0.0, 0.0);
            // Near-white so painted contact RGB (hot weld patch) reads clearly.
            fusionLiveVisual.setTint(1.0, 1.0, 1.0, 1.0);
            fusionLiveVisual.setRoughness(0.45);
            fusionLiveVisual.setMetallic(0.05);
            fusionLiveVisual.setCastShadow(true);
            fusionLiveVisual.setReceiveShadow(true);
            fusionObjects.append(fusionLiveVisual);
        }
    } else {
        fusionRequire(fusionLive.setSource(lifted), "replace live source");
        if (fusionGhostVisual != null) {
            local ghostUpload = fusionRequire(procgen.uploadMesh(lifted, gfx), "upload ghost").value;
            fusionGhostVisual.setMesh(ghostUpload);
        }
    }
    fusionRequire(fusionLive.setParams(edgeRadius, materialRadius, strength, 1.0, 1.0, 0.0, softSnap, "smooth"),
                  "set live params");
    fusionUploadLive();
}

function fusionBuildStaticShowcase() {
    local plain = fusionMergePair("hard");
    fusionAdd(plain, -4.6, 0.0, 0.72, 0.76, 0.82, 0.55);
    local fused = fusionMergePair("fuse");
    // Near-white tint so painted weld neck + deformed silhouette read clearly.
    fusionAdd(fused, 0.0, 0.0, 1.0, 1.0, 1.0, 0.42);
}

function fusionBuildUi() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("CONTACT FUSION", "root");
    ui.text("LEFT  spheres + gap, blend OFF", "staticOff");
    ui.text("CENTER  same gap + soft-snap  (welded neck)", "staticOn");
    ui.text("RIGHT  live melt pancake + ghost (far right)", "liveLabel");
    ui.slider("Edge radius", edgeRadius, 0.2, 2.0, "edgeRadius");
    ui.slider("Material radius", materialRadius, 0.2, 2.0, "materialRadius");
    ui.slider("Strength", strength, 0.0, 1.0, "strength");
    ui.slider("Source lift", sourceLift, 0.05, 0.9, "sourceLift");
    ui.beginRow("actions", 8.0);
    ui.button(softSnap ? "Soft snap: ON" : "Soft snap: OFF", "softSnap");
    ui.button("Re-evaluate", "reeval");
    ui.button("Bake live", "bake");
    ui.end();
    ui.text("Soft-snap bridges a gap into a neck; cubes only clap shut.", "hint");
    ui.text("Ready", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(920.0, 20.0, 340.0, 460.0);
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
    baked = fusionPaintContactBand(baked);
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
        print("MESH_CONTACT_FUSION_PASS hard+gapWeld+liveMelt derivedMeshResult=upload\n");
        fusionPassPrinted = true;
    }
}

if (fusionCamera == null) {
    fusionCamera = eve.Camera3D();
    // Elevated 3/4 view; FOV/eye sized so left gap pair + center neck + melt all fit.
    fusionCamera.setEye(0.4, 5.6, 15.5);
    fusionCamera.setTarget(0.4, 0.5, 0.0);
    fusionCamera.setUp(0.0, 1.0, 0.0);
    fusionCamera.setFov(42.0);
    fusionCamera.setAmbient(0.34, 0.36, 0.40);
    fusionCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, 0.45, 1.6, 1.4, 1.2);
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

    if (!fusionScreenshotSaved && fusionFrame > 16 && gfx.saveFramePng("mesh-contact-fusion.png")) {
        fusionScreenshotSaved = true;
        print("mesh-contact-fusion: screenshot saved\n");
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
}
