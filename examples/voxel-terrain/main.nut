// ============================================================================
// EVEngine 体素引擎 + 地形生成 测试场景
//
// 演示：eve.Voxel 32³ chunk 流式大世界 + procgen TerrainSampler 全参数地形
//       （岛屿衰减 / 大陆形状 / 山脊 / 域扭曲 / 沙滩带），第一人称飞行、
//       DDA 射线拾取建造/破坏。
//
// 操作：
//   WASD           前后左右移动      Space / C   上升 / 下降
//   Shift          加速
//   鼠标移动        第一人称视角      Esc        释放 / 捕获鼠标
//   左键点击        在准星处放置木头块
//   右键点击        破坏准星处方块
//   1 / 2 / 3 / 4  切换地形预设（群岛 / 大陆 / 山脉 / 沙海）
//   R              用随机种子重新生成当前预设
//   = / -          增大 / 减小流式半径（chunk）
//
// 运行：make run/win32-debug GAME=examples/voxel-terrain
// ============================================================================

if (!("math" in getroottable())) math <- eve.Math();
if (!("voxel" in getroottable())) voxel <- null;
if (!("editor" in getroottable())) editor <- null;
if (!("world" in getroottable())) world <- null;
if (!("types" in getroottable())) types <- null;
if (!("atlas" in getroottable())) atlas <- null;
if (!("editSession" in getroottable())) editSession <- null;
if (!("voxelTarget" in getroottable())) voxelTarget <- null;
if (!("volumeFalloff" in getroottable())) volumeFalloff <- null;
if (!("volumeKernel" in getroottable())) volumeKernel <- null;
if (!("volumeOperation" in getroottable())) volumeOperation <- null;
if (!("volumeTool" in getroottable())) volumeTool <- null;
if (!("playerPos" in getroottable())) playerPos <- [0.0, 34.0, 64.0];
if (!("yaw" in getroottable())) yaw <- PI;
if (!("pitch" in getroottable())) pitch <- -0.55;
if (!("elapsed" in getroottable())) elapsed <- 0.0;
if (!("presetIdx" in getroottable())) presetIdx <- 0;
if (!("seed" in getroottable())) seed <- 20260823;
if (!("streamRadius" in getroottable())) streamRadius <- 3;
if (!("mouseCaptured" in getroottable())) mouseCaptured <- false;
if (!("prevLeft" in getroottable())) prevLeft <- false;
if (!("prevRight" in getroottable())) prevRight <- false;
if (!("lastEdit" in getroottable())) lastEdit <- "";
if (!("fpsAvg" in getroottable())) fpsAvg <- 60.0;
if (!("uiReady" in getroottable())) uiReady <- false;
if (!("shownHelp" in getroottable())) shownHelp <- false;
if (!("statsPrinted" in getroottable())) statsPrinted <- false;
if (!("shotPending" in getroottable())) shotPending <- true;   // 首帧自动截一张便于冒烟验证
if (!("shotPath" in getroottable())) shotPath <- "screenshot.png";

const TILE = 32;            // atlas tile size in pixels
const TILES_PER_ROW = 4;
const FLY_SPEED = 18.0;     // world units / second
const FLY_FAST = 48.0;
const LOOK_SENS = 0.0032;   // radians per mouse pixel
const VIEW_RANGE = 180.0;

function maxf(a, b) { return a > b ? a : b; }

// ---------------------------------------------------------------------------
// 地形预设：完整 procgen TerrainSampler 参数 + 体素高度映射 + 沙滩带。
// ---------------------------------------------------------------------------
presets <- [
    {
        name = "群岛", baseH = 2.0, amp = 30.0, scale = 1.0 / 48.0,
        island = 0.55, worldWidth = 512, worldHeight = 512, continent = 0.55,
        ridge = 0.30, warp = 0.35, exponent = 2.0, octaves = 5,
        coast = 0.12, sandLevel = 0.36
    },
    {
        name = "大陆", baseH = 4.0, amp = 22.0, scale = 1.0 / 64.0,
        island = 0.12, worldWidth = 0, worldHeight = 0, continent = 0.62,
        ridge = 0.35, warp = 0.30, exponent = 2.0, octaves = 5,
        coast = 0.14, sandLevel = 0.34
    },
    {
        name = "山脉", baseH = 2.0, amp = 34.0, scale = 1.0 / 40.0,
        island = 0.38, worldWidth = 512, worldHeight = 512, continent = 0.52,
        ridge = 0.85, warp = 0.45, exponent = 1.4, octaves = 6,
        coast = 0.12, sandLevel = 0.38
    },
    {
        name = "沙海", baseH = 4.0, amp = 18.0, scale = 1.0 / 36.0,
        island = 0.30, worldWidth = 384, worldHeight = 384, continent = 0.58,
        ridge = 0.20, warp = 0.25, exponent = 2.2, octaves = 4,
        coast = 0.10, sandLevel = 1.0   // 全部柱子顶层用沙
    },
];

// ---------------------------------------------------------------------------
// 矩阵工具（列主序，与引擎 perspectiveVulkanRH_ZO / glm::lookAt 一致）
// ---------------------------------------------------------------------------
function mat4Perspective(fovyDeg, aspect, near, far) {
    local f = 1.0 / tan(fovyDeg * 0.5 * PI / 180.0);
    local m = math.newMat4();
    m.set(0, f / aspect);
    m.set(5, -f);   // Vulkan NDC Y 向下
    m.set(10, (far + near) / (near - far));
    m.set(11, -1.0);
    m.set(14, (2.0 * far * near) / (near - far));
    return m;
}

function vec3Sub(a, b) { return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]; }
function vec3Cross(a, b) {
    return [a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]];
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

// ---------------------------------------------------------------------------
// 程序化 4x2 图集：1草 2土 3石 4沙 5木 6砖
// ---------------------------------------------------------------------------
function buildAtlas() {
    local rows = 2;
    local canvas = gfx.newCanvas(TILE * TILES_PER_ROW, TILE * rows);
    local colors = [
        [0.24, 0.60, 0.26],  // 1 grass
        [0.48, 0.34, 0.20],  // 2 dirt
        [0.55, 0.55, 0.58],  // 3 stone
        [0.78, 0.72, 0.45],  // 4 sand
        [0.62, 0.42, 0.22],  // 5 wood
        [0.72, 0.38, 0.30],  // 6 brick
    ];
    gfx.setCanvas(canvas);
    gfx.setBackgroundColor(0.0, 0.0, 0.0, 1.0);
    gfx.clear();
    for (local i = 0; i < colors.len(); i++) {
        local cx = ((i % TILES_PER_ROW) * TILE).tofloat();
        local cy = ((i / TILES_PER_ROW) * TILE).tofloat();
        gfx.drawSolidRect(cx, cy, TILE.tofloat(), TILE.tofloat(),
                          colors[i][0], colors[i][1], colors[i][2], 1.0);
        // 简单斑驳纹理：每格画几块深浅不一的小方块
        for (local k = 0; k < 5; k++) {
            local px = cx + ((k * 7 + i * 3) % 6) * 4.0 + 3.0;
            local py = cy + ((k * 5 + i * 7) % 6) * 4.0 + 3.0;
            local f = (k % 2 == 0) ? 0.88 : 1.12;
            gfx.drawSolidRect(px, py, 5.0, 5.0,
                              clampf(colors[i][0] * f, 0.0, 1.0),
                              clampf(colors[i][1] * f, 0.0, 1.0),
                              clampf(colors[i][2] * f, 0.0, 1.0), 1.0);
        }
        // 底部深色描边，便于肉眼看分格
        gfx.drawSolidRect(cx + 1.0, cy + TILE - 3.0, (TILE - 2).tofloat(), 2.0,
                          0.0, 0.0, 0.0, 0.35);
    }
    gfx.setCanvas(null);
    return canvas.getTexture();
}

// ---------------------------------------------------------------------------
// 世界 / 地形
// ---------------------------------------------------------------------------
function setupWorld() {
    if (types == null) {
        types = voxel.newCubeTypes();
        types.loadFromJson(@"[ 
            {""name"":""grass"",""faceTex"":[1,1,2,2,2,2]},
            {""name"":""dirt"",""faceTex"":[2,2,2,2,2,2]},
            {""name"":""stone"",""faceTex"":[3,3,3,3,3,3]},
            {""name"":""sand"",""faceTex"":[4,4,4,4,4,4]},
            {""name"":""wood"",""faceTex"":[5,5,5,5,5,5]},
            {""name"":""brick"",""faceTex"":[6,6,6,6,6,6]}
        ]");
    }
    if (world == null) {
        world = voxel.newWorldWithTypes(types);
        applyTerrainPreset(0, seed);
        // 出生点放在本地最高点斜后方，朝向岛屿，保证第一眼能看到地形
        local bestH = 0;
        local bestX = 0;
        local bestZ = 0;
        for (local dz = -160; dz <= 160; dz += 8) {
            for (local dx = -160; dx <= 160; dx += 8) {
                local h = world.terrainHeightAt(dx, dz);
                if (h > bestH) { bestH = h; bestX = dx; bestZ = dz; }
            }
        }
        playerPos = [bestX.tofloat(), bestH + 12.0, (bestZ + 48).tofloat()];
    }
    if (atlas == null) atlas = buildAtlas();
    configureVoxelTool();
}

function configureVoxelTool() {
    if (editor == null) editor = eve.Editor();
    if (editSession != null) return;
    editSession = editor.newSession();
    voxelTarget = editor.newVoxelWorldTarget("runtime.voxel-world", world);
    volumeFalloff = editor.newConstantBrushFalloff();
    volumeKernel = editor.newSphereVolumeBrushKernel();
    volumeKernel.setConstantFalloff(volumeFalloff);
    volumeOperation = editor.newPaintIntVolumeOperation(5);
    volumeTool = editor.newVolumeBrushTool("voxel.paint", "Voxel Brush");
    volumeTool.setSphereKernel(volumeKernel);
    volumeTool.setPaintIntOperation(volumeOperation);
    volumeTool.setRadius(0.5);
    editSession.addVolumeTool(volumeTool);
    editSession.bindVoxelWorldTarget(voxelTarget);
    editSession.activateTool("voxel.paint");
}

function applyTerrainPreset(idx, seedOverride = null) {
    local p = presets[idx % presets.len()];
    seed = (seedOverride == null) ? (20260823 + idx * 100003) : seedOverride;
    world.setTerrainParam("seed", seed.tofloat());
    world.setTerrainParam("top", 1.0);      // 草
    world.setTerrainParam("sub", 2.0);      // 土
    world.setTerrainParam("stone", 3.0);    // 石
    world.setTerrainParam("sand", 4.0);     // 沙
    world.setTerrainParam("base", p.baseH);
    world.setTerrainParam("amplitude", p.amp);
    world.setTerrainParam("scale", p.scale);
    world.setTerrainParam("octaves", p.octaves.tofloat());
    world.setTerrainParam("lacunarity", 2.0);
    world.setTerrainParam("gain", 0.5);
    world.setTerrainParam("ridge", p.ridge);
    world.setTerrainParam("warp", p.warp);
    world.setTerrainParam("exponent", p.exponent);
    world.setTerrainParam("continent", p.continent);
    world.setTerrainParam("island", p.island);
    world.setTerrainParam("coast", p.coast);
    world.setTerrainParam("worldWidth", p.worldWidth.tofloat());
    world.setTerrainParam("worldHeight", p.worldHeight.tofloat());
    world.setTerrainParam("sandLevel", p.sandLevel);
    world.setTerrainParam("enable", 1.0);
    world.clear();
    if (editSession != null) editSession.clearHistory();
    streamWorld();
    print("voxel-terrain: preset " + (idx + 1) + "/" + presets.len() + " [" + p.name +
          "] seed=" + seed + " chunks=" + world.getChunkCount() + "\n");
}

function streamWorld() {
    local pcx = floor(playerPos[0] / 32.0).tointeger();
    local pcy = floor(playerPos[1] / 32.0).tointeger();
    local pcz = floor(playerPos[2] / 32.0).tointeger();
    world.streamAround(pcx, pcy, pcz, streamRadius);
}

// ---------------------------------------------------------------------------
// 相机与输入
// ---------------------------------------------------------------------------
function forwardVec() {
    return [cos(pitch) * sin(yaw), sin(pitch), cos(pitch) * cos(yaw)];
}

function updateCamera(dt) {
    local speed = (keyboard.isDown("lshift") || keyboard.isDown("rshift"))
                  ? FLY_FAST : FLY_SPEED;
    local f = forwardVec();
    local r = [-cos(yaw), 0.0, sin(yaw)];  // 水平右向量

    if (keyboard.isDown("w") || keyboard.isDown("W")) {
        playerPos[0] += f[0] * speed * dt;
        playerPos[1] += f[1] * speed * dt;
        playerPos[2] += f[2] * speed * dt;
    }
    if (keyboard.isDown("s") || keyboard.isDown("S")) {
        playerPos[0] -= f[0] * speed * dt;
        playerPos[1] -= f[1] * speed * dt;
        playerPos[2] -= f[2] * speed * dt;
    }
    if (keyboard.isDown("d") || keyboard.isDown("D")) {
        playerPos[0] += r[0] * speed * dt;
        playerPos[2] += r[2] * speed * dt;
    }
    if (keyboard.isDown("a") || keyboard.isDown("A")) {
        playerPos[0] -= r[0] * speed * dt;
        playerPos[2] -= r[2] * speed * dt;
    }
    if (keyboard.isDown("space") || keyboard.isDown("Space"))
        playerPos[1] += speed * dt;
    if (keyboard.isDown("c") || keyboard.isDown("C"))
        playerPos[1] -= speed * dt;
}

function setMouseCaptured(captured) {
    mouseCaptured = captured;
    mouse.setVisible(!captured);
    local cx = config.width * 0.5;
    local cy = config.height * 0.5;
    if (captured) mouse.setPosition(cx, cy);
}

function updateMouseLook() {
    if (key_just_pressed("escape", "Escape")) setMouseCaptured(!mouseCaptured);
    if (!mouseCaptured) return;

    local cx = config.width * 0.5;
    local cy = config.height * 0.5;
    local dx = mouse.getX() - cx;
    local dy = mouse.getY() - cy;
    yaw -= dx * LOOK_SENS;
    pitch += dy * LOOK_SENS;  // 屏幕 Y 向下
    pitch = clampf(pitch, -1.5, 1.5);
    mouse.setPosition(cx, cy);
}

function mouseLeftPressed() {
    local down = mouse.isDown(1);  // 1 = 左键
    local was = prevLeft;
    prevLeft = down;
    return down && !was;
}

function mouseRightPressed() {
    local down = mouse.isDown(2);
    local was = prevRight;
    prevRight = down;
    return down && !was;
}

function raycastCrosshair() {
    local f = forwardVec();
    return world.raycast(playerPos[0], playerPos[1], playerPos[2],
                         f[0], f[1], f[2], 180.0);
}

function placeAtCrosshair() {
    if (!raycastCrosshair()) {
        lastEdit = "放置失败：准星没有命中方块";
        return;
    }
    local x = world.getRaycastPrevX();
    local y = world.getRaycastPrevY();
    local z = world.getRaycastPrevZ();
    volumeOperation.setValue(5);
    editSession.dispatchPointer3D(0, 1, 0, x, y, z, 0, 0, 0, 1.0);
    editSession.dispatchPointer3D(2, 1, 0, x, y, z, 0, 0, 0, 1.0);
    world.remeshDirty();
    lastEdit = (world.getCubeTypeName(x, y, z) == "wood")
               ? format("已放置木块 (%d, %d, %d)", x, y, z)
               : format("放置失败 (%d, %d, %d)", x, y, z);
}

function breakAtCrosshair() {
    if (!raycastCrosshair()) {
        lastEdit = "破坏失败：准星没有命中方块";
        return;
    }
    local x = world.getRaycastHitX();
    local y = world.getRaycastHitY();
    local z = world.getRaycastHitZ();
    volumeOperation.setValue(0);
    editSession.dispatchPointer3D(0, 2, 0, x, y, z, 0, 0, 0, 1.0);
    editSession.dispatchPointer3D(2, 2, 0, x, y, z, 0, 0, 0, 1.0);
    world.remeshDirty();
    lastEdit = (world.getVoxel(x, y, z) == 0)
               ? format("已破坏方块 (%d, %d, %d)", x, y, z)
               : format("破坏失败 (%d, %d, %d)", x, y, z);
}

// ---------------------------------------------------------------------------
// HUD
// ---------------------------------------------------------------------------
function buildHud() {
    if (uiReady) return;
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("VoxelTerrain", "root");
    ui.text("", "stats");
    ui.text("", "help");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostPos(12.0, 8.0, 0.0, 0.0);
    uiReady = true;
}

function refreshHud() {
    if (!uiReady) return;
    local p = presets[presetIdx % presets.len()];
    ui.setText("stats",
        "voxel-terrain  预设[" + (presetIdx + 1) + "/" + presets.len() + "] " + p.name +
        "  seed " + seed + "\n" +
        format("位置 (%.0f, %.0f, %.0f)   流式半径 %d chunk", playerPos[0], playerPos[1],
               playerPos[2], streamRadius) + "\n" +
        format("chunks %d   可见 %d   批次 %d   矩形 %d   FPS %.0f",
               world.getChunkCount(), world.getVisibleChunkCount(),
               world.getVisibleBatchCount(), world.getVisibleRectCount(), fpsAvg) +
        (lastEdit == "" ? "" : "\n" + lastEdit));
    ui.setText("help",
        "WASD 飞行  Space/C 升降  Shift 加速  鼠标视角  Esc释放/捕获  左键放置  右键破坏  Z/Y撤销重做\n" +
        "1群岛 2大陆 3山脉 4沙海  R随机种子  =/-流式半径");
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
eve_init = function() {
    if (voxel == null) voxel = eve.Voxel();
    setupWorld();
    buildHud();
    gfx.setBackgroundColor(0.42, 0.62, 0.82, 1.0);  // 天空蓝
    gfx.setDirectionalLight(-0.4, -1.0, -0.35, 1.15, 1.08, 0.96);
    setMouseCaptured(true);
    if (!shownHelp) {
        print("voxel-terrain test scene: WASD fly, mouse = look, Esc = release/capture, " +
              "left = place wood, right = break, 1-4 = presets, R = random seed\n");
        shownHelp = true;
    }
};

eve_update = function(dt) {
    elapsed += dt;
    fpsAvg = fpsAvg * 0.92 + (1.0 / maxf(dt, 0.0001)) * 0.08;

    updateCamera(dt);
    updateMouseLook();

    if (mouseLeftPressed()) placeAtCrosshair();
    if (mouseRightPressed()) breakAtCrosshair();
    if (key_just_pressed("z") || key_just_pressed("Z")) {
        if (editSession.undo()) {
            world.remeshDirty();
            lastEdit = "已撤销体素笔刷";
        }
    }
    if (key_just_pressed("y") || key_just_pressed("Y")) {
        if (editSession.redo()) {
            world.remeshDirty();
            lastEdit = "已重做体素笔刷";
        }
    }

    // 每帧保持玩家周围 chunk；跨 chunk 边界时自动补建/卸载
    streamWorld();

    if (key_just_pressed("1")) { presetIdx = 0; applyTerrainPreset(presetIdx, seed); }
    if (key_just_pressed("2")) { presetIdx = 1; applyTerrainPreset(presetIdx, seed); }
    if (key_just_pressed("3")) { presetIdx = 2; applyTerrainPreset(presetIdx, seed); }
    if (key_just_pressed("4")) { presetIdx = 3; applyTerrainPreset(presetIdx, seed); }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        seed = (clock() * 2654435761.0).tointeger() & 0x7FFFFFFF;
        applyTerrainPreset(presetIdx, seed);
    }
    if (key_just_pressed("plus") || key_just_pressed("equals")) {
        streamRadius = clampf(streamRadius + 1, 2, 5);
        streamWorld();
    }
    if (key_just_pressed("minus")) {
        streamRadius = clampf(streamRadius - 1, 2, 5);
        streamWorld();
    }
    if (key_just_pressed("F12")) {
        shotPending = true;
        print("voxel-terrain: F12 screenshot -> " + shotPath + "\n");
    }
};

eve_render = function() {
    gfx.clear();

    // 2D 十字准星（渲染队列会在 3D 之后叠加）
    local cx = config.width * 0.5;
    local cy = config.height * 0.5;
    gfx.drawSolidRect(cx - 11.0, cy - 1.0, 22.0, 2.0, 1.0, 1.0, 1.0, 0.85);
    gfx.drawSolidRect(cx - 1.0, cy - 11.0, 2.0, 22.0, 1.0, 1.0, 1.0, 0.85);
    gfx.drawSolidRect(cx - 2.0, cy - 2.0, 4.0, 4.0, 1.0, 0.25, 0.25, 1.0);

    local f = forwardVec();
    local eye = playerPos;
    local target = [eye[0] + f[0], eye[1] + f[1], eye[2] + f[2]];
    local view = mat4LookAt(eye, target, [0.0, 1.0, 0.0]);
    local proj = mat4Perspective(55.0, config.width.tofloat() / config.height.tofloat(),
                                 0.1, 600.0);
    local viewProj = proj.multiplied(view);

    local vp = [];
    local viewArr = [];
    for (local i = 0; i < 16; i++) vp.push(viewProj.get(i));
    for (local i = 0; i < 16; i++) viewArr.push(view.get(i));

    world.selectVisible(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], vp[6], vp[7],
                        vp[8], vp[9], vp[10], vp[11], vp[12], vp[13], vp[14], vp[15],
                        eye[0], eye[1], eye[2], VIEW_RANGE, true);
    if (!statsPrinted) {
        print("voxel-terrain stats: chunks=" + world.getChunkCount() +
              " visible=" + world.getVisibleChunkCount() +
              " batches=" + world.getVisibleBatchCount() +
              " rects=" + world.getVisibleRectCount() +
              " spawn=(" + eye[0].tointeger() + "," + eye[1].tointeger() + "," + eye[2].tointeger() + ")\n");
        statsPrinted = true;
    }
    gfx.setMesh3DViewProj(vp);
    gfx.setMesh3DView(viewArr);
    gfx.setMesh3DCameraPos(eye[0], eye[1], eye[2]);
    gfx.begin3DFrame();
    world.drawVisible(gfx, atlas, TILES_PER_ROW);

    refreshHud();
    ui.beginFrameAndRender();

    // 交换链回读截图：首次调用启用 readback，下一帧 present 后才有数据，
    // 所以每帧重试直到成功（保存一次后自动关闭）。
    if (shotPending && gfx.saveFramePng(shotPath)) {
        print("voxel-terrain: saved screenshot to " + shotPath + "\n");
        shotPending = false;
    }
};
