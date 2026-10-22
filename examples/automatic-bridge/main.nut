persist bridgeParts = []
persist bridgeCamera = null
persist bridgeKeys = {}
persist bridgeReady = false
persist bridgeCache = {}
persist bridgeTypes = ["beam", "slab", "arch", "tiedArch", "throughTruss", "deckTruss",
                       "suspension", "cableStayed", "cantilever", "trestle"]
persist bridgeIndex = 0
persist bridgeFrame = 0
persist bridgeGallery = false
persist bridgeCaptured = false
persist bridgeSun = null

function bridgeRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return "value" in result ? result.value : null;
}

dofile("materials.nut");
dofile("environment.nut");

function bridgeBuild(index) {
    local type = bridgeTypes[index];
    if (!(type in bridgeCache)) {
        local params = bridgeRequire(procgen.newParams(), "bridge parameters");
        params.setString("bridgeType", type);
        // The selected preset is the single authority for dimensions and structural rules.
        // Explicit setFloat/setInt overrides may be placed before this call.
        bridgeRequire(procgen.applyMeshRecipeDefaults("mesh.autoBridge", params), "selected bridge defaults");
        local cpu = bridgeRequire(procgen.buildMesh("mesh.autoBridge", params), "generate " + type);
        local candidate = [];
        for (local i = 0; i < cpu.getGroupCount(); ++i) {
            local group = cpu.copyGroup(i);
            if (group == null || group.empty()) continue;
            local name = cpu.getGroupName(i);
            local at = name.find("@");
            if (at == null) throw "Missing bridge surface classification: " + name;
            local uploaded = bridgeRequire(procgen.uploadMesh(group, gfx), "upload " + name);
            candidate.append({ mesh = uploaded, material = bridgeMaterial(name.slice(at + 1)), name = name });
        }
        local span = params.getFloat("endX", 0.0) - params.getFloat("startX", 0.0);
        local height = params.getFloat("startY", 0.0);
        bridgeCache[type] <- { parts = candidate, span = span, height = height,
            tower = (type == "suspension" || type == "cableStayed") ? params.getFloat("towerHeight", 0.0) :
                    (type == "cantilever" ? params.getFloat("trussHeight", 0.0) :
                    (type == "tiedArch" ? params.getFloat("archRise", 0.0) : 0.0)) };
        print("AUTO_BRIDGE_GENERATED type=" + type + " vertices=" + cpu.getVertexCount() +
              " groups=" + cpu.getGroupCount() + "\n");
    }
    local built = bridgeCache[type];
    while (bridgeParts.len() < built.parts.len()) bridgeParts.append(eve.Renderable3D());
    foreach (part in bridgeParts) part.setVisible(false);
    foreach (i, asset in built.parts) {
        local part = bridgeParts[i];
        part.setMesh(asset.mesh); part.setMaterial(asset.material); part.setCamera(bridgeCamera);
        part.setCastShadow(asset.name.find("marking") != 0); part.setReceiveShadow(true); part.setVisible(true);
    }
    local span = built.span, height = built.height;
    bridgeCamera.setEye(-span * 0.30, height + span * 0.16 + built.tower * 0.20, span * 1.08);
    bridgeCamera.setTarget(0.0, height * 0.57 + built.tower * 0.20, 0.0);
    bridgeCamera.setClipPlanes(0.2, span * 5.0);
    bridgeBuildEnvironment(span, height);
    bridgeIndex = index; bridgeFrame = 0; bridgeCaptured = false;
}

if (!bridgeReady) {
    if (bridgeCamera == null) bridgeCamera = eve.Camera3D();
    bridgeCamera.setUp(0.0, 1.0, 0.0);
    bridgeCamera.setFov(40.0);
    bridgeCamera.setExposure(0.75);
    bridgeCamera.setAmbient(0.14, 0.16, 0.18);
    bridgeCamera.setActive(true);
    gfx.setDirectionalLight(-0.35, 1.0, 0.45, 0.55, 0.53, 0.50);
    if (bridgeSun == null) bridgeSun = eve.Light3D();
    bridgeSun.setType("dir"); bridgeSun.setDirection(-0.35, 1.0, 0.45);
    bridgeSun.setColor(0.55, 0.53, 0.50, 1.0);
    bridgeSun.setCastShadow(true); bridgeSun.setShadowMethod("csm");
    bridgeSun.setShadowStrength(0.75); bridgeSun.setShadowBias(0.0005);
    local rendering = gfx.getRenderControl();
    rendering.enable("forward"); rendering.enable("depthTest"); rendering.enable("shadow");

    bridgeRequire(gfx.setSceneToneMapping("aces"), "scene tone mapping");
    gfx.setMsaaSamples(4);
    gfx.setShadowSchemeDirectionalEnabled(true);
    gfx.setBackgroundColor(0.76, 0.78, 0.80, 1.0);
    local initial = 0;
    try {
        local input = file("capture.txt", "r");
        if (input != null) {
            local command = input.read().tostring(); input.close();
            local end = command.find("\n"); if (end != null) command = command.slice(0, end);
            end = command.find("\r"); if (end != null) command = command.slice(0, end);
            if (command == "gallery") bridgeGallery = true;
            foreach (i, type in bridgeTypes) if (command == type) { initial = i; bridgeGallery = true; }
        }
    } catch (missingCaptureRequest) {}
    bridgeBuild(initial);
    bridgeReady = true;
}

function eve_update(dt) {
    bridgeFrame += 1;
    foreach (key in ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "g"]) {
        local down = keyboard.isDown(key);
        local wasDown = key in bridgeKeys ? bridgeKeys[key] : false;
        bridgeKeys[key] <- down;
        if (down && !wasDown) {
            if (key == "g") { bridgeGallery = !bridgeGallery; if (bridgeGallery) bridgeBuild(0); }
            else { bridgeGallery = false; bridgeBuild(key == "0" ? 9 : key.tointeger() - 1); }
        }
    }
    if (bridgeGallery && !bridgeCaptured && bridgeFrame > 24) {
        local path = "renders/" + bridgeTypes[bridgeIndex] + ".png";
        if (gfx.saveFramePng(path)) {
            bridgeCaptured = true;
            print("AUTO_BRIDGE_CAPTURED type=" + bridgeTypes[bridgeIndex] + " path=" + path + "\n");
        }
    }
    if (bridgeGallery && bridgeCaptured && bridgeFrame > 32) {
        if (bridgeIndex < 9) bridgeBuild(bridgeIndex + 1);
        else { bridgeGallery = false; print("AUTO_BRIDGE_GALLERY_COMPLETE count=10\n"); }
    }
}
function eve_reload() { bridgeCache = {}; bridgeReady = false; }
function eve_render() {
    gfx.clear(); gfx.render3D();
}
