dofile("world.nut");

persist panelReady = false;
persist hudFont = null;
local previousKeys = {};
local previousMouse = false;

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local before = name in previousKeys ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !before;
}

function mousePressed() {
    local down = mouse.isDown(0);
    local clicked = down && !previousMouse;
    previousMouse = down;
    return clicked;
}

function formatInt(value) {
    return value.tointeger().tostring();
}

function buildPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("SANGOKUSHI 10", "root");
    ui.text("WEI CAMPAIGN", "title");
    ui.text("", "calendar");
    ui.text("", "treasury");
    ui.text("", "city");
    ui.text("", "officers");
    ui.text("", "army");
    ui.text("", "jobs");
    ui.text("", "ranking");
    ui.text("", "join_roster");
    ui.text("", "treasury_by_nation");
    ui.text("", "elites");
    ui.text("", "friends");
    ui.separator("sep");
    ui.button("End Month [Space]", "end_month");
    ui.button("Farm [1]", "farm");
    ui.button("Commerce [2]", "commerce");
    ui.button("Recruit [3]", "recruit");
    ui.button("Fortify [4]", "fortify");
    ui.button("Assign Governor [G]", "governor");
    ui.button("Assign Farmer [F]", "job_farm");
    ui.button("Assign Merchant [M]", "job_commerce");
    ui.button("Assign Recruiter [C]", "job_recruit");
    ui.button("Attack Adjacent [A]", "attack");
    ui.button("Next Officer [Tab]", "next_officer");
    ui.button("Reset [R]", "reset");
    ui.separator("sep2");
    ui.text("", "message");
    ui.text("Click cities on the map. Own 6 cities to win.", "help");
    ui.end();
    ui.mountBuildAs("sk10");
    ui.select("sk10");
    ui.setHostOverlay(true);
    ui.setHostPos(940.0, 16.0, 0.0, 0.0);
    ui.setHostSize(320.0, 688.0);
    panelReady = true;
}

function refreshPanel() {
    if (!panelReady || world == null) return;
    ui.select("sk10");
    local city = selectedCity(world);
    local officer = selectedOfficer(world);
    local weiGold = 0;
    local weiFood = 0;
    foreach (c in world.cities) {
        if (world.social.ownerOf(c.id) == PLAYER_NATION) {
            weiGold += c.gold;
            weiFood += c.food;
        }
    }
    ui.setText("calendar", "Year " + world.year + "  Month " + world.month +
        "   Wei cities " + countCities(world, PLAYER_NATION) + "/8");
    ui.setText("treasury", "Treasury gold " + formatInt(weiGold) + "  food " + formatInt(weiFood));
    if (city != null) {
        local task = city.task == null ? "idle" : city.task.kind;
        ui.setText("city", city.name + " [" + cityOwnerName(world, city) + "]  " +
            "A" + formatInt(city.agriculture) + " C" + formatInt(city.commerce) +
            " D" + formatInt(city.defense) + "  task:" + task);
        local roster = "";
        foreach (oid in city.officers) {
            local o = world.officers[oid];
            local mark = oid == world.selectedOfficerId ? ">" : " ";
            roster += mark + o.name + "(" + o.job + ") ";
        }
        if (roster == "") roster = "(no officers)";
        ui.setText("officers", roster);
        local army = armyById(world, city.armyId);
        ui.setText("army", army == null ? "No legion stationed" :
            army.name + "  troops " + army.troops + "  cmd " +
            (army.commanderId in world.officers ? world.officers[army.commanderId].name : "?"));
    }
    if (officer != null) {
        ui.setText("jobs", officer.name + "  lead " + officer.leadership +
            " war " + officer.war + " pol " + officer.politics +
            " loyal " + formatInt(officer.loyalty));
    } else {
        ui.setText("jobs", "No officer selected");
    }
    local ranking = topOfficersJson(world, PLAYER_NATION, 3);
    local decoded = eve.json.decode(ranking);
    local rankText = "Wei top leadership: ";
    if (decoded.ok) {
        foreach (row in decoded.value)
            rankText += row.name + "(" + row.leadership + ") ";
    }
    ui.setText("ranking", rankText);

    local rosterText = "DB join roster: ";
    if (city != null) {
        local rosterJson = cityRosterJson(world, city.id);
        local rosterDecoded = eve.json.decode(rosterJson);
        if (rosterDecoded.ok) {
            foreach (row in rosterDecoded.value)
                rosterText += row.officer_name + "/" + row.city_name + " ";
        }
    }
    ui.setText("join_roster", rosterText);

    local treasuryText = "DB groupBy: ";
    local treasuryDecoded = eve.json.decode(nationTreasuryJson(world));
    if (treasuryDecoded.ok) {
        foreach (row in treasuryDecoded.value)
            treasuryText += row.nation_id + "(g" + formatInt(row.treasury) +
                " f" + formatInt(row.granary) + " n" + formatInt(row.city_count) + ") ";
    }
    ui.setText("treasury_by_nation", treasuryText);

    local eliteText = "DB whereAny elites: ";
    local eliteDecoded = eve.json.decode(eliteCandidatesJson(world, PLAYER_NATION));
    if (eliteDecoded.ok) {
        foreach (row in eliteDecoded.value)
            eliteText += row.name + "(w" + formatInt(row.war) + ") ";
        if (eliteDecoded.value.len() == 0) eliteText += "(none)";
    }
    ui.setText("elites", eliteText);

    local friendText = "DB friendship: ";
    local friendDecoded = eve.json.decode(friendshipPairsJson(world));
    if (friendDecoded.ok) {
        foreach (row in friendDecoded.value)
            friendText += row.from_name + "->" + row.to_id + " ";
    }
    ui.setText("friends", friendText);

    ui.setText("message", world.message + (world.outcome != "" ? "  [" + world.outcome + "]" : ""));
}

function handlePanel() {
    if (world == null) return;
    local c = ui.consumeClick();
    while (c != "") {
        if (c == "sk10/end_month") endMonth(world);
        else if (c == "sk10/farm") startDomesticTask(world, "farm");
        else if (c == "sk10/commerce") startDomesticTask(world, "commerce");
        else if (c == "sk10/recruit") startDomesticTask(world, "recruit");
        else if (c == "sk10/fortify") startDomesticTask(world, "fortify");
        else if (c == "sk10/governor") {
            local o = selectedOfficer(world);
            local city = selectedCity(world);
            if (o != null && city != null) assignJob(world, o.id, JOB_GOVERNOR, city.id);
        } else if (c == "sk10/job_farm") {
            local o = selectedOfficer(world);
            local city = selectedCity(world);
            if (o != null && city != null) assignJob(world, o.id, JOB_FARM, city.id);
        } else if (c == "sk10/job_commerce") {
            local o = selectedOfficer(world);
            local city = selectedCity(world);
            if (o != null && city != null) assignJob(world, o.id, JOB_COMMERCE, city.id);
        } else if (c == "sk10/job_recruit") {
            local o = selectedOfficer(world);
            local city = selectedCity(world);
            if (o != null && city != null) assignJob(world, o.id, JOB_RECRUIT, city.id);
        } else if (c == "sk10/attack") attackSelected(world);
        else if (c == "sk10/next_officer") cycleOfficer(world, 1);
        else if (c == "sk10/reset") resetWorld();
        c = ui.consumeClick();
    }
}

function drawEdge(a, b, r, g, bl, alpha) {
    // approximate road with a thin stretched rect
    local dx = b.mapX - a.mapX;
    local dy = b.mapY - a.mapY;
    local len = sqrt(dx * dx + dy * dy);
    if (len < 1.0) return;
    local steps = (len / 8.0).tointeger();
    if (steps < 1) steps = 1;
    for (local i = 0; i <= steps; ++i) {
        local t = i.tofloat() / steps.tofloat();
        local x = a.mapX + dx * t;
        local y = a.mapY + dy * t;
        gfx.drawSolidRect(x - 1.5, y - 1.5, 3.0, 3.0, r, g, bl, alpha);
    }
}

function drawMap() {
    // parchment-like field
    gfx.drawSolidRect(40.0, 40.0, 860.0, 640.0, 0.10, 0.12, 0.14, 1.0);
    local drawn = {};
    foreach (city in world.cities) {
        local count = world.social.relationTargetCount(city.id, REL_ADJACENT, 0.0);
        for (local i = 0; i < count; ++i) {
            local otherId = world.social.relationTargetAt(city.id, REL_ADJACENT, 0.0, i);
            local key = city.id < otherId ? city.id + ">" + otherId : otherId + ">" + city.id;
            if (key in drawn) continue;
            drawn[key] <- true;
            local other = cityById(world, otherId);
            if (other != null) drawEdge(city, other, 0.35, 0.38, 0.42, 0.85);
        }
    }
    foreach (city in world.cities) {
        local owner = world.social.ownerOf(city.id);
        local color = nationColor(owner);
        local selected = city.id == world.selectedCityId;
        local radius = selected ? 22.0 : 16.0;
        gfx.drawSolidRect(city.mapX - radius, city.mapY - radius, radius * 2.0, radius * 2.0,
            color.r, color.g, color.b, 1.0);
        if (selected)
            gfx.drawSolidRect(city.mapX - radius - 3.0, city.mapY - radius - 3.0,
                radius * 2.0 + 6.0, radius * 2.0 + 6.0, 0.95, 0.88, 0.45, 0.35);
        if (hudFont != null)
            gfx.drawText(hudFont, city.name, city.mapX - 28.0, city.mapY + radius + 4.0,
                0.92, 0.90, 0.82, 1.0, 0.85);
        local army = armyById(world, city.armyId);
        if (army != null)
            gfx.drawSolidRect(city.mapX + radius - 4.0, city.mapY - radius - 4.0, 8.0, 8.0,
                0.95, 0.85, 0.35, 1.0);
    }
    if (hudFont != null) {
        gfx.drawText(hudFont, "Central Plains", 400.0, 120.0, 0.7, 0.72, 0.78, 1.0, 0.9);
        gfx.drawText(hudFont, "Bashu", 180.0, 480.0, 0.7, 0.72, 0.78, 1.0, 0.9);
        gfx.drawText(hudFont, "Jiangdong", 680.0, 500.0, 0.7, 0.72, 0.78, 1.0, 0.9);
        gfx.drawText(hudFont, "Wei", 980.0, 40.0, 0.22, 0.45, 0.82, 1.0, 1.0);
        gfx.drawText(hudFont, "Shu", 1040.0, 40.0, 0.22, 0.68, 0.38, 1.0, 1.0);
        gfx.drawText(hudFont, "Wu", 1100.0, 40.0, 0.82, 0.28, 0.28, 1.0, 1.0);
    }
}

function eve_init() {
    gfx.setBackgroundColor(0.05, 0.06, 0.08, 1.0);
    local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 18);
    if (fontData != null)
        hudFont = gfx.newFont(fontData,
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:'[]()%_>");
    buildPanel();
    resetWorld();
}

eve_before_reload <- function() {
    if (world != null && world.db != null) world.db.close();
    world = null;
}

eve_reload <- function() {
    resetWorld();
}

function eve_update(dt) {
    if (world == null) return;
    handlePanel();
    if (keyPressed("Space")) endMonth(world);
    if (keyPressed("1")) startDomesticTask(world, "farm");
    if (keyPressed("2")) startDomesticTask(world, "commerce");
    if (keyPressed("3")) startDomesticTask(world, "recruit");
    if (keyPressed("4")) startDomesticTask(world, "fortify");
    if (keyPressed("G")) {
        local o = selectedOfficer(world);
        local city = selectedCity(world);
        if (o != null && city != null) assignJob(world, o.id, JOB_GOVERNOR, city.id);
    }
    if (keyPressed("F")) {
        local o = selectedOfficer(world);
        local city = selectedCity(world);
        if (o != null && city != null) assignJob(world, o.id, JOB_FARM, city.id);
    }
    if (keyPressed("M")) {
        local o = selectedOfficer(world);
        local city = selectedCity(world);
        if (o != null && city != null) assignJob(world, o.id, JOB_COMMERCE, city.id);
    }
    if (keyPressed("C")) {
        local o = selectedOfficer(world);
        local city = selectedCity(world);
        if (o != null && city != null) assignJob(world, o.id, JOB_RECRUIT, city.id);
    }
    if (keyPressed("A")) attackSelected(world);
    if (keyPressed("Tab")) cycleOfficer(world, 1);
    if (keyPressed("R")) resetWorld();
    if (mousePressed()) {
        local mx = mouse.getX();
        local my = mouse.getY();
        if (mx < 920.0) selectCityAt(world, mx, my);
    }
    refreshPanel();
}

function eve_render() {
    gfx.clear();
    if (world != null) drawMap();
    ui.beginFrameAndRender();
}
