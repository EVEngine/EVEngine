// Three coupled cloth sheets. Fixed-step accumulator preserves elapsed time.
persist clothModule = null
persist sheets = null
persist supportBar = null
persist accumulator = 0.0
persist simulationTime = 0.0
persist previousKeys = {}
persist paused = false
persist windOn = true
persist reportTime = 0.0

function pressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !was;
}
eve_init = function() {
    clothModule = eve.Cloth();
    sheets = eve.LayeredCloth3D(3, 10, 10, 0.25, 0.10, -1.1, 2.6, -1.1);
    sheets.setSolver(4, 3);
    gfx.setBackgroundColor(0.055, 0.07, 0.10, 1.0);
    camera = eve.Camera3D();
    camera.setEye(4.5, 4.0, 5.5);
    camera.setTarget(0.0, 1.9, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.4, 0.4, 0.45);
    camera.setActive(true);
    supportBar = eve.Renderable3D();
    supportBar.setMesh(gfx.newMeshCube(1.0));
    supportBar.setScale(2.6, 0.06, 0.10);
    supportBar.setPosition(0.025, 2.9, -1.1);
    supportBar.setTint(0.35, 0.38, 0.44, 1.0);
    supportBar.setRoughness(0.8);

    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.25, 1.15, 1.0);
    print("layered-cloth: C inter collision | W wind | P pause | R reset\n");
};
eve_update = function(dt) {
    if (pressed("C")) {
        sheets.setInterCollision(!sheets.getInterCollision());
        print("inter collision: " + sheets.getInterCollision() + "\n");
    }
    if (pressed("W")) windOn = !windOn;
    if (pressed("P")) paused = !paused;
    if (pressed("R")) { sheets.reset(); accumulator = 0.0; simulationTime = 0.0; }
    if (paused) return;
    accumulator += dt;
    // Limit work per rendered frame while retaining the remaining backlog.
    local steps = 0;
    while (accumulator >= 1.0 / 120.0 && steps < 12) {
        simulationTime += 1.0 / 120.0;
        sheets.setWindVelocity(windOn ? math.polarY(1.3, simulationTime * 1.2) : 0.0,
                               windOn ? 0.5 : 0.0, windOn ? 0.4 : 0.0);
        sheets.update(1.0 / 120.0);
        accumulator -= 1.0 / 120.0;
        steps++;
    }
    reportTime += dt;
    if (reportTime > 2.0) {
        print("layered-cloth contacts: " + sheets.getContactCount() + "\n");
        reportTime = 0.0;
    }
};
eve_render = function() {
    gfx.clear();
    gfx.render3D();
    sheets.draw(gfx);
};
