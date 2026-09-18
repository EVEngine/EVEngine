// Procedural Road Lab — math-only interchange matching the reference look:
// dark asphalt, raised barriers, sidewalks, piers, markings, cyan nav overlays.

persist roadParts = []
persist roadGround = null
persist roadWater = null
persist roadCamera = null
persist roadFrame = 0
persist roadScreenshotSaved = false
persist roadReady = false
persist roadMaterials = {}

function roadRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

// albedo tint + roughness + metallic + receiveLight (Material drives tint; GPU-driven ignores vertex colors)
function roadStyleForGroup(name) {
    if (name == "asphalt") return [0.20, 0.20, 0.22, 0.95, 0.0, true];
    if (name == "curb") return [0.62, 0.62, 0.60, 0.55, 0.02, true];
    if (name == "sidewalk") return [0.90, 0.90, 0.88, 0.72, 0.0, true];
    if (name == "deck") return [0.40, 0.40, 0.38, 0.88, 0.0, true];
    if (name == "pier") return [0.74, 0.72, 0.68, 0.62, 0.0, true];
    if (name == "marking") return [0.98, 0.98, 0.96, 0.30, 0.0, false];
    if (name == "markingYellow") return [0.98, 0.82, 0.08, 0.35, 0.0, false];
    if (name == "nav") return [0.05, 0.95, 1.0, 0.18, 0.0, false];
    return [0.55, 0.55, 0.52, 0.75, 0.0, true];
}

if (roadCamera == null) {
    roadCamera = eve.Camera3D();
    roadCamera.setEye(36.0, 28.0, 34.0);
    roadCamera.setTarget(0.0, 2.0, 0.0);
    roadCamera.setUp(0.0, 1.0, 0.0);
    roadCamera.setFov(42.0);
    roadCamera.setClipPlanes(0.5, 400.0);
    roadCamera.setAmbient(0.42, 0.44, 0.48);
    roadCamera.setActive(true);
    gfx.setDirectionalLight(-0.40, -1.0, -0.30, 1.55, 1.48, 1.35);
    gfx.setBackgroundColor(0.62, 0.22, 0.16, 1.0);
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

    roadParts = [];
    roadMaterials = {};
    local groupSummary = "";
    for (local i = 0; i < cpu.getGroupCount(); ++i) {
        local component = cpu.copyGroup(i);
        if (component == null || component.empty()) continue;
        local mesh = roadRequire(procgen.uploadMesh(component, gfx), "upload group " + i).value;
        local part = eve.Renderable3D();
        local name = cpu.getGroupName(i);
        local style = roadStyleForGroup(name);
        if (!(name in roadMaterials)) {
            local material = gfx.newMaterial();
            material.setTint(style[0], style[1], style[2], 1.0);
            material.setRoughness(style[3]);
            material.setMetallic(style[4]);
            material.setReceiveLight(style[5]);
            if (!style[5]) material.setShadingModel("unlit");
            roadMaterials[name] <- material;
        }
        part.setMesh(mesh);
        part.setMaterial(roadMaterials[name]);
        part.setCastShadow(name != "nav" && name != "marking" && name != "markingYellow");
        part.setReceiveShadow(name == "asphalt" || name == "sidewalk" || name == "deck" || name == "curb");
        roadParts.append(part);
        groupSummary += name + ":" + component.getVertexCount() + " ";
    }

    roadGround = eve.Renderable3D();
    roadGround.setMesh(gfx.newMeshCube(1.0));
    roadGround.setPosition(0.0, -0.70, 0.0);
    roadGround.setScale(110.0, 0.6, 110.0);
    local groundMat = gfx.newMaterial();
    groundMat.setTint(0.82, 0.18, 0.12, 1.0);
    groundMat.setRoughness(0.98);
    groundMat.setMetallic(0.0);
    roadGround.setMaterial(groundMat);
    roadGround.setCastShadow(false);
    roadGround.setReceiveShadow(true);

    // Water sheet with real alpha so the red ground still reads underneath.
    roadWater = eve.Renderable3D();
    roadWater.setMesh(gfx.newMeshCube(1.0));
    roadWater.setPosition(0.0, -0.28, 0.0);
    roadWater.setScale(110.0, 0.03, 110.0);
    local waterMat = gfx.newMaterial();
    waterMat.setTint(0.05, 0.55, 0.62, 0.38);
    waterMat.setRoughness(0.10);
    waterMat.setMetallic(0.30);
    waterMat.setReceiveLight(false);
    waterMat.setShadingModel("unlit");
    waterMat.setSurfaceMode("transparent");
    waterMat.setBlendMode("alpha");
    roadWater.setMaterial(waterMat);
    roadWater.setCastShadow(false);
    roadWater.setReceiveShadow(false);

    local texParams = roadRequire(procgen.newParams(), "tex params").value;
    texParams.setSize(256, 64);
    texParams.setBool("zebra", true);
    local markings = roadRequire(procgen.generateTexture("tex.roadMarkings", texParams, gfx), "markings").value;

    print("PROCEDURAL_ROAD_PASS verts=" + cpu.getVertexCount() + " groups=" + cpu.getGroupCount() +
          " parts=" + roadParts.len() + " markingTex=" + markings.getWidth() + "x" + markings.getHeight() +
          " colors=" + (cpu.hasVertexColors() ? "yes" : "no") + " [" + groupSummary + "]\n");
    roadReady = true;
}

function eve_update(dt) {
    if (!roadReady) return;
    roadFrame += 1;
    if (roadCamera != null) {
        local t = roadFrame * 0.005;
        local radius = 42.0;
        local height = 26.0 + 3.0 * sin(t * 0.18);
        roadCamera.setEye(radius * cos(t * 0.20), height, radius * sin(t * 0.20));
        roadCamera.setTarget(0.0, 2.5, 0.0);
    }
    if (!roadScreenshotSaved && roadFrame > 28 && roadFrame < 36 && gfx.saveFramePng("procedural-road.png")) {
        roadScreenshotSaved = true;
        print("procedural-road: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
