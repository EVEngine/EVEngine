// EVEngine WebGPU Playground — 3D 纹理立方体 + 环绕球体
//
// 立方体贴 wood.jpg（CC0，Wikimedia Commons），金属球绕它公转。
// 修改 cubeSpeed / sphereRadius 后按 Ctrl+Enter 立即生效。

persist __pg = {}
// 顶层初始化：每次 dofile（应用/重置）都会执行，保证 eve_update 在任何时刻
// 都能安全访问状态，不依赖 eve_init 是否已运行。
if (!("frames" in __pg)) __pg.frames <- 0;
if (!("angle" in __pg)) __pg.angle <- 0.0;

local cubeSpeed = 0.8;      // ← 旋转速度
local sphereRadius = 1.8;   // ← 公转半径

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.06, 0.10, 1.0);

    if (!("cam" in __pg)) {
        __pg.cam <- eve.Camera3D();
        __pg.cam.setEye(0.0, 0.0, 3.5);
        __pg.cam.setTarget(0.0, 0.0, 0.0);
        __pg.cam.setFov(50.0);
        __pg.cam.setActive(true);
    }

    if (!("cube" in __pg)) {
        __pg.cube <- eve.Renderable3D();
        __pg.cube.setMesh(gfx.newMeshCube(1.0));
        __pg.cube.setTexture(gfx.newTextureFromFile("playground/assets/wood.jpg"));
        __pg.cube.setTint(1.0, 0.96, 0.88, 1.0);
        __pg.cube.setRoughness(0.65);
        __pg.cube.setMetallic(0.05);
    }

    if (!("sphere" in __pg)) {
        __pg.sphere <- eve.Renderable3D();
        __pg.sphere.setMesh(gfx.newMeshSphere(32, 16));
        __pg.sphere.setTint(0.55, 0.82, 0.95, 1.0);
        __pg.sphere.setMetallic(0.9);
        __pg.sphere.setRoughness(0.2);
    }

    gfx.setDirectionalLight(-0.4, 0.6, 0.5, 1.0, 0.95, 0.9);
    print("3d textured demo init ok\n");
};

eve_update = function(dt) {
    __pg.frames += 1;
    if (!("cube" in __pg)) return;
    __pg.cube.setYaw(__pg.cube.getYaw() + dt * cubeSpeed);
    __pg.angle += dt * 1.2;
    if ("sphere" in __pg)
        __pg.sphere.setPosition(sphereRadius * cos(__pg.angle), 0.5, sphereRadius * sin(__pg.angle));
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};

eve_reload <- function() {
    local frames = ("frames" in __pg) ? __pg.frames : 0;
    print("hot-reload applied — state kept (frames=" + frames +
          ", cubeSpeed=" + cubeSpeed + ", radius=" + sphereRadius + ")\n");
};
