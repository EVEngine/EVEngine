// Authoritative RTS battle: rendering reads poses and health after each fixed tick.
persist battle = null
persist frame = null
persist paused = false
persist spaceWasDown = false
persist blue = "00000000-0000-7000-8000-000000000001"
persist red = "00000000-0000-7000-8000-000000000002"
const CELL = 12.0;
const LEFT = 32.0;
const TOP = 32.0;

function requireBattle(result, name) {
    if (!result.ok) throw "Battle operation failed: " + name;
    return ("value" in result) ? result.value : null;
}
function identity(i) { return "00000000-0000-7000-8000-" + format("%012x", i); }
function px(x) { return LEFT + x * CELL; }
function py(y) { return TOP + y * CELL; }

eve_init = function() {
    if (config.battleUnits < 16 || config.battleUnits > 512 || config.battleUnits % 2 != 0)
        throw "battleUnits must be even and between 16 and 512";
    if (config.battleMode != "open" && config.battleMode != "corridor" && config.battleMode != "melee")
        throw "Unknown battleMode";
    battle = eve.RTS();
    requireBattle(battle.configureScriptWorld(64, 48, 1.0, 0.0, 0.0), "world");
    if (config.battleMode == "corridor") {
        for (local y = 0; y < 48; ++y)
            if (y != 24) requireBattle(battle.setScriptNavigationBlocked(27, y, true), "wall");
    }
    requireBattle(battle.configureCrowdSurfaceTracking({
        surfaceId=1 movementId=1 movementRevision=1 costModelRevision=1
        clearanceRadius=0.35 maxInflationChecks=1000000
    }), "surface");
    requireBattle(battle.configureCrowdFlowNavigation({maxWorkPerTick=32768 maxStartsPerTick=4}), "flow");
    local range = config.battleMode == "melee" ? "1.2" : "6";
    local content = "{\"weapons\":[{\"id\":\"rifle\",\"damageType\":\"normal\",\"damage\":10,\"range\":" + range +
        ",\"cooldown\":0.2,\"projectileSpeed\":0,\"blockedByObstacles\":true,\"magazineSize\":1000,\"reloadTime\":1}]," +
        "\"units\":[{\"id\":\"soldier\",\"role\":\"soldier\",\"weaponType\":\"rifle\",\"health\":40,\"speed\":3,\"radius\":0.35," +
        "\"attackRange\":" + range + ",\"sightRange\":10,\"targetTags\":[\"biological\"]}]}";
    requireBattle(battle.loadScriptContent(content), "content");
    requireBattle(battle.newFaction(blue), "blue faction");
    requireBattle(battle.newFaction(red), "red faction");
    local rows=(config.battleUnits+15)/16;
    local startY=(48-rows)/2;
    local armies = [[], []];
    for (local i=0; i<config.battleUnits; ++i) {
        local side = i % 2;
        local rank = i / 2;
        local name = identity(i+3);
        requireBattle(battle.newUnit(name, "unit:soldier", side == 0 ? blue : red,
            side == 0 ? 20.0 - rank % 8 : 34.0 + rank % 8, startY.tofloat() + rank / 8), "unit");
        armies[side].append(name);
    }
    local centerY = startY.tofloat() + (rows - 1) * 0.5;
    requireBattle(battle.attackMoveGroupUnits(armies[0],36.0,centerY,"grid",1.0,8,0.0,3.0),"blue orders");
    requireBattle(battle.attackMoveGroupUnits(armies[1],18.0,centerY,"grid",1.0,8,0.0,3.0),"red orders");
    frame = requireBattle(battle.inspectState(), "initial frame");
    gfx.setBackgroundColor(0.055,0.07,0.10,1.0);
};

eve_update = function(dt) {
    local down=keyboard.isDown("space");
    if (down && !spaceWasDown) paused=!paused;
    spaceWasDown=down;
    if (!paused) {
        requireBattle(battle.stepScript(0.05),"fixed tick");
        frame=requireBattle(battle.inspectState(),"frame");
    }
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(LEFT-6.0,TOP-6.0,64.0*CELL,48.0*CELL,0.09,0.12,0.16,1.0);
    if (config.battleMode == "corridor") {
        for(local y=0;y<48;++y) if(y!=24)
            gfx.drawSolidRect(px(27)-CELL*0.5,py(y)-CELL*0.5,CELL,CELL,0.27,0.31,0.37,1.0);
    }
    if (frame == null) return;
    local blueCount=0,redCount=0;
    foreach(unit in frame.units) {
        local friendly=unit.faction==blue;
        if(friendly) ++blueCount; else ++redCount;
        local x=px(unit.x),y=py(unit.y);
        gfx.drawSolidRect(x-4.0,y-4.0,8.0,8.0,friendly?0.20:0.96,friendly?0.65:0.31,friendly?0.95:0.24,1.0);
        gfx.drawSolidRect(x-4.0,y-6.0,8.0*unit.health/unit.maxHealth,1.5,0.55,0.95,0.66,1.0);
        if(unit.trafficWaiting || unit.combatSlotWaiting)
            gfx.drawSolidRect(x-1.0,y-1.0,2.0,2.0,1.0,0.85,0.25,1.0);
    }
    gfx.drawSolidRect(32.0,12.0,360.0*blueCount/(config.battleUnits/2),4.0,0.20,0.65,0.95,1.0);
    gfx.drawSolidRect(440.0,12.0,360.0*redCount/(config.battleUnits/2),4.0,0.96,0.31,0.24,1.0);
    if(paused) gfx.drawSolidRect(411.0,10.0,10.0,10.0,1.0,0.85,0.25,1.0);
};
