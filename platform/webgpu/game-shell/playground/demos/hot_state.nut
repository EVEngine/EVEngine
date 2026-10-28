// EVEngine WebGPU Playground — 热更新状态演示
//
// 玩法：
//   1. 把 hueSpeed 改成 3.0，按 Ctrl+Enter —— 颜色变化立刻变快；
//   2. 观察控制台：每次应用都会打印累计 frames，状态没有重置；
//   3. 点“重置状态”清空 __pg，立方体从 0 帧重新开始。

persist __pg = {}
// 顶层初始化：每次 dofile（应用/重置）都会执行，保证 eve_update 在任何时刻
// 都能安全访问状态，不依赖 eve_init 是否已运行。
if (!("frames" in __pg)) __pg.frames <- 0;

local hueSpeed = 0.6; // ← 试着改成 3.0 并应用

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.08, 0.14, 1.0);

    if (!("cam" in __pg)) {
        __pg.cam <- eve.Camera3D();
        __pg.cam.setEye(0.0, 1.2, 3.0);
        __pg.cam.setTarget(0.0, 0.0, 0.0);
        __pg.cam.setFov(50.0);
        __pg.cam.setActive(true);
    }

    if (!("cube" in __pg)) {
        __pg.cube <- eve.Renderable3D();
        __pg.cube.setMesh(gfx.newMeshCube(1.0));
    }

    print("hot-state demo init ok\n");
};

eve_update = function(dt) {
    __pg.frames += 1;
    if (!("cube" in __pg)) return;
    local hue = __pg.frames * 0.001 * hueSpeed;
    // 简易色相循环（仅演示用）
    local r = 0.5 + 0.5 * sin(hue * 6.28318);
    local g = 0.5 + 0.5 * sin(hue * 6.28318 + 2.09439);
    local b = 0.5 + 0.5 * sin(hue * 6.28318 + 4.18879);
    __pg.cube.setTint(r, g, b, 1.0);
    __pg.cube.setYaw(__pg.cube.getYaw() + dt * 1.2);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};

eve_reload <- function() {
    local frames = ("frames" in __pg) ? __pg.frames : 0;
    print("applied — state kept, frames=" + frames + " (hueSpeed=" + hueSpeed + ")\n");
};
