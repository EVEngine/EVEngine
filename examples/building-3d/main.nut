// ============================================================================
// EVEngine Building 模块示例 —— 3D 场景地面（XZ 平面）建筑放置
//
// 演示：
//   PlacementWorld.setGridPlane("xz")（网格第二轴 -> 世界 Z，Y 为高度）
//   内置 "plane" 放置表面 + 脚本侧 Camera3D.screenToRay -> Y=0 平面求交
//   BuildingFx 3D 视觉（简单 OBJ 房屋）+ 鬼影 + 3D 网格线框
//
// 按键：1-3 选建筑  R 旋转  左键放置  右键拆除  T 切换网格
// 运行： make run/<platform>-debug GAME=examples/building-3d
// ============================================================================

persist bld = null
persist fx = null
persist world = null
persist session = null
persist cam = null
persist ground = null
persist palette = []
persist paletteIndex = 0
persist prevKeys = {}
persist prevMouse = { left = false, right = false }
persist uiBuilt = false
persist mouseHit = false

const GRID_W = 16;
const GRID_H = 16;
const CELL = 1.0;

function keyPressed(name) {
    return key_just_pressed(name);
}

// 鼠标按键编号：1 = 左键，2 = 右键（与 engine mouse::isDown 一致）。
function mousePressed(button) {
    local down = mouse.isDown(button);
    local was = false;
    if (button == 1) was = prevMouse.left;
    else if (button == 2) was = prevMouse.right;
    if (button == 1) prevMouse.left = down;
    else if (button == 2) prevMouse.right = down;
    return down && !was;
}

function registerBuildings(loadMeshes = false) {
    local resources = {};
    local entries = [
        ["house", "山墙小屋", "cottage", 2, 2, 1.8, 0.82, 0.62, 0.38],
        ["barn", "长屋谷仓", "barn", 3, 2, 1.6, 0.72, 0.34, 0.24],
        ["pavilion", "尖顶小屋", "pavilion", 2, 2, 2.0, 0.38, 0.64, 0.64]
    ];
    foreach (entry in entries) {
        local resource = "assets/" + entry[2] + ".obj";
        if (loadMeshes) {
            local data = model3d.newModelDataFromFile(resource);
            if (data.getMeshCount() != 1) throw "Expected one mesh in " + resource;
            local renderable = model3d.createRenderable(gfx, data, 0);
            resources[resource] <- renderable.getMesh();
            renderable.setVisible(false);
            // ECS owns this hidden source; Graphics owns the mesh for the scene lifetime.
        }
        local json = "[{\"id\":\"" + entry[0] + "\",\"displayName\":\"" + entry[1] +
            "\",\"renderMode\":\"3d\",\"footprintW\":" + entry[3] +
            ",\"footprintH\":" + entry[4] + ",\"visual3d\":{\"mesh\":\"" + resource +
            "\",\"height\":\"" + entry[5] + "\",\"offsetY\":\"" + (entry[5]*0.5) +
            "\",\"colorR\":\"" + entry[6] + "\",\"colorG\":\"" + entry[7] +
            "\",\"colorB\":\"" + entry[8] + "\"}}]";
        if (bld.registerBuildingsFromJson(json) != 1) throw "Failed to register " + entry[0];
    }
    if (loadMeshes) fx.setMeshResolver(resources);
}

function rayPlaneHitY0(ox, oy, oz, dx, dy, dz, out) {
    // out = [x, z]；射线与 Y=0 平面求交。
    if (dy > -0.0001) return false;
    local t = -oy / dy;
    if (t < 0.0) return false;
    out[0] = ox + dx * t;
    out[1] = oz + dz * t;
    return true;
}

function setupScene() {
    if (cam == null) {
        cam = eve.Camera3D();
        cam.setEye(13.0, 7.0, 16.0);
        cam.setTarget(0.0, 0.7, 0.0);
        cam.setUp(0.0, 1.0, 0.0);
        cam.setFov(45.0);
        cam.setAmbient(0.35, 0.35, 0.38);
        cam.setActive(true);
        gfx.setDirectionalLight(-1.0, -1.0, 0.3, 2.2, 2.0, 1.7);
    }
    if (ground == null) {
        ground = eve.Renderable3D();
        ground.setMesh(gfx.newMeshCube(1.0));
        ground.setScale(GRID_W.tofloat(), 0.08, GRID_H.tofloat());
        ground.setPosition(0.0, -0.05, 0.0);
        ground.setTint(0.22, 0.30, 0.24, 1.0);
    }
}

function resetWorld() {
    if (session) { session.destroy(); session = null; }
    if (world) { fx.detach(world); world.destroy(); world = null; }
    world = bld.newWorld(GRID_W, GRID_H, CELL);
    world.setId("city3d");
    world.setGridPlane("xz");
    world.setOrigin(-GRID_W * 0.5, -GRID_H * 0.5);

    session = bld.newSession();
    session.startPlacement(world, palette[paletteIndex]);

    fx.attach(world);
    fx.setGridVisible(world, true);
    bld.clearChangeEvents();
    prevMouse = { left = false, right = false };
    mouseHit = false;
}

function selectPalette(index) {
    if (index < 0 || index >= palette.len()) return;
    paletteIndex = index;
    session.startPlacement(world, palette[paletteIndex]);
}

function updateGhostFromMouse() {
    mouseHit = false;
    local mx = mouse.getX();
    local my = mouse.getY();
    cam.screenToRay(mx, my, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local hit = [0.0, 0.0];
    if (rayPlaneHitY0(cam.getScreenRayOriginX(), cam.getScreenRayOriginY(),
                      cam.getScreenRayOriginZ(),
                      cam.getScreenRayDirX(), cam.getScreenRayDirY(), cam.getScreenRayDirZ(),
                      hit)) {
        session.updateFromSurface(world, "plane", hit[0], hit[1]);
        mouseHit = true;
    }
}

function refreshHud() {
    if (!uiBuilt) return;
    local name = bld.getBuildingDisplayName(palette[paletteIndex]);
    local info = session.isValid() ? "可放置" : ("不可：" + session.getReason());
    ui.setText("stats",
        "3D 地面放置  建筑数 " + world.getBuildingCount() +
        "  当前 [" + (paletteIndex + 1) + "] " + name +
        "  旋转 " + session.getRotationDeg().tointeger() + "°  " + info);
    ui.setText("help",
        "1山墙小屋 2谷仓 3尖顶小屋 | 移动预览 | R旋转 | 左键建 | 右键拆 | T网格");
}

eve_init = function() {
    print("examples/building-3d ready\n");
    gfx.setBackgroundColor(0.08, 0.10, 0.12, 1.0);
    if (bld == null) bld = eve.Building();
    if (fx == null) fx = eve.BuildingFx();
    registerBuildings(true);
    setupScene();
    palette = ["house", "barn", "pavilion"];
    paletteIndex = 0;
    resetWorld();

    if (!uiBuilt) {
        ui.beginBuild();
        ui.beginWindow("Building3D", "root");
        ui.text("3D 建筑放置", "title");
        ui.text("", "stats");
        ui.text("", "help");
        ui.end();
        ui.mountBuildAs("hud");
        ui.select("hud");
        ui.setHostOverlay(true);
        ui.setHostPos(12.0, 8.0, 0.0, 0.0);
        uiBuilt = true;
    }
    refreshHud();
};

eve_reload <- function() {
    registerBuildings();
    resetWorld();
};

eve_update = function(dt) {
    if (world == null || session == null) return;

    if (keyPressed("1")) selectPalette(0);
    if (keyPressed("2")) selectPalette(1);
    if (keyPressed("3")) selectPalette(2);
    if (keyPressed("r") || keyPressed("R")) session.rotateBy(90.0);
    if (keyPressed("t") || keyPressed("T"))
        fx.setGridVisible(world, !fx.getGridVisible(world));

    updateGhostFromMouse();

    if (mousePressed(1) && mouseHit) {
        session.setMode("place");
        local id = session.execute();
        if (id <= 0)
            print("failed to place: " + session.getReason() + "\n");
    }
    if (mousePressed(2) && mouseHit) {
        session.setMode("remove");
        local id = session.execute();
        session.setMode("place");
        if (id <= 0)
            print("no building here.\n");
    }
    fx.sync(world);
    if (mouseHit) fx.updateGhost(world, session.getGhost());
    else fx.hideGhost(world);
    refreshHud();
};

eve_render = function() {
    gfx.clear();
    if (world != null) {
        fx.drawGrid3D(world, gfx, 0.01);
    }
    gfx.render3D();
    ui.beginFrameAndRender();
};
