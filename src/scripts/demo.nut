// Default embedded demo: meteor defense mini-game.
// Procedural demo assets come from optional eve.Demo (EVENGINE_BUILD_DEMO).

demoAssets <- null;

// World/UI scale vs a ~400px-wide reference (desktop window / phone short side).
gs <- 1.0;
// Speed scales so crossing the screen takes ~same time as the 800x600 desktop layout.
vs <- 1.0;
hs <- 1.0;

game <- {
    playing = true
    score = 0
    lives = 3
    shootCd = 0.0
    spawnCd = 0.0
    mouseWasDown = false
    touchWasDown = false
    hotReload = false
};

plane <- {
    x = 0.0
    y = 0.0
    w = 36.0
    h = 28.0
    speed = 320.0
    marginBottom = 70.0
};

bullets <- [];
meteors <- [];
stars <- [];

musicSrc <- null;
shootSrc <- null;
explodeSrc <- null;
hitSrc <- null;
boomPool <- [];
boomNext <- 0;

function applyGameScale() {
    local shortSide = config.width < config.height ? config.width : config.height;
    gs = shortSide / 400.0;
    if (gs < 1.0) gs = 1.0;
    if (gs > 3.2) gs = 3.2;

    // Match desktop travel time on tall/wide screens (collide-before-cull handles tunneling).
    vs = config.height / 600.0;
    if (vs < 1.0) vs = 1.0;
    hs = config.width / 800.0;
    if (hs < 1.0) hs = 1.0;

    plane.w = 36.0 * gs;
    plane.h = 28.0 * gs;
    plane.speed = 320.0 * hs;
    plane.marginBottom = 70.0 * gs;

    try {
        ui.setScale(gs);
    } catch (e) {}
}

function randf(a, b) {
    return a + (b - a) * (rand().tofloat() / RAND_MAX.tofloat());
}

function rectHit(ax, ay, aw, ah, bx, by, bw, bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

function updateHud() {
    ui.select("lives");
    ui.setText("lives", "Lives  " + game.lives);
    ui.select("score");
    ui.setText("score", "Score  " + game.score);
}

function showGameOver(show) {
    ui.select("gameover");
    ui.setHostVisible(show);
    if (show) {
        ui.setHostModal(true);
        ui.setText("msg", "Game Over  —  Score " + game.score);
    } else {
        ui.setHostModal(false);
    }
}

function spawnBoom(x, y) {
    if (boomPool.len() == 0) return;
    local fx = boomPool[boomNext];
    boomNext = (boomNext + 1) % boomPool.len();
    fx.reset();
    // Same call order as the pre-scale working version; only size/speed use gs.
    fx.setPosition(x, y);
    fx.applyPreset("spark");
    fx.setEmissionRate(0.0);
    fx.setEmitterLifetime(0.2);
    fx.setSpread(6.28);
    fx.setDirection(0.0);
    fx.setSpeed(80.0 * gs, 220.0 * gs);
    fx.setParticleSize(4.0 * gs, 4.0 * gs);
    fx.setParticleLifetime(0.25, 0.55);
    fx.setColorStart(1.0, 0.85, 0.2, 1.0);
    fx.setColorEnd(1.0, 0.15, 0.0, 0.0);
    fx.emit(42);
    fx.start();
}

function playSfx(src) {
    if (src == null) return;
    src.stop();
    src.play();
}

function resetGame() {
    game.playing = true;
    game.score = 0;
    game.lives = 3;
    game.shootCd = 0.0;
    game.spawnCd = 0.4;
    game.mouseWasDown = false;
    game.touchWasDown = false;
    plane.x = config.width * 0.5 - plane.w * 0.5;
    plane.y = config.height - plane.marginBottom;
    bullets = [];
    meteors = [];
    showGameOver(false);
    updateHud();
    if (musicSrc != null && !musicSrc.isPlaying())
        musicSrc.play();
}

function fireBullet() {
    if (!game.playing || game.shootCd > 0.0) return;
    game.shootCd = 0.18;
    local bw = 6.0 * gs;
    local bh = 14.0 * gs;
    bullets.push({
        x = plane.x + plane.w * 0.5 - bw * 0.5
        y = plane.y - bh * 0.7
        w = bw
        h = bh
        vy = -520.0 * vs
    });
    playSfx(shootSrc);
}

function spawnMeteor() {
    local size = randf(22.0, 40.0) * gs;
    meteors.push({
        x = randf(10.0 * gs, config.width - size - 10.0 * gs)
        y = -size
        w = size
        h = size
        vy = (randf(70.0, 140.0) + game.score * 0.15) * vs
        vx = randf(-40.0, 40.0) * hs
        rot = 0.0
    });
}

function spawnStar() {
    stars.push({
        x = randf(0.0, config.width)
        y = randf(-40.0 * gs, config.height * 0.4)
        vx = randf(180.0, 420.0) * hs
        vy = randf(120.0, 280.0) * vs
        life = randf(0.35, 0.9)
        len = randf(18.0, 42.0) * gs
    });
}

function loseLife() {
    game.lives -= 1;
    updateHud();
    playSfx(hitSrc);
    if (game.lives <= 0) {
        game.playing = false;
        showGameOver(true);
    }
}

planetCam <- null;
planet <- null;

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.12, 1.0);
    applyGameScale();

    plane.x = config.width * 0.5 - plane.w * 0.5;
    plane.y = config.height - plane.marginBottom;

    // Background rotating planet (mesh is generic; albedo from Demo module)
    try {
        if (demoAssets == null) demoAssets = eve.Demo();
        planetCam = eve.Camera3D();
        planetCam.setEye(0.0, 0.15, 3.2);
        planetCam.setTarget(0.35, 0.05, 0.0);
        planetCam.setFov(42.0);

        planet = eve.Renderable3D();
        planet.setMesh(gfx.newMeshSphere(64, 32));
        planet.setTexture(demoAssets.newPlanetTexture(gfx));
        planet.setPosition(0.55, 0.1, 0.0);
        planet.setScale(1.15, 1.15, 1.15);
        planet.setTint(0.85, 0.88, 1.0, 1.0);
        gfx.setDirectionalLight(-0.35, 0.7, 0.45, 1.0, 0.95, 0.9);
    } catch (e) {
        print("planet init skipped: " + e + "\n");
        planet = null;
    }

    // Procedural audio via Demo module
    try {
        if (demoAssets == null) demoAssets = eve.Demo();
        local musicData = demoAssets.newSound("music");
        musicSrc = audio.newSource(musicData);
        musicSrc.setLooping(true);
        musicSrc.setVolume(0.28);
        musicSrc.play();

        shootSrc = audio.newSource(demoAssets.newSound("shoot"));
        shootSrc.setVolume(0.45);
        explodeSrc = audio.newSource(demoAssets.newSound("explode"));
        explodeSrc.setVolume(0.55);
        hitSrc = audio.newSource(demoAssets.newSound("hit"));
        hitSrc.setVolume(0.5);
    } catch (e) {
        print("audio init skipped: " + e + "\n");
    }

    for (local i = 0; i < 6; i += 1) {
        local fx = particles.newEmitter(96);
        fx.applyPreset("spark");
        fx.setEmissionRate(0.0);
        fx.stop();
        boomPool.push(fx);
    }

    for (local i = 0; i < 5; i += 1)
        spawnStar();

    local pad = 12.0 * gs;

    // HUD: lives top-left, score top-right
    ui.beginBuild();
    ui.beginWindow("Lives", "root");
    ui.text("Lives  3", "lives");
    ui.end();
    ui.mountBuildAs("lives");
    ui.select("lives");
    ui.setHostOverlay(true);
    ui.setHostPos(pad, pad, 0.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Score", "root");
    ui.text("Score  0", "score");
    ui.end();
    ui.mountBuildAs("score");
    ui.select("score");
    ui.setHostOverlay(true);
    ui.setHostPos(config.width - pad, pad, 1.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Game Over", "root");
    ui.text("Game Over", "msg");
    ui.button("Restart", "restart");
    ui.end();
    ui.mountBuildAs("gameover");
    ui.select("gameover");
    ui.setHostVisible(false);
    ui.setHostPos(config.width * 0.5, config.height * 0.45, 0.5, 0.5);

    updateHud();
};

eve_update = function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "gameover/restart")
            resetGame();
        id = ui.consumeClick();
    }

    if (rand() % 100 < 3)
        spawnStar();
    local si = 0;
    while (si < stars.len()) {
        local st = stars[si];
        st.x += st.vx * dt;
        st.y += st.vy * dt;
        st.life -= dt;
        if (st.life <= 0.0 || st.x > config.width + 50.0 * gs || st.y > config.height + 50.0 * gs)
            stars.remove(si);
        else
            si += 1;
    }

    if (game.playing) {
        local move = 0.0;
        if (keyboard.isDown("Left") || keyboard.isDown("A"))
            move -= 1.0;
        if (keyboard.isDown("Right") || keyboard.isDown("D"))
            move += 1.0;

        local aimX = null;
        local fireHeld = false;
        local firePressed = false;

        local tc = touch.getTouchCount();
        if (tc > 0) {
            aimX = touch.getTouchX(0);
            fireHeld = true;
            if (!game.touchWasDown)
                firePressed = true;
            game.touchWasDown = true;
        } else {
            game.touchWasDown = false;
        }

        local md = mouse.isDown(1);
        if (!ui.wantCaptureMouse()) {
            if (md) {
                aimX = mouse.getX();
                fireHeld = true;
                if (!game.mouseWasDown)
                    firePressed = true;
            }
            game.mouseWasDown = md;
        } else {
            game.mouseWasDown = false;
        }

        if (keyboard.isDown("Space") || keyboard.isDown("Up") || keyboard.isDown("W"))
            fireHeld = true;

        if (aimX != null) {
            local target = aimX - plane.w * 0.5;
            local dx = target - plane.x;
            local step = plane.speed * 1.4 * dt;
            if (dx > step) plane.x += step;
            else if (dx < -step) plane.x -= step;
            else plane.x = target;
        } else {
            plane.x += move * plane.speed * dt;
        }

        local edge = 8.0 * gs;
        if (plane.x < edge) plane.x = edge;
        if (plane.x + plane.w > config.width - edge)
            plane.x = config.width - edge - plane.w;

        if (game.shootCd > 0.0)
            game.shootCd -= dt;
        if (firePressed || (fireHeld && game.shootCd <= 0.0 && (tc > 0 || md)))
            fireBullet();
        else if (keyboard.isDown("Space") || keyboard.isDown("Up") || keyboard.isDown("W"))
            fireBullet();

        game.spawnCd -= dt;
        if (game.spawnCd <= 0.0) {
            spawnMeteor();
            game.spawnCd = randf(0.55, 1.15) * (1.0 / (1.0 + game.score * 0.002));
        }

        // Move bullets first; cull off-screen only AFTER hit tests so a fast
        // shot that crosses a meteor and y<0 in one frame still registers.
        local bi = 0;
        while (bi < bullets.len()) {
            bullets[bi].y += bullets[bi].vy * dt;
            bi += 1;
        }

        local mi = 0;
        while (mi < meteors.len()) {
            local m = meteors[mi];
            m.x += m.vx * dt;
            m.y += m.vy * dt;

            local destroyed = false;
            bi = 0;
            while (bi < bullets.len()) {
                local b = bullets[bi];
                if (rectHit(b.x, b.y, b.w, b.h, m.x, m.y, m.w, m.h)) {
                    // Remove both first so FX failures cannot leave a ghost meteor.
                    bullets.remove(bi);
                    meteors.remove(mi);
                    spawnBoom(m.x + m.w * 0.5, m.y + m.h * 0.5);
                    playSfx(explodeSrc);
                    game.score += 10;
                    updateHud();
                    destroyed = true;
                    break;
                }
                bi += 1;
            }

            if (destroyed)
                continue;

            if (rectHit(plane.x, plane.y, plane.w, plane.h, m.x, m.y, m.w, m.h)) {
                meteors.remove(mi);
                spawnBoom(m.x + m.w * 0.5, m.y + m.h * 0.5);
                playSfx(explodeSrc);
                loseLife();
                continue;
            }

            if (m.x + m.w < 0.0 || m.x > config.width) {
                meteors.remove(mi);
                continue;
            }

            if (m.y > config.height + 20.0 * gs) {
                meteors.remove(mi);
                loseLife();
                continue;
            }
            mi += 1;
        }

        bi = 0;
        while (bi < bullets.len()) {
            local b = bullets[bi];
            if (b.y + b.h < 0.0)
                bullets.remove(bi);
            else
                bi += 1;
        }
    }

    particles.update(dt);

    if (planet != null)
        planet.setYaw(planet.getYaw() + dt * 0.35);
};

eve_render = function() {
    gfx.clear();
    if (planet != null)
        gfx.render3D();

    foreach (st in stars) {
        local t = st.life;
        if (t > 1.0) t = 1.0;
        if (t < 0.0) t = 0.0;
        local nx = st.vx / (500.0 * gs);
        local ny = st.vy / (500.0 * gs);
        gfx.drawSolidRect(st.x, st.y, st.len * nx + 2.0 * gs, st.len * ny + 2.0 * gs, 0.75, 0.8, 1.0, 0.25 + 0.55 * t);
        gfx.drawSolidRect(st.x, st.y, 3.0 * gs, 3.0 * gs, 1.0, 1.0, 1.0, 0.5 + 0.5 * t);
    }

    foreach (m in meteors) {
        gfx.drawSolidRect(m.x, m.y, m.w, m.h, 0.55, 0.32, 0.18, 1.0);
        gfx.drawSolidRect(m.x + m.w * 0.2, m.y + m.h * 0.15, m.w * 0.35, m.h * 0.3, 0.7, 0.45, 0.25, 1.0);
    }

    foreach (b in bullets) {
        gfx.drawSolidRect(b.x, b.y, b.w, b.h, 0.95, 0.9, 0.3, 1.0);
    }

    if (game.lives > 0 || game.playing) {
        local px = plane.x;
        local py = plane.y;
        local s = gs;
        gfx.drawSolidRect(px + 12.0 * s, py, 12.0 * s, 10.0 * s, 0.35, 0.85, 1.0, 1.0);
        gfx.drawSolidRect(px, py + 10.0 * s, plane.w, 12.0 * s, 0.25, 0.7, 0.95, 1.0);
        gfx.drawSolidRect(px + 8.0 * s, py + 20.0 * s, 20.0 * s, 8.0 * s, 0.2, 0.55, 0.85, 1.0);
        gfx.drawSolidRect(px + 14.0 * s, py + 22.0 * s, 8.0 * s, 6.0 * s, 1.0, 0.55, 0.15, 1.0);
    }

    particles.render(gfx);
    ui.beginFrameAndRender();
};

eve_quit = function() {
    if (musicSrc != null) musicSrc.stop();
};
