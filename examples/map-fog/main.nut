// SLG large-map war fog (MapFog): dual scrolling soft clouds + RGBA mask bridge.
// Mask channels follow the common FoW presentation pattern:
//   R = unlocked (1 clear / 0 fogged) — presentation coverage
//   G = selected cell blink
//   B = dissolve threshold while unlocking
// Gameplay data unlocks immediately; presentation R advances with an eased
// reveal while B contracts and fades whole cloud clusters.
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
persist fogAlpha = 0.96
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
    // Start with a discovered western region so the important silhouette is a
    // long, irregular exploration front (matching an actual strategy map),
    // rather than a synthetic rounded rectangle punched through the fog.
    for (local y = 0; y < gridH; ++y) {
        local frontierX = 8 + ((y * 7 + 3) % 3);
        if (y == 3 || y == 4 || y == 11) frontierX++;
        for (local x = 0; x <= frontierX; ++x) {
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
    // Firm core (clearCore stays solid) + wide soft penumbra for gradual rim.
    if (dist >= radius) return 0.0;
    local core = radius * 0.50;
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

function rebuildMask() {
    ensureGrid();
    clearMask();
    for (local y = 0; y < gridH; ++y) {
        for (local x = 0; x < gridW; ++x) {
            local idx = cellIndex(x, y);
            local sel = (x == selectedX && y == selectedY) ? 1.0 : 0.0;
            if (dissolving[idx] >= 0.0) {
                // Advance reveal and dissolve together. Keeping R at zero until
                // the last frame leaves an opaque sheet that vanishes abruptly.
                local t = dissolving[idx];
                local reveal = t * t * (3.0 - 2.0 * t);
                stampSoft(x, y, reveal, sel, t, 2.15);
            } else if (unlocked[idx]) {
                // Soft unlock disc — solid core clears the hole; soft ring
                // feeds the shader frontier (dense → clumps → islands).
                stampSoft(x, y, 1.0, sel, 0.0, 2.15);
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

eve_init = function() {
    // Neutral stand-in only: MapFog remains an overlay and does not own the
    // revealed scene. Avoid a black clear color that reads as a second mask.
    gfx.setBackgroundColor(0.55, 0.60, 0.66, 1.0);
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
    // More than one repeat is essential: sub-1.0 tiling magnifies one texture
    // quadrant into the broad defocused blobs seen in the old result.
    fog.setCloudTiling(1.10, 1.65);
    fog.setCloudSpeed(0.010, 0.016);
    fog.setCloudMix(0.30);
    fog.setDistort(0.085);
    fog.setDistortFix(-0.006, 0.004);
    fog.setFogColor(0.98, 0.99, 1.00);
    fog.setFogAlpha(fogAlpha);
    // Soft approach for gradual rim sparseness; clearCore keeps hole clean.
    fog.setEdgeSoftness(0.16);
    // The reference shapes volume inside each cloud; an offset ground shadow
    // creates a second clipped silhouette and makes intact puffs look bitten.
    fog.setShadowEnabled(false);
    fog.setSelectStrength(0.90);
    fog.setDissolveScale(1.5);
    // Dense deep sheet; frontier raises cover gate → sparse islands at rim.
    fog.setCloudDensity(0.24, 0.10);
    // Wider soft frontier so density thins gradually into islands.
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
        dissolving[i] = dissolving[i] + dt * 0.32;
        if (dissolving[i] >= 1.0) dissolving[i] = -1.0;
        dirty = true;
    }
    if (dirty) rebuildMask();
};

eve_render = function() {
    gfx.clear();
    // MapFog is overlay-only.  A real game draws its map/scene before this
    // call; the example deliberately does not invent a green "revealed" layer.
    fog.draw(mapX, mapY, mapW, mapH);
};
