// ============================================================================
// EVEngine Building 模块示例 —— 在多样式 tilemap（等距 / 六角）上放置建筑
//
// 演示：
//   map 模块 TileLayer + building::PlacementWorld.bindTileLayer（投影复用）
//   GID -> 地形语义懒解析（world.setTerrainGid）
//   BuildingFx 双形态渲染桥（2D 精灵进统一 2D 队列）
//   PlacementSession（放置 / 拆除 / 旋转 / 鬼影校验）
//
// 按键：1-3 选建筑  R 旋转  左键放置  右键拆除  T 切换 grid 叠加
// 运行： make run/<platform>-debug GAME=examples/building-tilemap
// ============================================================================

if (!("bld" in getroottable())) bld <- null;
if (!("mapMod" in getroottable())) mapMod <- null;
if (!("layer" in getroottable())) layer <- null;
if (!("world" in getroottable())) world <- null;
if (!("session" in getroottable())) session <- null;
if (!("fx" in getroottable())) fx <- null;
if (!("palette" in getroottable())) palette <- [];
if (!("paletteIndex" in getroottable())) paletteIndex <- 0;
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("prevMouse" in getroottable())) prevMouse <- { left = false, right = false };
if (!("uiBuilt" in getroottable())) uiBuilt <- false;
if (!("layout" in getroottable())) layout <- "isometric";

const MAP_W = 16;
const MAP_H = 12;
const TILE_W = 64.0;
const TILE_H = 32.0;
const GID_LAND = 1;
const GID_WATER = 2;

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function mousePressed(button) {
    local down = mouse.isDown(button);
    local was = false;
    if (button == 0) was = prevMouse.left;
    else if (button == 2) was = prevMouse.right;
    if (button == 0) prevMouse.left = down;
    else if (button == 2) prevMouse.right = down;
    return down && !was;
}

function registerBuildings() {
    bld.registerBuildingsFromJson(@"[{
      ""id"":""house"",""displayName"":""房屋"",""category"":""housing"",
      ""footprintW"":2,""footprintH"":2,""tags"":[""house""],
      ""requireTerrain"":[1],
      ""visual2d"":{""colorR"":""0.72"",""colorG"":""0.48"",""colorB"":""0.28""}},
      {""id"":""dock"",""displayName"":""码头"",""category"":""infra"",
       ""footprintW"":2,""footprintH"":1,""tags"":[""dock""],
       ""requireTerrain"":[2],""rotationMode"":""cardinal"",
       ""visual2d"":{""colorR"":""0.35"",""colorG"":""0.55"",""colorB"":""0.70""}},
      {""id"":""tower"",""displayName"":""塔楼"",""category"":""defense"",
       ""footprintW"":1,""footprintH"":1,""tags"":[""tower""],
       ""requireTerrain"":[1],""rotationMode"":""none"",
       ""visual2d"":{""colorR"":""0.62"",""colorG"":""0.40"",""colorB"":""0.55""}}
    ]");
}

function buildTilemap() {
    if (layer != null) {
        layer.setTint(0.0, 0.0, 0.0, 0.0);
        layer.setVisible(false);
    }
    layer = mapMod.newLayer(MAP_W, MAP_H, TILE_W, TILE_H);
    local iso = (layout == "isometric");
    if (iso) {
        layer.applyConfig(@"{
          ""orientation"":""isometric"",
          ""tilewidth"":64,""tileheight"":32
        }");
    } else {
        layer.applyConfig(@"{
          ""orientation"":""hexagonal"",
          ""staggeraxis"":""y"",
          ""staggerindex"":""odd"",
          ""hexsidelength"":16,
          ""tilewidth"":64,""tileheight"":64
        }");
    }
    layer.fill(GID_LAND);
    // 一条河（列 5-8 行 4-7 为水域）。
    for (local x = 5; x <= 8; x += 1)
        for (local y = 4; y <= 7; y += 1)
            layer.setTile(x, y, GID_WATER);

    if (world) world.destroy();
    world = bld.newWorld(MAP_W, MAP_H, TILE_W);
    world.setId("tilemap-town");
    world.bindTileLayer(layer);
    world.setTerrainGid(GID_LAND, 1);
    world.setTerrainGid(GID_WATER, 2);

    if (session) session.destroy();
    session = bld.newSession();
    session.startPlacement(world, palette[paletteIndex]);

    if (fx) fx.detach(world);
    fx.attach(world);
    fx.setGridVisible(world, true);
    bld.clearChangeEvents();
}

function selectPalette(index) {
    if (index < 0 || index >= palette.len()) return;
    paletteIndex = index;
    session.startPlacement(world, palette[paletteIndex]);
}

function refreshHud() {
    if (!uiBuilt) return;
    local name = bld.getBuildingDisplayName(palette[paletteIndex]);
    local info = session.isValid() ? "可放置" : ("不可：" + session.getReason());
    ui.setText("stats",
        "布局 " + layout + "  建筑数 " + world.getBuildingCount() +
        "  当前 [" + (paletteIndex + 1) + "] " + name +
        "  旋转 " + session.getRotationDeg().tointeger() + "°  " + info);
    ui.setText("help",
        "1房屋(陆地) 2码头(水域) 3塔楼 | 移动预览 | R旋转 | 左键建 | 右键拆 | T网格");
}

eve_init = function() {
    print("examples/building-tilemap ready\n");
    gfx.setBackgroundColor(0.08, 0.10, 0.12, 1.0);
    if (bld == null) bld = eve.Building();
    if (mapMod == null) mapMod = eve.Map();
    if (fx == null) fx = eve.BuildingFx();
    registerBuildings();
    palette = ["house", "dock", "tower"];
    paletteIndex = 0;
    buildTilemap();

    if (!uiBuilt) {
        ui.beginBuild();
        ui.beginWindow("TilemapBuilding", "root");
        ui.text("Tilemap 建筑放置", "title");
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
    buildTilemap();
};

eve_update = function(dt) {
    if (world == null || session == null) return;

    if (keyPressed("1")) selectPalette(0);
    if (keyPressed("2")) selectPalette(1);
    if (keyPressed("3")) selectPalette(2);
    if (keyPressed("r") || keyPressed("R")) session.rotateBy(90.0);
    if (keyPressed("t") || keyPressed("T"))
        fx.setGridVisible(world, !fx.getGridVisible(world));

    local mx = mouse.getX();
    local my = mouse.getY();
    session.updateFromWorld(world, mx, my);

    if (mousePressed(0)) {
        session.setMode("place");
        local id = session.execute();
        if (id <= 0)
            print("放置失败: " + session.getReason() + "\n");
    }
    if (mousePressed(2)) {
        session.setMode("remove");
        local id = session.execute();
        session.setMode("place");
        if (id <= 0)
            print("此处无建筑\n");
    }
    fx.sync(world);
    fx.updateGhost(world, session.getGhost());
    refreshHud();
};

eve_render = function() {
    gfx.clear();
    if (world != null) {
        mapMod.render(gfx);
        fx.drawGrid2D(world, gfx);
    }
    ui.beginFrameAndRender();
};
