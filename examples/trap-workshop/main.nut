// Trap Workshop — compose physics triggers + sensing interactables + Emergence rules.
const CAT_TERRAIN = 1;
const CAT_PLAYER = 2;
const CAT_PROP = 4;
const CAT_TRIGGER = 8;

persist game = null
persist previousKeys = {}

function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
function keyDown(k) { return keyboard.isDown(k); }
function keyPressed(k) {
    local down = keyDown(k);
    local was = k in previousKeys ? previousKeys[k] : false;
    previousKeys[k] <- down;
    return down && !was;
}

function setFilter(fx, category, mask, tag, sensor = false) {
    fx.setCategoryBits(category);
    fx.setMaskBits(mask);
    fx.setTag(tag);
    fx.setSensor(sensor);
}

function solidRect(body, w, h, category, mask, tag, density = 0.0) {
    local fx = body.newRectangleFixture(w, h, density, 0.35, 0.05);
    setFilter(fx, category, mask, tag, false);
    return fx;
}

function sensorRect(body, w, h, mask, tag) {
    local fx = body.newRectangleFixture(w, h, 0.0, 0.0, 0.0);
    setFilter(fx, CAT_TRIGGER, mask, tag, true);
    return fx;
}

function buildRules(engine) {
    local json = @"
{
  ""schema"": ""eve.emergence.rules"",
  ""version"": 1,
  ""rules"": [
    {
      ""id"": ""disarm.spikes"",
      ""priority"": 20,
      ""fireMode"": ""rising"",
      ""once"": true,
      ""condition"": {""kind"": ""has_tag"", ""key"": ""lever.pulled""},
      ""actions"": [
        {""kind"": ""fact.set"", ""args"": {""domain"": ""state"", ""key"": ""spikes"", ""value"": ""off""}},
        {""kind"": ""emit"", ""args"": {""type"": ""spikes.disarmed""}}
      ]
    },
    {
      ""id"": ""open.gate"",
      ""priority"": 10,
      ""fireMode"": ""rising"",
      ""condition"": {""kind"": ""has_tag"", ""key"": ""crate.on.plate""},
      ""actions"": [
        {""kind"": ""fact.set"", ""args"": {""domain"": ""state"", ""key"": ""gate"", ""value"": ""open""}},
        {""kind"": ""emit"", ""args"": {""type"": ""gate.open""}}
      ]
    },
    {
      ""id"": ""close.gate"",
      ""priority"": 5,
      ""fireMode"": ""rising"",
      ""condition"": {""kind"": ""all"", ""children"": [
        {""kind"": ""state_equals"", ""key"": ""gate"", ""expected"": ""open""},
        {""kind"": ""not"", ""children"": [{""kind"": ""has_tag"", ""key"": ""crate.on.plate""}]}
      ]},
      ""actions"": [
        {""kind"": ""fact.set"", ""args"": {""domain"": ""state"", ""key"": ""gate"", ""value"": ""closed""}},
        {""kind"": ""emit"", ""args"": {""type"": ""gate.close""}}
      ]
    }
  ]
}
";
    local replaced = engine.replaceCatalogueJson(json);
    if (!replaced.ok) throw "trap-workshop: rule catalogue rejected: " + replaced.status.summary;
}

function createSession() {
    local physics = eve.Physics();
    physics.setMeter(30.0);
    local world = physics.newWorld(0.0, 1800.0, true);

    local sensingMod = eve.Sensing();
    local sensingCreated = sensingMod.newWorld();
    if (!sensingCreated.ok) throw "trap-workshop: sensing world failed";
    local sensing = sensingCreated.value;

    local emergenceMod = eve.Emergence();
    local engineCreated = emergenceMod.newEngine();
    if (!engineCreated.ok) throw "trap-workshop: emergence engine failed";
    local rules = engineCreated.value;
    buildRules(rules);
    rules.setState("spikes", "\"on\"");
    rules.setState("gate", "\"closed\"");

    // Floor segments with a spike pit gap (world x ≈ 260..380).
    // Each entry is [centerX, centerY, width, height].
    local floors = [
        [130.0, 560.0, 260.0, 40.0],   // 0..260
        [560.0, 560.0, 360.0, 40.0],   // 380..740 (pit gap 260..380)
        [900.0, 560.0, 320.0, 40.0],   // 740..1060
        [1170.0, 560.0, 180.0, 40.0]   // 1080..1260
    ];
    local floorBodies = [];
    foreach (f in floors) {
        local b = world.newBody("static", f[0], f[1]);
        solidRect(b, f[2], f[3], CAT_TERRAIN, 0xffff, "terrain");
        floorBodies.push({ body = b, w = f[2], h = f[3] });
    }
    // Left / right walls
    local wallL = world.newBody("static", 20.0, 360.0);
    solidRect(wallL, 40.0, 400.0, CAT_TERRAIN, 0xffff, "terrain");
    local wallR = world.newBody("static", 1260.0, 360.0);
    solidRect(wallR, 40.0, 400.0, CAT_TERRAIN, 0xffff, "terrain");

    // Spike sensor (invisible volume over the pit between first two floors).
    local spikeBody = world.newBody("static", 320.0, 540.0);
    sensorRect(spikeBody, 100.0, 60.0, CAT_PLAYER | CAT_PROP, "zone.spikes");

    // Pressure plate — crate only (filter in script via visitor fixture tag).
    local plateBody = world.newBody("static", 640.0, 532.0);
    sensorRect(plateBody, 70.0, 16.0, CAT_PLAYER | CAT_PROP, "zone.plate");

    // Exit sensor beyond the gate.
    local exitBody = world.newBody("static", 1100.0, 500.0);
    sensorRect(exitBody, 80.0, 80.0, CAT_PLAYER, "zone.exit");

    // Gate (kinematic slab that lifts when open).
    local gate = world.newBody("kinematic", 980.0, 480.0);
    solidRect(gate, 24.0, 160.0, CAT_TERRAIN, 0xffff, "gate");

    // Pushable crate.
    local crate = world.newBody("dynamic", 540.0, 500.0);
    crate.setFixedRotation(true);
    solidRect(crate, 40.0, 40.0, CAT_PROP, CAT_TERRAIN | CAT_PLAYER | CAT_TRIGGER, "prop.crate", 2.0);

    // Player capsule-ish box.
    local player = world.newBody("dynamic", 80.0, 480.0);
    player.setFixedRotation(true);
    player.setBullet(true);
    solidRect(player, 28.0, 48.0, CAT_PLAYER,
              CAT_TERRAIN | CAT_PROP | CAT_TRIGGER, "player", 1.0);
    local foot = player.newRectangleFixtureAt(20.0, 6.0, 0.0, 26.0, 0.0, 0.0, 0.0);
    setFilter(foot, CAT_PLAYER, CAT_TERRAIN | CAT_PROP, "player.foot", true);

    // Lever is a sensing fact only (no physics body) — press E when nearby.
    sensing.upsert("lever.1", 200.0, 500.0, "world", "interactable,lever", "");

    local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 22);
    local hudFont = gfx.newFont(fontData,
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.:-+[]()%");

    return {
        world = world,
        sensing = sensing,
        rules = rules,
        player = player,
        crate = crate,
        gate = gate,
        floors = floorBodies,
        gateClosedY = 480.0,
        gateOpenY = 320.0,
        hudFont = hudFont,
        grounded = false,
        footContacts = {},
        spikesArmed = true,
        gateOpen = false,
        plateCrateCount = 0,
        message = "Disarm spikes (E at lever), push crate onto plate, exit.",
        messageTimer = 6.0,
        completed = false,
        tick = 0,
        hurtCooldown = 0.0,
        remainder = 0.0
    };
}

function applyActivations() {
    local n = game.rules.activationCount();
    for (local i = 0; i < n; i += 1) {
        local id = game.rules.activationRuleId(i);
        if (id == "disarm.spikes") {
            game.spikesArmed = false;
            game.message = "Emergence: disarm.spikes — pit safe.";
            game.messageTimer = 3.0;
        } else if (id == "open.gate") {
            game.gateOpen = true;
            game.message = "Emergence: open.gate — path clear.";
            game.messageTimer = 3.0;
        } else if (id == "close.gate") {
            game.gateOpen = false;
            game.message = "Emergence: close.gate — plate empty.";
            game.messageTimer = 2.5;
        }
    }
    game.rules.clearActivations();
}

function pollTriggers() {
    // Prefer Sensor/Visitor Trigger buffers (2D parity with World3D).
    for (local i = 0; i < game.world.getBeginTriggerCount(); i += 1) {
        local sensor = game.world.getBeginTriggerSensorFixtureTag(i);
        local visitor = game.world.getBeginTriggerVisitorFixtureTag(i);
        if (sensor == "zone.spikes" && visitor == "player" && game.spikesArmed && game.hurtCooldown <= 0.0) {
            game.hurtCooldown = 0.8;
            game.player.setPosition(100.0, 480.0);
            game.player.setLinearVelocity(0.0, 0.0);
            game.message = "Physics trigger: spikes (sensor/visitor).";
            game.messageTimer = 2.0;
        }
        if (sensor == "zone.plate" && visitor == "prop.crate") {
            game.plateCrateCount += 1;
            if (game.plateCrateCount == 1) {
                local tagged = game.rules.setTag("crate.on.plate", true);
                if (!tagged.ok) throw "setTag crate.on.plate failed";
            }
        }
        if (sensor == "zone.exit" && visitor == "player" && game.gateOpen) {
            game.completed = true;
            game.message = "Exit reached — workshop complete.";
            game.messageTimer = 8.0;
        }
    }
    for (local i = 0; i < game.world.getEndTriggerCount(); i += 1) {
        local sensor = game.world.getEndTriggerSensorFixtureTag(i);
        local visitor = game.world.getEndTriggerVisitorFixtureTag(i);
        if (sensor == "zone.plate" && visitor == "prop.crate") {
            game.plateCrateCount -= 1;
            if (game.plateCrateCount <= 0) {
                game.plateCrateCount = 0;
                local cleared = game.rules.setTag("crate.on.plate", false);
                if (!cleared.ok) throw "clear crate.on.plate failed";
            }
        }
    }
}

function isSupportTag(tag) {
    return tag == "terrain" || tag == "prop.crate" || tag == "gate";
}

function pollGround() {
    for (local i = 0; i < game.world.getBeginContactCount(); i += 1) {
        local a = game.world.getBeginContactFixtureATag(i);
        local b = game.world.getBeginContactFixtureBTag(i);
        local aId = game.world.getBeginContactBodyAId(i);
        local bId = game.world.getBeginContactBodyBId(i);
        if (a == "player.foot" && isSupportTag(b)) game.footContacts[bId] <- true;
        if (b == "player.foot" && isSupportTag(a)) game.footContacts[aId] <- true;
    }
    for (local i = 0; i < game.world.getEndContactCount(); i += 1) {
        local a = game.world.getEndContactFixtureATag(i);
        local b = game.world.getEndContactFixtureBTag(i);
        local aId = game.world.getEndContactBodyAId(i);
        local bId = game.world.getEndContactBodyBId(i);
        if (a == "player.foot" && bId in game.footContacts) delete game.footContacts[bId];
        if (b == "player.foot" && aId in game.footContacts) delete game.footContacts[aId];
    }
    game.grounded = game.footContacts.len() > 0;
}

function nearestLever() {
    local px = game.player.getX();
    local py = game.player.getY();
    local found = game.sensing.circle(px, py, 70.0, "interactable", "", "", "", "", 4);
    if (!found.ok) return null;
    if (found.value <= 0) return null;
    return game.sensing.resultAt(0);
}

function simulate(dt) {
    if (game.hurtCooldown > 0.0) game.hurtCooldown -= dt;
    if (game.messageTimer > 0.0) game.messageTimer -= dt;

    local move = 0.0;
    if (keyDown("a") || keyDown("left")) move -= 1.0;
    if (keyDown("d") || keyDown("right")) move += 1.0;
    local vx = move * 280.0;
    local vy = game.player.getLinearVelocityY();
    if ((keyPressed("space") || keyPressed("w") || keyPressed("up")) && game.grounded)
        vy = -620.0;
    game.player.setLinearVelocity(vx, vy);

    local lever = nearestLever();
    if (lever != null && keyPressed("e")) {
        local tagged = game.rules.setTag("lever.pulled", true);
        if (!tagged.ok) throw "setTag lever.pulled failed";
        game.message = "Sensing interactable → Emergence tag lever.pulled";
        game.messageTimer = 2.5;
    }

    if (keyPressed("r")) {
        game = createSession();
        return;
    }

    game.world.update(dt);
    pollGround();
    pollTriggers();

    game.tick += 1;
    local drained = game.rules.drain(game.tick);
    if (!drained.ok) throw "emergence.drain failed: " + drained.status.summary;
    applyActivations();

    local targetY = game.gateOpen ? game.gateOpenY : game.gateClosedY;
    local gy = game.gate.getY();
    local nextY = gy + (targetY - gy) * clamp(dt * 4.0, 0.0, 1.0);
    game.gate.setPosition(game.gate.getX(), nextY);
}

function drawBody(body, w, h, r, g, b, a) {
    gfx.drawSolidRect(body.getX() - w * 0.5, body.getY() - h * 0.5, w, h, r, g, b, a);
}

function drawHud() {
    local f = game.hudFont;
    gfx.drawText(f, "Trap Workshop", 24.0, 18.0, 0.95, 0.95, 0.9, 1.0, 1.0);
    gfx.drawText(f, "physics trigger | sensing interact | emergence rule", 24.0, 46.0, 0.7, 0.8, 0.85, 1.0, 0.85);
    local status = "spikes=" + (game.spikesArmed ? "ARMED" : "off") +
        "  gate=" + (game.gateOpen ? "OPEN" : "closed") +
        "  plate=" + game.plateCrateCount;
    gfx.drawText(f, status, 24.0, 74.0, 0.85, 0.9, 0.7, 1.0, 0.9);
    if (game.messageTimer > 0.0)
        gfx.drawText(f, game.message, 24.0, 102.0, 1.0, 0.85, 0.45, 1.0, 0.95);
    if (nearestLever() != null)
        gfx.drawText(f, "[E] Pull lever", game.player.getX() - 40.0, game.player.getY() - 60.0,
                     0.95, 0.95, 0.4, 1.0, 0.9);
    if (game.completed)
        gfx.drawText(f, "COMPLETE — R to reset", 480.0, 200.0, 0.4, 1.0, 0.55, 1.0, 1.2);
}

function selfCheckComposition() {
    // Headless composition check: physics trigger buffers + Emergence rising rules.
    // Does not claim full route clearance; only that the workshop wiring is live.
    local physics = eve.Physics();
    physics.setMeter(30.0);
    local world = physics.newWorld(0.0, 0.0, true);
    local zone = world.newBody("static", 0.0, 0.0);
    local sensor = zone.newRectangleFixture(40.0, 40.0, 0.0, 0.0, 0.0);
    setFilter(sensor, CAT_TRIGGER, CAT_PROP, "zone.plate", true);
    local crate = world.newBody("dynamic", -80.0, 0.0);
    local crateFx = crate.newRectangleFixture(20.0, 20.0, 1.0, 0.0, 0.0);
    setFilter(crateFx, CAT_PROP, CAT_TRIGGER, "prop.crate", false);
    crate.setLinearVelocity(200.0, 0.0);
    local sawBegin = false;
    for (local i = 0; i < 90 && !sawBegin; i += 1) {
        world.update(1.0 / 60.0);
        for (local t = 0; t < world.getBeginTriggerCount(); t += 1) {
            if (world.getBeginTriggerSensorFixtureTag(t) == "zone.plate" &&
                world.getBeginTriggerVisitorFixtureTag(t) == "prop.crate")
                sawBegin = true;
        }
    }
    if (!sawBegin) throw "trap-workshop self-check: missing 2D Sensor/Visitor begin trigger";

    local emergence = eve.Emergence().newEngine();
    if (!emergence.ok) throw "trap-workshop self-check: emergence engine";
    local engine = emergence.value;
    buildRules(engine);
    engine.setState("spikes", "\"on\"");
    engine.setState("gate", "\"closed\"");
    local tagged = engine.setTag("lever.pulled", true);
    if (!tagged.ok) throw "trap-workshop self-check: setTag lever.pulled";
    local drained = engine.drain(1);
    if (!drained.ok || drained.value < 1) throw "trap-workshop self-check: disarm.spikes did not fire";
    if (engine.activationRuleId(0) != "disarm.spikes")
        throw "trap-workshop self-check: unexpected rule " + engine.activationRuleId(0);
    engine.clearActivations();
    local plate = engine.setTag("crate.on.plate", true);
    if (!plate.ok) throw "trap-workshop self-check: setTag crate.on.plate";
    drained = engine.drain(2);
    if (!drained.ok || drained.value < 1) throw "trap-workshop self-check: open.gate did not fire";
    if (engine.activationRuleId(0) != "open.gate")
        throw "trap-workshop self-check: unexpected rule " + engine.activationRuleId(0);
    print("trap-workshop self-check: PASS (2D triggers + Emergence disarm/open)\n");
}

eve_init <- function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0);
    selfCheckComposition();
    game = createSession();
};

eve_update <- function(dt) {
    game.remainder += clamp(dt, 0.0, 0.05);
    local step = 1.0 / 60.0;
    while (game.remainder >= step) {
        game.remainder -= step;
        simulate(step);
    }
};

eve_render <- function() {
    gfx.clear();
    // Pit under the gap between floor 0 and floor 1.
    gfx.drawSolidRect(260.0, 540.0, 120.0, 80.0, 0.08, 0.08, 0.1, 1.0);
    local spikeColor = game.spikesArmed ? [0.85, 0.25, 0.2] : [0.35, 0.4, 0.35];
    for (local i = 0; i < 5; i += 1) {
        local x = 275.0 + i * 20.0;
        gfx.drawSolidRect(x, 520.0, 10.0, 28.0, spikeColor[0], spikeColor[1], spikeColor[2], 1.0);
    }
    foreach (f in game.floors)
        drawBody(f.body, f.w, f.h, 0.28, 0.32, 0.36, 1.0);
    // Plate / exit / lever markers (visual only; logic uses physics sensors + sensing).
    gfx.drawSolidRect(605.0, 524.0, 70.0, 12.0, 0.2, 0.75, 0.85, 1.0);
    gfx.drawSolidRect(1060.0, 460.0, 80.0, 80.0, 0.25, 0.7, 0.35, 0.45);
    gfx.drawSolidRect(192.0, 480.0, 16.0, 40.0, 0.85, 0.7, 0.25, 1.0);
    drawBody(game.crate, 40.0, 40.0, 0.9, 0.55, 0.2, 1.0);
    drawBody(game.gate, 24.0, 160.0, 0.55, 0.58, 0.65, 1.0);
    drawBody(game.player, 28.0, 48.0, 0.35, 0.7, 1.0, 1.0);
    drawHud();
};
