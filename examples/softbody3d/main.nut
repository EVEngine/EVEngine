// ============================================================================
// Softbody 3D — interactive Verlet cloth (Cloth3D).
//
// 3D cloth in meter space (+Y up), grid in the XZ plane, top row pinned.
// Self-collision + dihedral fold-angle limit + collision with a Box3D world
// (static box platform + sphere), plus Fluid2D-style interactAt pointer field.
//
// Controls:
//   Left drag — grab / drag cloth
//   C         — toggle self-collision
//   F         — toggle fold-angle limit
//   R         — reset cloth pose
//
// Run: make run/linux-debug GAME=examples/softbody3d
// ============================================================================

persist physics = null
persist clothModule = null
persist world3 = null
persist clothBody = null
persist jelly = null
persist jellyRenderer = null
persist grabbing = false
persist selfCollisionOn = true
persist foldOn = true
persist windT = 0.0
persist prevKeys = {}
persist prevMouse = false

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
    if (clothModule == null) clothModule = eve.Cloth();

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

    if (clothBody == null) {
        clothBody = clothModule.newCloth3D(16, 12, 0.4, -3.0, CLOTH_TOP_Y, -2.0);
        clothBody.setGravity(0.0, -9.8, 0.0);
        clothBody.setStiffness(0.9);
        clothBody.setIterations(5);
        clothBody.setParticleSize(0.12);
        clothBody.setWindVelocity(1.8, 1.2, 0.6);
        clothBody.setAerodynamics(1.225, 1.1, 0.2);
        clothBody.setSelfCollision(true);
        clothBody.setFoldStiffness(0.8);
        clothBody.setMaxFoldAngle(130.0);
        clothBody.setBounds(-4.0, -1.0, -3.0, 8.0, 5.5, 6.0);
        clothBody.setColor(0.72, 0.80, 0.96, 1.0);
        clothBody.setCollideWorld(world3);
    }

    // Volumetric soft body: overlapping shape-matching clusters preserve the
    // local cube volume while still allowing squash, wobble and plasticity.
    if (jelly == null) {
        jelly = physics.newSoftBody3D(4, 4, 4, 0.38, 0.9, 2.2, -0.6);
        jelly.setGravity(0.0, -9.8, 0.0);
        jelly.setDeformationResistance(0.78);
        jelly.setIterations(6);
        jelly.setDamping(0.035);
        jelly.setParticleRadius(0.11);
        jelly.setPlasticity(0.32, 0.18, 0.25, 0.45);
        jelly.setBounds(-4.0, 0.0, -3.0, 8.0, 5.5, 6.0);
        jelly.setCollideWorld(world3);
    }
    if (jellyRenderer == null && jelly != null) {
        jellyRenderer = physics.newSoftBody3DRenderer(jelly);
        jellyRenderer.setColor(0.96, 0.36, 0.20, 1.0);
    }
}

function resetScene() {
    if (clothBody) {
        clothBody.reset();
        grabbing = false;
    }
    if (jelly) jelly.reset();
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
    print("softbody3d: left-drag grab | C self-collision | F fold | R reset\n");
};

eve_reload <- function() {
    buildScene();
};

eve_update = function(dt) {
    if (clothBody == null) return;

    windT += dt;

    if (edgePressed("C")) {
        selfCollisionOn = !selfCollisionOn;
        clothBody.setSelfCollision(selfCollisionOn);
        print("self-collision: " + (selfCollisionOn ? "ON" : "off") + "\n");
    }
    if (edgePressed("F")) {
        foldOn = !foldOn;
        clothBody.setFoldStiffness(foldOn ? 0.8 : 0.0);
        print("fold limit: " + (foldOn ? "ON" : "off") + "\n");
    }
    if (edgePressed("R") || edgePressed("r")) resetScene();

    if (mousePressed()) {
        local pt = grabPoint();
        if (pt) {
            local idx = clothBody.grabAt(pt[0], pt[1], pt[2], 0.5);
            grabbing = idx >= 0;
        }
    }
    if (mouse.isDown(1) && grabbing) {
        local pt = grabPoint();
        if (pt) clothBody.moveGrab(pt[0], pt[1], pt[2]);
    } else if (grabbing) {
        clothBody.releaseGrab();
        grabbing = false;
    }

    // Pointer field: right mouse repels nearby particles (Fluid2D-style).
    if (mouse.isDown(2)) {
        local pt = grabPoint();
        if (pt) clothBody.interactAt(pt[0], pt[1], pt[2], 1.1, -14.0);
    }

    if (world3) world3.update(dt);
    clothBody.update(dt);
    if (jelly) {
        // A small alternating lateral load keeps the volume response visible.
        jelly.applyForce(math.polarY(0.055, windT * 1.7), 0.0, 0.0);
        jelly.update(dt);
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    clothBody.draw(gfx);
    if (jellyRenderer) jellyRenderer.draw(gfx);
};
