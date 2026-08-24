// Screen-space model outline lab — t3ssel8r-style outline from the G-buffer
// depth + normal buffers. Tweak the knobs at the top to customize the line:
//   - outlineColor : RGB outline color
//   - outlineWidth : line thickness in screen pixels
//   - outlineDepthThreshold : silhouette/occlusion depth discontinuity
//   - outlineDepthSensitivity : extra per-distance depth tolerance
//   - outlineNormalThreshold : crease angle (1 - dot(n,nN))
//   - outlineSoftness : edge fade (0 hard .. 1 soft)
//
// Controls: 1 toggle outline, [ / ] thickness, C cycle color.

// --- Outline configuration ------------------------------------------------
if (!("outlineColor" in getroottable())) outlineColor <- [0.05, 0.04, 0.07];
if (!("outlineWidth" in getroottable())) outlineWidth <- 1.6;
if (!("outlineDepthThreshold" in getroottable())) outlineDepthThreshold <- 0.3;
if (!("outlineDepthSensitivity" in getroottable())) outlineDepthSensitivity <- 0.0;
if (!("outlineNormalThreshold" in getroottable())) outlineNormalThreshold <- 0.4;
if (!("outlineSoftness" in getroottable())) outlineSoftness <- 0.2;
if (!("outlineEnabled" in getroottable())) outlineEnabled <- true;

if (!("OUTLINE_COLORS" in getroottable()))
    OUTLINE_COLORS <- [
        [0.05, 0.04, 0.07],  // ink
        [0.97, 0.95, 0.90],  // paper
        [0.85, 0.10, 0.12],  // red
        [0.10, 0.35, 0.85],  // blue
    ];

// --- Persistent handles (kept across soft reloads) -------------------------
if (!("olCube" in getroottable())) olCube <- null;
if (!("olCamera" in getroottable())) olCamera <- null;
if (!("olYaw" in getroottable())) olYaw <- 0.0;
if (!("olColorIdx" in getroottable())) olColorIdx <- 0;
function olPressed(k) {
    return key_just_pressed(k);
}

function applyOutlineConfig() {
    local o = gfx.getOutline();
    o.setColor(outlineColor[0], outlineColor[1], outlineColor[2]);
    o.setWidth(outlineWidth);
    o.setDepthThreshold(outlineDepthThreshold);
    o.setDepthSensitivity(outlineDepthSensitivity);
    o.setNormalThreshold(outlineNormalThreshold);
    o.setSoftness(outlineSoftness);
}

// --- One-time setup --------------------------------------------------------
if (olCamera == null) {
    olCamera = eve.Camera3D();
    olCamera.setEye(2.6, 1.9, 3.4);
    olCamera.setTarget(0.0, 0.0, 0.0);
    olCamera.setUp(0.0, 1.0, 0.0);
    olCamera.setFov(42.0);
    olCamera.setAmbient(0.30, 0.30, 0.32);
    olCamera.setActive(true);
    gfx.setDirectionalLight(-0.4, -1.0, -0.35, 1.25, 1.15, 1.0);

    local rc = gfx.getRenderControl();
    rc.disable("ao");
    rc.disable("gi");
    rc.enable("outline");
    rc.compile();
}

if (olCube == null) {
    olCube = eve.Renderable3D();
    olCube.setMesh(gfx.newMeshCube(1.0));
    olCube.setPosition(0.0, 0.0, 0.0);
    olCube.setScale(1.0, 1.0, 1.0);
    olCube.setTint(0.92, 0.90, 0.86, 1.0);
    olCube.setRoughness(0.7);
    olCube.setCastShadow(true);
    olCube.setReceiveShadow(true);
}

applyOutlineConfig();
gfx.setBackgroundColor(0.13, 0.14, 0.17, 1.0);

function eve_update(dt) {
    olYaw += dt * 40.0;
    olCube.setYaw(olYaw);

    if (olPressed("1")) {
        outlineEnabled = !outlineEnabled;
        local rc = gfx.getRenderControl();
        if (outlineEnabled) rc.enable("outline"); else rc.disable("outline");
        rc.compile();
    }
    if (olPressed("leftbracket")) { outlineWidth = max(0.5, outlineWidth - 0.25); applyOutlineConfig(); }
    if (olPressed("rightbracket")) { outlineWidth = min(6.0, outlineWidth + 0.25); applyOutlineConfig(); }
    if (olPressed("c") || olPressed("C")) {
        olColorIdx = (olColorIdx + 1) % OUTLINE_COLORS.len();
        outlineColor = OUTLINE_COLORS[olColorIdx];
        applyOutlineConfig();
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
    gfx.drawSolidRect(20.0, 20.0, 260.0, 64.0, 0.94, 0.95, 0.90, 0.94);
    gfx.drawSolidRect(24.0, 24.0, 8.0, 8.0, outlineColor[0], outlineColor[1], outlineColor[2], 1.0);
}
