// SLG large-map war fog (MapFog): dual scrolling soft clouds + RGBA mask bridge.
// Mask channels follow the common FoW presentation pattern:
//   R = unlocked (1 clear / 0 fogged) — presentation coverage
//   G = selected cell blink
//   B = dissolve threshold while unlocking
// Gameplay data unlocks immediately; the visual mask keeps R fogged until
// dissolve finishes (so B can actually burn the cloud away).
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
persist maskW = 192
persist maskH = 128
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
persist fogAlpha = 0.78
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
    // Starting cleared patch so the dual-cloud sheet + soft hole are visible.
    for (local y = 6; y <= 9; ++y) {
        for (local x = 10; x <= 13; ++x) {
            unlocked[cellIndex(x, y)] = true;
        }
    }
}

function clearMask() {
    for (local y = 0; y < maskH; ++y) {
        for (local x = 0; x < maskW; ++x) {
            maskPixels.setPixel(x, y, 0.0, 0.0, 0.0, 1.0);
        }
    }
}

function maxChannel(x, y, r, g, b) {
    local pr = maskPixels.getPixelR(x, y);
    local pg = maskPixels.getPixelG(x, y);
    local pb = maskPixels.getPixelB(x, y);
    if (r > pr) pr = r;
    if (g > pg) pg = g;
    if (b > pb) pb = b;
    maskPixels.setPixel(x, y, pr, pg, pb, 1.0);
}

function stampSoft(gx, gy, r, g, b, radiusScale) {
    // Stamp a soft disc in mask-pixel space around a grid cell (organic FoW edge).
    local px = (gx.tofloat() + 0.5) * maskW.tofloat() / gridW.tofloat();
    local py = (gy.tofloat() + 0.5) * maskH.tofloat() / gridH.tofloat();
    local radius = radiusScale * (maskW.tofloat() / gridW.tofloat());
    if (radius < 2.0) radius = 2.0;
    local r2 = radius * radius;
    local x0 = (px - radius).tointeger();
    local y0 = (py - radius).tointeger();
    local x1 = (px + radius).tointeger() + 1;
    local y1 = (py + radius).tointeger() + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > maskW) x1 = maskW;
    if (y1 > maskH) y1 = maskH;
    for (local yy = y0; yy < y1; ++yy) {
        for (local xx = x0; xx < x1; ++xx) {
            local dx = (xx.tofloat() + 0.5) - px;
            local dy = (yy.tofloat() + 0.5) - py;
            local d2 = dx * dx + dy * dy;
            if (d2 > r2) continue;
            local t = 1.0 - d2 / r2;
            // Smooth falloff so edges are cloudy, not hard squares.
            local w = t * t * (3.0 - 2.0 * t);
            maxChannel(xx, yy, r * w, g * w, b * w);
        }
    }
}

function rebuildMask() {
    ensureGrid();
    clearMask();
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local idx = cellIndex(x, y);
            local sel = (x == selectedX && y == selectedY) ? 1.0 : 0.0;
            if (dissolving[idx] >= 0.0) {
                // Stay fogged visually (R=0) while B dissolves the cloud.
                stampSoft(x, y, 0.0, sel, dissolving[idx], 1.15);
            } else if (unlocked[idx]) {
                stampSoft(x, y, 1.0, sel, 0.0, 1.15);
            } else if (sel > 0.0) {
                stampSoft(x, y, 0.0, sel, 0.0, 0.85);
            }
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
    if ((x + y) % 7 == 0) return [0.55, 0.82, 0.42];
    if ((x * 3 + y) % 11 == 0) return [0.82, 0.70, 0.38];
    return [0.50 + h * 0.18, 0.68 + h * 0.16, 0.36 + h * 0.10];
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.07, 0.10, 1.0);
    mapW = (config.width - 80).tofloat();
    mapH = (config.height - 120).tofloat();
    cellW = mapW / gridW.tofloat();
    cellH = mapH / gridH.tofloat();

    ensureGrid();
    local imageModule = eve.Image();
    maskPixels = imageModule.newEmptyImageData(maskW, maskH, "RGBA8");
    maskTex = gfx.newTexture(maskPixels, false, false);
    gfx.setTextureSampler(maskTex, "linear", "none", 1.0, 0.0);

    fog = gfx.newMapFog();
    fog.setCloudTexture(fog.makeCloudTexture(256));
    fog.setMaskTexture(maskTex);
    // Modest tiling + aspect-corrected UVs => large soft billows, not wallpaper.
    fog.setCloudTiling(1.0, 1.35);
    fog.setCloudSpeed(0.012, 0.018);
    fog.setCloudMix(0.45);
    fog.setDistort(0.07);
    fog.setDistortFix(-0.006, 0.004);
    fog.setFogColor(0.82, 0.86, 0.92);
    fog.setFogAlpha(fogAlpha);
    fog.setEdgeSoftness(0.30);
    fog.setShadow(0.014, 0.020, 0.32);
    fog.setSelectStrength(0.90);
    fog.setDissolveScale(1.8);
    // Mild density shaping — sheet stays readable; mask cuts the hole.
    fog.setCloudDensity(0.55, 0.18);
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
        dissolving[i] = dissolving[i] + dt * 0.65;
        if (dissolving[i] >= 1.0) dissolving[i] = -1.0;
        dirty = true;
    }
    if (dirty) rebuildMask();
};

eve_render = function() {
    gfx.clear();

    gfx.drawSolidRect(mapX, mapY, mapW, mapH, 0.18, 0.28, 0.16, 1.0);
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local c = terrainColor(x, y);
            gfx.drawSolidRect(mapX + x.tofloat() * cellW, mapY + y.tofloat() * cellH,
                              cellW, cellH, c[0], c[1], c[2], 1.0);
        }
    }

    fog.draw(mapX, mapY, mapW, mapH);
};
