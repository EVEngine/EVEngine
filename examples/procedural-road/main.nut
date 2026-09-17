// Procedural Road Lab — math-only interchange (no prefab road/bridge meshes).

persist roadObjects = []
persist roadCamera = null
persist roadFrame = 0
persist roadScreenshotSaved = false

function roadRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function roadUploadGroup(mesh, groupName, tintR, tintG, tintB, yLift) {
    local groupIndex = -1;
    for (local i = 0; i < mesh.getGroupCount(); ++i) {
        if (mesh.getGroupName(i) == groupName) {
            groupIndex = i;
            break;
        }
    }
    if (groupIndex < 0) return;
    local part = mesh.copyGroup(groupIndex);
    if (part == null) return;
    local uploaded = roadRequire(procgen.uploadMesh(part, gfx), "upload " + groupName).value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(0.0, yLift, 0.0);
    object.setTint(tintR, tintG, tintB, 1.0);
    object.setRoughness(groupName == "asphalt" ? 0.85 : 0.55);
    object.setMetallic(0.02);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    roadObjects.append(object);
}

if (roadCamera == null) {
    roadCamera = eve.Camera3D();
    roadCamera.setEye(42.0, 28.0, 36.0);
    roadCamera.setTarget(0.0, 3.0, 0.0);
    roadCamera.setUp(0.0, 1.0, 0.0);
    roadCamera.setFov(48.0);
    roadCamera.setAmbient(0.25, 0.28, 0.34);
    roadCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.5, 1.4, 1.25);
    gfx.setBackgroundColor(0.45, 0.12, 0.10, 1.0);
}

if (roadObjects.len() == 0) {
    local params = roadRequire(procgen.newParams(), "new params").value;
    roadRequire(params.setSeed(7), "seed");
    params.setFloat("span", 48.0);
    params.setFloat("bridgeHeight", 8.0);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 56);
    params.setBool("piers", true);
    params.setBool("markings", true);
    params.setBool("navigation", true);
    params.setBool("junctions", true);

    local mesh = roadRequire(procgen.buildMesh("mesh.roadNetwork", params), "build road mesh").value;
    roadUploadGroup(mesh, "asphalt", 0.16, 0.16, 0.18, 0.0);
    roadUploadGroup(mesh, "curb", 0.55, 0.55, 0.52, 0.0);
    roadUploadGroup(mesh, "sidewalk", 0.72, 0.70, 0.66, 0.0);
    roadUploadGroup(mesh, "deck", 0.42, 0.42, 0.40, 0.0);
    roadUploadGroup(mesh, "pier", 0.62, 0.60, 0.56, 0.0);
    roadUploadGroup(mesh, "marking", 0.95, 0.92, 0.85, 0.01);
    roadUploadGroup(mesh, "nav", 0.15, 0.85, 1.0, 0.02);

    local ground = gfx.newMeshBox(1.0, 1.0, 1.0);
    local platform = eve.Renderable3D();
    platform.setMesh(ground);
    platform.setPosition(0.0, -0.4, 0.0);
    platform.setScale(70.0, 0.4, 70.0);
    platform.setTint(0.72, 0.14, 0.12, 1.0);
    platform.setRoughness(0.9);
    roadObjects.append(platform);

    local texParams = roadRequire(procgen.newParams(), "tex params").value;
    texParams.setSize(256, 64);
    texParams.setBool("zebra", true);
    local markings = roadRequire(procgen.generateTexture("tex.roadMarkings", texParams, gfx), "road markings tex").value;
    print("PROCEDURAL_ROAD_PASS edgesMeta=" + mesh.getMeta("edges", "?") +
          " nodesMeta=" + mesh.getMeta("nodes", "?") +
          " groups=" + mesh.getGroupCount() +
          " verts=" + mesh.getVertexCount() +
          " markingTex=" + markings.getWidth() + "x" + markings.getHeight() + "\n");
}

function eve_update(dt) {
    roadFrame += 1;
    if (roadCamera != null) {
        local t = roadFrame * 0.008;
        roadCamera.setEye(42.0 * cos(t * 0.35), 26.0 + 4.0 * sin(t * 0.2), 36.0 * sin(t * 0.35));
        roadCamera.setTarget(0.0, 3.0, 0.0);
    }
    if (!roadScreenshotSaved && roadFrame > 18 && gfx.saveFramePng("procedural-road.png")) {
        roadScreenshotSaved = true;
        print("procedural-road: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
