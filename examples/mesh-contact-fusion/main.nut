// Mesh Contact Fusion — geometric soft-snap fusion, not just a color stripe.
// Screen left → right (camera looks down -Z):
//   hard join · gap soft-snap weld · live A←B melt onto ground

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

persist edgeRadius = 1.15
persist materialRadius = 1.15
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
        // Cool body → hot contact so the welded band reads under lighting.
        fusionRequire(mesh.setColor(v, 0.55 + 0.45 * w, 0.62 - 0.12 * w, 0.78 - 0.48 * w, 1.0),
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

function fusionMergePair(mode) {
    // Cubes are bottom-aligned, width 1.2 → half-extent 0.6.
    // hard: slight overlap, blend OFF (control).
    // fuse: small GAP + softSnap ON — facing faces pull together into a weld.
    // Deep overlap + softSnap caves the joint; that is not the demo path.
    local dx = 0.58;
    local enableBlend = false;
    local soft = false;
    local radius = 0.7;
    local normals = 0.0;
    if (mode == "fuse") {
        dx = 0.70;          // gap ≈ 0.20 between facing faces
        enableBlend = true;
        soft = true;
        radius = 0.95;
        normals = 0.9;
    }

    local left = fusionSubdivide(fusionRecipe("prototype.cube", 1.2, 1.55, 1.2, 8), 2);
    local right = fusionSubdivide(fusionRecipe("prototype.cube", 1.2, 1.55, 1.2, 8), 2);
    local plan = eve.MeshMergePlan();
    fusionRequire(plan.appendSource(left, -dx, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, "matA"), "append left");
    fusionRequire(plan.appendSource(right, dx, 0.0, 0.0, 8.0, 1.0, 1.0, 1.0, "matB"), "append right");
    fusionRequire(plan.setPivotMode(1), "world pivot");
    fusionRequire(plan.setEnableContactBlend(enableBlend), "set blend enable");
    if (enableBlend) {
        fusionRequire(plan.setContactBlend(radius, radius, 1.0, normals, 1.0, 0.0, soft, "smooth"),
                      "set contact blend");
    }
    local merged = fusionRequire(eve.mergeStaticMeshes(plan), "merge " + mode);
    local mesh = merged.value;
    if (enableBlend) mesh = fusionPaintContactBand(mesh);
    print("mesh-contact-fusion: mode=" + mode + " dx=" + dx + " soft=" + soft +
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
    // Subdivided cylinder floats above ground; soft-snap melts the contact skirt onto B.
    local cyl = fusionSubdivide(fusionRecipe("prototype.cylinder1", 1.35, 1.7, 1.35, 28), 1);
    local lifted = fusionTransform(cyl, 0.0, sourceLift, 0.0);
    if (fusionLive == null || !fusionLive.isActive()) {
        local ground = fusionRecipe("prototype.ground", 4.2, 0.18, 4.2, 8);
        fusionLive = eve.MeshAdhereLive();
        fusionRequire(fusionLive.activate(lifted, ground), "activate live adhere");
        if (fusionSurfaceVisual == null)
            fusionSurfaceVisual = fusionAdd(ground, 6.0, 0.0, 0.38, 0.44, 0.50, 0.85);
        if (fusionGhostVisual == null) {
            // Ghost = pre-adhere pose so the melt is obvious.
            fusionGhostVisual = fusionAdd(lifted, 6.0, 0.0, 0.55, 0.60, 0.70, 0.95);
            fusionGhostVisual.setTint(0.55, 0.60, 0.70, 0.35);
        }
        if (fusionLiveVisual == null) {
            fusionLiveVisual = eve.Renderable3D();
            fusionLiveVisual.setPosition(6.0, 0.0, 0.0);
            fusionLiveVisual.setTint(0.95, 0.55, 0.28, 1.0);
            fusionLiveVisual.setRoughness(0.48);
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
    fusionAdd(plain, -6.0, 0.0, 0.70, 0.74, 0.80);
    local fused = fusionMergePair("fuse");
    // Near-white tint so painted contact RGB + welded silhouette read clearly.
    fusionAdd(fused, 0.0, 0.0, 1.0, 1.0, 1.0, 0.5);
}

function fusionBuildUi() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("CONTACT FUSION", "root");
    ui.text("LEFT  hard join  (blend OFF)", "staticOff");
    ui.text("CENTER  gap soft-snap weld  (faces pull together)", "staticOn");
    ui.text("RIGHT  live melt  (ghost = before, orange = after)", "liveLabel");
    ui.slider("Edge radius", edgeRadius, 0.2, 2.0, "edgeRadius");
    ui.slider("Material radius", materialRadius, 0.2, 2.0, "materialRadius");
    ui.slider("Strength", strength, 0.0, 1.0, "strength");
    ui.slider("Source lift", sourceLift, 0.05, 0.9, "sourceLift");
    ui.beginRow("actions", 8.0);
    ui.button(softSnap ? "Soft snap: ON" : "Soft snap: OFF", "softSnap");
    ui.button("Re-evaluate", "reeval");
    ui.button("Bake live", "bake");
    ui.end();
    ui.text("Soft-snap across a small gap welds; deep overlap caves — demo uses gap.", "hint");
    ui.text("Ready", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(900.0, 24.0, 360.0, 480.0);
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
        print("MESH_CONTACT_FUSION_PASS hard+gapWeld+liveMelt derivedMeshResult=upload\n");
        fusionPassPrinted = true;
    }
}

if (fusionCamera == null) {
    fusionCamera = eve.Camera3D();
    fusionCamera.setEye(0.0, 5.8, 15.5);
    fusionCamera.setTarget(0.0, 0.7, 0.0);
    fusionCamera.setUp(0.0, 1.0, 0.0);
    fusionCamera.setFov(40.0);
    fusionCamera.setAmbient(0.32, 0.34, 0.38);
    fusionCamera.setActive(true);
    gfx.setDirectionalLight(-0.25, -1.0, 0.55, 1.55, 1.35, 1.18);
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
