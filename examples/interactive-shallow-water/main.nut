// Interactive shallow-water lake with character-leg capsules.
// Water albedo is the field texture: R=depth, G=vx, B=vz, A=speed.
// Toggle F for shaded water look; mesh heights use visualHeightGain.

persist fluids = null
persist imageApi = null
persist water = null
persist waterMesh = null
persist waterObj = null
persist fieldImage = null
persist fieldTex = null
persist albedoImage = null
persist albedoTex = null
persist normalImage = null
persist normalTex = null
persist fieldBoard = null
persist fieldBoardVert = null
persist ground = null
persist cam = null
persist leftLeg = null
persist rightLeg = null
persist body = null
persist paused = false
persist showFieldOnWater = true
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
const LAKE = 12.8;
const WATER_DEPTH = 0.55;
const LEG_RADIUS = 0.11;
const LEG_HALF = 0.42;
const MOVE_SPEED = 2.4;
const VEL_SCALE = 1.0;
const DEPTH_CONTRAST = 0.08;
const VISUAL_GAIN = 8.0;

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

function makeUvPlane(size) {
    local half = size * 0.5;
    local pos = [
        -half, 0.0, -half,
         half, 0.0, -half,
         half, 0.0,  half,
        -half, 0.0,  half
    ];
    local nrm = [
        0.0, 1.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 1.0, 0.0,
        0.0, 1.0, 0.0
    ];
    local uv = [
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0
    ];
    local idx = [0, 1, 2, 0, 2, 3];
    return gfx.newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

function applyWaterLook() {
    if (showFieldOnWater) {
        // Unlit channel viz: R=depth anomaly, G=vx, B=vz (A unused by opaque albedo).
        // Keep a flat normal map — script bindings reject null Texture*.
        waterObj.setTexture(fieldTex);
        waterObj.setNormalTexture(normalTex);
        waterObj.setMetallic(0.0);
        waterObj.setRoughness(1.0);
        waterObj.setReceiveLight(false);
        waterObj.setTint(1.0, 1.0, 1.0, 1.0);
    } else {
        waterObj.setTexture(albedoTex);
        waterObj.setNormalTexture(normalTex);
        waterObj.setMetallic(0.02);
        waterObj.setRoughness(0.16);
        waterObj.setReceiveLight(true);
        waterObj.setTint(1.0, 1.0, 1.0, 1.0);
    }
}

function syncWaterTextures() {
    requireOk(water.copyFieldToImage(fieldImage, VEL_SCALE, DEPTH_CONTRAST), "copyFieldToImage");
    requireOk(water.copyShadedAlbedoToImage(albedoImage, WATER_DEPTH, VEL_SCALE), "copyShadedAlbedoToImage");
    requireOk(water.copyNormalToImage(normalImage, 22.0), "copyNormalToImage");
    gfx.updateTextureFromImageData(fieldTex, fieldImage);
    gfx.updateTextureFromImageData(albedoTex, albedoImage);
    gfx.updateTextureFromImageData(normalTex, normalImage);
    applyWaterLook();
    if (fieldBoard != null) {
        fieldBoard.setTexture(fieldTex);
        fieldBoard.setTint(1.0, 1.0, 1.0, 1.0);
    }
    if (fieldBoardVert != null) {
        fieldBoardVert.setTexture(fieldTex);
        fieldBoardVert.setTint(1.0, 1.0, 1.0, 1.0);
    }
}

function syncWaterMesh() {
    local built = requireOk(water.buildSurfaceMesh(VISUAL_GAIN), "buildSurfaceMesh");
    local mesh = built.value;
    if (waterMesh == null) {
        waterMesh = gfx.newMeshFromArrays(mesh.positions, mesh.normals, mesh.uvs,
                                          mesh.vertexCount, mesh.indices, mesh.indexCount);
        waterObj.setMesh(waterMesh);
    } else if (!gfx.updateMeshVertices(waterMesh, mesh.positions, mesh.normals, mesh.uvs,
                                       mesh.vertexCount, mesh.indices, mesh.indexCount)) {
        throw "updateMeshVertices failed";
    }
    syncWaterTextures();
}

function resetLake() {
    requireOk(water.fillDepth(WATER_DEPTH), "fillDepth");
    characterX = LAKE * 0.35;
    characterZ = LAKE * 0.45;
    characterVx = 0.0;
    characterVz = 0.0;
    requireOk(water.addImpulse(LAKE * 0.65, LAKE * 0.55, 2.2, 0.22), "seedImpulse");
    syncWaterMesh();
}

function updateLegs(dt) {
    local surface = water.surfaceHeightAt(characterX, characterZ);
    local visualSurface = WATER_DEPTH + (surface - WATER_DEPTH) * VISUAL_GAIN;
    local physHipY = surface + 0.15;
    local visHipY = visualSurface + 0.15;
    local stride = sin(elapsed * 8.0) * 0.18;
    local leftX = characterX - 0.16;
    local rightX = characterX + 0.16;
    local leftZ = characterZ + stride;
    local rightZ = characterZ - stride;
    local physLeftY = physHipY - LEG_HALF;
    local physRightY = physHipY - LEG_HALF;
    local visLeftY = visHipY - LEG_HALF;
    local visRightY = visHipY - LEG_HALF;

    requireOk(water.setColliderCapsule(1, leftX, physLeftY, leftZ, LEG_HALF, LEG_RADIUS,
                                       characterVx, 0.0, characterVz, 0.85, 0.55), "leftLeg");
    requireOk(water.setColliderCapsule(2, rightX, physRightY, rightZ, LEG_HALF, LEG_RADIUS,
                                       characterVx, 0.0, characterVz, 0.85, 0.55), "rightLeg");

    if (leftLeg != null) {
        leftLeg.setPosition(leftX, visLeftY, leftZ);
        leftLeg.setScale(LEG_RADIUS * 2.0, LEG_HALF * 2.0 + LEG_RADIUS * 2.0, LEG_RADIUS * 2.0);
    }
    if (rightLeg != null) {
        rightLeg.setPosition(rightX, visRightY, rightZ);
        rightLeg.setScale(LEG_RADIUS * 2.0, LEG_HALF * 2.0 + LEG_RADIUS * 2.0, LEG_RADIUS * 2.0);
    }
    if (body != null) {
        body.setPosition(characterX, visHipY + 0.55, characterZ);
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
    gfx.setBackgroundColor(0.18, 0.20, 0.24, 1.0);
    if (fluids == null) fluids = eve.Fluids();
    if (imageApi == null) imageApi = eve.Image();
    if (water == null) {
        water = fluids.newShallowWater(RES, RES, CELL, true);
        if (water == null) throw "newShallowWater failed";
    }

    if (fieldImage == null) {
        fieldImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        albedoImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        normalImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        // Loud seed so a missing GPU upload is obvious (magenta/yellow checker).
        for (local z = 0; z < RES; ++z) {
            for (local x = 0; x < RES; ++x) {
                local checker = ((x / 8) + (z / 8)) % 2;
                if (checker == 0)
                    fieldImage.setPixel(x, z, 1.0, 0.0, 1.0, 1.0);
                else
                    fieldImage.setPixel(x, z, 1.0, 1.0, 0.0, 1.0);
                albedoImage.setPixel(x, z, 0.05, 0.45, 0.75, 1.0);
                normalImage.setPixel(x, z, 0.5, 0.5, 1.0, 1.0);
            }
        }
        fieldTex = gfx.newTexture(fieldImage, false, false);
        albedoTex = gfx.newTexture(albedoImage, false, false);
        normalTex = gfx.newTexture(normalImage, false, false);
        gfx.setTextureSampler(fieldTex, "nearest", "none", 1.0, 0.0);
        gfx.setTextureSampler(albedoTex, "linear", "none", 1.0, 0.0);
        gfx.setTextureSampler(normalTex, "linear", "none", 1.0, 0.0);
    }

    if (cam == null) {
        cam = eve.Camera3D();
        cam.setEye(LAKE * 0.55, 10.5, LAKE * 1.45);
        cam.setTarget(LAKE * 0.5, 0.4, LAKE * 0.45);
        cam.setUp(0.0, 1.0, 0.0);
        cam.setFov(42.0);
        cam.setAmbient(0.45, 0.48, 0.52);
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
        waterObj.setCastShadow(false);
        applyWaterLook();
    }

    // Horizontal legend board (XZ) + vertical board (XY) with full UV coverage.
    if (fieldBoard == null) {
        fieldBoard = eve.Renderable3D();
        fieldBoard.setMesh(makeUvPlane(3.8));
        fieldBoard.setTexture(fieldTex);
        fieldBoard.setPosition(2.1, 3.4, 2.1);
        fieldBoard.setTint(1.0, 1.0, 1.0, 1.0);
        fieldBoard.setMetallic(0.0);
        fieldBoard.setRoughness(1.0);
        fieldBoard.setCastShadow(false);
        fieldBoard.setReceiveLight(false);
    }
    if (fieldBoardVert == null) {
        fieldBoardVert = eve.Renderable3D();
        fieldBoardVert.setMesh(makeUvPlane(3.8));
        fieldBoardVert.setTexture(fieldTex);
        fieldBoardVert.setPosition(LAKE - 1.8, 2.6, 1.6);
        // pitch 90° stands the XZ UV plane up as a wall facing +Z.
        fieldBoardVert.setRotation(0.0, 90.0, 0.0);
        fieldBoardVert.setTint(1.0, 1.0, 1.0, 1.0);
        fieldBoardVert.setMetallic(0.0);
        fieldBoardVert.setRoughness(1.0);
        fieldBoardVert.setCastShadow(false);
        fieldBoardVert.setReceiveLight(false);
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
    hud = "Shallow water (" + backend + ")  WASD  LMB splash  Space pause  R reset  F field/shaded";
    print(hud + "\n");
    print("field RGBA: R=depth(+contrast) G=vx B=vz A=speed\n");
};

eve_update = function(dt) {
    if (dt <= 0.0) return;
    if (dt > 0.05) dt = 0.05;
    if (edgePressed("space") || edgePressed("Space")) paused = !paused;
    if (edgePressed("r") || edgePressed("R")) resetLake();
    if (edgePressed("f") || edgePressed("F")) {
        showFieldOnWater = !showFieldOnWater;
        applyWaterLook();
        print(showFieldOnWater ? "water look: field channels\n" : "water look: shaded albedo\n");
    }
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
            requireOk(water.addImpulse(p[0], p[1], 1.8, 0.22), "mouseImpulse");
        }

        // Keep a gentle oscillating source so the field never goes fully flat.
        local pulse = 0.5 + 0.5 * sin(elapsed * 2.2);
        requireOk(water.addImpulse(LAKE * 0.55, LAKE * 0.40, 1.2, 0.018 * pulse), "pulseImpulse");

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
