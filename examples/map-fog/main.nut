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
persist fogAlpha = 0.76
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

function softWeight(dist, radius) {
    // Chunky cotton disc: large hard core, short falloff (reference FoW lobes).
    if (dist >= radius) return 0.0;
    local core = radius * 0.72;
    if (dist <= core) return 1.0;
    local t = (radius - dist) / (radius - core);
    return t * t * (3.0 - 2.0 * t);
}

function stampSoftAt(px, py, r, g, b, radius) {
    // Local brush in mask-pixel space (fast).
    if (radius < 2.0) radius = 2.0;
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
            local w = softWeight(sqrt(dx * dx + dy * dy), radius);
            if (w <= 0.0) continue;
            local pr = maskPixels.getPixelR(xx, yy);
            local pg = maskPixels.getPixelG(xx, yy);
            local pb = maskPixels.getPixelB(xx, yy);
            local nr = r * w;
            local ng = g * w;
            local nb = b * w;
            if (nr > pr) pr = nr;
            if (ng > pg) pg = ng;
            if (nb > pb) pb = nb;
            maskPixels.setPixel(xx, yy, pr, pg, pb, 1.0);
        }
    }
}

function stampSoft(gx, gy, r, g, b, radiusScale) {
    local cellPx = maskW.tofloat() / gridW.tofloat();
    local px = (gx.tofloat() + 0.5) * cellPx;
    local py = (gy.tofloat() + 0.5) * (maskH.tofloat() / gridH.tofloat());
    stampSoftAt(px, py, r, g, b, radiusScale * cellPx);
}

function hasFoggedNeighbor(gx, gy) {
    for (local dy = -1; dy <= 1; ++dy) {
        for (local dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            local nx = gx + dx;
            local ny = gy + dy;
            if (nx < 0 || ny < 0 || nx >= gridW || ny >= gridH) return true;
            if (!unlocked[cellIndex(nx, ny)]) return true;
        }
    }
    return false;
}

function stampRimPuffs(gx, gy, r, g, b) {
    // Satellite discs along fog frontier → bubbly peninsulas / islands (reference rim).
    if (!hasFoggedNeighbor(gx, gy)) return;
    local cellPx = maskW.tofloat() / gridW.tofloat();
    local cellPy = maskH.tofloat() / gridH.tofloat();
    local cx = (gx.tofloat() + 0.5) * cellPx;
    local cy = (gy.tofloat() + 0.5) * cellPy;
    local h = ((gx * 13 + gy * 7) % 5).tofloat() / 5.0;
    local offsets = [
        [0.55, -0.35, 0.70],
        [-0.40, 0.50, 0.62],
        [0.30, 0.55, 0.55],
        [-0.55, -0.25, 0.48]
    ];
    for (local i = 0; i < offsets.len(); ++i) {
        local o = offsets[i];
        // Skip some satellites for irregularity.
        if (((gx * 3 + gy * 5 + i * 7) % 4) == 0) continue;
        local s = (0.85 + 0.30 * h) * o[2];
        stampSoftAt(cx + o[0] * cellPx, cy + o[1] * cellPy, r, g, b, s * cellPx);
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
                stampSoft(x, y, 0.0, sel, dissolving[idx], 1.75);
                stampRimPuffs(x, y, 0.0, sel, dissolving[idx]);
            } else if (unlocked[idx]) {
                stampSoft(x, y, 1.0, sel, 0.0, 1.75);
                stampRimPuffs(x, y, 1.0, sel, 0.0);
            } else if (sel > 0.0) {
                stampSoft(x, y, 0.0, sel, 0.0, 0.90);
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
    // Slightly richer underlay so the unlock hole reads against real map tones.
    local h = ((x * 13 + y * 7) % 5).tofloat() / 5.0;
    local road = ((x + y * 3) % 17 == 0) || ((x * 2 + y) % 19 == 0);
    if (road) return [0.62, 0.58, 0.42];
    if ((x + y) % 7 == 0) return [0.42, 0.72, 0.38];
    if ((x * 3 + y) % 11 == 0) return [0.78, 0.68, 0.34];
    return [0.38 + h * 0.22, 0.58 + h * 0.20, 0.30 + h * 0.12];
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
    fog.setCloudTexture(fog.makeCloudTexture(512));
    fog.setMaskTexture(maskTex);
    // Large cotton puffs + dual reverse scroll (reference FoW billows).
    fog.setCloudTiling(0.48, 0.82);
    fog.setCloudSpeed(0.007, 0.013);
    fog.setCloudMix(0.28);
    fog.setDistort(0.26);
    fog.setDistortFix(-0.014, 0.010);
    fog.setFogColor(0.96, 0.97, 1.00);
    fog.setFogAlpha(fogAlpha);
    // Bubbly mask stamps + puff UV warp => lobed unlock rim + hole shadow.
    fog.setEdgeSoftness(0.11);
    fog.setShadow(0.042, 0.058, 0.88);
    fog.setSelectStrength(0.90);
    fog.setDissolveScale(1.5);
    // Partial coverage: valleys translucent, rim density falls off.
    fog.setCloudDensity(0.48, 0.32);
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
