// ============================================================================
// Interactive snow demo — depth-field snow on a heightmap terrain.
//
// The SnowField grid is the single source of truth for the snow surface:
//   - real displacement:  terrain + snow  ->  heightmap mesh rebuilt in place
//     (footprints / craters are actual geometry, not a decal)
//   - POM micro-detail:   the same grid is uploaded as the material's height
//     texture and drives parallax occlusion mapping
//   - recovery:           snowfall raises the grid back toward full cover
//
// Controls:
//   Left click   stamp a footprint (direction = camera forward on XZ)
//   Right click  carve an impact crater (snow cleared + ground deformed)
//   W            toggle auto-walker that leaves footprints behind
//   S            toggle snowfall recovery
//   P            toggle POM (compare displacement vs parallax detail)
//   R            reset the snow cover
//
// Run: make run/<platform>-debug GAME=examples/snow
// ============================================================================

// `snow` is the module slot bound by load.nut; `sf` is our SnowField instance.
persist sf = null
persist terrainHm = null
persist combinedHm = null
persist terrainMesh = null
persist terrainEnt = null
persist terrainTexA = null
persist terrainTexN = null
persist terrainTexH = null
persist walker = null
persist sun = null
persist cam = null
persist camAngle = 0.0
persist walkDemo = false
persist snowfallOn = false
persist parallaxOn = true
persist walkAngle = 0.0
persist stepTimer = 0.0
persist rebuildTimer = 0.0
persist helpPrinted = false

const W = 96;            // heightmap cells
const H = 96;
const CELL = 0.5;        // world units per cell
const HSCALE = 3.2;      // world units per unit of height
const SNOW_SCALE = 0.09; // snow depth added to the heightmap (x HSCALE = world)
const POM_SCALE = 0.06;  // parallax strength for the snow height texture

function surfaceHeight(wx, wz) {
    local cx = clampf((wx / CELL).tointeger(), 0, W - 1);
    local cz = clampf((wz / CELL).tointeger(), 0, H - 1);
    return combinedHm.height(cx, cz) * HSCALE;
}

function regenTerrain() {
    local p = procgen.newParams();
    p.setSize(W, H);
    p.setSeed(20260822);
    p.setFloat("frequency", 1.0 / 24.0);
    p.setInt("octaves", 5);
    local generated = procgen.generateHeightmap(p);
    if (generated != null) {
        terrainHm = generated;
    } else {
        if (terrainHm == null) terrainHm = procgen.newHeightmap(W, H);
        for (local z = 0; z < H; z++) {
            for (local x = 0; x < W; x++) {
                terrainHm.setHeight(x, z,
                    0.55 + 0.18 * sin(x * 0.12) * cos(z * 0.12) +
                    0.08 * cos(x * 0.31 + z * 0.19));
            }
        }
    }

    if (sf == null) {
        sf = snow.newField(W, H);
        sf.fill(0.85);
    }
    if (combinedHm == null) combinedHm = procgen.newHeightmap(W, H);

    if (terrainMesh == null) {
        snow.applyToHeightmap(sf, terrainHm, combinedHm, SNOW_SCALE);
        terrainMesh = editor.newHeightmapMeshSmooth(combinedHm, CELL, HSCALE);
        terrainEnt = eve.Renderable3D();
        terrainEnt.setMesh(terrainMesh);
        terrainEnt.setTint(1.0, 1.0, 1.0, 1.0);
        terrainEnt.setRoughness(0.95);
        terrainEnt.setMetallic(0.0);
        terrainEnt.setReceiveShadow(true);
        // Three views of the same SnowField: albedo (snow/ground color),
        // normal (depth-gradient shading) and POM height (R = depth).
        terrainTexA = snow.uploadTexture(sf, gfx, "albedo");
        terrainTexN = snow.uploadTexture(sf, gfx, "normal");
        terrainTexH = snow.uploadTexture(sf, gfx, "height");
        terrainEnt.setTexture(terrainTexA);
        terrainEnt.setNormalTexture(terrainTexN);
        terrainEnt.setHeightTexture(terrainTexH);
        terrainEnt.setParallax(POM_SCALE, 8.0, 32.0);
        terrainEnt.setPosition(0.0, 0.0, 0.0);
        terrainEnt.setVisible(true);
        walker = eve.Renderable3D();
        walker.setMesh(gfx.newMeshSphere(12, 8));
        walker.setScale(0.28, 0.28, 0.28);
        walker.setTint(0.22, 0.30, 0.55, 1.0);
        walker.setRoughness(0.6);
        walker.setVisible(false);
    } else {
        rebuildTerrain();
    }
}

function rebuildTerrain() {
    if (terrainMesh == null) return;
    snow.applyToHeightmap(sf, terrainHm, combinedHm, SNOW_SCALE);
    editor.updateHeightmapMeshSmooth(terrainMesh, gfx, combinedHm, CELL, HSCALE);
    if (sf.isDirty()) {
        snow.updateTexture(sf, terrainTexA, gfx, "albedo");
        snow.updateTexture(sf, terrainTexN, gfx, "normal");
        snow.updateTexture(sf, terrainTexH, gfx, "height");
        sf.clearDirty();
    }
}

// Mouse ray -> ground point (world XZ). Refine the plane hit once against the
// actual displaced surface height so clicks land on the snow.
function groundFromMouse(mx, my) {
    cam.screenToRay(mx, my, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local ox = cam.getScreenRayOriginX();
    local oy = cam.getScreenRayOriginY();
    local oz = cam.getScreenRayOriginZ();
    local dx = cam.getScreenRayDirX();
    local dy = cam.getScreenRayDirY();
    local dz = cam.getScreenRayDirZ();
    if (dy > -0.0001) return null;
    local t = (1.6 - oy) / dy;
    local wx = ox + dx * t;
    local wz = oz + dz * t;
    local t2 = (surfaceHeight(wx, wz) - oy) / dy;
    return [ox + dx * t2, oz + dz * t2];
}

function mousePressed(button) {
    return mouse.isDown(button);
}

eve_init = function() {
    regenTerrain();

    cam = eve.Camera3D();
    cam.setFov(50.0);
    cam.setAmbient(0.42, 0.45, 0.50);
    cam.setActive(true);
    gfx.setBackgroundColor(0.66, 0.72, 0.80, 1.0);
    // CSM shadows need a Light3D directional caster; the legacy
    // gfx.setDirectionalLight path never casts.
    sun = eve.Light3D();
    sun.setType("dir");
    sun.setDirection(-0.55, 0.62, 0.40);
    sun.setColor(1.02, 1.00, 0.97, 1.45);
    sun.setCastShadow(true);
    sun.setShadowStrength(1.0);

    if (!helpPrinted) {
        print("interactive snow: LMB = footprint, RMB = crater, W = walker, " +
              "S = snowfall, P = POM toggle, R = reset snow\n");
        helpPrinted = true;
    }
};

eve_update = function(dt) {
    camAngle += dt * 0.22;
    local cx = (W - 1) * CELL * 0.5;
    local cz = (H - 1) * CELL * 0.5;
    local dist = 22.0;
    local height = 12.0 + sin(camAngle * 0.7) * 2.5;
    cam.setEye(cx + dist * cos(camAngle), height, cz + dist * sin(camAngle));
    cam.setTarget(cx, 0.8, cz);

    // Mouse stamps.
    local g = groundFromMouse(mouse.getX(), mouse.getY());
    if (g != null && mousePressed(1)) {
        sf.stampFootprint(g[0] / CELL, g[1] / CELL,
                          cos(camAngle), sin(camAngle), 1.7, 0.75);
        rebuildTerrain();
    }
    if (g != null && mousePressed(2)) {
        local cellX = g[0] / CELL;
        local cellZ = g[1] / CELL;
        sf.stampImpact(cellX, cellZ, 3.0, 0.9);
        // Carve the ground too so the crater stays after the snow refills.
        for (local zz = -3; zz <= 3; zz++) {
            for (local xx = -3; xx <= 3; xx++) {
                local gx = cellX.tointeger() + xx;
                local gy = cellZ.tointeger() + zz;
                if (gx < 0 || gx >= W || gy < 0 || gy >= H) continue;
                local d = sqrt(xx * xx + zz * zz).tofloat();
                if (d > 3.0) continue;
                local fall = 1.0 - d / 3.0;
                local cur = terrainHm.height(gx, gy);
                terrainHm.setHeight(gx, gy, clampf(cur - 0.10 * fall * fall, 0.0, 1.0));
            }
        }
        rebuildTerrain();
    }

    // Auto-walker leaves a trail of footprints.
    if (key_just_pressed("w") || key_just_pressed("W")) walkDemo = !walkDemo;
    if (walkDemo) {
        walkAngle += dt * 0.8;
        local wx = cx + 7.0 * cos(walkAngle);
        local wz = cz + 7.0 * sin(walkAngle);
        walker.setPosition(wx, surfaceHeight(wx, wz) + 0.35, wz);
        walker.setVisible(true);
        stepTimer -= dt;
        if (stepTimer <= 0.0) {
            sf.stampFootprint(wx / CELL, wz / CELL, -sin(walkAngle), cos(walkAngle),
                              1.6, 0.7);
            stepTimer = 0.22;
        }
    } else {
        walker.setVisible(false);
    }

    // Snowfall recovery, throttled so we do not drain the GPU every frame.
    if (key_just_pressed("s") || key_just_pressed("S")) snowfallOn = !snowfallOn;
    if (snowfallOn) {
        rebuildTimer -= dt;
        if (rebuildTimer <= 0.0) {
            sf.addSnowfall(dt * 0.06);
            rebuildTerrain();
            rebuildTimer = 0.2;
        }
    }

    if (key_just_pressed("p") || key_just_pressed("P")) {
        parallaxOn = !parallaxOn;
        terrainEnt.setParallax(parallaxOn ? POM_SCALE : 0.0, 8.0, 32.0);
    }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        sf.fill(0.85);
        rebuildTerrain();
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
