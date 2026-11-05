// Autonomous IK spider — eve.IK Skeleton2D + Solver2D FABRIK crawl demo.
dofile("spider.nut");

persist spider = null
persist ikMod = null
persist spiderReadyPrinted = false
persist spiderScreenshotSaved = false
persist spiderFrame = 0
persist trail = []
persist showGuides = true
persist paused = false
persist lastSpace = false
persist lastG = false
persist lastR = false

function spiderDrawArena(gfx, w, h) {
    // Floor wash.
    gfx.drawSolidRect(0.0, 0.0, w.tofloat(), h.tofloat(), 0.10, 0.12, 0.11, 1.0);
    // Subtle grid.
    local step = 48.0;
    for (local x = 0.0; x < w; x += step) {
        gfx.drawSolidRect(x, 0.0, 1.0, h.tofloat(), 0.14, 0.17, 0.15, 0.55);
    }
    for (local y = 0.0; y < h; y += step) {
        gfx.drawSolidRect(0.0, y, w.tofloat(), 1.0, 0.14, 0.17, 0.15, 0.55);
    }
    // Border.
    gfx.drawSolidRect(0.0, 0.0, w.tofloat(), 4.0, 0.22, 0.28, 0.24, 1.0);
    gfx.drawSolidRect(0.0, h - 4.0, w.tofloat(), 4.0, 0.22, 0.28, 0.24, 1.0);
    gfx.drawSolidRect(0.0, 0.0, 4.0, h.tofloat(), 0.22, 0.28, 0.24, 1.0);
    gfx.drawSolidRect(w - 4.0, 0.0, 4.0, h.tofloat(), 0.22, 0.28, 0.24, 1.0);
}

function spiderDrawTrail(gfx) {
    for (local i = 0; i < trail.len(); i++) {
        local p = trail[i];
        local a = 0.12 + 0.35 * (i.tofloat() / trail.len().tofloat());
        gfx.drawSolidRect(p.x - 2.0, p.y - 2.0, 4.0, 4.0, 0.35, 0.55, 0.40, a);
    }
}

function spiderPushTrail(sp) {
    trail.push({ x = sp.x, y = sp.y });
    while (trail.len() > 96) trail.remove(0);
}

function spiderRebuild() {
    ikMod = eve.IK();
    math.setRandomSeed(20261105);
    spider = spiderMake(ikMod, config.width * 0.5, config.height * 0.55);
    spiderPickWaypoint(spider, config.width, config.height, 110.0);
    trail = [];
    spiderScreenshotSaved = false;
    spiderFrame = 0;
}

eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.09, 1.0);
    if (spider == null) spiderRebuild();
    if (!spiderReadyPrinted) {
        spiderReadyPrinted = true;
        print("IK_SPIDER_READY legs=8 gait=tetrapod solver=FABRIK solveChain space=pause g=guides r=reseed\n");
    }
};

eve_update <- function(dt) {
    if (spider == null) return;

    local spaceDown = false;
    local gDown = false;
    local rDown = false;
    if (has_module("keyboard")) {
        spaceDown = keyboard.isDown("space");
        gDown = keyboard.isDown("g") || keyboard.isDown("G");
        rDown = keyboard.isDown("r") || keyboard.isDown("R");
    }
    if (spaceDown && !lastSpace) paused = !paused;
    if (gDown && !lastG) showGuides = !showGuides;
    if (rDown && !lastR) spiderRebuild();
    lastSpace = spaceDown;
    lastG = gDown;
    lastR = rDown;

    if (!paused) {
        spiderUpdate(spider, dt, config.width.tofloat(), config.height.tofloat());
        if ((spiderFrame % 3) == 0) spiderPushTrail(spider);
    }
    spiderFrame += 1;

    if (!spiderScreenshotSaved && spiderFrame > 90 && spider.steps >= 8) {
        if (gfx.saveFramePng("ik-spider.png")) {
            spiderScreenshotSaved = true;
            print("IK_SPIDER_SHOT steps=" + spider.steps + " age=" + spider.age + "\n");
        }
    }
};

eve_render <- function() {
    gfx.clear();
    spiderDrawArena(gfx, config.width, config.height);
    spiderDrawTrail(gfx);
    if (spider != null) {
        if (showGuides) spiderDrawGuides(spider, gfx);
        spiderDraw(spider, gfx);
    }
};

eve_reload <- function() {
    spiderReadyPrinted = false;
    eve_init();
};
