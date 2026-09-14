// Surface picking against the terrain heightmap.
//
// The editor needs a world point for "place here" without a physics world, so
// the mouse ray is marched against the height field and refined by bisection.
// Height sampling is the single terrain authority (level_terrain.nut), so this
// file never reads heightmap samples directly beyond that API.

function terrainPickDistance() { return 0.35; }

/** @brief World-space point where a ray first meets the terrain, or null. */
function terrainRayPick(originX, originY, originZ, dirX, dirY, dirZ, maxDistance) {
    local terrain = level.terrain;
    if (terrain == null) return null;
    local length = sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (length < 0.00001) return null;
    dirX /= length; dirY /= length; dirZ /= length;

    local extent = terrainWorldExtent(terrain);
    local step = terrainPickDistance();
    local previousT = 0.0;
    local previousDelta = null;
    local t = 0.0;
    while (t <= maxDistance) {
        local x = originX + dirX * t;
        local y = originY + dirY * t;
        local z = originZ + dirZ * t;
        local inside = x >= extent.minX && x <= extent.maxX && z >= extent.minZ && z <= extent.maxZ;
        if (inside) {
            local surface = terrainHeightAt(x, z);
            local delta = y - surface;
            if (previousDelta != null && previousDelta > 0.0 && delta <= 0.0) {
                // Bisect the crossing so placement lands on the visible surface.
                local low = previousT, high = t;
                for (local i = 0; i < 12; ++i) {
                    local mid = (low + high) * 0.5;
                    local mx = originX + dirX * mid;
                    local my = originY + dirY * mid;
                    local mz = originZ + dirZ * mid;
                    if (my - terrainHeightAt(mx, mz) > 0.0) low = mid; else high = mid;
                }
                local hit = (low + high) * 0.5;
                return {x = originX + dirX * hit, y = originY + dirY * hit, z = originZ + dirZ * hit};
            }
            previousDelta = delta;
        } else {
            previousDelta = null;
        }
        previousT = t;
        t += step;
        // Widen the step as the ray travels so a level-sized terrain stays cheap.
        step = step < 4.0 ? step * 1.06 : 4.0;
    }
    return null;
}

/**
 * @brief Viewport-local mouse / canvas size for ray picks, or null when the
 * pointer is not over the center viewport.
 */
function levelViewportPointer() {
    ui.select("viewport");
    if (!ui.viewportHovered("level-vp")) return null;
    local canvas = ui.viewportCanvas("level-vp");
    if (canvas == null) return null;
    return {
        x = ui.viewportMouseX("level-vp"),
        y = ui.viewportMouseY("level-vp"),
        w = canvas.getWidth().tofloat(),
        h = canvas.getHeight().tofloat()
    };
}

/** @brief Terrain point under the current viewport mouse, or null. */
function terrainPickAtMouse() {
    if (level.camera == null) return null;
    local pointer = levelViewportPointer();
    if (pointer == null) return null;
    level.camera.screenToRay(pointer.x, pointer.y, pointer.w, pointer.h);
    return terrainRayPick(level.camera.getScreenRayOriginX(), level.camera.getScreenRayOriginY(),
        level.camera.getScreenRayOriginZ(), level.camera.getScreenRayDirX(),
        level.camera.getScreenRayDirY(), level.camera.getScreenRayDirZ(), 6000.0);
}

/** @brief Drop a point onto the terrain surface (clamped into the extent). */
function terrainDropToSurface(x, y, z) {
    return {x = x, y = terrainSnapHeight(x, z), z = z};
}
