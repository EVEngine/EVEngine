// Interactive shallow-water lake with character-leg capsules.
// GPU Saint-Venant solver via eve.Fluids().newShallowWater (CPU fallback).

persist fluids = null
persist water = null
persist waterMesh = null
persist waterObj = null
persist ground = null
persist cam = null
persist leftLeg = null
persist rightLeg = null
persist body = null
persist paused = false
persist prevKeys = {}
persist prevMouse = false
persist characterX = 4.0
persist characterZ = 4.0
persist characterVx = 0.0
persist characterVz = 0.0
persist elapsed = 0.0
persist hud = ""

const RES = 64;
const CELL = 0.2;
const LAKE = 12.8; // RES * CELL
const WATER_DEPTH = 0.55;
const LEG_RADIUS = 0.11;
const LEG_HALF = 0.42;
const MOVE_SPEED = 2.4;

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in prevKeys) ? prevKeys[name] : false;
    prevKeys[name] <- down;
    return down && !was;
}

function mouseClicked() {
    local down = mouse.isDown(1);
    local clicked = down && !prevMouse;
    prevMouse = down;
    return clicked;
}

function requireOk(result, label) {
    if (result == null || !result.ok) {
        local detail = "";
        if (result != null && ("status" in result) && result.status != null && ("summary" in result.status))
            detail = " (" + result.status.summary + ")";
        throw label + " failed" + detail;
    }
    return result;
}

function syncWaterMesh() {
    local built = requireOk(water.buildSurfaceMesh(), "buildSurfaceMesh");
    local mesh = built.value;
    if (waterMesh == null) {
        waterMesh = gfx.newMeshFromArrays(mesh.positions, mesh.normals, mesh.uvs,
                                          mesh.vertexCount, mesh.indices, mesh.indexCount);
        waterObj.setMesh(waterMesh);
    } else if (!gfx.updateMeshVertices(waterMesh, mesh.positions, mesh.normals, mesh.uvs,
                                       mesh.vertexCount, mesh.indices, mesh.indexCount)) {
        throw "updateMeshVertices failed";
    }
}

function resetLake() {
    requireOk(water.fillDepth(WATER_DEPTH), "fillDepth");
    characterX = LAKE * 0.35;
    characterZ = LAKE * 0.45;
    characterVx = 0.0;
    characterVz = 0.0;
    requireOk(water.addImpulse(LAKE * 0.65, LAKE * 0.55, 1.4, 0.28), "seedImpulse");
    syncWaterMesh();
}

function updateLegs(dt) {
    local surface = water.surfaceHeightAt(characterX, characterZ);
    local hipY = surface + 0.15;
    local stride = sin(elapsed * 8.0) * 0.18;
    local leftX = characterX - 0.16;
    local rightX = characterX + 0.16;
    local leftZ = characterZ + stride;
    local rightZ = characterZ - stride;
    local leftY = hipY - LEG_HALF;
    local rightY = hipY - LEG_HALF;

    requireOk(water.setColliderCapsule(1, leftX, leftY, leftZ, LEG_HALF, LEG_RADIUS,
                                       characterVx, 0.0, characterVz, 1.35, 1.1), "leftLeg");
    requireOk(water.setColliderCapsule(2, rightX, rightY, rightZ, LEG_HALF, LEG_RADIUS,
                                       characterVx, 0.0, characterVz, 1.35, 1.1), "rightLeg");

    if (leftLeg != null) {
        leftLeg.setPosition(leftX, leftY, leftZ);
        leftLeg.setScale(LEG_RADIUS * 2.0, LEG_HALF * 2.0 + LEG_RADIUS * 2.0, LEG_RADIUS * 2.0);
    }
    if (rightLeg != null) {
        rightLeg.setPosition(rightX, rightY, rightZ);
        rightLeg.setScale(LEG_RADIUS * 2.0, LEG_HALF * 2.0 + LEG_RADIUS * 2.0, LEG_RADIUS * 2.0);
    }
    if (body != null) {
        body.setPosition(characterX, hipY + 0.55, characterZ);
    }
}

function screenToLake(mx, my) {
    local nx = mx / gfx.getWidth().tofloat();
    local ny = my / gfx.getHeight().tofloat();
    local x = nx * LAKE;
    local z = (1.0 - ny) * LAKE;
    if (x < 0.2) x = 0.2;
    if (z < 0.2) z = 0.2;
    if (x > LAKE - 0.2) x = LAKE - 0.2;
    if (z > LAKE - 0.2) z = LAKE - 0.2;
    return [x, z];
}

eve_init = function() {
    print("examples/interactive-shallow-water ready\n");
    gfx.setBackgroundColor(0.45, 0.62, 0.78, 1.0);
    if (fluids == null) fluids = eve.Fluids();
    if (water == null) {
        water = fluids.newShallowWater(RES, RES, CELL, true);
        if (water == null) throw "newShallowWater failed";
    }

    if (cam == null) {
        cam = eve.Camera3D();
        cam.setEye(LAKE * 0.55, 9.5, LAKE * 1.35);
        cam.setTarget(LAKE * 0.5, 0.2, LAKE * 0.45);
        cam.setUp(0.0, 1.0, 0.0);
        cam.setFov(42.0);
        cam.setAmbient(0.35, 0.4, 0.45);
        cam.setActive(true);
        gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.15, 1.1, 1.0);
    }

    if (ground == null) {
        ground = eve.Renderable3D();
        ground.setMesh(gfx.newMeshCube(1.0));
        ground.setScale(LAKE + 2.0, 0.12, LAKE + 2.0);
        ground.setPosition(LAKE * 0.5, -0.12, LAKE * 0.5);
        ground.setTint(0.22, 0.28, 0.2, 1.0);
        ground.setCastShadow(false);
    }

    if (waterObj == null) {
        waterObj = eve.Renderable3D();
        waterObj.setTint(0.18, 0.48, 0.72, 0.78);
        waterObj.setMetallic(0.05);
        waterObj.setRoughness(0.18);
        waterObj.setCastShadow(false);
    }

    if (body == null) {
        body = eve.Renderable3D();
        body.setMesh(gfx.newMeshCube(1.0));
        body.setScale(0.45, 0.7, 0.28);
        body.setTint(0.85, 0.55, 0.35, 1.0);
    }
    if (leftLeg == null) {
        leftLeg = eve.Renderable3D();
        leftLeg.setMesh(gfx.newMeshCube(1.0));
        leftLeg.setTint(0.35, 0.3, 0.28, 1.0);
    }
    if (rightLeg == null) {
        rightLeg = eve.Renderable3D();
        rightLeg.setMesh(gfx.newMeshCube(1.0));
        rightLeg.setTint(0.35, 0.3, 0.28, 1.0);
    }

    resetLake();
    updateLegs(0.0);
    local backend = water.usingGpu() ? "GPU" : (water.usedBackendFallback() ? "CPU-fallback" : "CPU");
    hud = "Shallow water (" + backend + ")  WASD move  LMB splash  Space pause  R reset";
    print(hud + "\n");
};

eve_update = function(dt) {
    // Cap hitch frames; ignore non-positive ticks from the host clock.
    if (dt <= 0.0) return;
    if (dt > 0.05) dt = 0.05;
    if (edgePressed("space") || edgePressed("Space")) paused = !paused;
    if (edgePressed("r") || edgePressed("R")) resetLake();
    if (edgePressed("g") || edgePressed("G")) {
        local backend = water.usingGpu() ? "GPU" : (water.usedBackendFallback() ? "CPU-fallback" : "CPU");
        print("shallow-water backend: " + backend + "\n");
    }

    characterVx = 0.0;
    characterVz = 0.0;
    if (!paused) {
        if (keyboard.isDown("a") || keyboard.isDown("A") || keyboard.isDown("Left"))
            characterVx -= MOVE_SPEED;
        if (keyboard.isDown("d") || keyboard.isDown("D") || keyboard.isDown("Right"))
            characterVx += MOVE_SPEED;
        if (keyboard.isDown("w") || keyboard.isDown("W") || keyboard.isDown("Up"))
            characterVz -= MOVE_SPEED;
        if (keyboard.isDown("s") || keyboard.isDown("S") || keyboard.isDown("Down"))
            characterVz += MOVE_SPEED;
        characterX += characterVx * dt;
        characterZ += characterVz * dt;
        if (characterX < 0.6) characterX = 0.6;
        if (characterZ < 0.6) characterZ = 0.6;
        if (characterX > LAKE - 0.6) characterX = LAKE - 0.6;
        if (characterZ > LAKE - 0.6) characterZ = LAKE - 0.6;

        if (mouseClicked()) {
            local p = screenToLake(mouse.getX(), mouse.getY());
            requireOk(water.addImpulse(p[0], p[1], 0.9, 0.32), "mouseImpulse");
        }

        elapsed += dt;
        updateLegs(dt);
        requireOk(water.step(dt), "step");
        syncWaterMesh();
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
