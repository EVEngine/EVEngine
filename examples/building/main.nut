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
persist sidebarX = 724.0
persist sidebarWidth = 532.0
persist hiddenGhostCellX = -999
persist hiddenGhostCellY = -999

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
    hiddenGhostCellX = -999;
    hiddenGhostCellY = -999;
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
    hiddenGhostCellX = -999;
    hiddenGhostCellY = -999;
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
    if (ghost.getCellX() != hiddenGhostCellX || ghost.getCellY() != hiddenGhostCellY) {
        hiddenGhostCellX = -999;
        hiddenGhostCellY = -999;
    }
}

function mouseOnMap() {
    local mx = mouse.getX();
    local my = mouse.getY();
    return mx >= mapOriginX && my >= mapOriginY &&
           mx < mapOriginX + gridW * cellPx &&
           my < mapOriginY + gridH * cellPx;
}

function tryPlace() {
    updateGhostFromMouse();
    tryPlaceGhost();
}

function tryPlaceGhost() {
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
    // Keep the freshly placed cells visible. Otherwise the now-invalid overlap
    // ghost sits on top of the building until the pointer moves and makes a
    // successful click look as if nothing changed.
    hiddenGhostCellX = ghost.getCellX();
    hiddenGhostCellY = ghost.getCellY();
    pushLog("已建造 " + bld.getBuildingDisplayName(id) + " #" + inst +
            " @(" + ghost.getCellX() + "," + ghost.getCellY() + ")");
}

function tryPlaceCell(cx, cy) {
    ghost.setFromWorld(world, mapOriginX + (cx + 0.5) * cellPx,
                       mapOriginY + (cy + 0.5) * cellPx);
    ghost.validate(world);
    tryPlaceGhost();
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
    ui.select("hud");
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
        "快捷键：1-5 选择 · R 旋转 · 左键建造 · 右键拆除 · Space 重置");
    local logText = "";
    foreach (line in logLines)
        logText += line + "\n";
    ui.setText("log", logText);

}

function mountMapCells(remount) {
    ui.beginBuild();
    ui.beginWindow("", "map-input-root");
    for (local y = 0; y < gridH; y += 1) {
        for (local x = 0; x < gridW; x += 1) {
            ui.imageButton("cell-" + x + "-" + y, cellPx, cellPx);
            ui.setItemAbsolute(0.0, 0.0, x * cellPx, y * cellPx);
        }
    }
    ui.end();
    if (remount) ui.remountBuildAs("map-input");
    else ui.mountBuildAs("map-input");
    ui.select("map-input");
    ui.setHostOverlay(true);
    ui.setHostOverlayAlpha(0.0);
    ui.setHostMovable(false);
    ui.setHostResizable(false);
    ui.setHostPos(mapOriginX, mapOriginY, 0.0, 0.0);
    ui.setHostSize(gridW * cellPx, gridH * cellPx);
    for (local y = 0; y < gridH; y += 1)
        for (local x = 0; x < gridW; x += 1)
            ui.setImageTint("cell-" + x + "-" + y, 1.0, 1.0, 1.0, 0.001);
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
        ui.beginWindow("建造面板", "root");
        ui.textWrapped("", sidebarWidth - 44.0, "stats");
        ui.separator("palette-separator");
        ui.text("选择建筑", "palette-title");
        ui.button("1  石板路", "select-road");
        ui.button("2  木屋", "select-house");
        ui.button("3  摊位（需邻路）", "select-stall");
        ui.button("4  码头（仅水域）", "select-dock");
        ui.button("5  L 形仓", "select-barn");
        ui.separator("action-separator");
        ui.beginRow("actions", 10.0);
        ui.button("旋转 90°", "rotate");
        ui.button("重置城镇", "reset");
        ui.end();
        ui.textWrapped("", sidebarWidth - 44.0, "help");
        ui.separator("log-separator");
        ui.text("操作记录", "log-title");
        ui.text("", "log");
        ui.end();
        ui.mountBuildAs("hud");
        ui.select("hud");
        ui.setHostOverlay(false);
        ui.setHostPos(sidebarX, 20.0, 0.0, 0.0);
        ui.setHostSize(sidebarWidth, 756.0);
        mountMapCells(false);
        uiBuilt = true;
    }
    refreshHud();
};

eve_reload <- function() {
    registerBuildings();
    mountMapCells(true);
};

eve_update = function(dt) {
    if (world == null || ghost == null) return;

    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "hud/select-road") selectPalette(0);
        else if (clicked == "hud/select-house") selectPalette(1);
        else if (clicked == "hud/select-stall") selectPalette(2);
        else if (clicked == "hud/select-dock") selectPalette(3);
        else if (clicked == "hud/select-barn") selectPalette(4);
        else if (clicked == "hud/rotate") {
            ghost.rotateBy(90.0);
            hiddenGhostCellX = -999;
            hiddenGhostCellY = -999;
            pushLog("旋转 → " + ghost.getRotationDeg().tointeger() + "°");
        } else if (clicked == "hud/reset") {
            resetTown();
        } else {
            for (local y = 0; y < gridH; y += 1)
                for (local x = 0; x < gridW; x += 1)
                    if (clicked == "map-input/cell-" + x + "-" + y)
                        tryPlaceCell(x, y);
        }
        clicked = ui.consumeClick();
    }

    if (keyPressed("1")) selectPalette(0);
    if (keyPressed("2")) selectPalette(1);
    if (keyPressed("3")) selectPalette(2);
    if (keyPressed("4")) selectPalette(3);
    if (keyPressed("5")) selectPalette(4);

    if (keyPressed("r") || keyPressed("R")) {
        ghost.rotateBy(90.0);
        hiddenGhostCellX = -999;
        hiddenGhostCellY = -999;
        pushLog("旋转 → " + ghost.getRotationDeg().tointeger() + "°");
    }

    if (keyPressed("space") || keyPressed("Space")) {
        resetTown();
        refreshHud();
        return;
    }

    updateGhostFromMouse();

    // The map-input host is an overlay for semantic automation and therefore
    // intentionally does not receive native pointer input. Keep the actual
    // player interaction on the mouse path, bounded to the map so sidebar
    // clicks cannot place buildings behind the panel.
    if (mousePressed(1) && mouseOnMap())
        tryPlace();
    if (mousePressed(2) && mouseOnMap())
        tryRemove();

    refreshHud();
};

eve_render = function() {
    gfx.clear();
    if (world == null) {
        ui.beginFrameAndRender();
        return;
    }

    // —— 地形、建筑与放置鬼影 ——
    // Render all grid visuals in one pass. The current 2D backend can coalesce
    // separated solid batches, so a later ghost-only pass is not reliable.
    local col = [0.5, 0.5, 0.5];
    local ghostCells = [];
    local drawGhost = ghost != null &&
        (ghost.getCellX() != hiddenGhostCellX || ghost.getCellY() != hiddenGhostCellY);
    local ghostOk = false;
    if (drawGhost) {
        footprintCells(ghost.getBuildingId(), ghost.getCellX(), ghost.getCellY(),
                       ghost.getRotationDeg(), ghostCells);
        ghostOk = ghost.isValid() && canAfford(ghost.getBuildingId()) == "";
    }
    for (local y = 0; y < gridH; y += 1) {
        for (local x = 0; x < gridW; x += 1) {
            local px = mapOriginX + x * cellPx;
            local py = mapOriginY + y * cellPx;
            local isGhostCell = false;
            if (drawGhost) {
                foreach (c in ghostCells) {
                    if (c[0] == x && c[1] == y) {
                        isGhostCell = true;
                        break;
                    }
                }
            }
            local occ = world.getOccupant(x, y);
            if (isGhostCell) {
                gfx.drawSolidRect(px, py, cellPx - 1.0, cellPx - 1.0,
                                  ghostOk ? 0.25 : 0.78,
                                  ghostOk ? 0.75 : 0.20,
                                  ghostOk ? 0.35 : 0.18, 1.0);
            } else if (occ > 0) {
                buildingColor(world.getBuildingId(occ), col);
                gfx.drawSolidRect(px, py, cellPx - 1.0, cellPx - 1.0,
                                  col[0], col[1], col[2], 1.0);
            } else if (world.getTerrain(x, y) == TERRAIN_WATER) {
                gfx.drawSolidRect(px, py, cellPx - 1.0, cellPx - 1.0, 0.18, 0.42, 0.62, 1.0);
            } else {
                gfx.drawSolidRect(px, py, cellPx - 1.0, cellPx - 1.0, 0.22, 0.38, 0.24, 1.0);
            }
        }
    }

    ui.beginFrameAndRender();
};
