// Conveyor belt: static mesh + scrolling albedo UV, paired with Shape3D tangent velocity.
// Run: make run/linux-debug GAME=examples/conveyor-belt

persist belt = null

function checked(result, ctx) {
    if (!result.ok) throw "conveyor-belt: " + ctx + ": " + result.status.summary;
    return result.value;
}

function makeStripeTexture() {
    local size = 64;
    local image = eve.Image().newEmptyImageData(size, size, "RGBA8");
    for (local y = 0; y < size; ++y) {
        for (local x = 0; x < size; ++x) {
            local stripe = ((x / 8) % 2 == 0);
            if (stripe) image.setPixel(x, y, 0.92, 0.72, 0.18, 1.0);
            else image.setPixel(x, y, 0.18, 0.20, 0.24, 1.0);
        }
    }
    return gfx.newTexture(image, false, false);
}

function syncBody(ent, body) {
    ent.setPosition(body.getX(), body.getY(), body.getZ());
    local x = body.getRotX(), y = body.getRotY(), z = body.getRotZ(), w = body.getRotW();
    local c = 2.0 * (x * w - y * z);
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    ent.setRotation(atan2(2.0 * (x * z + y * w), 1.0 - 2.0 * (x * x + y * y)), asin(c),
                    atan2(2.0 * (x * y + z * w), 1.0 - 2.0 * (x * x + z * z)));
}

function placeCargo(c, index) {
    local x = -4.5 + index * 1.1;
    c.body.setPosition(x, 0.55, 0.0);
    c.body.setRotation(0.0, 0.0, 0.0, 1.0);
    c.body.setLinearVelocity(0.0, 0.0, 0.0);
    c.body.setAngularVelocity(0.0, 0.0, 0.0);
    c.ent.setVisible(true);
    syncBody(c.ent, c.body);
}

function spawnCargo(index) {
    local body = belt.world.newBody("dynamic", 0.0, 0.55, 0.0);
    local shape = body.newBoxShape(0.55, 0.55, 0.55, 1.2, 0.85, 0.05);
    shape.setFriction(0.95);
    body.setLinearDamping(0.05);
    body.setAngularDamping(0.08);
    local tint = (index % 2 == 0) ? [0.25, 0.55, 0.95] : [0.92, 0.35, 0.28];
    local mat = gfx.newMaterial();
    mat.setTint(tint[0], tint[1], tint[2], 1.0);
    mat.setMetallic(0.15);
    mat.setRoughness(0.55);
    local ent = eve.Renderable3D();
    ent.setMesh(belt.cube);
    ent.setMaterial(mat);
    ent.setScale(0.55, 0.55, 0.55);
    ent.setCastShadow(true);
    ent.setReceiveShadow(true);
    local c = { body = body, ent = ent, mat = mat, index = index };
    belt.cargo.push(c);
    placeCargo(c, index);
}

function resetCargo() {
    if (belt.cargo.len() == 0) {
        for (local i = 0; i < 5; ++i) spawnCargo(i);
        return;
    }
    for (local i = 0; i < belt.cargo.len(); ++i) placeCargo(belt.cargo[i], i);
}

function applyBeltSpeed(speed) {
    belt.speed = speed;
    belt.beltShape.setTangentVelocity(speed, 0.0, 0.0);
}

function buildScene() {
    belt = {
        world = null,
        beltShape = null,
        beltEnt = null,
        beltMat = null,
        cube = null,
        cargo = [],
        camera = null,
        speed = 2.5,
        tilesU = 8.0,
        lengthX = 12.0,
        scrollU = 0.0,
        paused = false,
        prevKeys = {},
    };

    belt.cube = gfx.newMeshCube(1.0);
    local stripe = makeStripeTexture();

    belt.beltMat = gfx.newMaterial();
    belt.beltMat.setAlbedoTexture(stripe);
    belt.beltMat.setTint(1.0, 1.0, 1.0, 1.0);
    belt.beltMat.setMetallic(0.05);
    belt.beltMat.setRoughness(0.7);
    checked(belt.beltMat.setAlbedoUvScale(belt.tilesU, 1.0), "belt uv scale");
    checked(belt.beltMat.setAlbedoUvOffset(0.0, 0.0), "belt uv offset");

    belt.beltEnt = eve.Renderable3D();
    belt.beltEnt.setMesh(belt.cube);
    belt.beltEnt.setMaterial(belt.beltMat);
    belt.beltEnt.setPosition(0.0, -0.15, 0.0);
    belt.beltEnt.setScale(belt.lengthX, 0.3, 2.4);
    belt.beltEnt.setCastShadow(true);
    belt.beltEnt.setReceiveShadow(true);

    // Side rails (visual only).
    local railMat = gfx.newMaterial();
    railMat.setTint(0.35, 0.38, 0.42, 1.0);
    railMat.setMetallic(0.4);
    railMat.setRoughness(0.45);
    foreach (z in [-1.35, 1.35]) {
        local rail = eve.Renderable3D();
        rail.setMesh(belt.cube);
        rail.setMaterial(railMat);
        rail.setPosition(0.0, 0.05, z);
        rail.setScale(belt.lengthX, 0.2, 0.12);
        rail.setCastShadow(true);
    }

    local floorMat = gfx.newMaterial();
    floorMat.setTint(0.10, 0.12, 0.15, 1.0);
    floorMat.setRoughness(0.9);
    local floor = eve.Renderable3D();
    floor.setMesh(belt.cube);
    floor.setMaterial(floorMat);
    floor.setPosition(0.0, -0.55, 0.0);
    floor.setScale(18.0, 0.5, 10.0);
    floor.setReceiveShadow(true);

    belt.world = physics.newWorld3D(0.0, -9.81, 0.0, false);
    local beltBody = belt.world.newBody("static", 0.0, -0.15, 0.0);
    belt.beltShape = beltBody.newBoxShape(belt.lengthX, 0.3, 2.4, 1.0, 0.95, 0.0);
    belt.beltShape.setFriction(0.95);
    applyBeltSpeed(belt.speed);

    // Catch floor so cargo does not fall forever at the end.
    local pit = belt.world.newBody("static", 0.0, -0.55, 0.0);
    pit.newBoxShape(18.0, 0.5, 10.0, 1.0, 0.6, 0.05);

    resetCargo();

    belt.camera = eve.Camera3D();
    belt.camera.setEye(0.0, 7.5, 11.0);
    belt.camera.setTarget(0.0, 0.4, 0.0);
    belt.camera.setUp(0.0, 1.0, 0.0);
    belt.camera.setFov(48.0);
    belt.camera.setAmbient(0.32, 0.36, 0.42);
    belt.camera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, 0.25, 1.55, 1.45, 1.25);
}

function edge(name) {
    local down = keyboard.isDown(name);
    local was = (name in belt.prevKeys) ? belt.prevKeys[name] : false;
    belt.prevKeys[name] <- down;
    return down && !was;
}

eve_init <- function() {
    gfx.setBackgroundColor(0.06, 0.08, 0.11, 1.0);
    buildScene();
    print("conveyor-belt: [ ] speed  R reset  Space pause\n");
};

eve_update <- function(dt) {
    if (belt == null) return;
    if (edge("space") || edge(" ")) belt.paused = !belt.paused;
    if (edge("r") || edge("R")) resetCargo();
    if (edge("]") || edge("=")) applyBeltSpeed(belt.speed + 0.5);
    if (edge("[") || edge("-")) applyBeltSpeed(belt.speed - 0.5);
    if (belt.speed > 8.0) applyBeltSpeed(8.0);
    if (belt.speed < -8.0) applyBeltSpeed(-8.0);

    if (!belt.paused) {
        // Match visual scroll to physical transport direction (see README).
        local du = -belt.speed * belt.tilesU / belt.lengthX * dt;
        belt.scrollU += du;
        // Keep offset bounded so float precision stays sane; REPEAT wraps anyway.
        if (belt.scrollU > 1000.0 || belt.scrollU < -1000.0) belt.scrollU = belt.scrollU % 1.0;
        checked(belt.beltMat.setAlbedoUvOffset(belt.scrollU, 0.0), "scroll");
        belt.world.update(dt);
    }

    foreach (c in belt.cargo) {
        syncBody(c.ent, c.body);
        // Recycle cargo that falls off the far end.
        if (c.body.getX() > belt.lengthX * 0.5 + 1.5 || c.body.getY() < -2.0) {
            placeCargo(c, c.index);
        }
    }
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
};
