// Interactive shallow-water lake with character-leg capsules.
// GPU Saint-Venant solver via eve.Fluids().newShallowWater (CPU fallback).
// Each frame packs depth/velocity into an RGBA field texture and uploads a
// shaded albedo + slope normal map so waves read clearly under lighting.

persist fluids = null
persist imageApi = null
persist water = null
persist waterMesh = null
persist waterObj = null
persist waterMat = null
persist fieldImage = null
persist fieldTex = null
persist albedoImage = null
persist albedoTex = null
persist normalImage = null
persist normalTex = null
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
const VEL_SCALE = 1.5;

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

function syncWaterTextures() {
    // Field texture: R=depth/maxDepth, G/B=±velocity around 0.5, A=speed.
    requireOk(water.copyFieldToImage(fieldImage, VEL_SCALE), "copyFieldToImage");
    requireOk(water.copyShadedAlbedoToImage(albedoImage, WATER_DEPTH, VEL_SCALE), "copyShadedAlbedoToImage");
    requireOk(water.copyNormalToImage(normalImage, 10.0), "copyNormalToImage");
    // updateTextureFromImageData throws on failure (no Result table).
    gfx.updateTextureFromImageData(fieldTex, fieldImage);
    gfx.updateTextureFromImageData(albedoTex, albedoImage);
    gfx.updateTextureFromImageData(normalTex, normalImage);
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
    syncWaterTextures();
}

function resetLake() {
    requireOk(water.fillDepth(WATER_DEPTH), "fillDepth");
    characterX = LAKE * 0.35;
    characterZ = LAKE * 0.45;
    characterVx = 0.0;
    characterVz = 0.0;
    requireOk(water.addImpulse(LAKE * 0.65, LAKE * 0.55, 1.8, 0.10), "seedImpulse");
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
                                       characterVx, 0.0, characterVz, 0.85, 0.55), "leftLeg");
    requireOk(water.setColliderCapsule(2, rightX, rightY, rightZ, LEG_HALF, LEG_RADIUS,
                                       characterVx, 0.0, characterVz, 0.85, 0.55), "rightLeg");

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
    if (imageApi == null) imageApi = eve.Image();
    if (water == null) {
        water = fluids.newShallowWater(RES, RES, CELL, true);
        if (water == null) throw "newShallowWater failed";
    }

    if (fieldImage == null) {
        fieldImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        albedoImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        normalImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        fieldTex = gfx.newTexture(fieldImage, false, false);
        albedoTex = gfx.newTexture(albedoImage, false, false);
        normalTex = gfx.newTexture(normalImage, false, false);
        gfx.setTextureSampler(fieldTex, "linear", "none", 1.0, 0.0);
        gfx.setTextureSampler(albedoTex, "linear", "none", 1.0, 0.0);
        gfx.setTextureSampler(normalTex, "linear", "none", 1.0, 0.0);
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
        waterMat = gfx.newMaterial();
        waterMat.setAlbedoTexture(albedoTex);
        waterMat.setNormalTexture(normalTex);
        waterObj.setMaterial(waterMat);
        waterObj.setTexture(albedoTex);
        waterObj.setNormalTexture(normalTex);
        // Tint multiplies albedo — keep near-white so the depth/foam texture shows.
        waterObj.setTint(1.0, 1.0, 1.0, 0.92);
        waterObj.setMetallic(0.02);
        waterObj.setRoughness(0.22);
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
    print("field tex RGBA = depth / vx / vz / speed\n");
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
            requireOk(water.addImpulse(p[0], p[1], 1.2, 0.09), "mouseImpulse");
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
