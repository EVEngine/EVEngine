// Autonomous 3D IK spider — eve.IK Skeleton3D + Solver3D FABRIK crawl demo.
dofile("spider3d.nut");

persist spider = null
persist ikMod = null
persist camera = null
persist arenaBuilt = false
persist spiderReadyPrinted = false
persist spiderScreenshotSaved = false
persist spiderFrame = 0
persist paused = false
persist orbit = 0.35
persist lastSpace = false
persist lastR = false

function spiderRebuild() {
    if (spider != null) s3DestroyVisuals(spider);
    ikMod = eve.IK();
    math.setRandomSeed(20261106);
    spider = s3Make(ikMod, 0.0, 0.0);
    s3PickWaypoint(spider);
    s3SolveIK(spider);
    if (!arenaBuilt) {
        s3BuildArena(spider.arena);
        arenaBuilt = true;
    }
    s3BuildVisuals(spider);
    spiderScreenshotSaved = false;
    spiderFrame = 0;
}

function spiderUpdateCamera(dt) {
    if (camera == null || spider == null) return;
    orbit += dt * 0.22;
    // Lower, closer orbit so limbs read in perspective rather than top-down.
    local radius = 3.6;
    local height = 1.85;
    local cx = spider.x + cos(orbit) * radius;
    local cz = spider.z + sin(orbit) * radius;
    camera.setEye(cx, height, cz);
    camera.setTarget(spider.x, spider.y + 0.05, spider.z);
}

eve_init <- function() {
    gfx.setBackgroundColor(0.04, 0.055, 0.05, 1.0);
    gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.15, 1.05, 0.95);

    if (camera == null) {
        camera = eve.Camera3D();
        camera.setFov(48.0);
        camera.setUp(0.0, 1.0, 0.0);
        camera.setAmbient(0.22, 0.25, 0.28);
        camera.setClipPlanes(0.05, 80.0);
        camera.setActive(true);
    }

    if (spider == null) spiderRebuild();
    spiderUpdateCamera(0.0);

    if (!spiderReadyPrinted) {
        spiderReadyPrinted = true;
        print("IK_SPIDER3D_READY legs=8 gait=tetrapod solver=FABRIK+pole solveChain space=pause r=reseed\n");
    }
};

eve_update <- function(dt) {
    if (spider == null) return;

    local spaceDown = false;
    local rDown = false;
    if (has_module("keyboard")) {
        spaceDown = keyboard.isDown("space");
        rDown = keyboard.isDown("r") || keyboard.isDown("R");
    }
    if (spaceDown && !lastSpace) paused = !paused;
    if (rDown && !lastR) {
        // Hot-reload safe rebuild: drop old primitives by restarting visuals
        // through a full remake (scene primitives are owned by gfx).
        spiderRebuild();
    }
    lastSpace = spaceDown;
    lastR = rDown;

    if (!paused) {
        s3Update(spider, dt);
        s3SyncVisuals(spider);
    }
    spiderUpdateCamera(dt);
    spiderFrame += 1;

    if (!spiderScreenshotSaved && spider.age > 2.8 && spider.steps >= 20) {
        if (gfx.saveFramePng("ik-spider-3d.png")) {
            spiderScreenshotSaved = true;
            print("IK_SPIDER3D_SHOT steps=" + spider.steps + " age=" + spider.age + "\n");
        }
    }
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
};

eve_reload <- function() {
    spiderReadyPrinted = false;
    spider = null;
    eve_init();
};
