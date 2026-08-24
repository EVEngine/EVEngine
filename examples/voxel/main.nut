// ============================================================================
// EVEngine 体素示例 —— 流式地形 + 建造 / 破坏
//
// 演示：eve.Voxel 的方块类型注册表、程序化图集、自动流式地形生成
//       （setTerrain + streamAround）、DDA 射线拾取与按名字放置方块。
//
// 操作：左键点击 放置木头块  右键点击 破坏方块  R 重建所有脏 chunk
//       （轨道相机自动环绕，无需键盘移动）
// 运行：make run/<platform>-debug GAME=examples/voxel
// ============================================================================

if (!("math" in getroottable())) math <- eve.Math();
if (!("voxel" in getroottable())) voxel <- null;
if (!("world" in getroottable())) world <- null;
if (!("types" in getroottable())) types <- null;
if (!("atlas" in getroottable())) atlas <- null;
if (!("cam" in getroottable())) cam <- null;
if (!("elapsed" in getroottable())) elapsed <- 0.0;
if (!("prevMouse" in getroottable())) prevMouse <- { left = false, right = false };
if (!("shownHelp" in getroottable())) shownHelp <- false;
if (!("statsPrinted" in getroottable())) statsPrinted <- false;

const TILE = 32;        // atlas tile size in pixels
const TILES_PER_ROW = 4;

// ---------------------------------------------------------------------------
// 小型列主序矩阵工具（脚本侧合成 proj * view，供 selectVisible 使用）
// ---------------------------------------------------------------------------
function mat4Perspective(fovyDeg, aspect, near, far) {
    local f = 1.0 / tan(fovyDeg * 0.5 * PI / 180.0);
    local m = math.newMat4();
    m.set(0, f / aspect);
    m.set(5, -f);   // Vulkan NDC Y 向下：与引擎 perspectiveVulkanRH_ZO 一致
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

// 与 glm::lookAt 相同的右手 Y-up 视图矩阵（列主序）。
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
// 程序化 4x4 图集：1=草顶 2=泥土 3=石头 4=木头
// ---------------------------------------------------------------------------
function buildAtlas() {
    local canvas = gfx.newCanvas(TILE * TILES_PER_ROW, TILE * TILES_PER_ROW);
    local colors = [
        [0.25, 0.62, 0.28],  // 1 grass top
        [0.48, 0.34, 0.20],  // 2 dirt
        [0.55, 0.55, 0.58],  // 3 stone
        [0.62, 0.42, 0.22],  // 4 wood
    ];
    gfx.setCanvas(canvas);
    gfx.setBackgroundColor(0.0, 0.0, 0.0, 1.0);
    gfx.clear();
    for (local i = 0; i < colors.len(); i++) {
        local cx = ((i % TILES_PER_ROW) * TILE).tofloat();
        local cy = ((i / TILES_PER_ROW) * TILE).tofloat();
        gfx.drawSolidRect(cx, cy, TILE.tofloat(), TILE.tofloat(),
                          colors[i][0], colors[i][1], colors[i][2], 1.0);
        // 简单边框，便于肉眼看图集分格
        gfx.drawSolidRect(cx + 2.0, cy + 2.0, (TILE - 4).tofloat(), 2.0, 0.0, 0.0, 0.0, 1.0);
    }
    gfx.setCanvas(null);
    return canvas.getTexture();
}

function setupWorld() {
    if (types == null) {
        types = voxel.newCubeTypes();
        types.loadFromJson(@"[
            {""name"":""grass"",""faceTex"":[1,1,2,2,2,2]},
            {""name"":""stone"",""faceTex"":[3,3,3,3,3,3]},
            {""name"":""wood"",""faceTex"":[4,4,4,4,4,4]}
        ]");
    }
    if (world == null) {
        world = voxel.newWorldWithTypes(types);
        // 确定性地形：seed + 草/土/石纹理 id + 基准高度/幅度/缩放
        world.setTerrain(20260822, 1, 2, 3, 8.0, 14.0, 1.0 / 32.0);
        world.streamAround(0, 0, 0, 2);   // 自动补建玩家周围 chunk（球形）
        world.remeshDirty();
    }
    if (cam == null) {
        cam = eve.Camera3D();
        cam.setEye(0.0, 26.0, 42.0);
        cam.setTarget(0.0, 8.0, 0.0);
        cam.setUp(0.0, 1.0, 0.0);
        cam.setFov(50.0);
        cam.setActive(true);
    }
    if (atlas == null) atlas = buildAtlas();
}

// 鼠标按键编号：1 = 左键，2 = 右键（与 engine mouse::isDown 一致）。
function mousePressed(button) {
    local down = mouse.isDown(button);
    local was = (button == 1) ? prevMouse.left : prevMouse.right;
    if (button == 1) prevMouse.left = down;
    else prevMouse.right = down;
    return down && !was;
}

function handlePick() {
    local mx = mouse.getX();
    local my = mouse.getY();
    cam.screenToRay(mx, my, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local ox = cam.getScreenRayOriginX();
    local oy = cam.getScreenRayOriginY();
    local oz = cam.getScreenRayOriginZ();
    local dx = cam.getScreenRayDirX();
    local dy = cam.getScreenRayDirY();
    local dz = cam.getScreenRayDirZ();

    if (!world.raycast(ox, oy, oz, dx, dy, dz, 200.0)) return;
    local hx = world.getRaycastHitX();
    local hy = world.getRaycastHitY();
    local hz = world.getRaycastHitZ();
    if (mousePressed(1)) {
        // 放置：命中方块表面法线方向放木头
        world.setVoxelByName(hx + world.getRaycastFaceX(),
                             hy + world.getRaycastFaceY(),
                             hz + world.getRaycastFaceZ(), "wood");
        world.remeshDirty();
    } else if (mousePressed(2)) {
        // 破坏
        world.setVoxel(hx, hy, hz, 0);
        world.remeshDirty();
    }
}

eve_init = function() {
    if (voxel == null) voxel = eve.Voxel();
    setupWorld();
    if (!shownHelp) {
        print("voxel demo: left click = place wood, right click = break, R = full remesh\n");
        shownHelp = true;
    }
};

eve_update = function(dt) {
    elapsed += dt;
    handlePick();
    if (keyboard.isDown("r")) world.remeshDirty();
};

eve_render = function() {
    // 轨道相机：绕目标点缓慢旋转
    local dist = 46.0;
    local height = 30.0 + sin(elapsed * 0.18) * 6.0;
    local eye = [cos(elapsed * 0.22) * dist, height, sin(elapsed * 0.22) * dist];
    local target = [0.0, 8.0, 0.0];
    local view = mat4LookAt(eye, target, [0.0, 1.0, 0.0]);
    local proj = mat4Perspective(50.0, config.width.tofloat() / config.height.tofloat(), 0.1, 500.0);
    local viewProj = proj.multiplied(view);

    local vp = [];
    local viewArr = [];
    for (local i = 0; i < 16; i++) vp.push(viewProj.get(i));
    for (local i = 0; i < 16; i++) viewArr.push(view.get(i));

    gfx.clear();
    gfx.setDirectionalLight(-0.4, -1.0, -0.35, 1.1, 1.05, 0.95);

    world.selectVisible(vp[0], vp[1], vp[2], vp[3], vp[4], vp[5], vp[6], vp[7],
                        vp[8], vp[9], vp[10], vp[11], vp[12], vp[13], vp[14], vp[15],
                        eye[0], eye[1], eye[2], 220.0, true);
    if (!statsPrinted) {
        print("voxel stats: chunks=" + world.getVisibleChunkCount() +
              " batches=" + world.getVisibleBatchCount() +
              " rects=" + world.getVisibleRectCount() + "\n");
        statsPrinted = true;
    }
    gfx.setMesh3DViewProj(vp);
    gfx.setMesh3DView(viewArr);
    gfx.setMesh3DCameraPos(eye[0], eye[1], eye[2]);
    gfx.begin3DFrame();
    world.drawVisible(gfx, atlas, TILES_PER_ROW);
};
