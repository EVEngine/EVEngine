// ============================================================================
// Interactive cloth + fluid demo (physics module).
//
// Left mouse  — drag cloth / stir fluid (repel)
// Right mouse — attract fluid
// Space       — emit a burst of fluid at the cursor
// R           — reset scene
//
// Run: make run/linux-debug GAME=examples/softbody
// ============================================================================

persist physics = null
persist cloth = null
persist fluid = null
persist grabbing = false
persist prevKeys = {}
persist windT = 0.0

function edgePressed(key, down) {
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function resetScene() {
    local w = config.width * 1.0;
    local h = config.height * 1.0;

    cloth = physics.newCloth(18, 12, 14.0, 48.0, 36.0);
    cloth.setGravity(0.0, 980.0);
    cloth.setStiffness(0.9);
    cloth.setIterations(5);
    cloth.setBounds(0.0, 0.0, w, h);
    cloth.setColor(0.78, 0.84, 0.98, 1.0);

    fluid = physics.newFluid2D(600);
    fluid.setBounds(w * 0.52, 48.0, w * 0.42, h - 96.0);
    fluid.setGravity(0.0, 980.0);
    fluid.setSmoothingRadius(18.0);
    fluid.setRestDensity(4.0);
    fluid.setPressureStiffness(0.55);
    fluid.setNearPressureStiffness(0.45);
    fluid.setViscosity(0.14);
    fluid.setParticleSize(5.0);
    fluid.setColor(0.22, 0.55, 0.95, 0.88);
    fluid.emit(w * 0.72, 100.0, 160, 0.0, 40.0);

    grabbing = false;
}

eve_init = function() {
    gfx.setBackgroundColor(0.07, 0.08, 0.11, 1.0);
    if (typeof physics != "instance") physics = eve.Physics();
    physics.setMeter(30.0);
    if (typeof cloth != "instance" || typeof fluid != "instance")
        resetScene();
    print("softbody: left-drag cloth/fluid | right-drag attract | Space emit | R reset\n");
};

eve_reload <- function() {
};

eve_update = function(dt) {
    if (typeof cloth != "instance" || typeof fluid != "instance") return;

    local mx = mouse.getX();
    local my = mouse.getY();
    // 1 = 左键，2 = 右键（与 engine mouse::isDown 一致）。
    local left = mouse.isDown(1);
    local right = mouse.isDown(2);

    // Cloth grab on the left half; fluid interact on the tank.
    local tankX = config.width * 0.52;
    local overFluid = mx >= tankX;

    if (overFluid) {
        if (grabbing) {
            cloth.releaseGrab();
            grabbing = false;
        }
        if (left)
            fluid.interactAt(mx, my, 70.0, -4200.0);
        else if (right)
            fluid.interactAt(mx, my, 80.0, 3800.0);
    } else {
        if (left) {
            if (!grabbing) {
                local idx = cloth.grabAt(mx, my, 28.0);
                grabbing = idx >= 0;
            }
            if (grabbing)
                cloth.moveGrab(mx, my);
        } else if (grabbing) {
            cloth.releaseGrab();
            grabbing = false;
        }
    }

    if (edgePressed("space", keyboard.isDown("Space")))
        fluid.emit(mx, my, 18, 0.0, 120.0);

    if (edgePressed("r", keyboard.isDown("R")) || edgePressed("r2", keyboard.isDown("r")))
        resetScene();

    // Gentle wind on the cloth.
    windT += dt;
    cloth.applyForce(math.polarX(420.0, windT * 1.4), 0.0);

    cloth.update(dt);
    fluid.update(dt);
};

eve_render = function() {
    gfx.clear();

    local w = config.width * 1.0;
    local h = config.height * 1.0;
    local tankX = w * 0.52;
    local tankY = 48.0;
    local tankW = w * 0.42;
    local tankH = h - 96.0;

    // Solid 2D draws currently keep the first fragment at overlapping pixels,
    // so submit foreground before the panels behind it.
    cloth.draw(gfx);
    fluid.draw(gfx);

    gfx.drawSolidRect(16.0, h - 28.0, 8.0 + cloth.getParticleCount() * 0.35, 10.0,
                      0.7, 0.78, 0.95, 1.0);
    gfx.drawSolidRect(tankX + 12.0, h - 28.0, 8.0 + fluid.getParticleCount() * 0.3, 10.0,
                      0.25, 0.55, 0.95, 1.0);
    gfx.drawSolidRect(16.0, h - 28.0, 220.0, 10.0, 0.2, 0.22, 0.28, 1.0);
    gfx.drawSolidRect(tankX + 12.0, h - 28.0, 220.0, 10.0, 0.2, 0.22, 0.28, 1.0);

    gfx.drawSolidRect(0.0, 0.0, tankX - 8.0, h, 0.09, 0.10, 0.13, 1.0);
    gfx.drawSolidRect(tankX, tankY, tankW, tankH, 0.11, 0.13, 0.17, 1.0);
};
