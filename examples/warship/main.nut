// ============================================================================
// EVEngine Warship 模块示例 —— 巨型舰体 + 可重组舱段
//
// 运行： make run/<platform>-debug GAME=examples/warship
// ============================================================================

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

patrols <- [
    { x = 720.0, y = 160.0 },
    { x = 720.0, y = 480.0 },
    { x = 180.0, y = 480.0 },
    { x = 180.0, y = 160.0 }
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

hullDefs <- @"[
  {""id"":""destroyer"",""category"":""warship"",""mobility"":""surface"",
   ""length"":140,""beam"":24,""maxSpeed"":48,""accel"":14,""turnRate"":28,""maxHealth"":900,
   ""grid"":{""cols"":4,""rows"":2,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
     {""deck"":0,""row"":1,""col"":0,""function"":""engine""},
     {""deck"":0,""row"":1,""col"":1,""function"":""weapon_bay""}
   ]},
  {""id"":""clipper"",""category"":""sailship"",""mobility"":""sail"",
   ""length"":90,""beam"":18,""maxSpeed"":36,""accel"":8,""turnRate"":32,""maxHealth"":400,
   ""grid"":{""cols"":3,""rows"":1,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""sail_rig""},
     {""deck"":0,""row"":0,""col"":2,""function"":""quarters""}
   ]},
  {""id"":""carrier"",""category"":""carrier"",""mobility"":""carrier"",
   ""length"":280,""beam"":60,""maxSpeed"":30,""accel"":6,""turnRate"":12,""maxHealth"":2200,
   ""grid"":{""cols"":5,""rows"":2,""decks"":1},
   ""defaultLayout"":[
     {""deck"":0,""row"":0,""col"":0,""function"":""bridge""},
     {""deck"":0,""row"":0,""col"":1,""function"":""reactor""},
     {""deck"":0,""row"":1,""col"":0,""function"":""hangar""},
     {""deck"":0,""row"":1,""col"":1,""function"":""hangar""},
     {""deck"":0,""row"":1,""col"":2,""function"":""engine""}
   ]},
  {""id"":""cruiser"",""category"":""starship"",""mobility"":""space"",
   ""length"":220,""beam"":48,""maxSpeed"":70,""accel"":20,""turnRate"":40,""maxHealth"":1600,
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

function ensureSetup() {
    if (bootstrapped) return;
    if (!hudFont) {
        local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 18);
        hudFont = gfx.newFont(fontData,
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%_x");
    }
    gfx.setBackgroundColor(0.04, 0.08, 0.14, 1.0);

    warship = eve.Warship();
    warship.clearHullDefinitions();
    warship.clearFunctionDefinitions();
    local fn = warship.registerFunctionsFromJson(functionDefs);
    if (!fn.ok) throw fn.status.summary;
    local hull = warship.registerHullsFromJson(hullDefs);
    if (!hull.ok) throw hull.status.summary;

    destroyer = warship.newWarship("destroyer", 160.0, 180.0, 0.0, "blue");
    clipper = warship.newWarship("clipper", 160.0, 300.0, 20.0, "teal");
    carrier = warship.newWarship("carrier", 160.0, 430.0, -10.0, "gray");
    cruiser = warship.newWarship("cruiser", 160.0, 540.0, 35.0, "violet");
    selected = 0;
    bootstrapped = true;
    pushLog("fleet ready — 1..4 select, Space move, H hangar, R remodel, C clear reactor");
}

function currentShip() {
    if (selected == 1) return clipper;
    if (selected == 2) return carrier;
    if (selected == 3) return cruiser;
    return destroyer;
}

function shipColor(category) {
    if (category == "sailship") return [0.35, 0.75, 0.7, 1.0];
    if (category == "carrier") return [0.55, 0.58, 0.62, 1.0];
    if (category == "starship") return [0.55, 0.4, 0.9, 1.0];
    return [0.25, 0.55, 0.95, 1.0];
}

function drawShip(ship, idx) {
    if (ship == null) return;
    local x = warship.getX(ship);
    local y = warship.getY(ship);
    local heading = warship.getHeading(ship) * 3.14159265 / 180.0;
    local length = warship.getLength(ship) * 0.45;
    local beam = warship.getBeam(ship) * 0.45;
    local c = shipColor(warship.getCategory(ship));
    local selectedGlow = (idx == selected) ? 1.0 : 0.55;
    gfx.drawSolidRect(x - length * 0.5, y - beam * 0.5, length, beam,
        c[0] * selectedGlow, c[1] * selectedGlow, c[2] * selectedGlow, 0.95);
    local noseX = x + cos(heading) * length * 0.55;
    local noseY = y + sin(heading) * beam * 0.15;
    gfx.drawSolidRect(noseX - 4.0, noseY - 4.0, 8.0, 8.0, 1.0, 0.85, 0.35, 1.0);

    local cols = warship.getGridCols(ship);
    local rows = warship.getGridRows(ship);
    local cell = 10.0;
    local ox = x - cols * cell * 0.5;
    local oy = y + beam * 0.5 + 8.0;
    for (local row = 0; row < rows; row += 1) {
        for (local col = 0; col < cols; col += 1) {
            local fn = warship.getCompartmentFunction(ship, 0, row, col);
            local filled = fn != "";
            gfx.drawSolidRect(ox + col * cell, oy + row * cell, cell - 1.0, cell - 1.0,
                filled ? 0.2 : 0.08, filled ? 0.7 : 0.12, filled ? 0.45 : 0.16, 0.9);
        }
    }
}

eve_init <- function() {
    ensureSetup();
}

eve_update <- function(dt) {
    ensureSetup();
    if (keyPressed("1")) { selected = 0; pushLog("selected destroyer"); }
    if (keyPressed("2")) { selected = 1; pushLog("selected clipper"); }
    if (keyPressed("3")) { selected = 2; pushLog("selected carrier"); }
    if (keyPressed("4")) { selected = 3; pushLog("selected cruiser"); }

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
}

eve_render <- function() {
    ensureSetup();
    gfx.clear();
    gfx.drawSolidRect(0.0, 0.0, 960.0, 640.0, 0.05, 0.12, 0.2, 1.0);
    for (local i = 0; i < 8; i += 1) {
        local y = 80.0 + i * 70.0;
        gfx.drawSolidRect(0.0, y, 960.0, 2.0, 0.08, 0.18, 0.28, 0.35);
    }

    drawShip(destroyer, 0);
    drawShip(clipper, 1);
    drawShip(carrier, 2);
    drawShip(cruiser, 3);

    if (hudFont) {
        local ship = currentShip();
        local lines = [];
        lines.push("Warship demo - 1..4 select | Space move | H hangar | R remodel | C clear reactor");
        if (ship != null) {
            lines.push("sel=" + warship.getDefId(ship) + " cat=" + warship.getCategory(ship) +
                       " power=" + warship.getPowerNet(ship) +
                       " thrust=" + warship.getThrustMul(ship));
            lines.push("hangar=" + warship.getHangarCapacity(ship) +
                       " weapons=" + warship.getWeaponBayCount(ship) +
                       " crew=" + warship.getCrewCapacity(ship) +
                       " underpowered=" + warship.isUnderpowered(ship));
        }
        foreach (i, line in lines)
            gfx.drawText(hudFont, line, 16.0, 18.0 + i * 22.0, 0.9, 0.95, 1.0, 1.0, 1.0);
        foreach (i, line in logLines)
            gfx.drawText(hudFont, line, 16.0, 520.0 + i * 14.0, 0.7, 0.85, 0.75, 1.0, 0.85);
    }
}
