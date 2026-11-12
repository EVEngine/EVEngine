// Urban Lab — interactive showcase of the "mesh.urban" / "urban.parcels" procgen
// algorithms, a conversion of the Eurographics 2024 paper
// "Hierarchical Co-generation of Parcels and Streets in Urban Modeling"
// (Chen, Song, Ortner — CGF 43(2), 10.1111/cgf.15053).
//
// The pipeline: hierarchical binary partitioning with streamline candidates
// (cross field + hyperstreamline tracing), the quality metric of Eq. (2),
// short-edge removal, reachability-driven street generation (I/L-shaped access +
// turn-aware Dijkstra) and a global geometric optimization (Eq. (3)).
//
// Controls:
//   R        new random seed
//   1..4     land preset: rect / triangle / ellipse / L
//   P        street pattern: default / loop / culdesac / tree
//   O        toggle geometric optimization
//   [ / ]    target parcel count -/+ 32
//   - / =    min parcel area -/+ 1
//   E        toggle flat plan / detailed buildings
//   L        toggle overhead parcel plan view

persist urbanSeed = 20260823
persist landPreset = "rect"
persist streetPattern = "default"
persist doOptimize = true
persist targetParcels = 68
persist minParcelArea = 4.0
persist extrude = 7.5
persist cityMesh = null
persist cityObject = null
persist cityCamera = null
persist cityYaw = 55.0
persist cityPitch = 0.62
persist cityOrbit = false
persist cityDist = 135.0
persist cityUiReady = false
persist cityGround = null
persist showGrid = false
persist gridLayer = null
persist info = ""
persist patterns = ["default", "loop", "culdesac", "tree"]
persist patternIndex = 0

function pressed(k) {
    return key_just_pressed(k);
}

function urbanClamp(value, low, high) {
    return value < low ? low : (value > high ? high : value);
}

function regenerate() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) {
        info = "PARAMS FAIL: " + paramsResult.status.summary;
        return;
    }
    local p = paramsResult.value;
    p.setSeed(urbanSeed);
    p.setString("land", landPreset);
    p.setFloat("landWidth", 100.0);
    p.setFloat("landHeight", 60.0);
    p.setFloat("minParcelArea", minParcelArea);
    p.setInt("targetParcels", targetParcels);
    p.setString("streetPattern", streetPattern);
    p.setInt("optimize", doOptimize ? 1 : 0);
    p.setFloat("streetWidth", 2.8);
    p.setFloat("extrude", extrude);
    p.setFloat("parcelInset", 2.0);
    p.setBool("presentationDetails", true);
    p.setFloat("heightVariation", 0.65);
    p.setBool("vertexColors", true);

    local meshResult = procgen.generateMesh("mesh.urban", p, gfx);
    if (!meshResult.ok) {
        info = "MESH FAIL: " + meshResult.status.summary;
        return;
    }
    cityMesh = meshResult.value;
    if (cityObject == null) {
        cityObject = eve.Renderable3D();
        cityObject.setPosition(0.0, 0.0, 0.0);
        cityObject.setTint(1.0, 1.0, 1.0, 1.0);
        cityObject.setRoughness(0.72);
        cityObject.setCastShadow(true);
    }
    cityObject.setMesh(cityMesh);

    // Use the actual land parcel footprint, including non-rectangular presets.
    p.setBool("presentationDetails", false);
    p.setFloat("extrude", 0.0);
    p.setFloat("parcelInset", 0.0);
    p.setFloat("heightVariation", 0.0);
    p.setBool("vertexColors", false);
    local groundResult = procgen.generateMesh("mesh.urban", p, gfx);
    if (!groundResult.ok) throw "urban ground: " + groundResult.status.summary;
    if (cityGround == null) cityGround = eve.Renderable3D();
    cityGround.setMesh(groundResult.value);
    cityGround.setPosition(0.0,-0.05,0.0);
    cityGround.setTint(0.025,0.035,0.045,1.0);
    cityGround.setCastShadow(false);
    cityGround.setRoughness(0.95);


    // Optional 2D parcel/road grid preview (Semantic::Road / Floor + parcel detail).
    local gridResult = procgen.generate("urban.parcels", p);
    if (gridResult.ok) {
        local g = gridResult.value;
        if (gridLayer == null) {
            gridLayer = map.newLayer(g.getWidth(), g.getHeight(), 0.5, 0.5);
            gridLayer.setOrigin(8.0, 400.0);
            gridLayer.setLayer(0);
            gridLayer.setVisible(false);
        } else if (gridLayer.getMapWidth() != g.getWidth() ||
                   gridLayer.getMapHeight() != g.getHeight()) {
            gridLayer.resize(g.getWidth(), g.getHeight());
        }
        procgen.setPaletteGid("urban_demo", "wall", 0);
        procgen.setPaletteGid("urban_demo", "road", 3);
        procgen.setPaletteGid("urban_demo", "floor", 1);
        local outputResult = procgen.newOutput();
        if (!outputResult.ok) {
            info = "OUTPUT FAIL: " + outputResult.status.summary;
            return;
        }
        local out = outputResult.value;
        out.setTarget("tilelayer");
        out.setLayer(gridLayer);
        out.setPalette("urban_demo");
        local outputWriteResult = procgen.generateTo("urban.parcels", p, out);
        if (!outputWriteResult.ok) {
            info = "OUTPUT WRITE FAIL: " + outputWriteResult.status.summary;
            return;
        }
        info = "seed=" + urbanSeed + "  land=" + landPreset +
               "  pattern=" + streetPattern + (doOptimize ? "  optimize" : "  raw") +
               "  parcels=" + g.getMeta("parcels", "?") +
               "  streets=" + g.getMeta("streets", "?") +
               "  avgIrr=" + g.getMeta("avgIrregularity", "?");
    }
    if(cityUiReady) ui.setText("stats", "Seed " + urbanSeed + " / " + landPreset + " / " + streetPattern);
}

if (cityCamera == null) {
    cityCamera = eve.Camera3D();
    cityCamera.setEye(0.0, 40.0, 150.0);
    cityCamera.setTarget(50.0, 0.0, 30.0);
    cityCamera.setUp(0.0, 1.0, 0.0);
    cityCamera.setFov(42.0);
    // The orbit is 150 units away; the default far plane is only 100.
    cityCamera.setClipPlanes(0.1, 500.0);
    cityCamera.setAmbient(0.18, 0.22, 0.26);
    cityCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, 0.9, 0.45, 1.05, 0.97, 0.82);
}
if (cityMesh == null) regenerate();
gfx.setBackgroundColor(0.028, 0.045, 0.055, 1.0);

function eve_update(dt) {
    if(cityOrbit) cityYaw += dt * 6.0;
    if(pressed("space")) cityOrbit = !cityOrbit;
    if (pressed("r") || pressed("R")) { urbanSeed = procgen.randomSeed(); regenerate(); }
    if (pressed("1")) { landPreset = "rect"; regenerate(); }
    if (pressed("2")) { landPreset = "triangle"; regenerate(); }
    if (pressed("3")) { landPreset = "ellipse"; regenerate(); }
    if (pressed("4")) { landPreset = "l"; regenerate(); }
    if (pressed("p") || pressed("P")) {
        patternIndex = (patternIndex + 1) % patterns.len();
        streetPattern = patterns[patternIndex];
        regenerate();
    }
    if (pressed("o") || pressed("O")) { doOptimize = !doOptimize; regenerate(); }
    if (pressed("leftbracket")) { targetParcels = urbanClamp(targetParcels - 32, 4, 1024); regenerate(); }
    if (pressed("rightbracket")) { targetParcels = urbanClamp(targetParcels + 32, 4, 1024); regenerate(); }
    if (pressed("minus") || pressed("-")) { minParcelArea = urbanClamp(minParcelArea - 1.0, 0.5, 24.0); regenerate(); }
    if (pressed("equals") || pressed("=") || pressed("plus")) { minParcelArea = urbanClamp(minParcelArea + 1.0, 0.5, 24.0); regenerate(); }
    if (pressed("e") || pressed("E")) { extrude = extrude > 0.0 ? 0.0 : 7.5; regenerate(); }
    if (pressed("l") || pressed("L")) {
        showGrid = !showGrid;

    }

    const cy = 0.53;  // fixed elevation angle
    local radius = cityDist;
    local ex = 50.0 + radius * cos(cityYaw * 0.01745) * cos(cy);
    local ez = 30.0 + radius * sin(cityYaw * 0.01745) * cos(cy);
    local ey = 10.0 + radius * sin(cy);
    if(showGrid) {
        cityCamera.setEye(50.0, 140.0, 20.01);
        cityCamera.setUp(0.0, 0.0, -1.0);
    } else {
        cityCamera.setEye(ex, ey, ez);
        cityCamera.setUp(0.0, 1.0, 0.0);
    }
    cityCamera.setTarget(50.0, 0.0, showGrid ? 20.0 : 30.0);
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
}

function eve_init() {
    ui.beginBuild();
    ui.beginWindow("Urban generator", "root");
    ui.text("URBAN GARDENS / PROCEDURAL CITY", "title");
    ui.text("Seed " + urbanSeed + " / " + landPreset + " / " + streetPattern, "stats");
    ui.text("R: regenerate   1-4: footprint   P: streets", "keys");
    ui.text("L: plan   Space: orbit   E: massing / plan", "keys2");
    ui.end();
    ui.mountBuildAs("city-hud"); ui.select("city-hud");
    ui.setHostOverlay(true); ui.setHostPos(18.0,16.0,0.0,0.0); ui.setHostSize(450.0,176.0);
    cityUiReady = true;
}
