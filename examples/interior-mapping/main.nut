// Interior Mapping demo — pre-projected 2D room atlas + tangent-space
// ray–AABB (Zhihu: 「Interior Mapping室内映射技术拆解」).
//
// Controls:
//   O        toggle auto-orbit
//   A/D      yaw
//   W/S      pitch
//   [ / ]    room depth
//   - / =    perspective strength
//   F        toggle procedural foreground furniture
//   R        toggle random room selection
//   1/2/3    preset looks (shallow / default / deep)

persist imShader = null
persist imCamera = null
persist imFacade = null
persist imGround = null
persist imAtlas = null
persist imTime = 0.0
persist imYaw = 0.35
persist imPitch = 0.18
persist imOrbit = true
persist imRoomDepth = 0.55
persist imPerspective = 1.15
persist imRandomRoom = 1.0
persist imShowFg = 1.0
persist imFgSteps = 16.0
persist imStatus = "orbit on"

const ATLAS_COLS = 4;
const ATLAS_ROWS = 4;
const CELL = 128;
const WIN_X = 6;
const WIN_Y = 4;

function clampf(v, a, b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}

function pushUniforms() {
    imShader.sendFloat("atlasColumns", ATLAS_COLS.tofloat());
    imShader.sendFloat("atlasRows", ATLAS_ROWS.tofloat());
    imShader.sendFloat("perspective", imPerspective);
    imShader.sendFloat("roomDepth", imRoomDepth);
    imShader.sendFloat("randomRoom", imRandomRoom);
    imShader.sendFloat("randomSeed", 3.0);
    imShader.sendFloat("fgSteps", imFgSteps);
    imShader.sendFloat("time", imTime);
    imShader.sendFloat("showFg", imShowFg);
    imShader.sendFloat("frameWidth", 0.055);
}

function roomPalette(id) {
    // Distinct wall / floor / ceiling / accent per room cell.
    local walls = [
        [0.78, 0.74, 0.68],
        [0.55, 0.68, 0.72],
        [0.72, 0.58, 0.52],
        [0.62, 0.66, 0.55],
        [0.70, 0.62, 0.78],
        [0.80, 0.72, 0.58],
        [0.58, 0.60, 0.68],
        [0.74, 0.70, 0.62],
        [0.66, 0.74, 0.70],
        [0.76, 0.66, 0.60],
        [0.60, 0.70, 0.78],
        [0.68, 0.60, 0.58],
        [0.72, 0.72, 0.66],
        [0.58, 0.64, 0.58],
        [0.70, 0.58, 0.66],
        [0.64, 0.68, 0.74]
    ];
    return walls[id % walls.len()];
}

function paintRoom(img, ox, oy, id) {
    local wall = roomPalette(id);
    local floorC = [wall[0] * 0.55, wall[1] * 0.48, wall[2] * 0.42];
    local ceilC = [wall[0] * 1.08, wall[1] * 1.06, wall[2] * 1.05];
    local accent = [
        0.35 + 0.4 * ((id * 17) % 5).tofloat() / 4.0,
        0.25 + 0.35 * ((id * 9) % 7).tofloat() / 6.0,
        0.20 + 0.45 * ((id * 13) % 4).tofloat() / 3.0
    ];
    local backDoor = (id % 3) == 0;
    local windowSide = (id % 5) == 2;
    local shelf = (id % 4) != 1;

    for (local y = 0; y < CELL; ++y) {
        for (local x = 0; x < CELL; ++x) {
            local u = (x + 0.5) / CELL.tofloat();
            local v = (y + 0.5) / CELL.tofloat();
            // Orthographic "pre-projection": floor / ceiling / side walls / back.
            local col = wall;
            if (v < 0.18) {
                col = ceilC;
            } else if (v > 0.78) {
                // Floor with simple board lines.
                local board = ((u * 8.0).tointeger() % 2) == 0 ? 0.92 : 1.0;
                col = [floorC[0] * board, floorC[1] * board, floorC[2] * board];
            } else if (u < 0.12) {
                col = [wall[0] * 0.82, wall[1] * 0.82, wall[2] * 0.85];
            } else if (u > 0.88) {
                col = [wall[0] * 0.88, wall[1] * 0.86, wall[2] * 0.84];
            } else {
                // Back wall with optional door / art / window.
                col = wall;
                if (backDoor && u > 0.38 && u < 0.62 && v > 0.28 && v < 0.78) {
                    col = [0.22, 0.18, 0.14];
                    if (u > 0.56 && u < 0.60 && v > 0.48 && v < 0.54) {
                        col = [0.75, 0.65, 0.35]; // knob
                    }
                }
                if (windowSide && u > 0.70 && u < 0.86 && v > 0.32 && v < 0.62) {
                    col = [0.45, 0.62, 0.82];
                }
                if (shelf && u > 0.16 && u < 0.34 && v > 0.40 && v < 0.70) {
                    local onShelf = ((v * 20.0).tointeger() % 4) == 0;
                    if (onShelf) col = [accent[0] * 0.5, accent[1] * 0.5, accent[2] * 0.5];
                    else col = accent;
                }
                // Ceiling lamp hint.
                if (u > 0.46 && u < 0.54 && v > 0.20 && v < 0.28) {
                    col = [0.95, 0.90, 0.70];
                }
            }
            // Soft vignette toward edges of the cell (helps perspective read).
            local edge = clampf(u * 4.0, 0.0, 1.0) * clampf((1.0 - u) * 4.0, 0.0, 1.0) *
                         clampf(v * 3.0, 0.0, 1.0) * clampf((1.0 - v) * 3.0, 0.0, 1.0);
            edge = 0.75 + 0.25 * edge;
            img.setPixel(ox + x, oy + y, col[0] * edge, col[1] * edge, col[2] * edge, 1.0);
        }
    }
}

function buildAtlas() {
    local imageModule = eve.Image();
    local w = ATLAS_COLS * CELL;
    local h = ATLAS_ROWS * CELL;
    local pixels = imageModule.newEmptyImageData(w, h, "RGBA8");
    local id = 0;
    for (local row = 0; row < ATLAS_ROWS; ++row) {
        for (local col = 0; col < ATLAS_COLS; ++col) {
            // Atlas V grows downward in ImageData; shader UV V grows up conventionally
            // for Mesh3D — paint so row 0 is the top of the image (v=1 in GL).
            local oy = (ATLAS_ROWS - 1 - row) * CELL;
            paintRoom(pixels, col * CELL, oy, id);
            id += 1;
        }
    }
    imAtlas = gfx.newTexture(pixels, false, false);
    gfx.setTextureSampler(imAtlas, "linear", "none", 1.0, 0.0);
}

function makeFacadeMesh() {
    // Single wall in XY facing +Z, UVs tiled by window grid.
    local hw = WIN_X.tofloat() * 0.5;
    local hh = WIN_Y.tofloat() * 0.5;
    local pos = [
        -hw, -hh, 0.0,
         hw, -hh, 0.0,
         hw,  hh, 0.0,
        -hw,  hh, 0.0
    ];
    local nrm = [
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0,
        0.0, 0.0, 1.0
    ];
    local uv = [
        0.0, 0.0,
        WIN_X.tofloat(), 0.0,
        WIN_X.tofloat(), WIN_Y.tofloat(),
        0.0, WIN_Y.tofloat()
    ];
    local idx = [0, 1, 2, 0, 2, 3];
    return gfx.newMeshFromArrays(pos, nrm, uv, 4, idx, 6);
}

function updateCamera() {
    local dist = 7.2;
    local cx = dist * cos(imPitch) * sin(imYaw);
    local cy = 0.4 + dist * sin(imPitch);
    local cz = dist * cos(imPitch) * cos(imYaw);
    imCamera.setEye(cx, cy, cz);
    imCamera.setTarget(0.0, 0.15, 0.0);
}

function applyPreset(which) {
    if (which == 1) {
        imRoomDepth = 0.35;
        imPerspective = 0.85;
        imStatus = "preset shallow";
    } else if (which == 2) {
        imRoomDepth = 0.55;
        imPerspective = 1.15;
        imStatus = "preset default";
    } else {
        imRoomDepth = 0.78;
        imPerspective = 1.6;
        imStatus = "preset deep";
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.12, 0.14, 0.18, 1.0);
    if (imShader == null) {
        imShader = gfx.newMeshShader(fs.readText("shaders/interior_mapping.frag"));
        imShader.declareFloat("atlasColumns");
        imShader.declareFloat("atlasRows");
        imShader.declareFloat("perspective");
        imShader.declareFloat("roomDepth");
        imShader.declareFloat("randomRoom");
        imShader.declareFloat("randomSeed");
        imShader.declareFloat("fgSteps");
        imShader.declareFloat("time");
        imShader.declareFloat("showFg");
        imShader.declareFloat("frameWidth");
    }
    if (imAtlas == null) buildAtlas();
    if (imFacade == null) {
        imFacade = eve.Renderable3D();
        imFacade.setMesh(makeFacadeMesh());
        imFacade.setShader(imShader);
        imFacade.setTexture(imAtlas);
        imFacade.setTint(1.0, 1.0, 1.0, 1.0);
        imFacade.setCastShadow(false);
        imFacade.setReceiveShadow(false);
        imFacade.setPosition(0.0, 0.9, 0.0);
    }
    if (imGround == null) {
        imGround = eve.Renderable3D();
        imGround.setMesh(gfx.newMeshCube(1.0));
        imGround.setPosition(0.0, -0.12, 0.0);
        imGround.setScale(10.0, 0.2, 6.0);
        imGround.setTint(0.18, 0.19, 0.22, 1.0);
        imGround.setRoughness(0.95);
        imGround.setCastShadow(false);
    }
    if (imCamera == null) {
        imCamera = eve.Camera3D();
        imCamera.setFov(42.0);
        imCamera.setAmbient(0.22, 0.24, 0.28);
        imCamera.setActive(true);
    }
    gfx.setDirectionalLight(-0.35, 0.75, 0.55, 1.35, 1.25, 1.15);
    pushUniforms();
    updateCamera();
    print("Interior Mapping: O orbit | A/D yaw | [/] depth | -/= perspective | F furniture | R random\n");
};

eve_asset_reload <- function(path) {
    if (path.find("interior_mapping.frag") != null) {
        local src = fs.readText("shaders/interior_mapping.frag");
        local result = gfx.replaceShaderFromGlsl(imShader, "", src);
        if (result.ok) {
            imStatus = "shader reloaded";
            print("[interior-mapping] shader reloaded\n");
        } else {
            imStatus = "compile failed";
            print("[interior-mapping] compile failed\n");
        }
    }
};

eve_update = function(dt) {
    imTime += dt;
    if (imOrbit) imYaw += dt * 0.22;

    if (key_just_pressed("o") || key_just_pressed("O")) {
        imOrbit = !imOrbit;
        imStatus = imOrbit ? "orbit on" : "orbit off";
    }
    if (key_just_pressed("a") || key_just_pressed("A")) imYaw -= 0.12;
    if (key_just_pressed("d") || key_just_pressed("D")) imYaw += 0.12;
    if (key_just_pressed("w") || key_just_pressed("W")) imPitch = clampf(imPitch + 0.06, -0.2, 0.55);
    if (key_just_pressed("s") || key_just_pressed("S")) imPitch = clampf(imPitch - 0.06, -0.2, 0.55);

    if (key_just_pressed("[")) {
        imRoomDepth = clampf(imRoomDepth - 0.05, 0.1, 0.9);
        imStatus = "depth " + imRoomDepth;
    }
    if (key_just_pressed("]")) {
        imRoomDepth = clampf(imRoomDepth + 0.05, 0.1, 0.9);
        imStatus = "depth " + imRoomDepth;
    }
    if (key_just_pressed("-") || key_just_pressed("_")) {
        imPerspective = clampf(imPerspective - 0.1, 0.2, 3.0);
        imStatus = "perspective " + imPerspective;
    }
    if (key_just_pressed("=") || key_just_pressed("+")) {
        imPerspective = clampf(imPerspective + 0.1, 0.2, 3.0);
        imStatus = "perspective " + imPerspective;
    }
    if (key_just_pressed("f") || key_just_pressed("F")) {
        imShowFg = imShowFg > 0.5 ? 0.0 : 1.0;
        imStatus = imShowFg > 0.5 ? "furniture on" : "furniture off";
    }
    if (key_just_pressed("r") || key_just_pressed("R")) {
        imRandomRoom = imRandomRoom > 0.5 ? 0.0 : 1.0;
        imStatus = imRandomRoom > 0.5 ? "random rooms" : "tiled rooms";
    }
    if (key_just_pressed("1")) applyPreset(1);
    if (key_just_pressed("2")) applyPreset(2);
    if (key_just_pressed("3")) applyPreset(3);

    pushUniforms();
    updateCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
