// Runtime comparison scene for TAA, SSR, RTGI and the automatic reflection chain.
// Run: eve run --debug --mcp-port=7529 examples/rendering-chain-lab
// Space toggles the complete chain; R resets the camera.

if (!("labCamera" in getroottable())) labCamera <- null;
if (!("labObjects" in getroottable())) labObjects <- [];
if (!("labMover" in getroottable())) labMover <- null;
if (!("labFillWarm" in getroottable())) labFillWarm <- null;
if (!("labFillCool" in getroottable())) labFillCool <- null;
if (!("labTime" in getroottable())) labTime <- 0.0;
if (!("labEffects" in getroottable())) labEffects <- true;
if (!("labUiBuilt" in getroottable())) labUiBuilt <- false;

function makeBox(x, y, z, sx, sy, sz, r, g, b, metallic, roughness) {
    local o = eve.Renderable3D();
    o.setMesh(gfx.newMeshCube(1.0));
    o.setPosition(x, y, z);
    o.setScale(sx, sy, sz);
    o.setTint(r, g, b, 1.0);
    o.setMetallic(metallic);
    o.setRoughness(roughness);
    labObjects.append(o);
    return o;
}

function makeSphere(x, y, z, r, g, b, metallic, roughness) {
    local o = eve.Renderable3D();
    o.setMesh(gfx.newMeshSphere(40, 24));
    o.setPosition(x, y, z);
    o.setScale(0.78, 0.78, 0.78);
    o.setTint(r, g, b, 1.0);
    o.setMetallic(metallic);
    o.setRoughness(roughness);
    labObjects.append(o);
    return o;
}

function configureEffects(enabled) {
    labEffects = enabled;
    local rc = gfx.getRenderControl();
    rc.setPostProcessQuality("high");
    local features = ["gi", "rtgi", "aa", "taa", "ssr", "reflectionChain"];
    foreach (feature in features) {
        if (enabled) rc.enable(feature);
        else rc.disable(feature);
    }
    rc.compile();
    if (labUiBuilt)
        ui.setText("mode", enabled ? "FULL: TAA + SSR + RTGI + reflection chain" : "BASELINE: effects disabled");
}

function resetCamera() {
    labCamera.setEye(0.0, 4.4, 12.8);
    labCamera.setTarget(0.0, 1.0, -0.8);
}

function buildScene() {
    labObjects.clear();

    // Reflective floor and a neutral rear wall make screen-space hits obvious.
    makeBox(0.0, -0.18, -0.5, 14.0, 0.18, 10.0, 0.16, 0.18, 0.21, 0.72, 0.12);
    makeBox(0.0, 3.1, -4.8, 14.0, 6.4, 0.18, 0.20, 0.22, 0.26, 0.08, 0.72);

    // Saturated blockers expose indirect-light color bleeding.
    makeBox(-5.2, 1.35, -1.2, 0.45, 2.8, 4.8, 0.82, 0.055, 0.035, 0.05, 0.62);
    makeBox(5.2, 1.35, -1.2, 0.45, 2.8, 4.8, 0.035, 0.20, 0.82, 0.05, 0.62);
    makeBox(0.0, 2.9, -3.95, 2.1, 0.32, 0.35, 1.0, 0.58, 0.08, 0.25, 0.28);

    local roughness = [0.06, 0.16, 0.30, 0.52, 0.82];
    local colors = [
        [0.98, 0.82, 0.48], [0.88, 0.92, 1.0], [0.92, 0.44, 0.20],
        [0.36, 0.72, 0.95], [0.64, 0.78, 0.52]
    ];
    for (local i = 0; i < 5; ++i) {
        local x = (i - 2) * 2.05;
        makeSphere(x, 0.78, -0.7, colors[i][0], colors[i][1], colors[i][2], 0.48, roughness[i]);
        makeBox(x, 0.08, -0.7, 1.35, 0.16, 1.35, 0.24, 0.26, 0.30, 0.78, 0.18);
    }

    // Thin moving geometry makes temporal shimmer/ghosting visible.
    labMover = makeBox(0.0, 2.05, -2.55, 2.5, 0.055, 0.055, 0.95, 0.98, 1.0, 0.10, 0.18);
}

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.075, 0.115, 1.0);
    gfx.setDirectionalLight(-0.42, -1.0, -0.28, 2.15, 1.92, 1.62);

    labFillWarm = eve.Light3D();
    labFillWarm.setType("point");
    labFillWarm.setPosition(-4.8, 3.4, 4.5);
    labFillWarm.setColor(1.0, 0.62, 0.38, 2.4);
    labFillWarm.setRadius(14.0);

    labFillCool = eve.Light3D();
    labFillCool.setType("point");
    labFillCool.setPosition(4.6, 2.8, 3.2);
    labFillCool.setColor(0.38, 0.68, 1.0, 1.8);
    labFillCool.setRadius(13.0);

    labCamera = eve.Camera3D();
    labCamera.setUp(0.0, 1.0, 0.0);
    labCamera.setFov(48.0);
    labCamera.setAmbient(0.30, 0.34, 0.42);
    labCamera.setExposure(0.85);
    labCamera.setBloom(0.18, 1.15);
    labCamera.setActive(true);
    resetCamera();
    buildScene();

    if (!labUiBuilt) {
        ui.beginBuild();
        ui.beginWindow("RenderingChainLab", "root");
        ui.text("EVENGINE / REFLECTION CHAIN LAB", "title");
        ui.text("", "mode");
        ui.text("Space: toggle effects    R: reset camera", "help");
        ui.end();
        ui.mountBuildAs("render-lab-hud");
        ui.select("render-lab-hud");
        ui.setHostOverlay(true);
        ui.setHostPos(18.0, 16.0, 0.0, 0.0);
        labUiBuilt = true;
    }
    configureEffects(true);
};

eve_update = function(dt) {
    labTime += dt;
    if (labMover != null)
        labMover.setPosition(sin(labTime * 0.82) * 2.8, 2.05 + cos(labTime * 1.27) * 0.16, -2.55);
    if (key_just_pressed("space")) configureEffects(!labEffects);
    if (key_just_pressed("r") || key_just_pressed("R")) resetCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
