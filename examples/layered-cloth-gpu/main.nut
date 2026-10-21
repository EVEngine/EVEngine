// Real Vulkan compute; update and draw never read simulation vertices back.
persist clothModule = null
persist compute = null
persist sheets = null
persist supportBar = null
persist accumulator = 0.0
persist simulationTime = 0.0
persist previousKeys = {}
persist paused = false
persist windOn = true
persist contactsOn = true
function pressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !was;
}
eve_init = function() {
    clothModule = eve.Cloth();
    compute = eve.Gpgpu();
    sheets = eve.LayeredClothGPU(compute, gfx, 3, 20, 16, 0.12, 0.09, -1.14, 2.6, -0.9);
    sheets.setSolver(2, 8);
    sheets.setContact(0.05, 0.25);
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
    supportBar.setPosition(0.0, 2.9, -0.9);
    supportBar.setTint(0.35, 0.38, 0.44, 1.0);
    supportBar.setRoughness(0.8);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.25, 1.15, 1.0);
    print("GPU layered cloth: 960 particles, resident vertices | C contacts | W wind | P pause | R reset\n");
};
eve_update = function(dt) {
    if (pressed("C")) { contactsOn = !contactsOn; sheets.setInterCollision(contactsOn); }
    if (pressed("W")) windOn = !windOn;
    if (pressed("P")) paused = !paused;
    if (pressed("R")) { sheets.reset(); accumulator = 0.0; simulationTime = 0.0; }
    if (paused) return;
    accumulator += dt;
    local steps = 0;
    while (accumulator >= 1.0 / 60.0 && steps < 4) {
        simulationTime += 1.0 / 60.0;
        sheets.setWind(windOn ? math.polarY(0.6, simulationTime) : 0.0, 0.0, windOn ? 0.3 : 0.0);
        sheets.update(1.0 / 60.0);
        accumulator -= 1.0 / 60.0;
        steps++;
    }
};
eve_render = function() {
    gfx.clear();
    gfx.render3D();
    sheets.draw();
};
