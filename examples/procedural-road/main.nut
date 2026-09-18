// Procedural Road Lab — simple scenes (1-4) + complex interchange (5).
// Keys 1-5 switch scenes. Headless: write scene name into scene.txt.

persist roadParts = []
persist roadGround = null
persist roadCamera = null
persist roadFrame = 0
persist roadScreenshotSaved = false
persist roadReady = false
persist roadMaterials = {}
persist roadPrevKeys = {}
persist roadSceneName = "straight"
persist roadSceneList = ["straight", "curve", "bridge", "cross", "interchange"]

function roadRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function roadPressed(k) {
    local down = keyboard.isDown(k);
    local old = k in roadPrevKeys ? roadPrevKeys[k] : false;
    roadPrevKeys[k] <- down;
    return down && !old;
}

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

function roadClearParts() {
    foreach (part in roadParts) {
        part.setVisible(false);
        part.setMesh(null);
    }
    roadParts = [];
}

function roadCameraForScene(name) {
    if (name == "straight") {
        roadCamera.setEye(22.0, 16.0, 18.0);
        roadCamera.setTarget(0.0, 0.5, 0.0);
    } else if (name == "curve") {
        roadCamera.setEye(16.0, 18.0, 8.0);
        roadCamera.setTarget(-4.0, 0.5, 6.0);
    } else if (name == "bridge") {
        roadCamera.setEye(20.0, 12.0, 18.0);
        roadCamera.setTarget(0.0, 2.5, 0.0);
    } else if (name == "cross") {
        roadCamera.setEye(0.0, 28.0, 18.0);
        roadCamera.setTarget(0.0, 0.3, 0.0);
    } else {
        // interchange — elevated three-quarter overview
        roadCamera.setEye(36.0, 28.0, 32.0);
        roadCamera.setTarget(0.0, 3.0, 0.0);
    }
}

function roadBuildScene(name) {
    roadClearParts();
    roadSceneName = name;
    roadScreenshotSaved = false;
    roadFrame = 0;

    local params = roadRequire(procgen.newParams(), "new params").value;
    roadRequire(params.setSeed(1), "seed");
    params.setString("scene", name);
    params.setFloat("span", name == "interchange" ? 48.0 : (name == "cross" ? 28.0 : 32.0));
    params.setFloat("bridgeHeight", name == "interchange" ? 8.0 : 6.0);
    params.setInt("lanes", 2);
    local segs = 28;
    if (name == "curve" || name == "bridge") segs = 48;
    if (name == "interchange") segs = 36;
    params.setInt("pathSegments", segs);
    params.setBool("piers", true);
    params.setBool("markings", true);
    // Nav overlay is noisy on the complex scene; keep it off for the visual pass.
    params.setBool("navigation", false);
    params.setBool("junctions", name == "cross" || name == "interchange");

    local cpu = roadRequire(procgen.buildMesh("mesh.roadNetwork", params), "buildMesh").value;
    local groupSummary = "";
    for (local i = 0; i < cpu.getGroupCount(); ++i) {
        local component = cpu.copyGroup(i);
        if (component == null || component.empty()) continue;
        local mesh = roadRequire(procgen.uploadMesh(component, gfx), "upload " + name + " " + i).value;
        local part = eve.Renderable3D();
        local gname = cpu.getGroupName(i);
        local style = roadStyleForGroup(gname);
        if (!(gname in roadMaterials)) {
            local material = gfx.newMaterial();
            material.setTint(style[0], style[1], style[2], 1.0);
            material.setRoughness(style[3]);
            material.setMetallic(style[4]);
            material.setReceiveLight(style[5]);
            if (!style[5]) material.setShadingModel("unlit");
            roadMaterials[gname] <- material;
        }
        part.setMesh(mesh);
        part.setMaterial(roadMaterials[gname]);
        part.setCastShadow(gname != "nav" && gname != "marking" && gname != "markingYellow");
        part.setReceiveShadow(gname == "asphalt" || gname == "sidewalk" || gname == "deck" || gname == "curb");
        roadParts.append(part);
        groupSummary += gname + ":" + component.getVertexCount() + " ";
    }

    // Interchange needs a larger ground plate.
    if (roadGround != null) {
        local gscale = name == "interchange" ? 110.0 : 70.0;
        roadGround.setScale(gscale, 0.4, gscale);
    }

    roadCameraForScene(name);
    print("PROCEDURAL_ROAD_SCENE scene=" + name + " verts=" + cpu.getVertexCount() +
          " groups=" + cpu.getGroupCount() + " parts=" + roadParts.len() + " [" + groupSummary + "]\n");
    roadReady = true;
}

if (roadCamera == null) {
    roadCamera = eve.Camera3D();
    roadCamera.setUp(0.0, 1.0, 0.0);
    roadCamera.setFov(42.0);
    roadCamera.setClipPlanes(0.5, 280.0);
    roadCamera.setAmbient(0.42, 0.44, 0.48);
    roadCamera.setActive(true);
    gfx.setDirectionalLight(-0.40, -1.0, -0.30, 1.55, 1.48, 1.35);
    gfx.setBackgroundColor(0.55, 0.18, 0.14, 1.0);
}

if (roadGround == null) {
    roadGround = eve.Renderable3D();
    roadGround.setMesh(gfx.newMeshCube(1.0));
    roadGround.setPosition(0.0, -0.55, 0.0);
    roadGround.setScale(70.0, 0.4, 70.0);
    local groundMat = gfx.newMaterial();
    groundMat.setTint(0.78, 0.16, 0.12, 1.0);
    groundMat.setRoughness(0.98);
    roadGround.setMaterial(groundMat);
    roadGround.setCastShadow(false);
    roadGround.setReceiveShadow(true);
}

if (!roadReady) {
    local boot = "straight";
    try {
        local handle = file("scene.txt", "r");
        if (handle != null) {
            local content = handle.read();
            handle.close();
            if (content != null) {
                local s = "";
                local raw = content.tostring();
                for (local i = 0; i < raw.len(); ++i) {
                    local ch = raw.slice(i, i + 1);
                    if (ch == "\n" || ch == "\r" || ch == " ") break;
                    s += ch;
                }
                if (s == "straight" || s == "curve" || s == "bridge" || s == "cross" || s == "interchange")
                    boot = s;
            }
        }
    } catch (e) {}
    roadBuildScene(boot);
}

function eve_update(dt) {
    if (!roadReady) return;
    roadFrame += 1;

    if (roadPressed("1")) roadBuildScene("straight");
    if (roadPressed("2")) roadBuildScene("curve");
    if (roadPressed("3")) roadBuildScene("bridge");
    if (roadPressed("4")) roadBuildScene("cross");
    if (roadPressed("5")) roadBuildScene("interchange");

    if (!roadScreenshotSaved && roadFrame > 24) {
        local file = "procedural-road-" + roadSceneName + ".png";
        if (gfx.saveFramePng(file)) {
            roadScreenshotSaved = true;
            print("procedural-road: screenshot " + file + "\n");
        }
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
