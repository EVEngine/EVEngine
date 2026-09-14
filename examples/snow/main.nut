// Close-up snow: dense simulation grid, adaptive render mesh.
// LMB boot; RMB impact; W walk; S recovery; R reset; O orbit; P parallax.
persist sf = null
persist terrainHm = null
persist combinedHm = null
persist terrainMesh = null
persist terrainEnt = null
persist terrainTexA = null
persist terrainTexH = null
persist walker = null
persist sun = null
persist cam = null
persist camAngle = 0.82
persist orbitOn = false
persist walkDemo = false
persist snowfallOn = false
persist parallaxOn = false
persist walkAngle = 0.0
persist stepTimer = 0.0
persist stepNumber = 0
persist leftWasDown = false
persist rightWasDown = false
const W = 257;
const H = 257;
const CELL = 0.0125;
const HSCALE = 1.0;
const SNOW_SCALE = 0.28;
const POM_SCALE = 0.0003;
function snowMin(a, b) { return a < b ? a : b; }
function snowMax(a, b) { return a > b ? a : b; }
dofile("adaptive_mesh.nut");

function snowSmooth(a, b, v) {
    local t = clampf((v - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
function surfaceHeight(wx, wz) {
    local x = clampf(wx / CELL, 0.0, (W - 1).tofloat());
    local z = clampf(wz / CELL, 0.0, (H - 1).tofloat());
    local ix = x.tointeger(), iz = z.tointeger();
    local jx = snowMin(ix + 1, W - 1), jz = snowMin(iz + 1, H - 1);
    local tx = x - ix, tz = z - iz;
    return ((combinedHm.height(ix, iz) * (1.0 - tx) + combinedHm.height(jx, iz) * tx) * (1.0 - tz) +
            (combinedHm.height(ix, jz) * (1.0 - tx) + combinedHm.height(jx, jz) * tx) * tz) * HSCALE;
}
// Rounded sole, narrower waist, shallow tread and a soft displaced-snow rim.
function stampBoot(wx, wz, dx, dz) {
    local length = sqrt(dx * dx + dz * dz);
    if (length < 0.0001) { dx = 0.0; dz = -1.0; }
    else { dx /= length; dz /= length; }
    local cx = wx / CELL, cz = wz / CELL;
    for (local z = cz.tointeger() - 24; z <= cz.tointeger() + 24; z++) {
        for (local x = cx.tointeger() - 24; x <= cx.tointeger() + 24; x++) {
            if (x < 0 || z < 0 || x >= W || z >= H) continue;
            local rx = (x - cx) * CELL, rz = (z - cz) * CELL;
            local along = rx * dx + rz * dz, across = -rx * dz + rz * dx;
            local width = 0.077 + 0.018 * snowSmooth(-0.04, 0.10, along);
            width -= 0.014 * (1.0 - snowSmooth(0.0, 0.055, abs(along + 0.025)));
            local u = along / 0.19, v = across / width;
            local n = sqrt(u * u + v * v);
            if (n > 1.5) continue;
            local inside = 1.0 - snowSmooth(0.65, 1.15, n);
            local tread = 0.016 * (0.5 + 0.5 * cos(along * 145.0));
            local heelGap = 0.045 * (1.0 - snowSmooth(0.006, 0.020, abs(along + 0.06)));
            local compression = (0.28 - tread - heelGap) * inside;
            local ring = snowSmooth(0.98, 1.14, n) * (1.0 - snowSmooth(1.14, 1.48, n));
            local rim = 0.065 * ring * (0.85 + 0.15 * sin(x * 0.61 + z * 0.47));
            sf.setHeight(x, z, clampf(sf.height(x, z) - compression + rim, 0.0, 1.0));
        }
    }
}
function rebuildTerrain() {
    if (!snow.applyToHeightmap(sf, terrainHm, combinedHm, SNOW_SCALE)) throw "snow composition failed";
    updateAdaptiveSnowMesh();
    if (terrainTexA != null) {
        if (!snow.updateTexture(sf, terrainTexA, gfx, "albedo")) throw "snow albedo update failed";
        if (!snow.updateTexture(sf, terrainTexH, gfx, "height")) throw "snow height update failed";
        sf.clearDirty();
    }
}
function groundFromMouse(mx, my) {
    cam.screenToRay(mx, my, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local ox = cam.getScreenRayOriginX(), oy = cam.getScreenRayOriginY(), oz = cam.getScreenRayOriginZ();
    local dx = cam.getScreenRayDirX(), dy = cam.getScreenRayDirY(), dz = cam.getScreenRayDirZ();
    if (dy >= -0.0001) return null;
    local t = (0.3 - oy) / dy;
    for (local i = 0; i < 5; i++) t = (surfaceHeight(ox + dx * t, oz + dz * t) - oy) / dy;
    local x = ox + dx * t, z = oz + dz * t;
    if (x < 0.0 || z < 0.0 || x > (W - 1) * CELL || z > (H - 1) * CELL) return null;
    return [x, z];
}
function mousePressed(button) {
    local down = mouse.isDown(button);
    local previous = button == 1 ? leftWasDown : rightWasDown;
    if (button == 1) leftWasDown = down; else rightWasDown = down;
    return down && !previous;
}
eve_init = function() {
    // Keep object identities on hot reload; restart when changing grid size.
    if (terrainHm == null) {
        local ground = procgen.newHeightmap(W, H), combined = procgen.newHeightmap(W, H);
        if (!ground.ok || !combined.ok) throw "snow grid creation failed";
        terrainHm = ground.value; combinedHm = combined.value;
        sf = snow.newField(W, H); sf.fill(0.85);
        for (local z = 0; z < H; z++) for (local x = 0; x < W; x++) {
            local wx = x * CELL, wz = z * CELL;
            terrainHm.setHeight(x, z, 0.12 + 0.035 * sin(wx * 1.4 + wz * 0.65) +
                                         0.022 * cos(wz * 2.2 - wx * 0.4));
        }
        for (local i = 0; i < 5; i++) {
            local z = 2.45 - i * 0.40;
            local x = 1.55 + 0.08 * sin(i * 0.55) + (i % 2 == 0 ? -0.14 : 0.14);
            stampBoot(x, z, 0.05, -1.0);
        }
    }
    rebuildTerrain();
    if (terrainEnt == null) {
        terrainTexA = snow.uploadTexture(sf, gfx, "albedo");
        terrainTexH = snow.uploadTexture(sf, gfx, "height");
        if (terrainTexA == null || terrainTexH == null) throw "snow textures failed";
        terrainEnt = eve.Renderable3D(); terrainEnt.setMesh(terrainMesh);
        terrainEnt.setTexture(terrainTexA); terrainEnt.setHeightTexture(terrainTexH);
        terrainEnt.setRoughness(0.86); terrainEnt.setMetallic(0.0);
        terrainEnt.setReceiveShadow(true); sf.clearDirty();
        walker = eve.Renderable3D(); walker.setMesh(gfx.newMeshSphere(12, 8));
        walker.setScale(0.06, 0.06, 0.06); walker.setTint(0.20, 0.32, 0.45, 1.0); walker.setVisible(false);
    }
    terrainEnt.setParallax(parallaxOn ? POM_SCALE : 0.0, 8.0, 24.0);
    if (cam == null) cam = eve.Camera3D();
    cam.setFov(42.0); cam.setAmbient(0.30, 0.35, 0.44); cam.setActive(true);
    gfx.setBackgroundColor(0.65, 0.73, 0.82, 1.0);
    if (sun == null) sun = eve.Light3D();
    sun.setType("dir"); sun.setDirection(-0.7, 0.48, -0.4);
    sun.setColor(1.0, 0.94, 0.83, 0.65); sun.setCastShadow(true); sun.setShadowStrength(0.5);
    print("SNOW: LMB boot / RMB impact / W walk / S snowfall / R reset / O orbit / P parallax\n");
};
eve_update = function(dt) {
    if (key_just_pressed("o") || key_just_pressed("O")) orbitOn = !orbitOn;
    if (orbitOn) camAngle += dt * 0.18;
    local cx = (W - 1) * CELL * 0.5, cz = (H - 1) * CELL * 0.5;
    cam.setEye(cx + 1.8 * cos(camAngle), 1.75, cz + 1.8 * sin(camAngle));
    cam.setTarget(cx, 0.28, cz);
    local g = groundFromMouse(mouse.getX(), mouse.getY());
    local left = mousePressed(1), right = mousePressed(2);
    if (g != null && left) stampBoot(g[0], g[1], -cos(camAngle), -sin(camAngle));
    if (g != null && right) sf.stampImpact(g[0] / CELL, g[1] / CELL, 0.22 / CELL, 0.95);
    if (key_just_pressed("w") || key_just_pressed("W")) walkDemo = !walkDemo;
    if (walkDemo) {
        walkAngle += dt * 0.45;
        local wx = cx + 0.65 * cos(walkAngle), wz = cz + 0.65 * sin(walkAngle);
        walker.setPosition(wx, surfaceHeight(wx, wz) + 0.15, wz); walker.setVisible(true);
        stepTimer -= dt;
        if (stepTimer <= 0.0) {
            local side = stepNumber % 2 == 0 ? -0.13 : 0.13;
            stampBoot(wx + side * cos(walkAngle), wz + side * sin(walkAngle), -sin(walkAngle), cos(walkAngle));
            stepNumber++; stepTimer += 0.7;
        }
    } else walker.setVisible(false);
    if (key_just_pressed("s") || key_just_pressed("S")) snowfallOn = !snowfallOn;
    if (snowfallOn) sf.addSnowfall(dt * 0.06);
    if (key_just_pressed("p") || key_just_pressed("P")) {
        parallaxOn = !parallaxOn;
        terrainEnt.setParallax(parallaxOn ? POM_SCALE : 0.0, 8.0, 24.0);
    }
    if (key_just_pressed("r") || key_just_pressed("R")) sf.fill(0.85);
    if (sf.isDirty()) rebuildTerrain();
};
eve_render = function() { gfx.clear(); gfx.render3D(); };
