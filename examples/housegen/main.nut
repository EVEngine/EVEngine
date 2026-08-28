// HouseGen runtime example: generate a house from several different configurations
// (rectangle multi-room, L-shape, free-form polygon, tall block), print its layout +
// procgen persistence identity, and render it live. Press 1-4 to switch configs.
persist housegen = null
persist layout = null
persist cam = null
persist cubeMesh = null
persist boxes = []
persist boxIdx = 0
persist pal = {}
persist configs = null
persist current = 0

function selectPalette(seed) {
    local m = seed % 3;
    if (m == 0) {
        pal = { wallR=0.78, wallG=0.80, wallB=0.76, trimR=0.25, trimG=0.28, trimB=0.30,
                roofR=0.16, roofG=0.20, roofB=0.25, glassR=0.22, glassG=0.48, glassB=0.64 };
    } else if (m == 1) {
        pal = { wallR=0.76, wallG=0.58, wallB=0.38, trimR=0.82, trimG=0.64, trimB=0.42,
                roofR=0.42, roofG=0.16, roofB=0.13, glassR=0.18, glassG=0.55, glassB=0.72 };
    } else {
        pal = { wallR=0.64, wallG=0.70, wallB=0.78, trimR=0.92, trimG=0.90, trimB=0.82,
                roofR=0.20, roofG=0.28, roofB=0.36, glassR=0.16, glassG=0.52, glassB=0.70 };
    }
}

// Reuse a pool of Renderable3D entities across config switches (no destroy API).
function box(x, y, z, sx, sy, sz, r, g, b, roll) {
    local e;
    if (boxIdx < boxes.len()) { e = boxes[boxIdx]; e.setVisible(true); }
    else { e = eve.Renderable3D(); e.setMesh(cubeMesh); boxes.append(e); }
    boxIdx++;
    e.setMesh(cubeMesh);
    e.setPosition(x, y, z);
    e.setScale(sx, sy, sz);
    e.setTint(r, g, b, 1.0);
    if (roll != 0.0) e.setRotation(0.0, 0.0, roll);
}

function hideRemaining() {
    for (local i = boxIdx; i < boxes.len(); ++i) boxes[i].setVisible(false);
}

function addWallModule(id, x, y, z, rot, fh) {
    local outX = rot == 90 ? 1.0 : (rot == 270 ? -1.0 : 0.0);
    local outZ = rot == 180 ? 1.0 : (rot == 0 ? -1.0 : 0.0);
    local px = x.tofloat() + outX * 0.50;
    local py = z.tofloat() * fh;
    local pz = y.tofloat() + outZ * 0.50;
    local alongX = (rot == 0 || rot == 180);
    local sx = alongX ? 1.02 : 0.12;
    local sz = alongX ? 0.12 : 1.02;
    if (id == "wall.window") {
        box(px, py + 0.40, pz, sx, 0.80, sz, pal.wallR, pal.wallG, pal.wallB, 0.0);
        box(px, py + 2.08, pz, sx, 0.64, sz, pal.wallR, pal.wallG, pal.wallB, 0.0);
        local gx = px + outX * 0.015, gz = pz + outZ * 0.015;
        box(gx, py + 1.28, gz, alongX ? 0.72 : 0.07, 0.92, alongX ? 0.07 : 0.72,
            pal.glassR, pal.glassG, pal.glassB, 0.0);
        foreach (side in [-0.40, 0.40])
            box(gx + (alongX ? side : 0.0), py + 1.28, gz + (alongX ? 0.0 : side),
                alongX ? 0.07 : 0.08, 1.08, alongX ? 0.08 : 0.07, pal.trimR, pal.trimG, pal.trimB, 0.0);
        foreach (side in [0.76, 1.80])
            box(gx, py + side, gz, alongX ? 0.86 : 0.08, 0.07, alongX ? 0.08 : 0.86,
                pal.trimR, pal.trimG, pal.trimB, 0.0);
    } else {
        box(px, py + fh * 0.5, pz, sx, fh, sz, pal.wallR, pal.wallG, pal.wallB, 0.0);
    }
}

function addDoorModule(x, y, z, rot, fh) {
    local outX = rot == 90 ? 1.0 : (rot == 270 ? -1.0 : 0.0);
    local outZ = rot == 180 ? 1.0 : (rot == 0 ? -1.0 : 0.0);
    local px = x.tofloat() + outX * 0.50;
    local py = z.tofloat() * fh;
    local pz = y.tofloat() + outZ * 0.50;
    local alongX = (rot == 0 || rot == 180);
    local leafX = alongX ? 0.58 : 0.12, leafZ = alongX ? 0.12 : 0.58;
    box(px, py + 0.92, pz, leafX, 1.84, leafZ, 0.16, 0.24, 0.20, 0.0);
    foreach (side in [-0.39, 0.39])
        box(px + (alongX ? side : 0.0), py + fh * 0.5, pz + (alongX ? 0.0 : side),
            alongX ? 0.15 : 0.16, fh, alongX ? 0.16 : 0.15, pal.trimR, pal.trimG, pal.trimB, 0.0);
    box(px, py + 2.25, pz, alongX ? 0.64 : 0.16, 0.30, alongX ? 0.16 : 0.64,
        pal.trimR, pal.trimG, pal.trimB, 0.0);
    box(px + (alongX ? 0.20 : outX * 0.075), py + 0.95, pz + (alongX ? outZ * 0.075 : 0.20),
        0.055, 0.055, 0.055, 0.92, 0.72, 0.20, 0.0);
    local px2 = px + outX * 0.55, pz2 = pz + outZ * 0.55;
    box(px2, py + 0.04, pz2, alongX ? 1.55 : 0.80, 0.10, alongX ? 0.80 : 1.55,
        0.38, 0.28, 0.19, 0.0);
    box(px2, py + 2.48, pz2, alongX ? 1.75 : 0.92, 0.10, alongX ? 0.92 : 1.75,
        pal.roofR, pal.roofG, pal.roofB, 0.0);
    foreach (side in [-0.67, 0.67])
        box(px2 + (alongX ? side : 0.0), py + 1.24, pz2 + (alongX ? 0.0 : side),
            0.07, 2.42, 0.07, pal.trimR, pal.trimG, pal.trimB, 0.0);
}

function buildHouse(cfg) {
    boxIdx = 0;
    layout = housegen.newLayout();
    local request = housegen.newRequest();
    request.setSeed(cfg.seed);
    request.setPlot(cfg.w, cfg.d);
    request.setFloors(cfg.floors);
    request.setFootprint(cfg.footprint);
    request.setRoof(cfg.roof);
    request.setEntrance("auto");
    if (cfg.rooms != "") request.setRequiredRooms(cfg.rooms);
    if (cfg.perimeter != "") request.setPerimeter(cfg.perimeter);

    local result = housegen.generate(request, layout);
    if (!result.ok) {
        print("generate failed: " + result.status.summary + "\n");
        return;
    }
    print("[" + cfg.label + "] generated " + layout.getInstanceCount() + " components; roof=" +
          layout.getRoofStyle() + " footprint=" + layout.getFootprintStyle() +
          " rooms=" + layout.getRoomCount() + "\n");

    local fh = layout.getFloorHeight();
    local w = cfg.w, d = cfg.d;
    local cx = (w - 1) * 0.5, cz = (d - 1) * 0.5;
    selectPalette(cfg.seed);

    // Grass (always visible)
    box(cx, -0.30, cz, (w + 4).tofloat(), 0.25, (d + 4).tofloat(), 0.20, 0.42, 0.18, 0.0);

    // Structural modules
    for (local i = 0; i < layout.getInstanceCount(); ++i) {
        local id = layout.getInstanceComponentId(i);
        local x = layout.getInstanceX(i);
        local y = layout.getInstanceY(i);
        local z = layout.getInstanceZ(i);
        local rot = layout.getInstanceRotationDeg(i);
        local wx = x.tofloat(), wz = y.tofloat(), wy = z.tofloat() * fh;
        if (i < 3) print("i=" + i + " id=" + id + " x=" + x + " y=" + y + " z=" + z + " rot=" + rot + "\n");
        if (id == "foundation") {
            box(wx, -0.08, wz, 0.96, 0.16, 0.96, 0.35, 0.34, 0.33, 0.0);
        } else if (id == "floor") {
            box(wx, wy - 0.03, wz, 0.96, 0.06, 0.96, 0.48, 0.33, 0.22, 0.0);
        } else if (id == "interior_wall") {
            local alongX = (rot == 0 || rot == 180);
            box(wx, wy + fh * 0.5, wz, alongX ? 0.12 : 0.96, fh, alongX ? 0.96 : 0.12,
                pal.wallR * 0.75, pal.wallG * 0.75, pal.wallB * 0.75, 0.0);
        } else if (id == "interior_door") {
            box(wx, wy + fh * 0.35, wz, 0.55, fh * 0.7, 0.55, 0.40, 0.30, 0.24, 0.0);
        } else if (id == "wall" || id == "wall.window" || id == "wall.solid") {
            addWallModule(id, x, y, z, rot, fh);
        } else if (id == "door") {
            addDoorModule(x, y, z, rot, fh);
        } else if (id == "stairs") {
            box(wx, wy + fh * 0.5, wz, 0.70, fh, 0.70, 0.90, 0.82, 0.62, 0.0);
        }
    }

    // Roof
    local roofLevel = 0;
    for (local i = 0; i < layout.getInstanceCount(); ++i)
        if (layout.getInstanceComponentId(i) == "roof")
            roofLevel = layout.getInstanceZ(i) > roofLevel ? layout.getInstanceZ(i) : roofLevel;
    local roofMinX = w, roofMaxX = 0, roofMinZ = d, roofMaxZ = 0;
    local topRoof = {};
    for (local i = 0; i < layout.getInstanceCount(); ++i) {
        local id = layout.getInstanceComponentId(i);
        if (id != "roof") continue;
        local x = layout.getInstanceX(i), z = layout.getInstanceZ(i);
        if (z != roofLevel) continue;
        local y = layout.getInstanceY(i);
        if (x < roofMinX) roofMinX = x;
        if (x > roofMaxX) roofMaxX = x;
        if (y < roofMinZ) roofMinZ = y;
        if (y > roofMaxZ) roofMaxZ = y;
        topRoof[y * w + x] <- true;
    }
    local roofStyle = layout.getRoofStyle();
    local footprintStyle = layout.getFootprintStyle();
    local roofWidth = roofMaxX - roofMinX + 1;
    local roofDepth = roofMaxZ - roofMinZ + 1;
    local roofCenterX = (roofMinX + roofMaxX) * 0.5;
    local roofCenterZ = (roofMinZ + roofMaxZ) * 0.5;
    local eaveY = roofLevel.tofloat() * fh;

    if (roofStyle == "gable" && footprintStyle == "rectangle") {
        local halfRun = roofWidth * 0.5 + 0.30;
        local rise = roofWidth * 0.22;
        local angle = atan2(rise, halfRun);
        local panelLength = sqrt(halfRun * halfRun + rise * rise);
        local panelOffset = halfRun * 0.5;
        local roofY = eaveY + rise * 0.5;
        box(roofCenterX - panelOffset, roofY, roofCenterZ, panelLength, 0.18, roofDepth + 0.65,
            pal.roofR, pal.roofG, pal.roofB, angle);
        box(roofCenterX + panelOffset, roofY, roofCenterZ, panelLength, 0.18, roofDepth + 0.65,
            pal.roofR, pal.roofG, pal.roofB, -angle);
        foreach (ez in [roofMinZ - 0.50, roofMaxZ + 0.50])
            box(roofCenterX, eaveY - 0.02, ez.tofloat(), roofWidth.tofloat(), rise * 0.5, 0.12,
                pal.wallR, pal.wallG, pal.wallB, 0.0);
    } else {
        local shedAngle = roofStyle == "shed" ? -0.16 : 0.0;
        local roofY = eaveY + (roofStyle == "shed" ? roofWidth * 0.08 : 0.0);
        for (local i = 0; i < layout.getInstanceCount(); ++i) {
            local id = layout.getInstanceComponentId(i);
            if (id != "roof") continue;
            local x = layout.getInstanceX(i), y = layout.getInstanceY(i), z = layout.getInstanceZ(i);
            if (z != roofLevel) continue;
            local slopeY = roofStyle == "shed" ? (roofCenterX - x.tofloat()) * 0.16 : 0.0;
            box(x.tofloat(), roofY + slopeY, y.tofloat(), 1.08, roofStyle == "flat" ? 0.20 : 0.14, 1.08,
                pal.roofR, pal.roofG, pal.roofB, shedAngle);
        }
        local dx = [0, 1, 0, -1], dz = [-1, 0, 1, 0];
        for (local zz = roofMinZ; zz <= roofMaxZ; ++zz) {
            for (local xx = roofMinX; xx <= roofMaxX; ++xx) {
                if (!topRoof.rawin(zz * w + xx)) continue;
                local tileY = roofY + (roofStyle == "shed" ? (roofCenterX - xx.tofloat()) * 0.16 : 0.0);
                for (local s = 0; s < 4; ++s) {
                    if (topRoof.rawin((zz + dz[s]) * w + (xx + dx[s]))) continue;
                    local alongX = (s == 0 || s == 2);
                    local ppx = xx.tofloat() + dx[s].tofloat() * 0.50;
                    local ppz = zz.tofloat() + dz[s].tofloat() * 0.50;
                    if (roofStyle == "flat") {
                        box(ppx, eaveY + 0.20, ppz, alongX ? 1.02 : 0.10, 0.34, alongX ? 0.10 : 1.02,
                            pal.trimR, pal.trimG, pal.trimB, 0.0);
                    } else {
                        local fillHeight = (tileY - eaveY - 0.14) > 0.02 ? (tileY - eaveY - 0.14) : 0.02;
                        box(ppx, eaveY + fillHeight * 0.5 - 0.07, ppz, alongX ? 1.02 : 0.11,
                            fillHeight, alongX ? 0.11 : 1.02, pal.wallR, pal.wallG, pal.wallB, 0.0);
                        box(ppx, tileY - 0.10, ppz, alongX ? 1.06 : 0.13, 0.18, alongX ? 0.13 : 1.06,
                            pal.roofR, pal.roofG, pal.roofB, 0.0);
                    }
                }
            }
        }
    }
    hideRemaining();

    cam.setTarget(cx, roofLevel.tofloat() * fh * 0.5, cz);
    cam.setEye(cx + 7.5, roofLevel.tofloat() * fh + 3.0, cz - 11.0);
}

eve_init = function() {
    housegen = eve.HouseGen();
    local kit = @"[
      {""id"":""foundation"",""model"":""assets/foundation.glb"",""category"":""foundation""},
      {""id"":""floor"",""model"":""assets/floor.glb"",""category"":""floor""},
      {""id"":""wall"",""model"":""assets/wall.glb"",""category"":""wall"",""weight"":2},
      {""id"":""wall.window"",""model"":""assets/window.glb"",""category"":""wall"",""weight"":5,""tags"":[""window""]},
      {""id"":""door"",""model"":""assets/door.glb"",""category"":""door""},
      {""id"":""roof"",""model"":""assets/roof.glb"",""category"":""roof""},
      {""id"":""stairs"",""model"":""assets/stairs.glb"",""category"":""stairs""},
      {""id"":""iwall"",""model"":""assets/iwall.glb"",""category"":""interior_wall""},
      {""id"":""idoor"",""model"":""assets/idoor.glb"",""category"":""interior_door""}
    ]";
    local componentResult = housegen.loadComponentsFromJson(kit);
    if (!componentResult.ok) {
        print("house component error: " + componentResult.status.summary + "\n");
        return;
    }

    configs = [
        { label="1 矩形 多房间 gable", seed=20260815, w=8, d=7, floors=2, footprint="rectangle",
          roof="gable", rooms="living,kitchen,bedroom", perimeter="" },
        { label="2 L形 平屋顶", seed=101, w=8, d=7, floors=1, footprint="l_shape",
          roof="flat", rooms="", perimeter="" },
        { label="3 多边形 自由轮廓 2层 坡顶", seed=31337, w=9, d=8, floors=2, footprint="polygon",
          roof="shed", rooms="living,kitchen", perimeter="0,0;5,0;5,3;9,3;9,8;0,8" },
        { label="4 矩形 3层 平顶", seed=7, w=7, d=6, floors=3, footprint="rectangle",
          roof="flat", rooms="living,bedroom", perimeter="" }
    ];

    cubeMesh = gfx.newMeshCube(1.0);
    gfx.setBackgroundColor(0.58, 0.75, 0.90, 1.0);
    gfx.setDirectionalLight(-0.7, -1.0, -0.45, 1.2, 1.1, 1.0);

    cam = eve.Camera3D();
    cam.setUp(0.0, 1.0, 0.0);
    cam.setFov(45.0);
    cam.setAmbient(0.34, 0.34, 0.38);
    cam.setActive(true);

    buildHouse(configs[0]);
    print("press 1-4 to switch house configurations\n");
};

eve_update = function(dt) {
    local keys = ["1", "2", "3", "4"];
    for (local n = 0; n < configs.len(); ++n) {
        if (key_just_pressed(keys[n])) {
            current = n;
            buildHouse(configs[n]);
        }
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};