// ============================================================================
// EVEngine Building 模块示例 —— 城镇建筑放置沙盒
//
// 演示 eve.Building()：
//   建筑定义  —— JSON：占地、掩码、地形/邻接、代价
//   PlacementWorld —— 地形语义 + 占用格子
//   Ghost     —— 吸附预览 + 校验原因（绿/红）
//   操作      —— 放置 / 旋转 / 拆除 / 重置
//
// 按键：1-5 选建筑  R 旋转  左键放置  右键拆除  Space 重置
// 运行： make run/<platform>-debug GAME=examples/building
//        或 make building
// ============================================================================

persist bld = null
persist world = null
persist ghost = null
persist palette = []
persist paletteIndex = 0
persist gold = 0
persist wood = 0
persist logLines = []
persist prevKeys = {}
persist prevMouse = { left = false, right = false }
persist uiBuilt = false
persist mapOriginX = 24.0
persist mapOriginY = 88.0
persist cellPx = 28.0
persist gridW = 24
persist gridH = 18

// 地形语义：1=陆地 2=水域
TERRAIN_LAND <- 1;
TERRAIN_WATER <- 2;

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 7)
        logLines.remove(0);
}

function keyPressed(name) {
    return key_just_pressed(name);
}

// 鼠标按键编号：1 = 左键，2 = 右键（与 engine mouse::isDown 一致）。
function mousePressed(button) {
    local down = mouse.isDown(button);
    local was = false;
    if (button == 1)
        was = prevMouse.left;
    else if (button == 2)
        was = prevMouse.right;
    if (button == 1)
        prevMouse.left = down;
    else if (button == 2)
        prevMouse.right = down;
    return down && !was;
}

function registerBuildings() {
    bld.registerBuildingsFromJson(@"[
      {""id"":""road"",""displayName"":""石板路"",""category"":""infra"",
       ""footprintW"":1,""footprintH"":1,""tags"":[""road""],""cost"":{""gold"":2}},
      {""id"":""house.wood"",""displayName"":""木屋"",""category"":""housing"",
       ""footprintW"":2,""footprintH"":2,""tags"":[""house"",""housing""],
       ""requireTerrain"":[1],""cost"":{""gold"":15,""wood"":20},
       ""extra"":{""color"":""warm""}},
      {""id"":""stall"",""displayName"":""摊位"",""category"":""commerce"",
       ""footprintW"":1,""footprintH"":1,""tags"":[""shop""],
       ""requireTerrain"":[1],""requireAdjacentTag"":""road"",
       ""cost"":{""gold"":8,""wood"":6}},
      {""id"":""dock"",""displayName"":""码头"",""category"":""infra"",
       ""footprintW"":2,""footprintH"":1,""tags"":[""dock""],
       ""requireTerrain"":[2],""rotationMode"":""cardinal"",
       ""cost"":{""gold"":25,""wood"":30}},
      {""id"":""barn.l"",""displayName"":""L形仓"",""category"":""storage"",
       ""footprintW"":2,""footprintH"":2,""tags"":[""barn""],
       ""footprintMask"":[1,1,1,0],""requireTerrain"":[1],
       ""cost"":{""gold"":18,""wood"":24}}
    ]");
}

function paintTerrain() {
    world.fillTerrain(TERRAIN_LAND);
    // 横向河道
    local y = 11;
    for (local x = 0; x < gridW; x += 1)
        world.setTerrain(x, y, TERRAIN_WATER);
    // 南岸小湖
    for (local x = 16; x <= 20; x += 1) {
        for (local yy = 13; yy <= 16; yy += 1)
            world.setTerrain(x, yy, TERRAIN_WATER);
    }
    // 北岸浅湾
    for (local x = 2; x <= 5; x += 1)
        world.setTerrain(x, 10, TERRAIN_WATER);
}

function resetTown() {
    if (world) world.destroy();
    if (ghost) ghost.destroy();

    world = bld.newWorld(gridW, gridH, cellPx);
    world.setId("town");
    world.setOrigin(mapOriginX, mapOriginY);
    world.setSnapMode("grid");
    paintTerrain();

    ghost = bld.newGhost();
    palette = ["road", "house.wood", "stall", "dock", "barn.l"];
    paletteIndex = 0;
    ghost.setBuildingId(palette[paletteIndex]);
    ghost.setRotationDeg(0.0);

    gold = 120;
    wood = 80;
    logLines = [];
    pushLog("欢迎来到城镇沙盒：先铺路，再摆摊；码头只能建在水上。");
    bld.clearChangeEvents();
}

function currentBuildingId() {
    return palette[paletteIndex];
}

function selectPalette(index) {
    if (index < 0 || index >= palette.len()) return;
    paletteIndex = index;
    ghost.setBuildingId(palette[paletteIndex]);
    pushLog("选择：" + bld.getBuildingDisplayName(palette[paletteIndex]));
}

function canAfford(buildingId) {
    local needGold = bld.getBuildingCost(buildingId, "gold");
    local needWood = bld.getBuildingCost(buildingId, "wood");
    if (gold < needGold) return "not_enough_gold";
    if (wood < needWood) return "not_enough_wood";
    return "";
}

function payCost(buildingId) {
    gold -= bld.getBuildingCost(buildingId, "gold");
    wood -= bld.getBuildingCost(buildingId, "wood");
}

function refundApprox(buildingId) {
    // 拆除返还一半（演示用，非正式经济）
    gold += (bld.getBuildingCost(buildingId, "gold") / 2).tointeger();
    wood += (bld.getBuildingCost(buildingId, "wood") / 2).tointeger();
}

function updateGhostFromMouse() {
    local mx = mouse.getX();
    local my = mouse.getY();
    ghost.setFromWorld(world, mx, my);
    ghost.validate(world);
}

function tryPlace() {
    updateGhostFromMouse();
    local id = currentBuildingId();
    local afford = canAfford(id);
    if (afford != "") {
        pushLog("资源不足：" + afford);
        return;
    }
    if (!ghost.isValid()) {
        pushLog("无法放置：" + ghost.getReason());
        return;
    }
    local inst = world.placeGhost(ghost);
    if (inst <= 0) {
        pushLog("放置失败。");
        return;
    }
    payCost(id);
    pushLog("已建造 " + bld.getBuildingDisplayName(id) + " #" + inst +
            " @(" + ghost.getCellX() + "," + ghost.getCellY() + ")");
}

function tryRemove() {
    local mx = mouse.getX();
    local my = mouse.getY();
    local cx = world.worldToCellX(mx);
    local cy = world.worldToCellY(my);
    local occ = world.getOccupant(cx, cy);
    if (occ <= 0) {
        pushLog("此处没有建筑。");
        return;
    }
    local bid = world.getBuildingId(occ);
    if (world.removeBuilding(occ)) {
        refundApprox(bid);
        pushLog("拆除 " + bld.getBuildingDisplayName(bid) + " #" + occ + "（半价返还）");
    }
}

function buildingColor(buildingId, out) {
    // out = [r,g,b]
    if (buildingId == "road") {
        out[0] = 0.45; out[1] = 0.45; out[2] = 0.48;
    } else if (buildingId == "house.wood") {
        out[0] = 0.72; out[1] = 0.48; out[2] = 0.28;
    } else if (buildingId == "stall") {
        out[0] = 0.85; out[1] = 0.62; out[2] = 0.22;
    } else if (buildingId == "dock") {
        out[0] = 0.35; out[1] = 0.55; out[2] = 0.70;
    } else if (buildingId == "barn.l") {
        out[0] = 0.55; out[1] = 0.38; out[2] = 0.55;
    } else {
        out[0] = 0.6; out[1] = 0.6; out[2] = 0.6;
    }
}

function footprintCells(buildingId, ox, oy, rot, sink) {
    // 与引擎 cardinal 旋转约定一致，便于绘制鬼影（不依赖私有 API）
    local w = bld.getBuildingFootprintW(buildingId);
    local h = bld.getBuildingFootprintH(buildingId);
    local mask = null;
    // barn.l 已知掩码；其它视为实心。旋转后枚举局部格。
    local q = ((rot / 90.0 + 0.5).tointeger()) % 4;
    if (q < 0) q += 4;

    for (local ly = 0; ly < h; ly += 1) {
        for (local lx = 0; lx < w; lx += 1) {
            local solid = true;
            if (buildingId == "barn.l") {
                // mask [1,1,1,0] row-major 2x2
                local idx = ly * w + lx;
                solid = (idx != 3);
            }
            if (!solid) continue;
            local rx = lx;
            local ry = ly;
            if (q == 1) { rx = ly; ry = w - 1 - lx; }
            else if (q == 2) { rx = w - 1 - lx; ry = h - 1 - ly; }
            else if (q == 3) { rx = h - 1 - ly; ry = lx; }
            sink.push([ox + rx, oy + ry]);
        }
    }
}

function refreshHud() {
    if (!uiBuilt) return;
    local name = bld.getBuildingDisplayName(currentBuildingId());
    local costG = bld.getBuildingCost(currentBuildingId(), "gold");
    local costW = bld.getBuildingCost(currentBuildingId(), "wood");
    local ghostInfo = ghost.isValid() ? "可放置" : ("不可：" + ghost.getReason());
    ui.setText("stats",
        "金币 " + gold + "  木材 " + wood +
        "  建筑数 " + world.getBuildingCount() +
        "  当前 [" + (paletteIndex + 1) + "] " + name +
        "  花费 G" + costG + "/W" + costW +
        "  旋转 " + ghost.getRotationDeg().tointeger() + "°  " + ghostInfo);
    ui.setText("help",
        "1路 2木屋 3摊位(需邻路) 4码头(水域) 5L仓 | 移动预览 | R旋转 | 左键建 | 右键拆 | Space重置");
    local logText = "";
    foreach (line in logLines)
        logText += line + "\n";
    ui.setText("log", logText);
}

eve_init = function() {
    print("examples/building: town placement sandbox ready\n");
    gfx.setBackgroundColor(0.08, 0.10, 0.12, 1.0);
    if (bld == null) {
        bld = eve.Building();
        registerBuildings();
        resetTown();
    } else {
        registerBuildings();
        if (world == null || ghost == null)
            resetTown();
    }

    if (!uiBuilt) {
        ui.beginBuild();
        ui.beginWindow("BuildingDemo", "root");
        ui.text("建筑放置系统示例", "title");
        ui.text("", "stats");
        ui.text("", "help");
        ui.text("", "log");
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
};

eve_update = function(dt) {
    if (world == null || ghost == null) return;

    if (keyPressed("1")) selectPalette(0);
    if (keyPressed("2")) selectPalette(1);
    if (keyPressed("3")) selectPalette(2);
    if (keyPressed("4")) selectPalette(3);
    if (keyPressed("5")) selectPalette(4);

    if (keyPressed("r") || keyPressed("R")) {
        ghost.rotateBy(90.0);
        pushLog("旋转 → " + ghost.getRotationDeg().tointeger() + "°");
    }

    if (keyPressed("space") || keyPressed("Space")) {
        resetTown();
        refreshHud();
        return;
    }

    updateGhostFromMouse();

    if (mousePressed(1))
        tryPlace();
    if (mousePressed(2))
        tryRemove();

    refreshHud();
};

eve_render = function() {
    gfx.clear();
    if (world == null) {
        ui.beginFrameAndRender();
        return;
    }

    // —— 地形 ——
    for (local y = 0; y < gridH; y += 1) {
        for (local x = 0; x < gridW; x += 1) {
            local px = mapOriginX + x * cellPx;
            local py = mapOriginY + y * cellPx;
            local sem = world.getTerrain(x, y);
            if (sem == TERRAIN_WATER)
                gfx.drawSolidRect(px, py, cellPx - 1.0, cellPx - 1.0, 0.18, 0.42, 0.62, 1.0);
            else
                gfx.drawSolidRect(px, py, cellPx - 1.0, cellPx - 1.0, 0.22, 0.38, 0.24, 1.0);
        }
    }

    // —— 已放置建筑 ——
    local col = [0.5, 0.5, 0.5];
    local n = world.getBuildingCount();
    for (local i = 0; i < n; i += 1) {
        local inst = world.getBuildingInstanceAt(i);
        local bid = world.getBuildingId(inst);
        local ox = world.getBuildingCellX(inst);
        local oy = world.getBuildingCellY(inst);
        local rot = world.getBuildingRotation(inst);
        local cells = [];
        footprintCells(bid, ox, oy, rot, cells);
        buildingColor(bid, col);
        foreach (c in cells) {
            local px = mapOriginX + c[0] * cellPx;
            local py = mapOriginY + c[1] * cellPx;
            gfx.drawSolidRect(px + 1.0, py + 1.0, cellPx - 3.0, cellPx - 3.0,
                              col[0], col[1], col[2], 1.0);
        }
    }

    // —— 鬼影 ——
    if (ghost != null) {
        local cells = [];
        footprintCells(ghost.getBuildingId(), ghost.getCellX(), ghost.getCellY(),
                       ghost.getRotationDeg(), cells);
        local ok = ghost.isValid() && canAfford(ghost.getBuildingId()) == "";
        local gr = ok ? 0.25 : 0.75;
        local gg = ok ? 0.75 : 0.22;
        local gb = ok ? 0.35 : 0.20;
        foreach (c in cells) {
            if (c[0] < 0 || c[1] < 0 || c[0] >= gridW || c[1] >= gridH) continue;
            local px = mapOriginX + c[0] * cellPx;
            local py = mapOriginY + c[1] * cellPx;
            gfx.drawSolidRect(px + 3.0, py + 3.0, cellPx - 7.0, cellPx - 7.0,
                              gr, gg, gb, 0.85);
        }
    }

    // —— 右侧调色板预览条 ——
    local px0 = mapOriginX + gridW * cellPx + 28.0;
    local py0 = mapOriginY;
    for (local i = 0; i < palette.len(); i += 1) {
        local bid = palette[i];
        buildingColor(bid, col);
        local y = py0 + i * 56.0;
        local selected = (i == paletteIndex);
        if (selected)
            gfx.drawSolidRect(px0 - 4.0, y - 4.0, 120.0, 48.0, 0.35, 0.40, 0.48, 1.0);
        else
            gfx.drawSolidRect(px0 - 4.0, y - 4.0, 120.0, 48.0, 0.16, 0.18, 0.22, 1.0);
        gfx.drawSolidRect(px0, y, 40.0, 40.0, col[0], col[1], col[2], 1.0);
    }

    ui.beginFrameAndRender();
};
