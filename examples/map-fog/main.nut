// SLG large-map war fog (MapFog): dual scrolling clouds + RGBA mask bridge.
// Mask channels follow the common FoW presentation pattern:
//   R = unlocked (1 clear / 0 fogged)
//   G = selected cell blink
//   B = dissolve threshold while unlocking
//
// Controls:
//   LMB  select a fogged cell
//   Space unlock the selected cell (dissolve animation)
//   R    reset fog
//   [ / ] fog opacity

persist fog = null
persist maskPixels = null
persist maskTex = null
persist gridW = 24
persist gridH = 16
persist unlocked = null
persist selectedX = -1
persist selectedY = -1
persist dissolving = null
persist mapX = 40.0
persist mapY = 60.0
persist mapW = 0.0
persist mapH = 0.0
persist cellW = 0.0
persist cellH = 0.0
persist fogAlpha = 0.92
persist prevLeft = false
persist status = "LMB select · Space unlock · R reset"

function cellIndex(x, y) {
    return y * gridW + x;
}

function ensureGrid() {
    if (unlocked != null) return;
    unlocked = [];
    dissolving = [];
    for (local i = 0; i < gridW * gridH; ++i) {
        unlocked.push(false);
        dissolving.push(-1.0);
    }
    // Seed a revealed starting pocket so the map is readable.
    for (local y = 6; y <= 9; ++y) {
        for (local x = 10; x <= 13; ++x) {
            unlocked[cellIndex(x, y)] = true;
        }
    }
}

function rebuildMask() {
    ensureGrid();
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local idx = cellIndex(x, y);
            local r = unlocked[idx] ? 1.0 : 0.0;
            local g = (x == selectedX && y == selectedY && !unlocked[idx]) ? 1.0 : 0.0;
            local b = 0.0;
            if (dissolving[idx] >= 0.0) {
                b = dissolving[idx];
                // Keep R unlocked in data once dissolve starts (article: data first).
                r = 1.0;
            }
            maskPixels.setPixel(x, y, r, g, b, 1.0);
        }
    }
    gfx.updateTextureFromImageData(maskTex, maskPixels);
}

function screenToCell(mx, my) {
    if (mx < mapX || my < mapY || mx >= mapX + mapW || my >= mapY + mapH) {
        return null;
    }
    local cx = ((mx - mapX) / cellW).tointeger();
    local cy = ((my - mapY) / cellH).tointeger();
    if (cx < 0 || cy < 0 || cx >= gridW || cy >= gridH) return null;
    return [cx, cy];
}

function terrainColor(x, y) {
    local h = ((x * 13 + y * 7) % 5).tofloat() / 5.0;
    if ((x + y) % 7 == 0) return [0.22, 0.38, 0.22];
    if ((x * 3 + y) % 11 == 0) return [0.35, 0.32, 0.20];
    return [0.18 + h * 0.08, 0.28 + h * 0.10, 0.16 + h * 0.05];
}

eve_init = function() {
    gfx.setBackgroundColor(0.06, 0.08, 0.11, 1.0);
    mapW = (config.width - 80).tofloat();
    mapH = (config.height - 120).tofloat();
    cellW = mapW / gridW.tofloat();
    cellH = mapH / gridH.tofloat();

    ensureGrid();
    local imageModule = eve.Image();
    maskPixels = imageModule.newEmptyImageData(gridW, gridH, "RGBA8");
    maskTex = gfx.newTexture(maskPixels, false, false);

    fog = gfx.newMapFog();
    fog.setCloudTexture(fog.makeCloudTexture(160));
    fog.setMaskTexture(maskTex);
    fog.setCloudTiling(2.6, 4.1);
    fog.setCloudSpeed(0.03, 0.05);
    fog.setDistort(0.04);
    fog.setDistortFix(-0.008, 0.004);
    fog.setFogColor(0.70, 0.76, 0.86);
    fog.setFogAlpha(fogAlpha);
    fog.setEdgeSoftness(0.14);
    fog.setShadow(0.014, 0.02, 0.5);
    fog.setSelectStrength(0.95);
    fog.setDissolveScale(3.2);
    rebuildMask();
    print("Map fog: LMB select, Space unlock, R reset, [/] opacity\n");
};

eve_update = function(dt) {
    if (fog == null) return;
    fog.update(dt);

    local left = mouse.isDown(1);
    if (left && !prevLeft) {
        local cell = screenToCell(mouse.getX().tofloat(), mouse.getY().tofloat());
        if (cell != null) {
            selectedX = cell[0];
            selectedY = cell[1];
            status = "selected (" + selectedX + "," + selectedY + ")";
            rebuildMask();
        }
    }
    prevLeft = left;

    if (key_just_pressed("space") || key_just_pressed("Space")) {
        if (selectedX >= 0 && selectedY >= 0) {
            local idx = cellIndex(selectedX, selectedY);
            if (!unlocked[idx] && dissolving[idx] < 0.0) {
                // Data unlocks immediately; dissolve is presentation-only.
                unlocked[idx] = true;
                dissolving[idx] = 0.0;
                status = "unlocking (" + selectedX + "," + selectedY + ")";
                rebuildMask();
            }
        }
    }

    if (key_just_pressed("r") || key_just_pressed("R")) {
        unlocked = null;
        selectedX = -1;
        selectedY = -1;
        ensureGrid();
        status = "fog reset";
        rebuildMask();
    }

    if (key_just_pressed("leftbracket") || key_just_pressed("[")) {
        fogAlpha = fogAlpha - 0.08;
        if (fogAlpha < 0.2) fogAlpha = 0.2;
        fog.setFogAlpha(fogAlpha);
    }
    if (key_just_pressed("rightbracket") || key_just_pressed("]")) {
        fogAlpha = fogAlpha + 0.08;
        if (fogAlpha > 1.0) fogAlpha = 1.0;
        fog.setFogAlpha(fogAlpha);
    }

    local dirty = false;
    for (local i = 0; i < dissolving.len(); ++i) {
        if (dissolving[i] < 0.0) continue;
        dissolving[i] = dissolving[i] + dt * 0.85;
        if (dissolving[i] >= 1.0) dissolving[i] = -1.0;
        dirty = true;
    }
    if (dirty) rebuildMask();
};

eve_render = function() {
    gfx.clear();

    // Base strategic map (simple terrain tiles under the fog).
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local c = terrainColor(x, y);
            gfx.drawSolidRect(mapX + x.tofloat() * cellW, mapY + y.tofloat() * cellH,
                              cellW - 1.0, cellH - 1.0, c[0], c[1], c[2], 1.0);
        }
    }

    fog.draw(mapX, mapY, mapW, mapH);
};
