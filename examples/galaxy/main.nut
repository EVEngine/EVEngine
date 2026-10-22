// A seeded, three-dimensional point cloud, projected into batched 2D sprites.
// Positions stay fixed in galaxy space: rotation is a rigid artistic animation,
// not an N-body simulation. Time comes exclusively from eve_update(dt).
persist galaxy = null;

function clampGalaxy(value, lo, hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

// Local Park-Miller stream; never changes the engine's global random state.
function galaxyRandom() {
    galaxy.seed = (galaxy.seed % 127773) * 16807 - (galaxy.seed / 127773) * 2836;
    if (galaxy.seed <= 0) galaxy.seed += 2147483647;
    return galaxy.seed.tofloat() / 2147483647.0;
}

function galaxyScatter() {
    return (galaxyRandom() + galaxyRandom() + galaxyRandom() - 1.5);
}

function resetGalaxyView() {
    galaxy.yaw = -0.32;
    galaxy.pitch = 0.72;
    galaxy.zoom = 1.0;
    galaxy.time = 0.0;
    galaxy.spinning = true;
}

function buildGalaxy() {
    galaxy.stars.clear();
    galaxy.clouds.clear();
    galaxy.dust.clear();
    galaxy.background.clear();
    galaxy.seed = 73129;
    for (local i = 0; i < 650; ++i) {
        galaxy.background.push([galaxyRandom(), galaxyRandom(),
            0.8 + galaxyRandom() * 2.2, galaxyRandom()]);
    }
    for (local i = 0; i < 6500; ++i) {
        local radius = 0.07 + pow(galaxyRandom(), 0.65) * 4.8;
        local angle = (i % 4) * 1.570796327 + log(radius + 0.25) * 2.5;
        angle += galaxyScatter() * (0.22 + 0.14 / (radius + 0.3));
        if (i % 5 == 0) angle = galaxyRandom() * 6.283185307;
        local warm = galaxyRandom();
        galaxy.stars.push([cos(angle) * radius, galaxyScatter() * 0.13,
            sin(angle) * radius, 3.0 + pow(galaxyRandom(), 5.0) * 9.0,
            warm, 0.42 + galaxyRandom() * 0.55]);
    }
    // A thicker, warm stellar bulge supplies parallax at inclined views.
    for (local i = 0; i < 1100; ++i) {
        local radius = pow(galaxyRandom(), 1.7) * 1.25;
        local angle = galaxyRandom() * 6.283185307;
        galaxy.stars.push([cos(angle) * radius, galaxyScatter() * 0.45,
            sin(angle) * radius, 2.5 + galaxyRandom() * 3.5,
            0.8 + galaxyRandom() * 0.2, 0.5 + galaxyRandom() * 0.4]);
    }
    for (local i = 0; i < 700; ++i) {
        local radius = 0.25 + galaxyRandom() * 4.5;
        local angle = (i % 4) * 1.570796327 + log(radius + 0.25) * 2.5;
        angle += galaxyScatter() * 0.24;
        galaxy.clouds.push([cos(angle) * radius, galaxyScatter() * 0.09,
            sin(angle) * radius, 0.3 + galaxyRandom() * 0.55,
            galaxyRandom(), 0.06 + galaxyRandom() * 0.13]);
        // Offset the obscuring dust toward the inner edge of each spiral arm.
        angle -= 0.16;
        galaxy.dust.push([cos(angle) * radius, 0.06,
            sin(angle) * radius, 0.18 + galaxyRandom() * 0.24,
            0.0, 0.12 + galaxyRandom() * 0.18]);
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.002, 0.004, 0.012, 1.0);
    if (galaxy == null) {
        galaxy = {
            stars = [], clouds = [], dust = [], background = [], seed = 73129,
            sprite = null, glow = null, yaw = 0.0, pitch = 0.0, zoom = 1.0,
            time = 0.0, spinning = true, dragging = false,
            mouseX = 0.0, mouseY = 0.0, capture = false, frames = 0
        };
        galaxy.sprite = gfx.newTextureFromFile("assets/star.png");
        galaxy.glow = gfx.newTextureFromFile("assets/glow.png");
        if (galaxy.sprite == null || galaxy.glow == null) {
            galaxy = null;
            throw "galaxy: required radial sprite assets are unavailable";
        }
        resetGalaxyView();
        buildGalaxy();
    }
    print("GALAXY_READY stars=" + galaxy.stars.len() + " seed=73129\n");
};

eve_update = function(dt) {
    galaxy.frames += 1;
    if (galaxy.spinning) galaxy.time += dt;
    if (key_just_pressed("space", "Space")) galaxy.spinning = !galaxy.spinning;
    if (key_just_pressed("r", "R")) resetGalaxyView();
    if (key_just_pressed("1")) galaxy.pitch = 1.45;
    if (key_just_pressed("2")) galaxy.pitch = 0.72;
    if (key_just_pressed("3")) galaxy.pitch = 0.12;
    if (key_just_pressed("F5")) galaxy.capture = true;
    local x = mouse.getX().tofloat();
    local y = mouse.getY().tofloat();
    local down = mouse.isDown(1);
    if (down && galaxy.dragging) {
        galaxy.yaw += (x - galaxy.mouseX) * 0.006;
        galaxy.pitch = clampGalaxy(galaxy.pitch + (y - galaxy.mouseY) * 0.006, 0.08, 1.5);
    }
    galaxy.dragging = down;
    galaxy.mouseX = x;
    galaxy.mouseY = y;
    galaxy.zoom = clampGalaxy(galaxy.zoom + mouse.getWheelY() * 0.09, 0.45, 2.2);
    // saveFramePng captures the engine-owned, previously presented frame.
    if (galaxy.capture && galaxy.frames > 2) {
        if (gfx.saveFramePng("galaxy.png")) print("GALAXY_CAPTURE saved galaxy.png\n");
        else print("GALAXY_CAPTURE could not save galaxy.png\n");
        galaxy.capture = false;
    }
};

function drawGalaxyLayer(points, kind, width, height, cy, sy, cp, sp) {
    local scale = height * 0.105 * galaxy.zoom;
    foreach (p in points) {
        local x = p[0] * cy - p[2] * sy;
        local z = p[0] * sy + p[2] * cy;
        local depth = z * cp - p[1] * sp;
        local perspective = 12.0 / (12.0 + depth);
        local screenX = width * 0.5 + x * scale * perspective;
        local screenY = height * 0.5 + (z * sp + p[1] * cp) * scale * perspective;
        local size = kind == 0 ? p[3] * galaxy.zoom * perspective : p[3] * scale * perspective;
        if (screenX < -size || screenX > width + size || screenY < -size || screenY > height + size) continue;
        local r = 0.2, g = 0.38, b = 0.85;
        if (kind == 0) {
            r = 0.5 + p[4] * 0.5;
            g = 0.7 + p[4] * 0.13;
            b = 1.0 - p[4] * 0.4;
        } else if (kind == 1) {
            r = 0.2 + p[4] * 0.38;
            g = 0.25 + p[4] * 0.1;
        } else {
            r = 0.008; g = 0.006; b = 0.018;
        }
        gfx.drawTexturedRect(kind == 0 ? galaxy.sprite : galaxy.glow,
            screenX - size * 0.5, screenY - size * 0.5, size, size, r, g, b, p[5]);
    }
}

eve_render = function() {
    gfx.clear();
    local width = gfx.getWidth().tofloat();
    local height = gfx.getHeight().tofloat();
    foreach (star in galaxy.background) {
        local size = star[2];
        gfx.drawTexturedRect(galaxy.sprite, star[0] * width, star[1] * height,
            size, size, 0.58, 0.7, 1.0, 0.2 + star[3] * 0.55);
    }
    local yaw = galaxy.yaw + galaxy.time * 0.025;
    local cy = cos(yaw), sy = sin(yaw);
    // pitch is elevation above the galactic plane; 90 degrees is face-on.
    local cp = cos(galaxy.pitch), sp = sin(galaxy.pitch);
    drawGalaxyLayer(galaxy.clouds, 1, width, height, cy, sy, cp, sp);
    local size = height * 0.58 * galaxy.zoom;
    gfx.drawTexturedRect(galaxy.glow, width * 0.5 - size * 0.5,
        height * 0.5 - size * 0.34, size, size * 0.68, 1.0, 0.62, 0.28, 0.85);
    drawGalaxyLayer(galaxy.dust, 2, width, height, cy, sy, cp, sp);
    drawGalaxyLayer(galaxy.stars, 0, width, height, cy, sy, cp, sp);
    size = height * 0.16 * galaxy.zoom;
    gfx.drawTexturedRect(galaxy.glow, width * 0.5 - size * 0.5,
        height * 0.5 - size * 0.5, size, size, 1.0, 0.89, 0.68, 0.95);
};
