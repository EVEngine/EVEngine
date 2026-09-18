// Procedural Road Lab — math-only interchange matching the reference look:
// dark asphalt, raised barriers, sidewalks, piers, markings, cyan nav overlays.

persist roadParts = []
persist roadGround = null
persist roadWater = null
persist roadCamera = null
persist roadFrame = 0
persist roadScreenshotSaved = false
persist roadReady = false

function roadRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function roadTintForGroup(name) {
    if (name == "asphalt") return [0.14, 0.14, 0.16, 1.0, 0.92, 0.0];
    if (name == "curb") return [0.80, 0.80, 0.78, 1.0, 0.55, 0.02];
    if (name == "sidewalk") return [0.86, 0.86, 0.84, 1.0, 0.70, 0.0];
    if (name == "deck") return [0.48, 0.48, 0.46, 1.0, 0.80, 0.0];
    if (name == "pier") return [0.70, 0.68, 0.64, 1.0, 0.62, 0.0];
    if (name == "marking") return [0.96, 0.96, 0.94, 1.0, 0.35, 0.0];
    if (name == "markingYellow") return [0.95, 0.78, 0.10, 1.0, 0.40, 0.0];
    if (name == "nav") return [0.10, 0.95, 1.0, 1.0, 0.25, 0.05];
    return [0.55, 0.55, 0.52, 1.0, 0.75, 0.0];
}

if (roadCamera == null) {
    roadCamera = eve.Camera3D();
    // High-angle overview matching the reference interchange framing.
    roadCamera.setEye(26.0, 52.0, 30.0);
    roadCamera.setTarget(0.0, 2.5, 0.0);
    roadCamera.setUp(0.0, 1.0, 0.0);
    roadCamera.setFov(40.0);
    roadCamera.setClipPlanes(0.5, 400.0);
    roadCamera.setAmbient(0.32, 0.34, 0.38);
    roadCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, -0.45, 1.65, 1.55, 1.40);
    gfx.setBackgroundColor(0.55, 0.18, 0.14, 1.0);
}

if (!roadReady) {
    local params = roadRequire(procgen.newParams(), "new params").value;
    roadRequire(params.setSeed(7), "seed");
    params.setFloat("span", 44.0);
    params.setFloat("bridgeHeight", 8.0);
    params.setInt("lanes", 2);
    params.setInt("pathSegments", 40);
    params.setBool("piers", true);
    params.setBool("markings", true);
    params.setBool("navigation", true);
    params.setBool("junctions", true);

    local cpu = roadRequire(procgen.buildMesh("mesh.roadNetwork", params), "buildMesh").value;

    // Per-group materials — one Renderable3D each (castle-generator pattern).
    roadParts = [];
    for (local i = 0; i < cpu.getGroupCount(); ++i) {
        local component = cpu.copyGroup(i);
        if (component == null || component.empty()) continue;
        local mesh = roadRequire(procgen.uploadMesh(component, gfx), "upload group " + i).value;
        local part = eve.Renderable3D();
        local name = cpu.getGroupName(i);
        local tint = roadTintForGroup(name);
        part.setMesh(mesh);
        part.setTint(tint[0], tint[1], tint[2], tint[3]);
        part.setRoughness(tint[4]);
        part.setMetallic(tint[5]);
        part.setCastShadow(name != "nav" && name != "marking" && name != "markingYellow");
        part.setReceiveShadow(name != "nav");
        roadParts.append(part);
    }

    // Red ground plane (reference void/ground).
    roadGround = eve.Renderable3D();
    roadGround.setMesh(gfx.newMeshCube(1.0));
    roadGround.setPosition(0.0, -0.55, 0.0);
    roadGround.setScale(95.0, 0.5, 95.0);
    roadGround.setTint(0.72, 0.14, 0.12, 1.0);
    roadGround.setRoughness(0.95);
    roadGround.setMetallic(0.0);
    roadGround.setCastShadow(false);
    roadGround.setReceiveShadow(true);

    // Shallow cyan water sheet over the ground.
    roadWater = eve.Renderable3D();
    roadWater.setMesh(gfx.newMeshCube(1.0));
    roadWater.setPosition(0.0, -0.18, 0.0);
    roadWater.setScale(95.0, 0.06, 95.0);
    roadWater.setTint(0.05, 0.42, 0.52, 0.72);
    roadWater.setRoughness(0.15);
    roadWater.setMetallic(0.35);
    roadWater.setCastShadow(false);
    roadWater.setReceiveShadow(false);

    local texParams = roadRequire(procgen.newParams(), "tex params").value;
    texParams.setSize(256, 64);
    texParams.setBool("zebra", true);
    local markings = roadRequire(procgen.generateTexture("tex.roadMarkings", texParams, gfx), "markings").value;

    print("PROCEDURAL_ROAD_PASS verts=" + cpu.getVertexCount() + " groups=" + cpu.getGroupCount() +
          " parts=" + roadParts.len() + " markingTex=" + markings.getWidth() + "x" + markings.getHeight() +
          " colors=" + (cpu.hasVertexColors() ? "yes" : "no") + "\n");
    roadReady = true;
}

function eve_update(dt) {
    if (!roadReady) return;
    roadFrame += 1;
    if (roadCamera != null) {
        // Slow high-orbit so the interchange reads like the reference overview.
        local t = roadFrame * 0.006;
        local radius = 38.0;
        local height = 48.0 + 4.0 * sin(t * 0.15);
        roadCamera.setEye(radius * cos(t * 0.22), height, radius * sin(t * 0.22));
        roadCamera.setTarget(0.0, 3.0, 0.0);
    }
    if (!roadScreenshotSaved && roadFrame > 30 && gfx.saveFramePng("procedural-road.png")) {
        roadScreenshotSaved = true;
        print("procedural-road: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
