// Castle Forge — interactive mesh.castle showcase.
// R seed, 1/2 wall rings, 3/4 keep floors, D detail level.

persist castleSeed = 20260826
persist castleParts = []
persist castleCamera = null
persist rings = 2
persist floors = 4
persist detail = 2
persist castleYaw = 0.0
persist castlePrevKeys = {}

function castlePressed(k) {
    local down = keyboard.isDown(k);
    local old = k in castlePrevKeys ? castlePrevKeys[k] : false;
    castlePrevKeys[k] <- down;
    return down && !old;
}

function rebuildCastle() {
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) { print("castle parameter creation failed: " + paramsResult.status.summary); return; }
    local p = paramsResult.value;
    p.setSeed(castleSeed);
    p.setFloat("width", 48.0);
    p.setFloat("depth", 40.0);
    p.setInt("rings", rings);
    p.setFloat("wallHeight", 6.0);
    p.setFloat("wallThickness", 1.8);
    p.setFloat("towerRadius", 3.2);
    p.setFloat("towerHeight", 9.0);
    p.setInt("towerSides", 12);
    p.setFloat("towerSpacing", 15.0);
    p.setFloat("gateWidth", 5.0);
    p.setInt("keepFloors", floors);
    p.setFloat("stairWidth", 2.0);
    p.setFloat("stepHeight", 0.28);
    p.setInt("detail", detail);
    local buildResult = procgen.buildMesh("mesh.castle", p);
    if (!buildResult.ok) { print("castle generation failed: " + buildResult.status.summary); return; }
    local build = buildResult.value;
    local tints = {
        walls=[0.74,0.69,0.58], battlements=[0.82,0.76,0.64],
        towers=[0.66,0.61,0.52], gatehouses=[0.58,0.53,0.46],
        stairs=[0.48,0.43,0.36], keep=[0.71,0.65,0.54], courtyard=[0.60,0.55,0.45]
    };
    for (local i = 0; i < build.getGroupCount(); ++i) {
        local component = build.copyGroup(i);
        if (component == null) continue;
        local meshResult = procgen.uploadMesh(component, gfx);
        if (!meshResult.ok) { print("component upload failed: " + meshResult.status.summary); continue; }
        local mesh = meshResult.value;
        while (castleParts.len() <= i) castleParts.append(eve.Renderable3D());
        local part = castleParts[i];
        local name = build.getGroupName(i);
        local tint = name in tints ? tints[name] : [0.68,0.64,0.55];
        part.setMesh(mesh);
        part.setTint(tint[0], tint[1], tint[2], 1.0);
        part.setRoughness(0.92);
        part.setMetallic(0.01);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
    }
}

if (castleCamera == null) {
    castleCamera = eve.Camera3D();
    castleCamera.setEye(54.0, 34.0, -58.0);
    castleCamera.setTarget(0.0, 5.0, 0.0);
    castleCamera.setUp(0.0, 1.0, 0.0);
    castleCamera.setFov(43.0);
    castleCamera.setAmbient(0.23, 0.25, 0.29);
    castleCamera.setActive(true);
    gfx.setDirectionalLight(-0.55, -1.0, -0.35, 1.45, 1.30, 1.08);
}
if (castleParts.len() == 0) rebuildCastle();
gfx.setBackgroundColor(0.10, 0.14, 0.20, 1.0);

function eve_update(dt) {
    castleYaw += dt * 3.5;
    foreach (part in castleParts) part.setYaw(castleYaw);
    if (castlePressed("r") || castlePressed("R")) { castleSeed += 1; rebuildCastle(); }
    if (castlePressed("1")) { rings = max(1, rings - 1); rebuildCastle(); }
    if (castlePressed("2")) { rings = min(4, rings + 1); rebuildCastle(); }
    if (castlePressed("3")) { floors = max(1, floors - 1); rebuildCastle(); }
    if (castlePressed("4")) { floors = min(8, floors + 1); rebuildCastle(); }
    if (castlePressed("d") || castlePressed("D")) { detail = (detail + 1) % 3; rebuildCastle(); }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
