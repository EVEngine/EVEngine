// Procedural Road Lab — math-only interchange (no prefab road/bridge meshes).

persist roadObject = null
persist roadCamera = null
persist roadFrame = 0
persist roadScreenshotSaved = false
persist roadReady = false

function roadRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
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

if (!roadReady) {
    local params = roadRequire(procgen.newParams(), "new params").value;
    roadRequire(params.setSeed(7), "seed");
    params.setFloat("span", 40.0);
    params.setFloat("bridgeHeight", 7.0);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 24);
    params.setBool("piers", true);
    params.setBool("markings", true);
    params.setBool("navigation", true);
    params.setBool("junctions", true);

    local cpu = roadRequire(procgen.buildMesh("mesh.roadNetwork", params), "buildMesh").value;
    local mesh = roadRequire(procgen.uploadMesh(cpu, gfx), "uploadMesh").value;
    roadObject = eve.Renderable3D();
    roadObject.setMesh(mesh);
    roadObject.setTint(0.55, 0.55, 0.52, 1.0);
    roadObject.setRoughness(0.7);
    roadObject.setCastShadow(true);
    roadObject.setReceiveShadow(true);

    local platform = eve.Renderable3D();
    platform.setMesh(gfx.newMeshCube(1.0));
    platform.setPosition(0.0, -0.4, 0.0);
    platform.setScale(70.0, 0.4, 70.0);
    platform.setTint(0.72, 0.14, 0.12, 1.0);
    platform.setRoughness(0.9);

    local texParams = roadRequire(procgen.newParams(), "tex params").value;
    texParams.setSize(256, 64);
    texParams.setBool("zebra", true);
    local markings = roadRequire(procgen.generateTexture("tex.roadMarkings", texParams, gfx), "markings").value;

    print("PROCEDURAL_ROAD_PASS verts=" + cpu.getVertexCount() + " groups=" + cpu.getGroupCount() +
          " markingTex=" + markings.getWidth() + "x" + markings.getHeight() + "\n");
    roadReady = true;
}

function eve_update(dt) {
    if (!roadReady) return;
    roadFrame += 1;
    if (roadCamera != null) {
        local t = roadFrame * 0.008;
        roadCamera.setEye(42.0 * cos(t * 0.35), 26.0 + 4.0 * sin(t * 0.2), 36.0 * sin(t * 0.35));
        roadCamera.setTarget(0.0, 3.0, 0.0);
    }
    if (!roadScreenshotSaved && roadFrame > 24 && gfx.saveFramePng("procedural-road.png")) {
        roadScreenshotSaved = true;
        print("procedural-road: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
