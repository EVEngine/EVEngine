// LITHIC — deterministic, real-time 3D rock authoring example.
// Every seed recreates the same mesh and procedural stone textures.

if (!("rock" in getroottable())) rock <- null;
if (!("camera" in getroottable())) camera <- null;
if (!("seed" in getroottable())) seed <- 1847;
if (!("shapeIndex" in getroottable())) shapeIndex <- 0;
if (!("shapeNames" in getroottable())) shapeNames <- ["mixed", "boulder", "slab", "block", "shard"];
if (!("flattening" in getroottable())) flattening <- 0.22;
if (!("angularity" in getroottable())) angularity <- 0.38;
if (!("erosion" in getroottable())) erosion <- 0.16;
if (!("noiseScale" in getroottable())) noiseScale <- 2.4;
if (!("roughness" in getroottable())) roughness <- 0.86;
if (!("autoSpin" in getroottable())) autoSpin <- true;
if (!("yaw" in getroottable())) yaw <- 0.0;
if (!("uiReady" in getroottable())) uiReady <- false;
if (!("rWasDown" in getroottable())) rWasDown <- false;

function coreId() {
    local s = seed.tostring();
    while (s.len() < 6) s = "0" + s;
    return "BAS-" + s;
}

function buildRock() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        ui.select("lab");
        ui.setText("status", "Parameter creation failed: " + paramsResult.status.summary);
        return;
    }
    local p = paramsResult.value;
    p.setSeed(seed);
    p.setInt("subdivisions", 3);
    p.setString("baseShape", shapeNames[shapeIndex]);
    p.setFloat("variation", 0.48);
    p.setFloat("radius", 0.72);
    p.setFloat("flattening", flattening);
    p.setFloat("angularity", angularity);
    p.setFloat("erosion", erosion);
    p.setFloat("scale", noiseScale);
    p.setInt("octaves", 4);

    local meshResult = procgen.generateMesh("mesh.rock", p, gfx);
    if (!meshResult.ok) {
        ui.select("lab");
        ui.setText("status", "Generation failed: " + meshResult.status.summary);
        return;
    }
    local mesh = meshResult.value;
    p.setInt("subdivisions", 2);
    local lod1Result = procgen.generateMesh("mesh.rock", p, gfx);
    if (!lod1Result.ok) {
        ui.setText("status", "LOD1 generation failed: " + lod1Result.status.summary);
        return;
    }
    local lod1 = lod1Result.value;
    p.setInt("subdivisions", 1);
    local lod2Result = procgen.generateMesh("mesh.rock", p, gfx);
    if (!lod2Result.ok) {
        ui.setText("status", "LOD2 generation failed: " + lod2Result.status.summary);
        return;
    }
    local lod2 = lod2Result.value;

    local textureParamsResult = procgen.newParams();
    if (!textureParamsResult.ok) {
        ui.setText("status", "Texture parameter creation failed: " + textureParamsResult.status.summary);
        return;
    }
    local tp = textureParamsResult.value;
    tp.setSeed(seed);
    tp.setSize(256, 256);
    tp.setFloat("scale", 4.2);
    tp.setInt("octaves", 5);
    tp.setInt("seamless", 1);
    local albedoResult = procgen.generateTexture("tex.stone", tp, gfx);
    if (!albedoResult.ok) {
        ui.setText("status", "Albedo generation failed: " + albedoResult.status.summary);
        return;
    }
    local albedo = albedoResult.value;
    local normalImageResult = procgen.generateNormalImage("tex.stone", tp);
    if (!normalImageResult.ok) {
        ui.setText("status", "Normal generation failed: " + normalImageResult.status.summary);
        return;
    }
    local normal = gfx.newTexture(normalImageResult.value, true, true);

    if (rock == null) rock = eve.Renderable3D();
    rock.clearMeshLod();
    rock.setMeshLod(0, mesh, 0.0);
    if (lod1 != null) rock.setMeshLod(1, lod1, 8.0);
    if (lod2 != null) rock.setMeshLod(2, lod2, 16.0);
    rock.setTexture(albedo);
    if (normal != null) rock.setNormalTexture(normal);
    rock.setScale(2.25, 2.25, 2.25);
    rock.setPosition(-0.42, -0.08, 0.0);
    rock.setTint(0.69, 0.64, 0.54, 1.0);
    rock.setMetallic(0.02);
    rock.setRoughness(roughness);
    rock.setCastShadow(true);
    rock.setReceiveShadow(true);

    if (uiReady) {
        ui.select("lab");
        ui.setText("sample", coreId());
        ui.setText("status", shapeNames[shapeIndex] + " base  /  1,280 triangles  /  3 LODs");
    }
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("LITHIC / FIELD LAB", "root");
    ui.text("CURRENT CORE", "eyebrow");
    ui.text(coreId(), "sample");
    ui.text("Shape", "shapeLabel");
    ui.slider("Flattening", flattening, 0.0, 0.65, "flattening");
    ui.slider("Fracture planes", angularity, 0.0, 1.0, "angularity");
    ui.slider("Surface erosion", erosion, 0.0, 0.42, "erosion");
    ui.slider("Geologic scale", noiseScale, 0.6, 5.5, "noiseScale");
    ui.text("Surface", "surfaceLabel");
    ui.slider("Roughness", roughness, 0.35, 1.0, "roughness");
    ui.beginRow("actions", 8.0);
    ui.button("New specimen", "randomize");
    ui.button("Rebuild", "rebuild");
    ui.end();
    ui.button("Next base shape", "nextShape");
    ui.button("Pause rotation", "spin");
    ui.text("Drag-free inspection • R creates a new specimen", "hint");
    ui.text("Generating…", "status");
    ui.end();
    ui.mountBuildAs("lab");
    ui.select("lab");
    ui.setHostOverlay(true);
    ui.setHostPos(850.0, 30.0, 300.0, 650.0);
    uiReady = true;
}

function randomize() {
    seed = (seed * 1664525 + 1013904223) & 0x7fffffff;
    buildRock();
}

function randomizeKeyPressed() {
    local down = keyboard.isDown("r") || keyboard.isDown("R");
    local pressed = down && !rWasDown;
    rWasDown = down;
    return pressed;
}

eve_init = function() {
    gfx.setBackgroundColor(0.045, 0.052, 0.050, 1.0);
    camera = eve.Camera3D();
    camera.setEye(3.25, 2.15, 5.1);
    camera.setTarget(-0.4, -0.1, 0.0);
    camera.setFov(37.0);
    camera.setAmbient(0.24, 0.25, 0.23);
    gfx.setDirectionalLight(-0.55, 0.85, 0.35, 2.0, 1.72, 1.35);
    if (!uiReady) buildPanel();
    buildRock();
};

eve_update = function(dt) {
    if (autoSpin && rock != null) {
        yaw += dt * 1.0;
        rock.setYaw(yaw);
    }
    if (randomizeKeyPressed()) randomize();

    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "lab/randomize") randomize();
        else if (clicked == "lab/rebuild") buildRock();
        else if (clicked == "lab/nextShape") {
            shapeIndex = (shapeIndex + 1) % shapeNames.len();
            buildRock();
        }
        else if (clicked == "lab/spin") {
            autoSpin = !autoSpin;
            ui.select("lab");
            ui.setText("spin", autoSpin ? "Pause rotation" : "Resume rotation");
        }
        clicked = ui.consumeClick();
    }

    local changed = ui.consumeChange();
    local rebuild = false;
    while (changed != "") {
        ui.select("lab");
        if (changed == "lab/flattening") { flattening = ui.getValue("flattening"); rebuild = true; }
        else if (changed == "lab/angularity") { angularity = ui.getValue("angularity"); rebuild = true; }
        else if (changed == "lab/erosion") { erosion = ui.getValue("erosion"); rebuild = true; }
        else if (changed == "lab/noiseScale") { noiseScale = ui.getValue("noiseScale"); rebuild = true; }
        else if (changed == "lab/roughness") {
            roughness = ui.getValue("roughness");
            if (rock != null) rock.setRoughness(roughness);
        }
        changed = ui.consumeChange();
    }
    if (rebuild) buildRock();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    // Copper datum lines make the viewport read like a geological instrument.
    gfx.drawSolidRect(54.0, 650.0, 720.0, 1.0, 0.55, 0.34, 0.18, 0.55);
    gfx.drawSolidRect(54.0, 646.0, 1.0, 9.0, 0.55, 0.34, 0.18, 0.75);
    gfx.drawSolidRect(774.0, 646.0, 1.0, 9.0, 0.55, 0.34, 0.18, 0.75);
    ui.beginFrameAndRender();
};
