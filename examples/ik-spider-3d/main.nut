// Autonomous 3D IK spider — lit Renderable3D meshes + Skeleton3D FABRIK.
dofile("spider3d.nut");

persist spider = null
persist ikMod = null
persist camera = null
persist keyLight = null
persist fillLight = null
persist rimLight = null
persist arenaBuilt = false
persist spiderReadyPrinted = false
persist spiderScreenshotSaved = false
persist spiderFrame = 0
persist paused = false
persist orbit = 0.85
persist lastSpace = false
persist lastR = false

function spiderRebuild() {
    if (spider != null) s3DestroyVisuals(spider);
    ikMod = eve.IK();
    math.setRandomSeed(20261106);
    spider = s3Make(ikMod, 0.0, 0.0);
    s3PickWaypoint(spider);
    s3SolveIK(spider);
    s3BuildVisuals(spider);
    arenaBuilt = true;
    spiderScreenshotSaved = false;
    spiderFrame = 0;
}

function spiderUpdateCamera(dt) {
    if (camera == null || spider == null) return;
    orbit += dt * 0.32;
    // Three-quarter view so radial legs + cast shadows both read clearly.
    local radius = 4.6;
    local height = 2.35;
    local cx = spider.x + cos(orbit) * radius;
    local cz = spider.z + sin(orbit) * radius;
    camera.setEye(cx, height, cz);
    camera.setTarget(spider.x, 0.22, spider.z);
}

eve_init <- function() {
    gfx.setBackgroundColor(0.028, 0.034, 0.048, 1.0);
    // Global dir for the default lit path; Light3D below owns the shadow caster.
    gfx.setDirectionalLight(-0.60, -1.0, -0.35, 1.70, 1.45, 1.15);

    if (keyLight == null) {
        keyLight = eve.Light3D();
        keyLight.setType("dir");
        keyLight.setDirection(-0.55, 1.0, -0.35);
        keyLight.setColor(1.0, 0.95, 0.84, 2.8);
        keyLight.setCastShadow(true);
    }
    if (fillLight == null) {
        fillLight = eve.Light3D();
        fillLight.setType("dir");
        fillLight.setDirection(0.70, 0.35, 0.55);
        fillLight.setColor(0.38, 0.50, 0.78, 0.85);
        fillLight.setCastShadow(false);
    }
    if (rimLight == null) {
        rimLight = eve.Light3D();
        rimLight.setType("dir");
        rimLight.setDirection(0.15, 0.25, -0.95);
        rimLight.setColor(0.55, 0.70, 0.95, 0.55);
        rimLight.setCastShadow(false);
    }

    if (camera == null) {
        camera = eve.Camera3D();
        camera.setFov(40.0);
        camera.setUp(0.0, 1.0, 0.0);
        camera.setAmbient(0.07, 0.08, 0.11);
        camera.setClipPlanes(0.05, 80.0);
        camera.setActive(true);
    }

    if (spider == null) spiderRebuild();
    spiderUpdateCamera(0.0);

    if (!spiderReadyPrinted) {
        spiderReadyPrinted = true;
        print("IK_SPIDER3D_READY legs=8 meshes=lit+shadow gait=tetrapod seed=worldPose space=pause r=reseed\n");
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
    if (rDown && !lastR) spiderRebuild();
    lastSpace = spaceDown;
    lastR = rDown;

    if (!paused) {
        s3Update(spider, dt);
        s3SyncVisuals(spider);
    }
    spiderUpdateCamera(dt);
    spiderFrame += 1;

    if (!spiderScreenshotSaved && spider.age > 2.8 && spider.steps >= 12) {
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
    arenaBuilt = false;
    eve_init();
};
