// MagicaVoxel-style sculpt: occupancy lives in VoxelCatalogEditor (undo).
// Presentation is the voxel engine: VoxelWorld + Camera3D raycast.

persist math = eve.Math();
persist voxelUi = {
    workspace = null, catalog = null, voxel = null, world = null, atlas = null, cam = null,
    status = "Loading voxel sculpt...",
    undoWas = false, redoWas = false,
    mouseDown = false, rightDown = false, lastX = 0.0, lastY = 0.0,
    yaw = 0.7, pitch = 0.45, distance = 18.0,
    syncedRevision = -1, syncedModel = "",
    frame = 0, screenshotSaved = false,
};

const TILE = 32;
const TILES_PER_ROW = 4;
const FOV = 50.0;
const NEAR_Z = 0.1;
const FAR_Z = 200.0;
const BG_R = 0.09;
const BG_G = 0.11;
const BG_B = 0.14;

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function mat4Perspective(fovyDeg, aspect, near, far) {
    local f = 1.0 / tan(fovyDeg * 0.5 * PI / 180.0);
    local m = math.newMat4();
    m.set(0, f / aspect);
    m.set(5, -f);
    m.set(10, (far + near) / (near - far));
    m.set(11, -1.0);
    m.set(14, (2.0 * far * near) / (near - far));
    return m;
}

function vec3Sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function vec3Cross(a, b) {
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
}
function vec3Dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
function vec3Normalize(a) {
    local len = sqrt(vec3Dot(a, a));
    if (len < 0.00001) return [0.0, 0.0, 0.0];
    return [a[0] / len, a[1] / len, a[2] / len];
}

function mat4LookAt(eye, target, up) {
    local f = vec3Normalize(vec3Sub(target, eye));
    local s = vec3Normalize(vec3Cross(f, up));
    local u = vec3Cross(s, f);
    local m = math.newMat4();
    m.set(0, s[0]); m.set(1, u[0]); m.set(2, -f[0]);
    m.set(4, s[1]); m.set(5, u[1]); m.set(6, -f[1]);
    m.set(8, s[2]); m.set(9, u[2]); m.set(10, -f[2]);
    m.set(12, -vec3Dot(s, eye));
    m.set(13, -vec3Dot(u, eye));
    m.set(14, vec3Dot(f, eye));
    m.set(15, 1.0);
    return m;
}

function clampf(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

function modelCenter() {
    return [
        voxelUi.catalog.getModelSizeX() * 0.5,
        voxelUi.catalog.getModelSizeY() * 0.5,
        voxelUi.catalog.getModelSizeZ() * 0.5
    ];
}

function orbitEye() {
    local c = modelCenter();
    local cp = cos(voxelUi.pitch);
    return [
        c[0] + sin(voxelUi.yaw) * cp * voxelUi.distance,
        c[1] + sin(voxelUi.pitch) * voxelUi.distance,
        c[2] + cos(voxelUi.yaw) * cp * voxelUi.distance
    ];
}

function buildAtlas() {
    local canvas = gfx.newCanvas(TILE * TILES_PER_ROW, TILE * TILES_PER_ROW);
    local colors = [
        [0.18, 0.20, 0.24],
        [0.92, 0.62, 0.22],
        [0.78, 0.48, 0.18],
        [0.55, 0.38, 0.22],
    ];
    gfx.setCanvas(canvas);
    gfx.clear();
    for (local i = 0; i < colors.len(); ++i) {
        local cx = ((i % TILES_PER_ROW) * TILE).tofloat();
        local cy = ((i / TILES_PER_ROW) * TILE).tofloat();
        gfx.drawSolidRect(cx, cy, TILE.tofloat(), TILE.tofloat(),
                          colors[i][0], colors[i][1], colors[i][2], 1.0);
    }
    gfx.setCanvas(null);
    gfx.setBackgroundColor(BG_R, BG_G, BG_B, 1.0);
    return canvas.getTexture();
}

function setupVoxelWorld() {
    if (voxelUi.voxel == null) voxelUi.voxel = eve.Voxel();
    if (voxelUi.world == null) {
        local types = voxelUi.voxel.newCubeTypes();
        types.loadFromJson("[{\"name\":\"clay\",\"faceTex\":[1,1,1,1,1,1]}]");
        voxelUi.world = voxelUi.voxel.newWorldWithTypes(types);
    }
    if (voxelUi.atlas == null) voxelUi.atlas = buildAtlas();
    if (voxelUi.cam == null) {
        voxelUi.cam = eve.Camera3D();
        voxelUi.cam.setActive(false);
        voxelUi.cam.setFov(FOV);
        voxelUi.cam.setClipPlanes(NEAR_Z, FAR_Z);
        voxelUi.cam.setUp(0.0, 1.0, 0.0);
    }
}

function syncWorld() {
    local rev = voxelUi.catalog.getRevision();
    local id = voxelUi.catalog.getSelectedId();
    if (rev == voxelUi.syncedRevision && id == voxelUi.syncedModel) return;
    voxelUi.world.clear();
    local n = voxelUi.catalog.getVoxelCount();
    for (local i = 0; i < n; ++i)
        voxelUi.world.setVoxel(voxelUi.catalog.getVoxelX(i), voxelUi.catalog.getVoxelY(i),
                               voxelUi.catalog.getVoxelZ(i), 1);
    voxelUi.world.remeshDirty();
    voxelUi.syncedRevision = rev;
    voxelUi.syncedModel = id;
}

function buildWorkspace() {
    voxelUi.workspace = editor.newWorkspace("preview.voxel", "Voxel Sculpt");
    voxelUi.catalog = requireResult(eve.VoxelEditorModule().create("asset.preview.sculpt"),
                                    "Create voxel sculpt editor");
    requireResult(voxelUi.catalog.configureWorkspace(voxelUi.workspace), "Compose workspace");
    voxelUi.status = "Left-click attach/erase · right-drag orbit";
    voxelUi.workspace.setRegionSize("left", 200.0);
    voxelUi.workspace.setRegionSize("right", 240.0);
    voxelUi.workspace.setRegionSize("bottom", 120.0);
    voxelUi.workspace.layout(config.width.tofloat(), config.height.tofloat());
}

function panelModels() {
    ui.text("Models", "models-title");
    for (local i = 0; i < voxelUi.catalog.getModelCount(); ++i)
        ui.listItem(voxelUi.catalog.getModelName(i) + " [" + voxelUi.catalog.getModelFill(i) + "]",
                    "model-" + voxelUi.catalog.getModelId(i));
}

function panelTools() {
    ui.text("Tools", "tools-title");
    ui.button("Attach", "tool-attach");
    ui.button("Erase", "tool-erase");
    ui.separator("tools-sep");
    ui.button("New 8x8x8", "new-model");
    ui.separator("tools-sep2");
    ui.beginRow("orbit-row", 8.0);
    ui.button("Orbit L", "orbit-l"); ui.button("Orbit R", "orbit-r");
    ui.end();
    ui.beginRow("history-row", 8.0); ui.button("Undo", "undo"); ui.button("Redo", "redo"); ui.end();
}

function panelInspector() {
    ui.text("Model", "inspector-title");
    ui.text("", "selection");
    ui.text("", "status");
}

panelBuilders <- {
    ["voxel.models"]=panelModels,
    ["voxel.tools"]=panelTools,
    ["voxel.inspector"]=panelInspector
};

function mountPanels() {
    for (local i = 0; i < voxelUi.workspace.getPanelCount(); ++i) {
        local id = voxelUi.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        ui.beginBuild(); ui.beginWindow(voxelUi.workspace.getPanelTitle(i), "root");
        panelBuilders[id](); ui.end(); ui.mountBuildAs(id); ui.select(id);
        local region = voxelUi.workspace.getPanelRegion(i);
        ui.setHostPos(voxelUi.workspace.getRegionX(region), voxelUi.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(voxelUi.workspace.getRegionW(region), voxelUi.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
}

function applyHistory(command) {
    local result = command == "undo" ? voxelUi.catalog.undo() : voxelUi.catalog.redo();
    voxelUi.status = result.ok ? command + " applied" : command + ": " + result.status.summary;
}

function eventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function handleUiEvents() {
    local click = ui.consumeClick();
    while (click != "") {
        local event = eventParts(click); local host = event[0]; local id = event[1];
        if (id == "undo" || id == "redo") applyHistory(id);
        else if (id == "tool-attach") requireResult(voxelUi.catalog.setTool("attach"), "Attach");
        else if (id == "tool-erase") requireResult(voxelUi.catalog.setTool("erase"), "Erase");
        else if (id == "new-model") {
            local n = voxelUi.catalog.getModelCount() + 1;
            requireResult(voxelUi.catalog.createModel("model" + n, "model" + n, 8, 8, 8), "New model");
            voxelUi.status = "New empty 8x8x8 canvas";
        }
        else if (id == "orbit-l") voxelUi.yaw -= 0.2;
        else if (id == "orbit-r") voxelUi.yaw += 0.2;
        else if (host == "voxel.models" && id.find("model-") == 0) {
            requireResult(voxelUi.catalog.selectModel(id.slice(6)), "Select model");
            voxelUi.status = "Sculpting " + voxelUi.catalog.getSelectedId();
        }
        click = ui.consumeClick();
    }
}

function inCenter(mx, my) {
    local x = voxelUi.workspace.getRegionX("center");
    local y = voxelUi.workspace.getRegionY("center");
    local w = voxelUi.workspace.getRegionW("center");
    local h = voxelUi.workspace.getRegionH("center");
    return mx >= x && my >= y && mx < x + w && my < y + h;
}

function updateCamera() {
    local eye = orbitEye();
    local c = modelCenter();
    voxelUi.cam.setEye(eye[0], eye[1], eye[2]);
    voxelUi.cam.setTarget(c[0], c[1], c[2]);
}

function updatePointer() {
    local mx = mouse.getX().tofloat();
    local my = mouse.getY().tofloat();
    local hovered = inCenter(mx, my) && !ui.wantCaptureMouse();
    local left = hovered && mouse.isDown(1);
    local right = hovered && mouse.isDown(2);
    if (right && voxelUi.rightDown) {
        voxelUi.yaw -= (mx - voxelUi.lastX) * 0.01;
        voxelUi.pitch = clampf(voxelUi.pitch + (my - voxelUi.lastY) * 0.01, -1.2, 1.2);
    } else if (left && !voxelUi.mouseDown) {
        local w = gfx.getWidth().tofloat();
        local h = gfx.getHeight().tofloat();
        voxelUi.cam.screenToRay(mx, my, w, h);
        local hit = voxelUi.catalog.pointerWorldRay(
            voxelUi.cam.getScreenRayOriginX(), voxelUi.cam.getScreenRayOriginY(),
            voxelUi.cam.getScreenRayOriginZ(), voxelUi.cam.getScreenRayDirX(),
            voxelUi.cam.getScreenRayDirY(), voxelUi.cam.getScreenRayDirZ());
        voxelUi.status = hit.ok ? voxelUi.catalog.getTool() + " " + voxelUi.catalog.getVoxelCount() + " voxels"
                                : hit.status.summary;
    }
    voxelUi.mouseDown = left;
    voxelUi.rightDown = right;
    voxelUi.lastX = mx;
    voxelUi.lastY = my;
}

function updateKeyboardShortcuts() {
    local control = keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl");
    local undo = control && (keyboard.isDown("z") || keyboard.isDown("Z"));
    local redo = control && (keyboard.isDown("y") || keyboard.isDown("Y"));
    if (undo && !voxelUi.undoWas) applyHistory("undo");
    if (redo && !voxelUi.redoWas) applyHistory("redo");
    voxelUi.undoWas = undo; voxelUi.redoWas = redo;
}

function updateLabels() {
    ui.select("voxel.inspector");
    ui.setText("selection", voxelUi.catalog.getSelectedId() + "  " +
        format("%dx%dx%d  %d voxels  rev %d",
            voxelUi.catalog.getModelSizeX(), voxelUi.catalog.getModelSizeY(),
            voxelUi.catalog.getModelSizeZ(), voxelUi.catalog.getVoxelCount(),
            voxelUi.catalog.getRevision()));
    ui.setText("status", voxelUi.catalog.getTool() + "  " + voxelUi.status);
    ui.select("voxel.tools");
    ui.setEnabled("undo", voxelUi.catalog.canUndo());
    ui.setEnabled("redo", voxelUi.catalog.canRedo());
}

function drawWorld() {
    local eye = orbitEye();
    local c = modelCenter();
    local view = mat4LookAt(eye, c, [0.0, 1.0, 0.0]);
    local proj = mat4Perspective(FOV, config.width.tofloat() / config.height.tofloat(), NEAR_Z, FAR_Z);
    local viewProj = proj.multiplied(view);
    local vp = [];
    local viewArr = [];
    for (local i = 0; i < 16; ++i) {
        vp.push(viewProj.get(i));
        viewArr.push(view.get(i));
    }
    voxelUi.world.selectVisible(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], vp[6], vp[7],
        vp[8], vp[9], vp[10], vp[11], vp[12], vp[13], vp[14], vp[15],
        eye[0], eye[1], eye[2], 80.0, true);
    gfx.setMesh3DViewProj(vp);
    gfx.setMesh3DView(viewArr);
    gfx.setMesh3DCameraPos(eye[0], eye[1], eye[2]);
    gfx.begin3DFrame();
    voxelUi.world.drawVisible(gfx, voxelUi.atlas, TILES_PER_ROW);
    if (voxelUi.frame == 0)
        print("voxel-catalog-editor: visible chunks=" + voxelUi.world.getVisibleChunkCount() +
              " rects=" + voxelUi.world.getVisibleRectCount() + "\n");
}

eve_init = function() {
    try {
        gfx.setBackgroundColor(BG_R, BG_G, BG_B, 1.0);
        gfx.setDirectionalLight(-0.4, -1.0, -0.35, 1.1, 1.05, 0.95);
        buildWorkspace();
        setupVoxelWorld();
        gfx.setBackgroundColor(BG_R, BG_G, BG_B, 1.0);
        syncWorld();
        ui.setTheme("dark");
        ui.setNavKeyboard(true);
        mountPanels();
        print("voxel-catalog-editor: models=" + voxelUi.catalog.getModelCount() +
              " voxels=" + voxelUi.catalog.getVoxelCount() + "\n");
    } catch (err) {
        print("voxel-catalog-editor eve_init: " + err + "\n");
        throw err;
    }
};

eve_update = function(dt) {
    handleUiEvents();
    updateCamera();
    updatePointer();
    updateKeyboardShortcuts();
    syncWorld();
    updateLabels();
};

eve_render = function() {
    gfx.clear();
    drawWorld();
    ui.beginFrameAndRender();
    voxelUi.frame += 1;
    if (!voxelUi.screenshotSaved && voxelUi.frame > 90 && gfx.saveFramePng("voxel-catalog-editor.png")) {
        voxelUi.screenshotSaved = true;
        print("voxel-catalog-editor: saved voxel-catalog-editor.png\n");
    }
};
