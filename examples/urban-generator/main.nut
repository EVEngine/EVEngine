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
//   E        toggle block extrusion (flat / 6 units)
//   L        toggle parcel grid overlay (urban.parcels Grid2D preview)

if (!("urbanSeed" in getroottable())) urbanSeed <- 20260823;
if (!("landPreset" in getroottable())) landPreset <- "rect";
if (!("streetPattern" in getroottable())) streetPattern <- "default";
if (!("doOptimize" in getroottable())) doOptimize <- true;
if (!("targetParcels" in getroottable())) targetParcels <- 120;
if (!("minParcelArea" in getroottable())) minParcelArea <- 4.0;
if (!("extrude" in getroottable())) extrude <- 6.0;
if (!("cityMesh" in getroottable())) cityMesh <- null;
if (!("cityObject" in getroottable())) cityObject <- null;
if (!("cityCamera" in getroottable())) cityCamera <- null;
if (!("cityYaw" in getroottable())) cityYaw <- 0.0;
if (!("cityPitch" in getroottable())) cityPitch <- 0.62;
if (!("cityDist" in getroottable())) cityDist <- 150.0;
if (!("showGrid" in getroottable())) showGrid <- false;
if (!("gridLayer" in getroottable())) gridLayer <- null;
if (!("info" in getroottable())) info <- "";
if (!("patterns" in getroottable()))
    patterns <- ["default", "loop", "culdesac", "tree"];
if (!("patternIndex" in getroottable())) patternIndex <- 0;

function pressed(k) {
    return key_just_pressed(k);
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
    p.setFloat("streetWidth", 1.2);
    p.setFloat("extrude", extrude);

    local meshResult = procgen.generateMesh("mesh.urban", p, gfx);
    if (!meshResult.ok) {
        info = "MESH FAIL: " + meshResult.status.summary;
        return;
    }
    cityMesh = meshResult.value;
    if (cityObject == null) {
        cityObject = eve.Renderable3D();
        cityObject.setPosition(0.0, 0.0, 0.0);
        cityObject.setTint(0.82, 0.80, 0.74, 1.0);
        cityObject.setRoughness(0.72);
        cityObject.setCastShadow(true);
    }
    cityObject.setMesh(cityMesh);

    // Optional 2D parcel/road grid preview (Semantic::Road / Floor + parcel detail).
    local gridResult = procgen.generate("urban.parcels", p);
    if (gridResult.ok) {
        local g = gridResult.value;
        if (gridLayer == null) {
            gridLayer = map.newLayer(g.getWidth(), g.getHeight(), 0.5, 0.5);
            gridLayer.setOrigin(8.0, 400.0);
            gridLayer.setLayer(0);
            gridLayer.setVisible(showGrid);
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
}

if (cityCamera == null) {
    cityCamera = eve.Camera3D();
    cityCamera.setEye(0.0, 40.0, 150.0);
    cityCamera.setTarget(50.0, 0.0, 30.0);
    cityCamera.setUp(0.0, 1.0, 0.0);
    cityCamera.setFov(42.0);
    cityCamera.setAmbient(0.42, 0.46, 0.50);
    cityCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.4, 1.5, 1.42, 1.30);
}
if (cityMesh == null) regenerate();
gfx.setBackgroundColor(0.10, 0.13, 0.17, 1.0);

function eve_update(dt) {
    cityYaw += dt * 6.0;
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
    if (pressed("leftbracket")) { targetParcels = max(4, targetParcels - 32); regenerate(); }
    if (pressed("rightbracket")) { targetParcels = min(1024, targetParcels + 32); regenerate(); }
    if (pressed("minus") || pressed("-")) { minParcelArea = max(0.5, minParcelArea - 1.0); regenerate(); }
    if (pressed("equals") || pressed("=") || pressed("plus")) { minParcelArea = min(24.0, minParcelArea + 1.0); regenerate(); }
    if (pressed("e") || pressed("E")) { extrude = extrude > 0.0 ? 0.0 : 6.0; regenerate(); }
    if (pressed("l") || pressed("L")) {
        showGrid = !showGrid;
        if (gridLayer != null) gridLayer.setVisible(showGrid);
    }

    const cy = 0.62;  // fixed elevation angle
    local radius = cityDist;
    local ex = 50.0 + radius * cos(cityYaw * 0.01745) * cos(cy);
    local ez = 30.0 + radius * sin(cityYaw * 0.01745) * cos(cy);
    local ey = 28.0 + radius * sin(cy);
    cityCamera.setEye(ex, ey, ez);
    cityCamera.setTarget(50.0, 0.0, 30.0);
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    // HUD
    gfx.drawSolidRect(16.0, 16.0, 560.0, 52.0, 0.05, 0.06, 0.09, 0.82);
    gfx.drawSolidRect(24.0, 24.0, 14.0, 14.0, 0.86, 0.63, 0.13, 1.0);  // roads
    gfx.drawSolidRect(24.0, 44.0, 14.0, 14.0, 0.72, 0.70, 0.64, 1.0);  // parcels
}
