dofile("world.nut");

persist panelReady = false;
persist hudFont = null;
persist titleFont = null;
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

function hudCharset() {
    // Must stay in sync with assets/fonts/SangokushiUI.ttf subset.
    return " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz·./:-()[]%_+>!›→'×÷—•…‰※←↑↓" +
        "↔■□▲△◆◇○●★☆、。《》「」『』【】一三下不与且业东中为主义了于亮人任位作侯修元先公六关兴兵内册军农出击刘利割力务势募北千半卒占卡卫即原发受叛只可各合同名后吴周命商嘉回团国垦" +
        "城处备夏大太失始存孙守安对将少局展属屯山左已巴市帅平年库应府座建开张录彧征志忠情惇成或战扎打报担择损据捷掌排接控操攻政敌数新方无昌智曹月有未本机权束柴标格桑榜步武气水汉江池没法洛点" +
        "然率瑜用田留的目相知禁种空类粮精经结统缮置群羽职肃胜能至花英荀获葛蜀行西要许诚请诸败路辽进逃选通道邻郭都重金键长闲防阳陈雄需须领飞驻魅魏鲁！％（）＋，－／：？";
}

function tryLoadFont(path, size, charset) {
    try {
        local data = eve.Font().newFontDataFromFile(path, size);
        if (data == null) return null;
        return gfx.newFont(data, charset);
    } catch (err) {
        return null;
    }
}

function loadChineseFont(size) {
    local charset = hudCharset();
    // Engine FS is rooted at the game directory — ship a CJK+Latin face under assets/.
    local paths = [
        "assets/fonts/SangokushiUI.ttf",
        "assets/fonts/DejaVuSans-Bold.ttf"
    ];
    foreach (path in paths) {
        local font = tryLoadFont(path, size, charset);
        if (font != null) return font;
    }
    return null;
}

function buildPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("军机处", "root");
    ui.sectionHeader("城池", "sec_city");
    ui.text("", "city");
    ui.text("", "officers");
    ui.text("", "army");
    ui.sectionHeader("武将", "sec_officer");
    ui.text("", "jobs");
    ui.sectionHeader("内政与征战", "sec_actions");
    ui.button("结束本月  [空格]", "end_month");
    ui.button("屯田  [1]", "farm");
    ui.sameLine("sl1");
    ui.button("经商  [2]", "commerce");
    ui.button("征兵  [3]", "recruit");
    ui.sameLine("sl2");
    ui.button("修城  [4]", "fortify");
    ui.button("任命太守  [G]", "governor");
    ui.button("任命屯田  [F]", "job_farm");
    ui.button("任命经商  [M]", "job_commerce");
    ui.button("任命征兵  [C]", "job_recruit");
    ui.button("攻打邻城  [A]", "attack");
    ui.button("下一位武将  [Tab]", "next_officer");
    ui.button("重新开局  [R]", "reset");
    ui.sectionHeader("目录情报", "sec_intel");
    ui.text("", "ranking");
    ui.text("", "join_roster");
    ui.text("", "treasury_by_nation");
    ui.text("", "elites");
    ui.text("", "friends");
    ui.separator("sep_help");
    ui.text("左键点选城池 · 占六城获胜 · 洛阳失守即败", "help");
    ui.end();
    ui.mountBuildAs("sk10");
    ui.select("sk10");
    ui.setHostOverlay(true);
    ui.setHostOverlayAlpha(0.88);
    ui.setHostPos(948.0, 72.0, 0.0, 0.0);
    ui.setHostSize(316.0, 632.0);
    panelReady = true;
}

function taskLabel(kind) {
    if (kind == null || kind == "") return "空闲";
    if (kind == "farm") return "屯田";
    if (kind == "commerce") return "经商";
    if (kind == "recruit") return "征兵";
    if (kind == "fortify") return "修城";
    return kind;
}

function outcomeLabel(outcome) {
    if (outcome == "victory") return "【大捷】";
    if (outcome == "defeat") return "【败北】";
    return "";
}

function refreshPanel() {
    if (!panelReady || world == null) return;
    ui.select("sk10");
    local city = selectedCity(world);
    local officer = selectedOfficer(world);

    if (city != null) {
        local task = city.task == null ? "空闲" : taskLabel(city.task.kind);
        ui.setText("city", city.name + " · " + cityOwnerName(world, city) +
            "\n农" + formatInt(city.agriculture) +
            "  商" + formatInt(city.commerce) +
            "  防" + formatInt(city.defense) +
            "  金" + formatInt(city.gold) +
            "  粮" + formatInt(city.food) +
            "\n内政：" + task);
        local roster = "";
        foreach (oid in city.officers) {
            local o = world.officers[oid];
            local mark = oid == world.selectedOfficerId ? "›" : "·";
            roster += mark + o.name + "（" + jobLabel(o.job) + "） ";
        }
        if (roster == "") roster = "（城中无武将）";
        ui.setText("officers", roster);
        local army = armyById(world, city.armyId);
        ui.setText("army", army == null ? "无驻军" :
            army.name + "  " + army.troops + "人  主将" +
            (army.commanderId in world.officers ? world.officers[army.commanderId].name : "？"));
    } else {
        ui.setText("city", "未选城池");
        ui.setText("officers", "");
        ui.setText("army", "");
    }

    if (officer != null) {
        ui.setText("jobs", officer.name + "  " + jobLabel(officer.job) +
            "\n统" + officer.leadership +
            "  武" + officer.war +
            "  智" + officer.intellect +
            "  政" + officer.politics +
            "  魅" + officer.charm +
            "  忠" + formatInt(officer.loyalty));
    } else {
        ui.setText("jobs", "未选武将");
    }

    local ranking = topOfficersJson(world, PLAYER_NATION, 3);
    local decoded = eve.json.decode(ranking);
    local rankText = "魏·统率榜：";
    if (decoded.ok) {
        foreach (row in decoded.value)
            rankText += row.name + "(" + row.leadership + ") ";
    }
    ui.setText("ranking", rankText);

    local rosterText = "花名册：";
    if (city != null) {
        local rosterJson = cityRosterJson(world, city.id);
        local rosterDecoded = eve.json.decode(rosterJson);
        if (rosterDecoded.ok) {
            foreach (row in rosterDecoded.value)
                rosterText += row.officer_name + "/" + row.city_name + " ";
        }
    }
    ui.setText("join_roster", rosterText);

    local treasuryText = "各国府库：";
    local treasuryDecoded = eve.json.decode(nationTreasuryJson(world));
    if (treasuryDecoded.ok) {
        foreach (row in treasuryDecoded.value) {
            local nid = row.nation_id;
            local nname = nid in world.nations ? world.nations[nid].name : nid;
            treasuryText += nname + "(金" + formatInt(row.treasury) +
                " 粮" + formatInt(row.granary) + " " + formatInt(row.city_count) + "城) ";
        }
    }
    ui.setText("treasury_by_nation", treasuryText);

    local eliteText = "可用精英：";
    local eliteDecoded = eve.json.decode(eliteCandidatesJson(world, PLAYER_NATION));
    if (eliteDecoded.ok) {
        foreach (row in eliteDecoded.value)
            eliteText += row.name + "(武" + formatInt(row.war) + ") ";
        if (eliteDecoded.value.len() == 0) eliteText += "（无）";
    }
    ui.setText("elites", eliteText);

    local friendText = "义气：";
    local friendDecoded = eve.json.decode(friendshipPairsJson(world));
    if (friendDecoded.ok) {
        foreach (row in friendDecoded.value) {
            local toName = row.to_id in world.officers ? world.officers[row.to_id].name : row.to_id;
            friendText += row.from_name + "→" + toName + " ";
        }
    }
    ui.setText("friends", friendText);
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

function fillRect(x, y, w, h, r, g, b, a) {
    gfx.drawSolidRect(x, y, w, h, r, g, b, a);
}

function drawEdge(a, b, r, g, bl, alpha) {
    local dx = b.mapX - a.mapX;
    local dy = b.mapY - a.mapY;
    local len = sqrt(dx * dx + dy * dy);
    if (len < 1.0) return;
    local steps = (len / 6.0).tointeger();
    if (steps < 1) steps = 1;
    for (local i = 0; i <= steps; ++i) {
        local t = i.tofloat() / steps.tofloat();
        local x = a.mapX + dx * t;
        local y = a.mapY + dy * t;
        local pulse = (i % 2 == 0) ? 1.0 : 0.55;
        fillRect(x - 2.0, y - 2.0, 4.0, 4.0, r, g, bl, alpha * pulse);
    }
}

function drawCityMarker(city, selected) {
    local owner = world.social.ownerOf(city.id);
    local color = nationColor(owner);
    local radius = selected ? 18.0 : 14.0;
    if (selected)
        fillRect(city.mapX - radius - 6.0, city.mapY - radius - 6.0,
            radius * 2.0 + 12.0, radius * 2.0 + 12.0, 0.95, 0.82, 0.35, 0.30);
    // keep / wall / courtyard stack — reads more like a city than a flat chip
    fillRect(city.mapX - radius - 3.0, city.mapY - radius - 3.0,
        radius * 2.0 + 6.0, radius * 2.0 + 6.0, 0.10, 0.08, 0.06, 1.0);
    fillRect(city.mapX - radius, city.mapY - radius, radius * 2.0, radius * 2.0,
        color.r, color.g, color.b, 1.0);
    fillRect(city.mapX - radius + 3.0, city.mapY - radius + 3.0,
        radius * 2.0 - 6.0, radius * 2.0 - 6.0, color.r * 0.55, color.g * 0.55, color.b * 0.55, 1.0);
    fillRect(city.mapX - 5.0, city.mapY - radius - 8.0, 10.0, 8.0, 0.85, 0.78, 0.55, 1.0);
    fillRect(city.mapX - 3.0, city.mapY - 4.0, 6.0, radius + 2.0, 0.94, 0.90, 0.78, 0.95);

    local army = armyById(world, city.armyId);
    if (army != null) {
        fillRect(city.mapX + radius - 2.0, city.mapY - radius - 12.0, 26.0, 13.0,
            0.18, 0.14, 0.10, 0.94);
        if (hudFont != null)
            gfx.drawText(hudFont, (army.troops / 1000).tostring() + "千",
                city.mapX + radius, city.mapY - radius - 11.0,
                0.98, 0.92, 0.55, 1.0, 0.72);
    }

    if (hudFont != null) {
        fillRect(city.mapX - 30.0, city.mapY + radius + 3.0, 60.0, 17.0,
            0.08, 0.07, 0.06, 0.82);
        gfx.drawText(hudFont, city.name, city.mapX - 22.0, city.mapY + radius + 4.0,
            0.96, 0.93, 0.84, 1.0, 0.82);
    }
}

function drawMapBackdrop() {
    // frame
    fillRect(18.0, 58.0, 904.0, 644.0, 0.22, 0.16, 0.10, 1.0);
    // parchment field
    fillRect(28.0, 68.0, 884.0, 624.0, 0.18, 0.22, 0.18, 1.0);
    // soft regional washes
    fillRect(250.0, 120.0, 420.0, 220.0, 0.20, 0.28, 0.34, 0.35); // 中原
    fillRect(120.0, 320.0, 260.0, 260.0, 0.18, 0.30, 0.20, 0.32); // 巴蜀
    fillRect(560.0, 300.0, 300.0, 280.0, 0.30, 0.20, 0.18, 0.30); // 江东
    // river hint
    for (local i = 0; i < 40; ++i) {
        local t = i.tofloat() / 39.0;
        local x = 180.0 + t * 620.0;
        local y = 390.0 + sin(t * 6.0) * 18.0;
        fillRect(x, y, 10.0, 4.0, 0.25, 0.40, 0.48, 0.35);
    }
}

function drawTopBar() {
    fillRect(0.0, 0.0, 1280.0, 52.0, 0.10, 0.08, 0.06, 0.96);
    fillRect(0.0, 52.0, 1280.0, 2.0, 0.72, 0.58, 0.28, 0.85);
    if (titleFont != null) {
        gfx.drawText(titleFont, "三国志·群雄割据", 20.0, 12.0, 0.95, 0.88, 0.62, 1.0, 1.0);
    }
    if (hudFont == null || world == null) return;

    local weiGold = 0;
    local weiFood = 0;
    foreach (c in world.cities) {
        if (world.social.ownerOf(c.id) == PLAYER_NATION) {
            weiGold += c.gold;
            weiFood += c.food;
        }
    }
    local cities = countCities(world, PLAYER_NATION);
    // Stat chips so values stay readable beside the title.
    fillRect(300.0, 10.0, 118.0, 32.0, 0.16, 0.13, 0.10, 0.95);
    fillRect(426.0, 10.0, 118.0, 32.0, 0.16, 0.13, 0.10, 0.95);
    fillRect(552.0, 10.0, 118.0, 32.0, 0.16, 0.13, 0.10, 0.95);
    fillRect(678.0, 10.0, 118.0, 32.0, 0.16, 0.13, 0.10, 0.95);
    gfx.drawText(hudFont, "年月", 308.0, 12.0, 0.70, 0.66, 0.55, 1.0, 0.78);
    gfx.drawText(hudFont, "" + world.year + "." + world.month, 308.0, 26.0,
        0.98, 0.92, 0.70, 1.0, 1.05);
    gfx.drawText(hudFont, "金", 434.0, 12.0, 0.70, 0.66, 0.55, 1.0, 0.78);
    gfx.drawText(hudFont, "" + formatInt(weiGold), 434.0, 26.0, 1.0, 0.90, 0.40, 1.0, 1.05);
    gfx.drawText(hudFont, "粮", 560.0, 12.0, 0.70, 0.66, 0.55, 1.0, 0.78);
    gfx.drawText(hudFont, "" + formatInt(weiFood), 560.0, 26.0, 0.75, 0.98, 0.50, 1.0, 1.05);
    gfx.drawText(hudFont, "城", 686.0, 12.0, 0.70, 0.66, 0.55, 1.0, 0.78);
    gfx.drawText(hudFont, "" + cities + "/8", 686.0, 26.0, 0.85, 0.92, 1.0, 1.0, 1.05);

    fillRect(980.0, 14.0, 14.0, 14.0, 0.22, 0.45, 0.82, 1.0);
    gfx.drawText(hudFont, "魏", 1000.0, 14.0, 0.85, 0.88, 0.95, 1.0, 0.9);
    fillRect(1040.0, 14.0, 14.0, 14.0, 0.22, 0.68, 0.38, 1.0);
    gfx.drawText(hudFont, "蜀", 1060.0, 14.0, 0.85, 0.95, 0.88, 1.0, 0.9);
    fillRect(1100.0, 14.0, 14.0, 14.0, 0.82, 0.28, 0.28, 1.0);
    gfx.drawText(hudFont, "吴", 1120.0, 14.0, 0.95, 0.85, 0.85, 1.0, 0.9);
}

function drawBottomBanner() {
    if (world == null) return;
    fillRect(28.0, 666.0, 884.0, 26.0, 0.08, 0.07, 0.05, 0.88);
    if (hudFont == null) return;
    local msg = world.message + outcomeLabel(world.outcome);
    gfx.drawText(hudFont, msg, 40.0, 670.0, 0.95, 0.90, 0.75, 1.0, 0.88);
}

function drawMap() {
    drawMapBackdrop();
    local drawn = {};
    foreach (city in world.cities) {
        local count = world.social.relationTargetCount(city.id, REL_ADJACENT, 0.0);
        for (local i = 0; i < count; ++i) {
            local otherId = world.social.relationTargetAt(city.id, REL_ADJACENT, 0.0, i);
            local key = city.id < otherId ? city.id + ">" + otherId : otherId + ">" + city.id;
            if (key in drawn) continue;
            drawn[key] <- true;
            local other = cityById(world, otherId);
            if (other != null) drawEdge(city, other, 0.55, 0.48, 0.32, 0.75);
        }
    }
    foreach (city in world.cities)
        drawCityMarker(city, city.id == world.selectedCityId);

    if (hudFont != null) {
        gfx.drawText(hudFont, "中原", 430.0, 110.0, 0.72, 0.78, 0.82, 0.85, 1.0);
        gfx.drawText(hudFont, "巴蜀", 160.0, 480.0, 0.72, 0.82, 0.72, 0.85, 1.0);
        gfx.drawText(hudFont, "江东", 700.0, 500.0, 0.85, 0.72, 0.70, 0.85, 1.0);
    }
}

function eve_init() {
    gfx.setBackgroundColor(0.07, 0.06, 0.05, 1.0);
    hudFont = loadChineseFont(18);
    titleFont = loadChineseFont(26);
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
        if (mx < 940.0 && my > 58.0) selectCityAt(world, mx, my);
    }
    refreshPanel();
}

function eve_render() {
    gfx.clear();
    drawTopBar();
    if (world != null) {
        drawMap();
        drawBottomBanner();
    }
    ui.beginFrameAndRender();
}
