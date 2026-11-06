// ============================================================================
// EVEngine Warship example — Type 055 presentation + rearrangeable compartments
//
// Simulation: eve.Warship() (hull defs, layout, power/thrust projection, moveTo).
// Presentation: repository assets/models/type055 OBJ (custom 3D view layer).
//
// Run: make run/<platform>-debug GAME=examples/warship
// ============================================================================

dofile("type055.nut");

persist warship = null
persist destroyer = null
persist clipper = null
persist carrier = null
persist cruiser = null
persist selected = 0
persist bootstrapped = false
persist hudFont = null
persist logLines = []
persist patrolIndex = 0
persist prevKeys = {}
persist camera = null
persist sea = null
persist visuals = null
persist viewMode = "chase"

patrols <- [
    { x = 420.0, y = -80.0 },
    { x = 420.0, y = 280.0 },
    { x = -80.0, y = 280.0 },
    { x = -80.0, y = -80.0 }
];

functionDefs <- @"[
  {""id"":""bridge"",""name"":""Bridge"",""powerDraw"":2,""crewCapacity"":8},
  {""id"":""reactor"",""name"":""Reactor"",""powerProvide"":12},
  {""id"":""engine"",""name"":""Engine"",""powerDraw"":4,""thrustBonus"":0.25},
  {""id"":""hangar"",""name"":""Hangar"",""powerDraw"":3,""hangarCapacity"":6},
  {""id"":""weapon_bay"",""name"":""Weapon Bay"",""powerDraw"":2,""weaponBayCount"":2},
  {""id"":""quarters"",""name"":""Quarters"",""powerDraw"":1,""crewCapacity"":20},
  {""id"":""sail_rig"",""name"":""Sail Rig"",""sailPower"":0.8,""thrustBonus"":0.05},
  {""id"":""shield"",""name"":""Shield"",""powerDraw"":5}
]";

// Destroyer length matches the Type 055 asset (180 m). Other hulls keep category demos.
hullDefs <- @"[
  {""id"":""destroyer"",""category"":""warship"",""mobility"":""surface"",
   ""length"":180,""beam"":20,""maxSpeed"":48,""accel"":14,""turnRate"":22,""maxHealth"":1200,
   ""grid"":{""cols"":4,""rows"":2,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
     {""deck"":0,""row"":1,""col"":0,""function"":""engine""},
     {""deck"":0,""row"":1,""col"":1,""function"":""weapon_bay""}
   ]},
  {""id"":""clipper"",""category"":""sailship"",""mobility"":""sail"",
   ""length"":110,""beam"":16,""maxSpeed"":36,""accel"":8,""turnRate"":28,""maxHealth"":400,
   ""grid"":{""cols"":3,""rows"":1,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""sail_rig""},
     {""deck"":0,""row"":0,""col"":2,""function"":""quarters""}
   ]},
  {""id"":""carrier"",""category"":""carrier"",""mobility"":""carrier"",
   ""length"":260,""beam"":40,""maxSpeed"":30,""accel"":6,""turnRate"":10,""maxHealth"":2200,
   ""grid"":{""cols"":5,""rows"":2,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
     {""deck"":0,""row"":1,""col"":0,""function"":""hangar""},
     {""deck"":0,""row"":1,""col"":1,""function"":""hangar""},
     {""deck"":0,""row"":1,""col"":2,""function"":""engine""}
   ]},
  {""id"":""cruiser"",""category"":""starship"",""mobility"":""space"",
   ""length"":200,""beam"":28,""maxSpeed"":70,""accel"":20,""turnRate"":30,""maxHealth"":1600,
   ""grid"":{""cols"":4,""rows"":2,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
     {""deck"":0,""row"":0,""col"":2,""function"":""shield""},
     {""deck"":0,""row"":1,""col"":0,""function"":""engine""}
   ]}
]";

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 8)
        logLines.remove(0);
    print("[warship] " + text + "\n");
}

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local prev = (name in prevKeys) ? prevKeys[name] : false;
    prevKeys[name] <- down;
    return down && !prev;
}

function currentShip() {
    if (selected == 1) return clipper;
    if (selected == 2) return carrier;
    if (selected == 3) return cruiser;
    return destroyer;
}

function currentVisual() {
    if (visuals == null) return null;
    if (selected == 1) return visuals.clipper;
    if (selected == 2) return visuals.carrier;
    if (selected == 3) return visuals.cruiser;
    return visuals.destroyer;
}

function makeVisual(scale) {
    local loaded = warshipLoadType055();
    foreach (part in loaded.parts)
        part.setScale(scale, scale, scale);
    return { parts = loaded.parts, scale = scale };
}

function syncVisual(visual, ship) {
    if (visual == null || ship == null) return;
    local x = warship.getX(ship);
    local z = warship.getY(ship); // 2D sim Y → world Z
    local heading = warship.getHeading(ship) * 3.14159265 / 180.0;
    // glm Y-up: yaw maps model +X → (cos yaw, 0, -sin yaw); want (cos h, 0, sin h).
    local yaw = -heading;
    foreach (part in visual.parts) {
        part.setPosition(x, 0.0, z);
        part.setRotation(yaw, 0.0, 0.0);
        part.setVisible(true);
    }
}

function updateCamera() {
    local ship = currentShip();
    if (ship == null || camera == null) return;
    local x = warship.getX(ship);
    local z = warship.getY(ship);
    local heading = warship.getHeading(ship) * 3.14159265 / 180.0;
    local fx = cos(heading);
    local fz = sin(heading);
    if (viewMode == "top") {
        camera.setEye(x, 520.0, z - 40.0);
        camera.setTarget(x, 0.0, z);
        return;
    }
    // Bow-quarter chase: behind and above, looking toward the bow.
    local eyeX = x - fx * 220.0 + fz * 140.0;
    local eyeZ = z - fz * 220.0 - fx * 140.0;
    camera.setEye(eyeX, 95.0, eyeZ);
    camera.setTarget(x + fx * 40.0, 8.0, z + fz * 40.0);
}

function ensureSetup() {
    if (bootstrapped) return;
    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
    }

    if (!hudFont) {
        local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 20);
        hudFont = gfx.newFont(fontData,
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%_x");
    }

    gfx.setBackgroundColor(0.42, 0.62, 0.78, 1.0);
    camera = eve.Camera3D();
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setClipPlanes(1.0, 20000.0);
    camera.setAmbient(0.28, 0.32, 0.38);
    camera.setExposure(0.55);
    camera.setActive(true);

    local sun = eve.Light3D();
    sun.setType("directional");
    sun.setDirection(-0.45, -1.0, 0.35);
    sun.setColor(1.0, 0.96, 0.9, 0.85);
    sun.setCastShadow(true);
    sun.setShadowMethod("csm");
    sun.setShadowStrength(0.7);

    sea = eve.Renderable3D();
    sea.setMesh(gfx.newMeshCube(1.0));
    sea.setPosition(0.0, -0.4, 0.0);
    sea.setScale(4000.0, 0.2, 4000.0);
    sea.setTint(0.12, 0.28, 0.38, 1.0);
    sea.setMetallic(0.2);
    sea.setRoughness(0.25);
    sea.setCastShadow(false);
    sea.setReceiveShadow(true);

    local rc = gfx.getRenderControl();
    foreach (feature in ["shadow", "gbuffer", "gbufferAlbedo", "ao", "aa"])
        rc.enable(feature);
    rc.setPostProcessQuality("medium");
    gfx.setMsaaSamples(4);
    rc.enable("msaa");
    rc.compile();

    warship = eve.Warship();
    warship.clearHullDefinitions();
    warship.clearFunctionDefinitions();
    local fn = warship.registerFunctionsFromJson(functionDefs);
    if (!fn.ok) throw fn.status.summary;
    local hull = warship.registerHullsFromJson(hullDefs);
    if (!hull.ok) throw hull.status.summary;

    destroyer = warship.newWarship("destroyer", 0.0, 0.0, 0.0, "blue");
    clipper = warship.newWarship("clipper", -40.0, 220.0, 25.0, "teal");
    carrier = warship.newWarship("carrier", 260.0, 40.0, -15.0, "gray");
    cruiser = warship.newWarship("cruiser", 200.0, 260.0, 40.0, "violet");

    // Type 055 mesh per hull; non-destroyer scales approximate category length only.
    visuals = {
        destroyer = makeVisual(1.0),
        clipper = makeVisual(110.0 / 180.0),
        carrier = makeVisual(260.0 / 180.0),
        cruiser = makeVisual(200.0 / 180.0)
    };

    syncVisual(visuals.destroyer, destroyer);
    syncVisual(visuals.clipper, clipper);
    syncVisual(visuals.carrier, carrier);
    syncVisual(visuals.cruiser, cruiser);
    updateCamera();

    selected = 0;
    bootstrapped = true;
    pushLog("Type 055 presentation ready — 1..4 select, Space move, H hangar, R remodel, C clear, T view");
}

eve_init <- function() {
    ensureSetup();
}

eve_update <- function(dt) {
    ensureSetup();
    if (keyPressed("1")) { selected = 0; pushLog("selected destroyer (055)"); }
    if (keyPressed("2")) { selected = 1; pushLog("selected clipper (scaled 055)"); }
    if (keyPressed("3")) { selected = 2; pushLog("selected carrier (scaled 055)"); }
    if (keyPressed("4")) { selected = 3; pushLog("selected cruiser (scaled 055)"); }
    if (keyPressed("t")) {
        viewMode = (viewMode == "top") ? "chase" : "top";
        pushLog("view=" + viewMode);
    }

    local ship = currentShip();
    if (ship != null) {
        if (keyPressed("space") || keyPressed("SPACE")) {
            local p = patrols[patrolIndex % patrols.len()];
            patrolIndex += 1;
            warship.moveTo(ship, p.x, p.y);
            pushLog("moveTo " + p.x + "," + p.y);
        }
        if (keyPressed("h")) {
            local r = warship.placeCompartment(ship, 0, 0, 3, "hangar");
            if (r.ok) pushLog("placed hangar; hangar=" + warship.getHangarCapacity(ship));
            else pushLog("hangar place failed: " + r.status.summary);
        }
        if (keyPressed("r")) {
            local layout = @"[
              {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
              {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
              {""deck"":0,""row"":0,""col"":2,""function"":""hangar""},
              {""deck"":0,""row"":1,""col"":0,""function"":""engine""},
              {""deck"":0,""row"":1,""col"":1,""function"":""weapon_bay""}
            ]";
            local r = warship.rearrangeFromJson(ship, layout);
            if (r.ok) pushLog("rearranged layout; power=" + warship.getPowerNet(ship));
            else pushLog("rearrange failed: " + r.status.summary);
        }
        if (keyPressed("c")) {
            local r = warship.clearCompartment(ship, 0, 0, 1);
            if (r.ok) pushLog("cleared reactor cell; underpowered=" + warship.isUnderpowered(ship));
            else pushLog("clear failed: " + r.status.summary);
        }
    }

    warship.update(dt);
    for (local i = 0; i < warship.getEventCount(); i += 1) {
        pushLog("event " + warship.getEventType(i) + " " + warship.getEventDetail(i));
    }
    warship.clearEvents();

    syncVisual(visuals.destroyer, destroyer);
    syncVisual(visuals.clipper, clipper);
    syncVisual(visuals.carrier, carrier);
    syncVisual(visuals.cruiser, cruiser);
    updateCamera();
}

eve_render <- function() {
    ensureSetup();
    gfx.clear();
    gfx.render3D();

    if (hudFont) {
        local ship = currentShip();
        local lines = [];
        lines.push("Warship + Type 055 — 1..4 select | Space move | H hangar | R remodel | C clear | T view");
        if (ship != null) {
            lines.push("sel=" + warship.getDefId(ship) + " cat=" + warship.getCategory(ship) +
                       " power=" + warship.getPowerNet(ship) +
                       " thrust=" + warship.getThrustMul(ship));
            lines.push("hangar=" + warship.getHangarCapacity(ship) +
                       " weapons=" + warship.getWeaponBayCount(ship) +
                       " crew=" + warship.getCrewCapacity(ship) +
                       " underpowered=" + warship.isUnderpowered(ship) +
                       " view=" + viewMode);
        }
        foreach (i, line in lines)
            gfx.drawText(hudFont, line, 18.0, 16.0 + i * 24.0, 0.95, 0.98, 1.0, 1.0, 1.0);
        foreach (i, line in logLines)
            gfx.drawText(hudFont, line, 18.0, 640.0 + i * 16.0, 0.75, 0.9, 0.8, 1.0, 0.85);
    }
}
