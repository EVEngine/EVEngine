// EVEngine WebGPU Playground — 2D 精灵 + 键盘
//
// 使用打包在 /game/playground/assets 下的 CC0 贴图（Kenney Platformer Pack）。
// 方向键控制方块移动；修改下面的 spd 后按 Ctrl+Enter 立即生效。

if (!("__pg" in getroottable())) __pg <- {};
// 顶层初始化：每次 dofile（应用/重置）都会执行，保证 eve_update 在任何时刻
// 都能安全访问状态，不依赖 eve_init 是否已运行。
if (!("frames" in __pg)) __pg.frames <- 0;
if (!("x" in __pg)) { __pg.x <- 120.0; __pg.y <- 120.0; }

local spd = 260.0; // ← 试试改成 500.0，然后按 Ctrl+Enter

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);

    if (!("bg" in __pg))
        __pg.bg <- gfx.newTextureFromFile("playground/assets/background_forest.png");
    if (!("sprite" in __pg))
        __pg.sprite <- gfx.newTextureFromFile("playground/assets/crate.png");

    print("2d sprites demo init ok\n");
};

eve_update = function(dt) {
    __pg.frames += 1;
    local vx = 0.0, vy = 0.0;
    if (keyboard.isDown("left"))  vx = -spd;
    if (keyboard.isDown("right")) vx =  spd;
    if (keyboard.isDown("up"))    vy = -spd;
    if (keyboard.isDown("down"))  vy =  spd;
    __pg.x += vx * dt;
    __pg.y += vy * dt;
    if (__pg.x < 32.0) __pg.x = 32.0;
    if (__pg.x > config.width - 32.0) __pg.x = config.width - 32.0;
    if (__pg.y < 32.0) __pg.y = 32.0;
    if (__pg.y > config.height - 32.0) __pg.y = config.height - 32.0;
};

eve_render = function() {
    gfx.clear();
    // 背景铺满窗口
    if ("bg" in __pg && __pg.bg != null)
        gfx.drawTexturedRect(__pg.bg, 0.0, 0.0,
            config.width.tofloat(), config.height.tofloat(), 1.0, 1.0, 1.0, 1.0);
    // 精灵（64×64）
    if ("sprite" in __pg && __pg.sprite != null)
        gfx.drawTexturedRect(__pg.sprite, __pg.x - 32.0, __pg.y - 32.0,
            64.0, 64.0, 1.0, 1.0, 1.0, 1.0);
    // 底部状态条
    gfx.drawSolidRect(0.0, config.height - 24.0,
        config.width.tofloat(), 24.0, 0.25, 0.30, 0.42, 1.0);
};

eve_reload <- function() {
    local frames = ("frames" in __pg) ? __pg.frames : 0;
    print("hot-reload applied — state kept (frames=" + frames + ", spd=" + spd + ")\n");
};
