// EVEngine WebGPU Playground — 默认 3D 示例（旋转木纹球体）
//
// 热更新说明：
//   - 修改 eve_update / eve_render 后按 Ctrl+Enter（或“应用”），下一帧立即生效；
//   - `__pg` 表里的状态会在热更新时保留（试试把 spinSpeed 改大，应用后球体转得更快且
//     frames 不会清零）；
//   - 点“重置状态”会清空 __pg 并重建场景。
// 木纹贴图让旋转一目了然——试试改成 spinSpeed = 2.5 看转速变化。

if (!("__pg" in getroottable())) __pg <- {};
// 顶层初始化：每次 dofile（应用/重置）都会执行，保证 eve_update 在任何时刻
// 都能安全访问状态，不依赖 eve_init 是否已运行。
if (!("frames" in __pg)) __pg.frames <- 0;

local spinSpeed = 0.4; // ← 试着改成 1.5，然后按 Ctrl+Enter

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.12, 1.0);

    if (!("cam" in __pg)) {
        __pg.cam <- eve.Camera3D();
        __pg.cam.setEye(0.0, 0.0, 3.2);
        __pg.cam.setTarget(0.0, 0.0, 0.0);
        __pg.cam.setFov(45.0);
        __pg.cam.setActive(true);
    }

    if (!("ball" in __pg)) {
        __pg.ball <- eve.Renderable3D();
        __pg.ball.setMesh(gfx.newMeshSphere(48, 24));
        __pg.ball.setTexture(gfx.newTextureFromFile("playground/assets/wood.jpg"));
        __pg.ball.setTint(1.0, 0.96, 0.9, 1.0);
        __pg.ball.setRoughness(0.72);
        __pg.ball.setMetallic(0.04);
    }

    gfx.setDirectionalLight(-0.35, 0.7, 0.45, 1.0, 0.95, 0.9);

    print("3d sphere demo init ok\n");
};

eve_update = function(dt) {
    if (!("ball" in __pg)) return;
    __pg.ball.setYaw(__pg.ball.getYaw() + dt * spinSpeed);
    __pg.frames += 1;
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};

// 热更新后由引擎调用（soft_reload_scripts → dofile → eve_reload）。
eve_reload <- function() {
    local frames = ("frames" in __pg) ? __pg.frames : 0;
    print("hot-reload applied — state kept (frames=" + frames + ", spinSpeed=" + spinSpeed + ")\n");
};
