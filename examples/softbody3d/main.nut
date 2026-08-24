// ============================================================================
// Softbody 3D — interactive Verlet cloth (Cloth3D).
//
// 3D cloth in meter space (+Y up), grid in the XZ plane, top row pinned.
// Self-collision + dihedral fold-angle limit + collision with a Box3D world
// (static box platform + sphere), plus Fluid-style interactAt pointer field.
//
// Controls:
//   Left drag — grab / drag cloth
//   Space     — toggle wind
//   C         — toggle self-collision
//   F         — toggle fold-angle limit
//   R         — reset cloth pose
//
// Run: make run/linux-debug GAME=examples/softbody3d
// ============================================================================

if (!("physics" in getroottable())) physics <- null;
if (!("world3" in getroottable())) world3 <- null;
if (!("cloth" in getroottable())) cloth <- null;
if (!("grabbing" in getroottable())) grabbing <- false;
if (!("windOn" in getroottable())) windOn <- true;
if (!("selfCollisionOn" in getroottable())) selfCollisionOn <- true;
if (!("foldOn" in getroottable())) foldOn <- true;
if (!("windT" in getroottable())) windT <- 0.0;
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("prevMouse" in getroottable())) prevMouse <- false;

const CLOTH_TOP_Y = 3.2;   // pinned top row height (meters)
const GRAB_HEIGHT  = 2.0;  // ray-plane height used for mouse grab

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function mousePressed() {
    // 1 = 左键（与 engine mouse::isDown 一致）。
    local down = mouse.isDown(1);
    local was = prevMouse;
    prevMouse = down;
    return down && !was;
}

function buildScene() {
    if (physics == null) physics = eve.Physics();

    if (world3 == null) {
        world3 = physics.newWorld3D(0.0, -9.8, 0.0, true);
        // Ground (top at y = 0).
        local ground = world3.newBody("static", 0.0, -0.5, 0.0);
        ground.newBoxShape(12.0, 1.0, 12.0, 1.0, 0.2, 0.0);
        // A box platform the curtain drapes over.
        local box = world3.newBody("static", 0.0, 0.4, 0.0);
        box.newBoxShape(2.2, 0.8, 2.2, 1.0, 0.2, 0.0);
        // A sphere obstacle beside it.
        local ball = world3.newBody("static", 1.4, 0.5, 1.2);
        ball.newSphereShape(0.5, 1.0, 0.2, 0.0);
    }

    if (cloth == null) {
        cloth = physics.newCloth3D(16, 12, 0.4, -3.0, CLOTH_TOP_Y, -2.0);
        cloth.setGravity(0.0, -9.8, 0.0);
        cloth.setStiffness(0.9);
        cloth.setIterations(5);
        cloth.setParticleSize(0.12);
        cloth.setSelfCollision(true);
        cloth.setFoldStiffness(0.8);
        cloth.setMaxFoldAngle(130.0);
        cloth.setBounds(-4.0, -1.0, -3.0, 8.0, 5.5, 6.0);
        cloth.setColor(0.72, 0.80, 0.96, 1.0);
        cloth.setCollideWorld(world3);
    }
}

function resetScene() {
    if (cloth) {
        cloth.reset();
        grabbing = false;
    }
}

// Intersect the mouse ray with a horizontal plane (y = GRAB_HEIGHT).
function grabPoint() {
    local mx = mouse.getX();
    local my = mouse.getY();
    camera.screenToRay(mx, my, gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    local oy = camera.getScreenRayOriginY();
    local dy = camera.getScreenRayDirY();
    local out = null;
    if (dy < -0.0001) {
        local t = (GRAB_HEIGHT - oy) / dy;
        if (t > 0.0) {
            out = [
                camera.getScreenRayOriginX() + camera.getScreenRayDirX() * t,
                GRAB_HEIGHT,
                camera.getScreenRayOriginZ() + camera.getScreenRayDirZ() * t
            ];
        }
    }
    return out;
}

eve_init = function() {
    gfx.setBackgroundColor(0.07, 0.09, 0.12, 1.0);
    camera = eve.Camera3D();
    camera.setEye(6.5, 5.2, 8.0);
    camera.setTarget(0.0, 1.6, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(50.0);
    camera.setAmbient(0.30, 0.32, 0.36);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.25, 1.15, 1.0);

    buildScene();
    print("softbody3d: left-drag grab | Space wind | C self-collision | F fold | R reset\n");
};

eve_reload <- function() {
    buildScene();
};

eve_update = function(dt) {
    if (cloth == null) return;

    if (edgePressed("Space")) windOn = !windOn;
    if (edgePressed("C")) {
        selfCollisionOn = !selfCollisionOn;
        cloth.setSelfCollision(selfCollisionOn);
        print("self-collision: " + (selfCollisionOn ? "ON" : "off") + "\n");
    }
    if (edgePressed("F")) {
        foldOn = !foldOn;
        cloth.setFoldStiffness(foldOn ? 0.8 : 0.0);
        print("fold limit: " + (foldOn ? "ON" : "off") + "\n");
    }
    if (edgePressed("R") || edgePressed("r")) resetScene();

    if (mousePressed()) {
        local pt = grabPoint();
        if (pt) {
            local idx = cloth.grabAt(pt[0], pt[1], pt[2], 0.5);
            grabbing = idx >= 0;
        }
    }
    if (mouse.isDown(1) && grabbing) {
        local pt = grabPoint();
        if (pt) cloth.moveGrab(pt[0], pt[1], pt[2]);
    } else if (grabbing) {
        cloth.releaseGrab();
        grabbing = false;
    }

    if (windOn) {
        windT += dt;
        cloth.applyForce(math.polarY(1.8, windT * 1.3), 0.0, math.polarX(0.6, windT * 0.9));
    }

    // Pointer field: right mouse repels nearby particles (Fluid-style).
    if (mouse.isDown(2)) {
        local pt = grabPoint();
        if (pt) cloth.interactAt(pt[0], pt[1], pt[2], 1.1, -14.0);
    }

    if (world3) world3.update(dt);
    cloth.update(dt);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    cloth.draw(gfx);
};
