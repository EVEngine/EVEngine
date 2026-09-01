// Karst Lab: R changes seed; 1..5 select cave styles; 6..8 select limestone microstructure.

persist caveSeed = 20260830
persist caveStyleIndex = 4
persist caveRockIndex = 1
persist caveParts = []
persist caveMaterials = {}
persist caveCamera = null
persist caveLights = []
persist caveScaleMarker = null
persist caveScaleHead = null
persist cavePreviousKeys = {}
persist caveCaptureFrame = 0
persist caveCaptureIndex = 0

local caveStyles = ["cavern", "tunnels", "vertical", "labyrinth", "mixed"];
local caveRockNames = ["microporous", "heterogeneous", "wormhole-prone"];
local caveMicroporosity = [0.90, 0.42, 0.12];
local cavePermeabilityContrast = [0.25, 0.72, 0.95];

function cavePressed(key) {
    local down = keyboard.isDown(key);
    local previous = key in cavePreviousKeys ? cavePreviousKeys[key] : false;
    cavePreviousKeys[key] <- down;
    return down && !previous;
}

function rebuildCave() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) { print("cave parameter creation failed: " + paramsResult.status.summary); return; }
    local p = paramsResult.value;
    p.setSeed(caveSeed);
    p.setString("style", caveStyles[caveStyleIndex]);
    p.setString("genesis", "mixed");
    p.setInt("resolution", 64);
    p.setFloat("width", 32.0);
    p.setFloat("height", 13.0);
    p.setFloat("depth", 25.0);
    p.setInt("chambers", 7);
    p.setInt("branches", 6);
    p.setFloat("tunnelRadius", 0.18);
    p.setFloat("chamberScale", 1.22);
    p.setFloat("chamberHierarchy", 0.92);
    p.setFloat("passageVariation", 0.82);
    p.setFloat("chamberIrregularity", 0.72);
    p.setFloat("roughness", 0.10);
    p.setFloat("multiscaleRoughness", 0.70);
    p.setFloat("roughnessFlowCoupling", 0.72);
    p.setFloat("surfaceSlopeReactivity", 0.74);
    p.setFloat("reactivePatchiness", 0.68);
    p.setFloat("erosion", 0.72);
    p.setFloat("bedding", 0.68);
    p.setFloat("fractureDissolution", 0.62);
    p.setFloat("fractureApertureVariability", 0.76);
    p.setFloat("fractureStressControl", 0.82);
    p.setFloat("fractureFlowFeedback", 0.74);
    p.setFloat("vadoseIncision", 0.48);
    p.setFloat("waterTableCorrosion", 0.72);
    p.setFloat("waterTableLevel", 0.26);
    p.setInt("waterTableStages", 3);
    p.setFloat("waterTableDrop", 0.19);
    p.setFloat("waterTableFluctuation", 0.42);
    p.setFloat("scallopErosion", 0.72);
    p.setFloat("scallopScale", 0.11);
    p.setFloat("scallopHydraulicScaling", 1.0);
    p.setFloat("scallopMaturity", 0.68);
    p.setFloat("scallopScaleVariability", 0.72);
    p.setFloat("scallopFlowSeparation", 0.78);
    p.setFloat("scallopFlowHistory", 0.68);
    p.setFloat("bendUndercut", 0.72);
    p.setFloat("fragmentDetachment", 1.0);
    p.setFloat("curvatureDissolution", 0.72);
    p.setFloat("reactiveSurfaceCoupling", 0.88);
    p.setFloat("hydraulicErosion", 0.86);
    p.setFloat("mixingCorrosion", 0.76);
    p.setFloat("lithologicHeterogeneity", 0.82);
    p.setFloat("floodAbrasion", 0.78);
    p.setFloat("sedimentLoad", 0.48);
    p.setFloat("floodPlucking", 0.66);
    p.setFloat("pluckingBlockScale", 0.12);
    p.setFloat("constrictionScour", 0.78);
    p.setFloat("knickpointErosion", 0.74);
    p.setFloat("streamBedKarren", 0.72);
    p.setFloat("eddyPotholes", 0.70);
    p.setFloat("potholeGravelSize", 0.42);
    p.setFloat("breakdownScour", 0.68);
    p.setFloat("hydraulicGradient", 0.52);
    p.setFloat("recharge", 0.78);
    p.setFloat("flowFocusing", 0.82);
    p.setFloat("damkohler", 0.0035);
    p.setFloat("transportG", 1.6);
    p.setFloat("microstructure", 0.78);
    p.setFloat("microporosityAccess", caveMicroporosity[caveRockIndex]);
    p.setFloat("permeabilityContrast", cavePermeabilityContrast[caveRockIndex]);
    p.setInt("fractureCount", 7);
    p.setInt("cupolas", 7);
    p.setInt("feeders", 4);
    p.setInt("dripstones", 4);
    p.setFloat("dripstoneScale", 0.90);
    p.setString("stalagmiteShape", "conical");
    p.setFloat("normalSmoothing", 0.86);
    p.setString("surfaceNormalMode", "densityGradient");
    p.setInt("wetnessRefinement", 1);
    p.setFloat("boundaryClosure", 1.0);
    p.setFloat("condensationCorrosion", 0.72);
    p.setFloat("biogenicCorrosion", 0.68);
    p.setFloat("mineralArmoring", 0.52);
    p.setFloat("condensationFaceting", 0.62);
    p.setFloat("differentialVeinErosion", 0.58);
    p.setFloat("breakdown", 0.68);
    p.setInt("breakdownEvents", 5);
    p.setFloat("sedimentDeposition", 0.64);
    p.setFloat("paragenesis", 0.7);
    p.setInt("sedimentBars", 5);
    p.setInt("isosurfaceSampling", 2);
    p.setInt("surfaceRefinement", 2);
    p.setFloat("refinementThreshold", 0.0015);
    p.setInt("flowstones", 2);
    p.setInt("curtains", 1);
    p.setFloat("flowstoneScale", 1.0);
    local buildResult = procgen.buildMesh("mesh.cave", p);
    if (!buildResult.ok) { print("cave generation failed: " + buildResult.status.summary); return; }
    local build = buildResult.value;
    local tints = {
        caveWalls=[0.34, 0.30, 0.25], speleothems=[0.43, 0.39, 0.32],
        wetWalls=[0.30, 0.31, 0.29], breakdown=[0.28, 0.25, 0.21],
        sediment=[0.40, 0.29, 0.18]
    };
    for (local i = 0; i < build.getGroupCount(); ++i) {
        local component = build.copyGroup(i);
        if (component == null) continue;
        local uploadResult = procgen.uploadMesh(component, gfx);
        if (!uploadResult.ok) { print("cave upload failed: " + uploadResult.status.summary); continue; }
        while (caveParts.len() <= i) caveParts.append(eve.Renderable3D());
        local name = build.getGroupName(i);
        if (!(name in caveMaterials)) {
            local material = gfx.newMaterial();
            local tint = name in tints ? tints[name] : [0.5, 0.4, 0.28];
            material.setTint(tint[0], tint[1], tint[2], 1.0);
            material.setRoughness(name == "wetWalls" ? 0.48 :
                                  (name == "speleothems" ? 0.76 :
                                   ((name == "breakdown" || name == "sediment") ? 0.96 : 0.88)));
            material.setMetallic(0.0);
            material.setDoubleSided(false);
            caveMaterials[name] <- material;
        }
        caveParts[i].setMesh(uploadResult.value);
        caveParts[i].setMaterial(caveMaterials[name]);
        caveParts[i].setCastShadow(name == "speleothems" || name == "breakdown");
        caveParts[i].setReceiveShadow(true);
    }
    print("generated " + caveStyles[caveStyleIndex] + " cave in " + caveRockNames[caveRockIndex] +
          " limestone, seed=" + caveSeed + "\n");
}

if (caveCamera == null) {
    caveCamera = eve.Camera3D();
    caveCamera.setEye(-10.0, 0.2, 7.0);
    caveCamera.setTarget(2.0, -1.4, -1.0);
    caveCamera.setUp(0.0, 1.0, 0.0);
    caveCamera.setFov(52.0);
    caveCamera.setClipPlanes(0.15, 180.0);
    caveCamera.setAmbient(0.12, 0.13, 0.14);
    caveCamera.setActive(true);
    local entranceLight = eve.Light3D(); entranceLight.setType("dir");
    entranceLight.setDirection(-0.62, 0.78, 0.30);
    entranceLight.setColor(0.72, 0.82, 1.0, 0.48);
    entranceLight.setCastShadow(false); caveLights.append(entranceLight);
    local keyLight = eve.Light3D(); keyLight.setType("point");
    keyLight.setPosition(-5.0, 0.2, 2.0); keyLight.setRadius(18.0);
    keyLight.setColor(1.0, 0.61, 0.34, 1.85); caveLights.append(keyLight);
    local fillLight = eve.Light3D(); fillLight.setType("point");
    fillLight.setPosition(7.0, 2.5, -3.0); fillLight.setRadius(20.0);
    fillLight.setColor(0.34, 0.45, 0.54, 0.82); caveLights.append(fillLight);

    caveScaleMarker = eve.Renderable3D();
    caveScaleMarker.setMesh(gfx.newMeshCube(1.0));
    caveScaleMarker.setPosition(-2.0, -4.35, 0.5);
    caveScaleMarker.setScale(0.42, 1.35, 0.34);
    caveScaleMarker.setTint(0.08, 0.10, 0.12, 1.0);
    caveScaleMarker.setRoughness(0.78);
    caveScaleMarker.setCastShadow(true); caveScaleMarker.setReceiveShadow(true);
    caveScaleHead = eve.Renderable3D();
    caveScaleHead.setMesh(gfx.newMeshSphere(20, 12));
    caveScaleHead.setPosition(-2.0, -3.25, 0.5);
    caveScaleHead.setScale(0.31, 0.34, 0.31);
    caveScaleHead.setTint(0.10, 0.12, 0.14, 1.0);
    caveScaleHead.setRoughness(0.72);
    caveScaleHead.setCastShadow(true); caveScaleHead.setReceiveShadow(true);
}
if (caveParts.len() == 0) rebuildCave();
gfx.setBackgroundColor(0.018, 0.014, 0.012, 1.0);

function eve_update(dt) {
    caveCaptureFrame += 1;
    local captureNames = ["entrance", "chamber", "detail"];
    local captureEyes = [[-10.0, 0.2, 7.0], [-7.0, -0.4, -5.0], [7.0, -0.6, 4.0]];
    local captureTargets = [[2.0, -1.4, -1.0], [2.0, -1.2, 0.0], [-1.0, -1.4, -1.0]];
    if (caveCaptureIndex < captureNames.len() && caveCaptureFrame > 12) {
        local name = captureNames[caveCaptureIndex];
        if (gfx.saveFramePng("../../build/cave-debug/cave-generator-" + name + ".png")) {
            print("cave-generator: saved " + name + " QA frame\n");
            caveCaptureIndex += 1;
            caveCaptureFrame = 0;
            if (caveCaptureIndex < captureNames.len()) {
                local eye = captureEyes[caveCaptureIndex];
                local target = captureTargets[caveCaptureIndex];
                caveCamera.setEye(eye[0], eye[1], eye[2]);
                caveCamera.setTarget(target[0], target[1], target[2]);
            }
        }
    }
    if (cavePressed("r") || cavePressed("R")) { caveSeed += 1; rebuildCave(); }
    for (local i = 0; i < caveStyles.len(); ++i) {
        local key = (i + 1).tostring();
        if (cavePressed(key)) { caveStyleIndex = i; rebuildCave(); }
    }
    for (local i = 0; i < caveRockNames.len(); ++i) {
        local key = (i + 6).tostring();
        if (cavePressed(key)) { caveRockIndex = i; rebuildCave(); }
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
