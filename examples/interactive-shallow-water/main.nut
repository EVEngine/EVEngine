// Interactive shallow-water lake with character-leg capsules.
// Simulation: eve.Fluids().newShallowWater (height + momentum).
// Presentation: gfx.newWater() transparent stylized shader (refraction /
// depth fade / foam / Fresnel). Field RGBA texture is data-only (legend boards).

persist fluids = null
persist imageApi = null
persist sim = null
persist waterFx = null
persist waterMat = null
persist waterMesh = null
persist waterObj = null
persist fieldImage = null
persist fieldTex = null
persist fieldBoard = null
persist fieldBoardVert = null
persist showFieldBoards = true
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
const LAKE = 12.8;
const HALF = 6.4; // LAKE * 0.5 — water mesh is centered; sim is [0, LAKE]
const WATER_DEPTH = 0.55;
const LEG_RADIUS = 0.11;
const LEG_HALF = 0.42;
const MOVE_SPEED = 2.4;
const VEL_SCALE = 0.75;
const DEPTH_CONTRAST = 0.06;
const VISUAL_GAIN = 2.5;

const WATER_STYLE_JSON = @"
{
  ""schema"": ""eve.graphics.stylized-water"",
  ""schemaVersion"": 1,
  ""unknownFields"": ""reject"",
  ""colors"": {
    ""deep"": [0.02, 0.14, 0.22],
    ""shallow"": [0.14, 0.55, 0.62],
    ""foam"": [0.90, 0.96, 1.0],
    ""reflectionTint"": [0.70, 0.85, 1.0]
  },
  ""waves"": {
    ""speed"": 0.9,
    ""amplitude"": 0.035,
    ""scale"": 9.0,
    ""sharpness"": 1.2,
    ""rippleAmplitude"": 0.04,
    ""rippleCount"": 2,
    ""rippleInterval"": 2.4
  },
  ""depth"": { ""distance"": 2.2, ""opacity"": 0.55 },
  ""foam"": { ""width"": 0.50, ""softness"": 0.10, ""strength"": 0.85 },
  ""lighting"": {
    ""fresnelPower"": 4.0,
    ""reflectionIntensity"": 0.55,
    ""sunIntensity"": 0.85,
    ""screenSpaceReflection"": false,
    ""screenSpaceReflectionStrength"": 0.85
  },
  ""refraction"": { ""strength"": 0.045 },
  ""caustics"": { ""strength"": 0.22, ""scale"": 4.0 }
}
";

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

function syncFieldLegend() {
    requireOk(sim.copyFieldToImage(fieldImage, VEL_SCALE, DEPTH_CONTRAST), "copyFieldToImage");
    gfx.updateTextureFromImageData(fieldTex, fieldImage);
    if (fieldBoard != null) {
        fieldBoard.setTexture(fieldTex);
        fieldBoard.setVisible(showFieldBoards);
    }
    if (fieldBoardVert != null) {
        fieldBoardVert.setTexture(fieldTex);
        fieldBoardVert.setVisible(showFieldBoards);
    }
}

function syncWaterMesh() {
    // Presentation mesh is Water.createPlane-centered; convert sim world XZ → local.
    local built = requireOk(sim.buildSurfaceMesh(VISUAL_GAIN), "buildSurfaceMesh");
    local mesh = built.value;
    local pos = mesh.positions;
    local i = 0;
    while (i < mesh.vertexCount) {
        local p = i * 3;
        pos[p] = pos[p] - HALF;
        pos[p + 2] = pos[p + 2] - HALF;
        i += 1;
    }
    if (waterMesh == null) {
        waterMesh = waterFx.getMesh();
        waterObj.setMesh(waterMesh);
    }
    if (!gfx.updateMeshVertices(waterMesh, pos, mesh.normals, mesh.uvs,
                                mesh.vertexCount, mesh.indices, mesh.indexCount)) {
        throw "updateMeshVertices failed";
    }
    syncFieldLegend();
}

function resetLake() {
    requireOk(sim.fillDepth(WATER_DEPTH), "fillDepth");
    characterX = LAKE * 0.35;
    characterZ = LAKE * 0.45;
    characterVx = 0.0;
    characterVz = 0.0;
    requireOk(sim.addImpulse(LAKE * 0.65, LAKE * 0.55, 2.2, 0.22), "seedImpulse");
    syncWaterMesh();
}

function updateLegs(dt) {
    local surface = sim.surfaceHeightAt(characterX, characterZ);
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

    requireOk(sim.setColliderCapsule(1, leftX, physLeftY, leftZ, LEG_HALF, LEG_RADIUS,
                                     characterVx, 0.0, characterVz, 0.85, 0.55), "leftLeg");
    requireOk(sim.setColliderCapsule(2, rightX, physRightY, rightZ, LEG_HALF, LEG_RADIUS,
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
    gfx.setBackgroundColor(0.42, 0.58, 0.72, 1.0);
    // Depth buffer helps the water shader's opacity / shore foam path when available.
    gfx.getRenderControl().enable("gbuffer");

    if (fluids == null) fluids = eve.Fluids();
    if (imageApi == null) imageApi = eve.Image();
    if (sim == null) {
        sim = fluids.newShallowWater(RES, RES, CELL, true);
        if (sim == null) throw "newShallowWater failed";
    }

    if (fieldImage == null) {
        fieldImage = imageApi.newEmptyImageData(RES, RES, "RGBA8");
        fieldTex = gfx.newTexture(fieldImage, false, false);
        gfx.setTextureSampler(fieldTex, "nearest", "none", 1.0, 0.0);
    }

    if (waterFx == null) {
        waterFx = gfx.newWater();
        // Matching vertex count: (RES-1)+1 squared == RES*RES.
        waterFx.createPlane(LAKE, LAKE, RES - 1, RES - 1);
        requireOk(waterFx.applyConfigJson(WATER_STYLE_JSON), "water style");
        waterFx.setViewport(gfx.getWidth().tofloat(), gfx.getHeight().tofloat());
    }

    if (cam == null) {
        cam = eve.Camera3D();
        cam.setEye(LAKE * 0.55, 9.5, LAKE * 1.40);
        cam.setTarget(LAKE * 0.5, 0.35, LAKE * 0.45);
        cam.setUp(0.0, 1.0, 0.0);
        cam.setFov(42.0);
        cam.setAmbient(0.40, 0.46, 0.52);
        cam.setActive(true);
        gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.15, 1.1, 1.0);
    }

    if (ground == null) {
        ground = eve.Renderable3D();
        ground.setMesh(gfx.newMeshCube(1.0));
        ground.setScale(LAKE + 2.0, 0.12, LAKE + 2.0);
        ground.setPosition(LAKE * 0.5, -0.12, LAKE * 0.5);
        // Bright sand so refraction / depth fade has something to show through.
        ground.setTint(0.55, 0.48, 0.32, 1.0);
        ground.setCastShadow(false);
    }

    if (waterObj == null) {
        waterMat = gfx.newMaterial();
        waterMat.setSurfaceMode("transparent");
        waterMat.setBlendMode("alpha");
        waterMat.setDepthWrite(false);
        waterMat.setShader(waterFx.getShader());
        waterObj = eve.Renderable3D();
        waterObj.setMaterial(waterMat);
        waterObj.setShader(waterFx.getShader());
        waterObj.setMesh(waterFx.getMesh());
        waterObj.setPosition(HALF, 0.0, HALF);
        waterObj.setTint(1.0, 1.0, 1.0, 1.0);
        waterObj.setCastShadow(false);
        waterObj.setReceiveShadow(false);
        waterMesh = waterFx.getMesh();
    }

    // Optional data boards: R=depth crest heat, G/B=velocity (not the lake albedo).
    if (fieldBoard == null) {
        fieldBoard = eve.Renderable3D();
        fieldBoard.setMesh(makeUvPlane(3.2));
        fieldBoard.setTexture(fieldTex);
        fieldBoard.setPosition(LAKE * 0.18, 4.0, LAKE * 0.18);
        fieldBoard.setTint(1.0, 1.0, 1.0, 1.0);
        fieldBoard.setMetallic(0.0);
        fieldBoard.setRoughness(1.0);
        fieldBoard.setCastShadow(false);
        fieldBoard.setReceiveLight(false);
    }
    if (fieldBoardVert == null) {
        fieldBoardVert = eve.Renderable3D();
        fieldBoardVert.setMesh(makeUvPlane(3.2));
        fieldBoardVert.setTexture(fieldTex);
        fieldBoardVert.setPosition(LAKE * 0.82, 3.2, LAKE * 0.16);
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
    local backend = sim.usingGpu() ? "GPU" : (sim.usedBackendFallback() ? "CPU-fallback" : "CPU");
    hud = "Shallow water (" + backend + ")  WASD  LMB splash  Space pause  R reset  F field boards";
    print(hud + "\n");
    print("presentation: transparent Water shader; field boards = R/G/B/A data\n");
};

eve_update = function(dt) {
    if (dt <= 0.0) return;
    if (dt > 0.05) dt = 0.05;
    if (edgePressed("space") || edgePressed("Space")) paused = !paused;
    if (edgePressed("r") || edgePressed("R")) resetLake();
    if (edgePressed("f") || edgePressed("F")) {
        showFieldBoards = !showFieldBoards;
        if (fieldBoard != null) fieldBoard.setVisible(showFieldBoards);
        if (fieldBoardVert != null) fieldBoardVert.setVisible(showFieldBoards);
        print(showFieldBoards ? "field boards: on\n" : "field boards: off\n");
    }
    if (edgePressed("g") || edgePressed("G")) {
        local backend = sim.usingGpu() ? "GPU" : (sim.usedBackendFallback() ? "CPU-fallback" : "CPU");
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
            requireOk(sim.addImpulse(p[0], p[1], 1.8, 0.22), "mouseImpulse");
        }

        local pulse = 0.5 + 0.5 * sin(elapsed * 2.2);
        requireOk(sim.addImpulse(LAKE * 0.55, LAKE * 0.40, 1.2, 0.014 * pulse), "pulseImpulse");

        elapsed += dt;
        updateLegs(dt);
        requireOk(sim.step(dt), "step");
        syncWaterMesh();
        waterFx.update(dt);
        waterFx.bindParams();
    }
};

eve_render = function() {
    waterFx.bindParams();
    gfx.clear();
    gfx.render3D();
};
