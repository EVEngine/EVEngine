// 潮汐电台：一段可分支、可存读档、可回看的原创 Galgame 短篇。

gal <- persist("gal", function() {
    return {
        mode = "title", step = 0, route = "", trust = 0, ending = "",
        uiMode = "", lineRecorded = false, toast = "", toastLeft = 0.0,
        mouseWas = false, touchWas = false,
        bg = null, linTex = null, zhouTex = null, lin = null, zhou = null,
        ux = null
    };
});

local story = [
    { who = "", text = "雨停在末班车到站前。七年没有回来的海鸣站，仍在播放那段无人认领的电台杂音。" },
    { who = "林澄", text = "如果那封信没有骗我，今晚九点，旧频率会再次响起。" },
    { who = "周岚", text = "你果然还是来了。伞还是以前那把，修过三次的那把。" },
    { who = "林澄", text = "你也还是老样子——把想问的话藏在收音机后面。" },
    { who = "周岚", text = "七年前的停电不是事故。我保存了最后四十秒录音，但一个人不敢听完。" },
    { who = "", text = "海风穿过空站台。收音机的指示灯忽然亮起，像遥远星体发来的回信。" }
];

local routeTruth = [
    { who = "林澄", text = "那就一起听。真相不会因为我们转身而消失。" },
    { who = "周岚", text = "录音里是灯塔管理员的呼救，也是你父亲让全镇撤离的证据。" },
    { who = "林澄", text = "所以他不是抛下我。他留在了风暴中心。" },
    { who = "周岚", text = "我一直欠你这句话：对不起，还有……欢迎回家。" },
    { who = "", text = "九点整，潮声盖过旧录音。两个人并肩走向亮灯的小镇，没有谁再被留在过去。" }
];

local routeSilence = [
    { who = "林澄", text = "先关掉吧。今晚我只想知道，你为什么一直等在这里。" },
    { who = "周岚", text = "因为有人答应过，等我们都不再害怕，就回来一起看流星。" },
    { who = "林澄", text = "那份约定迟到了七年。不过，天还没有亮。" },
    { who = "", text = "收音机安静下来。第一颗流星落向海面时，两只手在站台长椅旁轻轻碰到一起。" }
];

function currentLines() {
    if (gal.route == "truth") return routeTruth;
    if (gal.route == "silence") return routeSilence;
    return story;
}

function currentLine() {
    local lines = currentLines();
    if (gal.step < 0 || gal.step >= lines.len()) return null;
    return lines[gal.step];
}

function makeAvatar(texture, slot) {
    local av = avatar.newImageAvatar();
    av.addLayer("portrait", texture, 0);
    av.setLayerSize("portrait", 520.0, 780.0);
    av.setLayerOffset("portrait", -260.0, 0.0);
    av.setPosition(0.0, 24.0);
    av.setLayer(10);
    av.setVisible(false);
    return av;
}

function setToast(text) {
    gal.toast = text;
    gal.toastLeft = 2.5;
}

function saveGame() {
    local payload = gal.step + "|" + gal.route + "|" + gal.trust + "|" + gal.mode;
    if (fs.writeText("tidal_frequency_slot1.sav", payload)) setToast("已保存到存档 1");
    else setToast("存档失败：保存目录不可写");
}

function loadGame() {
    local data = fs.readText("tidal_frequency_slot1.sav");
    if (data == "") { setToast("还没有存档"); return; }
    local p1 = data.find("|");
    local tail1 = p1 == null ? "" : data.slice(p1 + 1);
    local rel2 = tail1.find("|");
    local p2 = rel2 == null ? null : p1 + 1 + rel2;
    local tail2 = p2 == null ? "" : data.slice(p2 + 1);
    local rel3 = tail2.find("|");
    local p3 = rel3 == null ? null : p2 + 1 + rel3;
    if (p1 == null || p2 == null || p3 == null) { setToast("存档格式无效"); return; }
    gal.step = data.slice(0, p1).tointeger();
    gal.route = data.slice(p1 + 1, p2);
    gal.trust = data.slice(p2 + 1, p3).tointeger();
    gal.mode = data.slice(p3 + 1);
    if (gal.mode != "game") gal.mode = "game";
    gal.uiMode = "";
    showCurrentLine();
    setToast("已读取存档 1");
}

function configureStage() {
    dialogue.reset();
    dialogue.registerCharacter("lin", "林澄");
    dialogue.registerCharacter("zhou", "周岚");
    dialogue.bindAvatar("lin", gal.lin);
    dialogue.bindAvatar("zhou", gal.zhou);
    dialogue.setSlotX("left", 0.27);
    dialogue.setSlotX("right", 0.73);
    dialogue.setTypeSpeed(34.0);
}

function showCurrentLine() {
    configureStage();
    local line = currentLine();
    if (line == null) return;
    if (line.who == "林澄") {
        dialogue.show("lin", "left");
        if (gal.route != "") dialogue.show("zhou", "right");
        dialogue.say("lin", line.text);
    } else if (line.who == "周岚") {
        dialogue.show("lin", "left");
        dialogue.show("zhou", "right");
        dialogue.say("zhou", line.text);
    } else {
        if (gal.step > 1) { dialogue.show("lin", "left"); dialogue.show("zhou", "right"); }
        dialogue.narrate(line.text);
    }
    gal.lineRecorded = false;
}

function startGame() {
    gal.mode = "game"; gal.step = 0; gal.route = ""; gal.trust = 0; gal.ending = "";
    gal.ux.clearHistory();
    gal.uiMode = "";
    showCurrentLine();
}

function chooseRoute(route) {
    gal.route = route;
    gal.trust = route == "truth" ? 2 : 1;
    gal.step = 0;
    gal.uiMode = "";
    showCurrentLine();
}

function finishRoute() {
    gal.ending = gal.route == "truth" ? "TRUE END · 归航" : "GOOD END · 星约";
    gal.mode = "ending";
    gal.uiMode = "";
}

function advanceStory() {
    if (dialogue.isTyping()) { dialogue.skipTyping(); return; }
    if (!dialogue.isWaitingAdvance()) return;
    dialogue.advance();
    local line = currentLine();
    if (line != null && !gal.lineRecorded) {
        gal.ux.record(gal.route + ":" + gal.step, line.who, line.text);
        gal.lineRecorded = true;
    }
    gal.step += 1;
    if (gal.route == "" && gal.step >= story.len()) {
        gal.mode = "choice"; gal.uiMode = ""; return;
    }
    if (gal.route != "" && gal.step >= currentLines().len()) { finishRoute(); return; }
    showCurrentLine();
}

function rebuildUI() {
    ui.beginBuild();
    if (gal.mode == "title") {
        ui.beginWindow("潮汐电台", "root");
        ui.text("TIDAL FREQUENCY", "subtitle");
        ui.separator("s");
        ui.text("一段关于旧电台、海风与迟到七年的约定", "blurb");
        ui.button("开始故事", "start");
        ui.button("读取存档 1", "load");
    } else if (gal.mode == "history") {
        ui.beginWindow("回想", "root");
        local count = gal.ux.getHistoryCount();
        local first = count > 9 ? count - 9 : 0;
        for (local i = first; i < count; ++i) {
            local speaker = gal.ux.getHistorySpeaker(i);
            ui.text((speaker == "" ? "旁白" : speaker) + "：" + gal.ux.getHistoryText(i), "h" + i);
        }
        ui.separator("s"); ui.button("返回", "back");
    } else if (gal.mode == "choice") {
        ui.beginWindow("你的选择", "root");
        ui.text("电台开始播放。你要和周岚一起听完七年前的录音吗？", "question");
        ui.button("[1] 一起听完，面对真相", "truth");
        ui.button("[2] 关掉电台，先说此刻", "silence");
    } else if (gal.mode == "ending") {
        ui.beginWindow(gal.ending, "root");
        ui.text(gal.route == "truth" ? "你们让沉默多年的真相重新被听见。" : "有些答案可以等到不再害怕的明天。", "endingText");
        ui.text("信赖度  " + gal.trust + " / 2", "trust");
        ui.separator("s"); ui.button("从头开始", "restart"); ui.button("回到标题", "title");
    } else {
        ui.beginWindow("潮汐电台", "root");
        ui.text("", "speaker");
        ui.text("", "line");
        ui.separator("s");
        ui.button("AUTO", "auto"); ui.sameLine("a");
        ui.button("SKIP", "skip"); ui.sameLine("b");
        ui.button("LOG", "history"); ui.sameLine("c");
        ui.button("SAVE", "save"); ui.sameLine("d");
        ui.button("LOAD", "load");
        ui.text("", "status");
    }
    ui.end();
    ui.mountBuildAs("galui"); ui.select("galui"); ui.setHostOverlay(true);
    ui.setHostOverlayAlpha(gal.mode == "game" ? 0.82 : 0.90);
    if (gal.mode == "game") {
        // Classic visual-novel composition: a wide dialogue box anchored to
        // the physical bottom edge, independent of its content height.
        ui.setHostAnchor(0.5, 1.0);
        ui.setHostPos(0.0, -18.0, 0.5, 1.0);
        ui.setHostPercent(0.94, 0.32);
    } else {
        ui.setHostAnchor(0.5, 0.5);
        ui.setHostPos(0.0, 0.0, 0.5, 0.5);
        ui.setHostSize(gal.mode == "history" ? 1120.0 : 820.0,
                       gal.mode == "history" ? 680.0 : 440.0);
    }
    gal.uiMode = gal.mode;
}

function handleClick(id) {
    if (id == "galui/start" || id == "galui/restart") startGame();
    else if (id == "galui/title") { gal.mode = "title"; gal.uiMode = ""; }
    else if (id == "galui/load") loadGame();
    else if (id == "galui/save") saveGame();
    else if (id == "galui/history") { gal.mode = "history"; gal.uiMode = ""; }
    else if (id == "galui/back") { gal.mode = "game"; gal.uiMode = ""; }
    else if (id == "galui/truth") chooseRoute("truth");
    else if (id == "galui/silence") chooseRoute("silence");
    else if (id == "galui/auto") {
        gal.ux.setAutoMode(!gal.ux.isAutoMode());
        setToast(gal.ux.isAutoMode() ? "自动播放：开" : "自动播放：关");
    } else if (id == "galui/skip") {
        local next = gal.ux.getSkipMode() == "all" ? "off" : "all";
        gal.ux.setSkipMode(next); setToast(next == "all" ? "快进：开" : "快进：关");
    }
}

function eve_init() {
    gfx.setBackgroundColor(0.02, 0.03, 0.08, 1.0);
    ui.setScale(1.35);
    fs.setIdentity("tidal-frequency", true); fs.setupWriteDirectory();
    gal.bg = gfx.newTextureFromFile("assets/station_twilight.png");
    gal.linTex = gfx.newTextureFromFile("assets/lin.png");
    gal.zhouTex = gfx.newTextureFromFile("assets/zhou.png");
    gal.lin = makeAvatar(gal.linTex, "left"); gal.zhou = makeAvatar(gal.zhouTex, "right");
    gal.ux = eve.DialogueUX(); gal.ux.setAutoDelay(1.8);
    rebuildUI();
}

function eve_reload() { gal.uiMode = ""; }

function eve_update(dt) {
    if (gal.uiMode != gal.mode) rebuildUI();
    local clickedUI = false;
    while (true) {
        local id = ui.consumeClick();
        if (id == "") break;
        clickedUI = true;
        handleClick(id);
    }
    local mouseNow = mouse.isDown(1);
    local touchNow = touch.getTouchCount() > 0;
    local pointerAdvance = (mouseNow && !gal.mouseWas) || (touchNow && !gal.touchWas);
    gal.mouseWas = mouseNow;
    gal.touchWas = touchNow;
    if (gal.mode == "choice") {
        if (key_just_pressed("1")) chooseRoute("truth");
        if (key_just_pressed("2")) chooseRoute("silence");
    } else if (gal.mode == "game") {
        dialogue.update(dt); avatar.update(dt); dialogue.syncStage(config.width.tofloat(), config.height.tofloat());
        if (dialogue.isWaitingAdvance() && (gal.ux.getSkipMode() == "all" || gal.ux.updateAuto(dt, false))) advanceStory();
        if (key_just_pressed("Space") || key_just_pressed("Return")) advanceStory();
        if (pointerAdvance && !clickedUI) advanceStory();
        if (key_just_pressed("F5")) saveGame();
        if (key_just_pressed("F9")) loadGame();
        if (key_just_pressed("L")) { gal.mode = "history"; gal.uiMode = ""; }
        ui.select("galui");
        ui.setText("speaker", dialogue.getSpeakerName() == "" ? "旁白" : dialogue.getSpeakerName());
        ui.setText("line", dialogue.getVisibleText());
        local flags = (gal.ux.isAutoMode() ? "AUTO  " : "") + (gal.ux.getSkipMode() != "off" ? "SKIP  " : "");
        ui.setText("status", flags + (gal.toastLeft > 0.0 ? gal.toast : "Space 推进 · F5 存档 · F9 读档 · L 回想"));
    }
    if (gal.toastLeft > 0.0) gal.toastLeft -= dt;
}

function eve_render() {
    gfx.clear();
    gfx.drawTexturedRect(gal.bg, 0.0, 0.0, config.width.tofloat(), config.height.tofloat(), 1.0, 1.0, 1.0, 1.0);
    if (gal.mode != "title") {
        avatar.render(gfx);
        gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), config.height.tofloat(), 0.03, 0.05, 0.12,
                          gal.mode == "game" ? 0.05 : 0.38);
    }
    ui.beginFrameAndRender();
}
