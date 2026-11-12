dofile("visuals.nut");
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
        walls=[0.32,0.34,0.30], battlements=[0.43,0.44,0.39],
        towers=[0.29,0.32,0.29], gatehouses=[0.25,0.28,0.25],
        stairs=[0.28,0.27,0.23], keep=[0.37,0.35,0.30], courtyard=[0.38,0.30,0.22]
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
        part.setVisible(true);
        part.setMesh(mesh);
        part.setTint(tint[0], tint[1], tint[2], 1.0);
        part.setRoughness(0.92);
        part.setMetallic(0.01);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
    }
    for(local i=build.getGroupCount();i<castleParts.len();i++)castleParts[i].setVisible(false);
    castleUpdateRoof();
}

if (castleCamera == null) {
    castleCamera = eve.Camera3D();
    castleCamera.setActive(true);
}
// Default far plane is 100 m; a rotating keep with outer rings sits past that.
castleCamera.setEye(61.0, 44.0, -72.0);
castleCamera.setTarget(0.0, 7.0, 0.0);
castleCamera.setUp(0.0, 1.0, 0.0);
castleCamera.setFov(38.0);
castleCamera.setClipPlanes(0.5, 2000.0);
castleCamera.setAmbient(0.23, 0.25, 0.29);
castleCamera.setExposure(-0.3);
gfx.setDirectionalLight(-0.55, 1.0, -0.35, 1.0, 0.95, 0.84);
if (castleParts.len() == 0) {rebuildCastle();castleBuildLandscape();}
gfx.setBackgroundColor(0.10, 0.14, 0.20, 1.0);
ui.beginBuild();ui.beginWindow("CASTLE FORGE","root");
ui.text("CASTLE FORGE / STONE & SLATE","title");ui.text("","stats");
ui.text("R reseed | 1/2 rings | 3/4 floors | D detail","help");
ui.text("Hold O to orbit","orbit");ui.end();ui.mountBuildAs("castle-hud");ui.select("castle-hud");
ui.setHostOverlay(true);ui.setHostPos(16.0,16.0,0.0,0.0);ui.setHostSize(470.0,150.0);


function eve_update(dt) {
    ui.select("castle-hud");ui.setText("stats","Seed "+castleSeed+" / rings "+rings+" / keep floors "+floors);
    // Keep the castle and its environment in one coordinate frame.
    if (keyboard.isDown("o")) castleYaw += dt*0.3;
    castleCamera.setEye(sin(2.44+castleYaw)*94.0,44.0,cos(2.44+castleYaw)*94.0);
    if (castlePressed("r") || castlePressed("R")) { castleSeed += 1; rebuildCastle(); }
    if (castlePressed("1")) { rings = rings>1?rings-1:1; rebuildCastle(); }
    if (castlePressed("2")) { rings = rings<4?rings+1:4; rebuildCastle(); }
    if (castlePressed("3")) { floors = floors>1?floors-1:1; rebuildCastle(); }
    if (castlePressed("4")) { floors = floors<8?floors+1:8; rebuildCastle(); }
    if (castlePressed("d") || castlePressed("D")) { detail = (detail + 1) % 3; rebuildCastle(); }
}

function eve_render() { gfx.clear(); gfx.render3D(); ui.beginFrameAndRender(); }
