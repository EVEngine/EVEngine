// ============================================================================
// EVEngine Dialogue + Avatar demo — visual-novel style, still pure Squirrel.
//
//   eve.Dialogue  ——  台词打字机 / 选项 / 舞台槽位
//   eve.Avatar    ——  Image 分层人物（Live2D / VRoid API 见设计文档）
//   eve.I18n      ——  对话文案走翻译表（locales/en.json, locales/zh.json）
//
// 剧情用 Squirrel generator 编写（yield "wait" / yield "choice"），
// 不引入第二套脚本 DSL。
//
// 操作：空格 / 鼠标左键 / 触屏 推进；选项阶段按 1 / 2。
// 运行：make run/linux-debug GAME=examples/dialogue
// ============================================================================

if (!("dlg" in getroottable())) dlg <- null;
if (!("i18n" in getroottable())) i18n <- eve.I18n();
if (!("aliceAv" in getroottable())) aliceAv <- null;
if (!("bobAv" in getroottable())) bobAv <- null;
if (!("vnGen" in getroottable())) vnGen <- null;
if (!("vnDone" in getroottable())) vnDone <- false;
if (!("waitingResume" in getroottable())) waitingResume <- false;
// mouse / touch 边沿检测仍用 prevKeys（edgePressed）。
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("uiReady" in getroottable())) uiReady <- false;
if (!("curSceneName" in getroottable())) curSceneName <- "town";
if (!("sceneReady" in getroottable())) sceneReady <- false;

function edgePressed(key, down) {
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function keyPressed(name) {
    return edgePressed("k_" + name, keyboard.isDown(name));
}

function mouseClicked() {
    // 1 = 左键（与 engine mouse::isDown 一致）。
    return edgePressed("m0", mouse.isDown(1));
}

function touchTapped() {
    return edgePressed("t0", touch.getTouchCount() > 0);
}

function makePortrait(kind, bodyR, bodyG, bodyB) {
    local av = avatar.newImageAvatar();
    av.addLayer("body", null, 0);
    av.addLayer("face", null, 1);
    av.addLayer("blush", null, 2);
    av.addLayer("mouthOpen", null, 3);  // lip-sync alpha driven by dialogue
    av.setLayerSize("body", 140.0, 280.0);
    av.setLayerSize("face", 90.0, 90.0);
    av.setLayerSize("blush", 70.0, 40.0);
    av.setLayerSize("mouthOpen", 36.0, 16.0);
    av.setLayerOffset("face", 25.0, 30.0);
    av.setLayerOffset("blush", 35.0, 70.0);
    av.setLayerOffset("mouthOpen", 52.0, 95.0);
    av.setLayerColor("body", bodyR, bodyG, bodyB, 1.0);
    av.setLayerColor("face", 0.96, 0.86, 0.78, 1.0);
    av.setLayerColor("blush", 0.95, 0.45, 0.55, 0.75);
    av.setLayerColor("mouthOpen", 0.45, 0.12, 0.15, 0.0);
    av.setLayerVisible("blush", false);
    av.setLayerVisible("mouthOpen", false);
    av.defineExpression("neutral", "blush=0");
    av.defineExpression("happy", "blush=0");
    av.defineExpression("shy", "blush=1");
    av.applyExpression("neutral");
    av.setPosition(0.0, config.height - 320.0);
    av.setLayer(20);
    if (kind == "happy") {
        av.setLayerColor("face", 0.98, 0.90, 0.72, 1.0);
    }
    return av;
}

// ---- VN script: still Squirrel (generator), not a new language ----
// 文案全部来自 i18n 翻译表（默认中文，按 数字键 1 / 2 切换语言）。
function tr(key, params = null) {
    if (params == null) return i18n.get(key);
    return i18n.getWithParams(key, params);
}

function scene_intro() {
    dlg.show("alice", "left");
    if (!dlg.playPool("alice.greet", { name = tr("name.player") }))
        dlg.say("alice", tr("line.hello", { name = tr("name.player") }));
    yield "wait";

    dlg.show("bob", "right");
    if (!dlg.playPool("bob.greet", { name = tr("name.player") }))
        dlg.say("bob", tr("line.intro"));
    yield "wait";

    dlg.setExpression("alice", "shy");
    dlg.say("alice", tr("line.avatar"));
    yield "wait";

    dlg.narrate(tr("line.continue_ask"));
    yield "wait";

    dlg.clearChoices();
    dlg.addChoice("yes", tr("choice.yes"));
    dlg.addChoice("no", tr("choice.no"));
    dlg.presentChoices();
    yield "choice";

    if (dlg.getSelectedChoiceId() == "yes") {
        dlg.setExpression("alice", "happy");
        dlg.setMotion("alice", "wave");
        dlg.say("alice", tr("line.backends"));
        yield "wait";
        dlg.say("bob", tr("line.image_layers"));
        yield "wait";
    } else {
        dlg.say("bob", tr("line.bye"));
        yield "wait";
    }

    dlg.narrate(tr("line.end"));
    yield "wait";
}

function buildScenes() {
    // 两个最小 Scene host：按键 3 切换，演示 scene 变量区随场景切换自动清空。
    scene.beginBuild();
    scene.beginNode("root", "Root");
    scene.end();
    scene.mountBuildAs("town");
    scene.beginBuild();
    scene.beginNode("root", "Root");
    scene.end();
    scene.mountBuildAs("tavern");
    scene.select("town");
    sceneReady = true;
}

function eve_asset_reload(path) {
    // .dnut 属于内容资产：热重载时重新编译并注册台词池。
    if (path == "pools.dnut") {
        dlg.loadPoolsFromDnutFile("pools.dnut");
        print("dialogue pools reloaded: " + path + "\n");
    }
}

function resumeVn() {
    if (vnDone || vnGen == null) return;
    try {
        local r = resume vnGen;
        if (r == null) {
            vnDone = true;
            waitingResume = false;
            return;
        }
        waitingResume = true;
    } catch (e) {
        // generator finished
        vnDone = true;
        waitingResume = false;
    }
}

function startScene() {
    dlg.reset();
    dlg.registerCharacter("alice", tr("name.alice"));
    dlg.registerCharacter("bob", tr("name.bob"));
    dlg.bindAvatar("alice", aliceAv);
    dlg.bindAvatar("bob", bobAv);
    dlg.setTypeSpeed(48.0);
    dlg.setLipSyncEnabled(true);
    dlg.setLipSyncParameter("mouthOpen");
    dlg.setLipSyncAmplitude(0.9);
    dlg.setSlotX("left", 0.22);
    dlg.setSlotX("right", 0.78);
    bobAv.setPosition(0.0, config.height - 320.0);
    aliceAv.setPosition(0.0, config.height - 320.0);
    aliceAv.setVisible(false);
    bobAv.setVisible(false);
    vnGen = scene_intro();
    vnDone = false;
    waitingResume = false;
    resumeVn();
}

function buildUI() {
    ui.beginBuild();
    ui.beginWindow("Dialogue", "root");
    ui.text("", "speaker");
    ui.text("", "line");
    ui.separator("sep");
    ui.text("", "hint");
    ui.text("", "c1");
    ui.text("", "c2");
    ui.end();
    ui.mountBuildAs("dlgbox");
    ui.select("dlgbox");
    ui.setHostOverlay(true);
    ui.setHostPos(config.width * 0.5, config.height - 12.0, 0.5, 1.0);
    uiReady = true;
}

function refreshUI() {
    if (!uiReady) return;
    ui.select("dlgbox");
    local speaker = dlg.getSpeakerName();
    if (speaker == null || speaker == "") speaker = " ";
    ui.setText("speaker", speaker);
    ui.setText("line", dlg.getVisibleText());

    if (dlg.isWaitingChoice()) {
        local n = dlg.getChoiceCount();
        local a = n > 0 ? ("[1] " + dlg.getChoiceLabel(0)) : "";
        local b = n > 1 ? ("[2] " + dlg.getChoiceLabel(1)) : "";
        ui.setText("c1", a);
        ui.setText("c2", b);
        ui.setText("hint", tr("hint.choose"));
    } else {
        ui.setText("c1", "");
        ui.setText("c2", "");
        local sceneInfo = sceneReady
            ? ("  场景:" + curSceneName + " met=" + dlg.getVarBool("met", false, "scene") + "  [3]切场景")
            : "";
        ui.setText("hint", (vnDone ? tr("hint.restart") : tr("hint.advance")) + sceneInfo);
    }
}

function tryAdvance() {
    if (vnDone) {
        startScene();
        return;
    }
    if (dlg.isTyping()) {
        dlg.skipTyping();
        return;
    }
    if (dlg.isWaitingAdvance()) {
        dlg.advance();
        resumeVn();
        return;
    }
}

function tryChoice(index) {
    if (!dlg.isWaitingChoice()) return;
    if (dlg.selectChoice(index)) resumeVn();
}

function eve_init() {
    gfx.setBackgroundColor(0.12, 0.14, 0.18, 1.0);
    if (dlg == null) dlg = dialogue;
    // 载入翻译表：en / zh，默认中文；1 / 2 键可切换（热重载开）。
    if (!i18n.hasLanguage("en")) {
        i18n.loadFromFile("en", "locales/en.json");
        i18n.loadFromFile("zh", "locales/zh.json");
        i18n.setDefaultLanguage("en");
        i18n.setLanguage("zh");
        i18n.setAutoReload(true);
    }
    // 程序化对话：.dnut 台词池 + 故事变量 + 脚本谓词。
    dlg.loadPoolsFromDnutFile("pools.dnut");
    dlg.setVar("name", tr("name.player"), "global");
    dlg.setVar("mood", "happy", "global");
    dlg.setVar("hour", 20, "global");
    dlg.registerCondition("isEvening", function(ctx) {
        return ctx.vars.hour >= 18;
    });
    buildScenes();
    if (aliceAv == null) aliceAv = makePortrait("happy", 0.35, 0.55, 0.85);
    if (bobAv == null) bobAv = makePortrait("neutral", 0.55, 0.40, 0.65);
    buildUI();
    startScene();
}

function eve_reload() {
    buildUI();
}

function eve_update(dt) {
    if ("anim" in getroottable())
        anim.update(dt);
    i18n.update(dt);
    // Sample each edge once: keyPressed() updates its edge-detection state.
    local key1 = keyPressed("1");
    local key2 = keyPressed("2");
    local key3 = keyPressed("3");

    // 1 / 2 select choices while a choice is active; otherwise they switch language.
    if (dlg.isWaitingChoice()) {
        if (key1) tryChoice(0);
        if (key2) tryChoice(1);
    } else {
        if (key1 && i18n.setLanguage("en")) {
            dlg.reset();
            startScene();
        }
        if (key2 && i18n.setLanguage("zh")) {
            dlg.reset();
            startScene();
        }
    }
    // 3 = 切换场景：Dialogue 感知 Scene host 变化并自动清空 scene 变量区。
    if (key3) {
        curSceneName = (curSceneName == "town") ? "tavern" : "town";
        scene.select(curSceneName);
        dlg.setVar("met", true, "scene");
        dlg.reset();
        startScene();
    }
    dlg.update(dt);
    avatar.update(dt);
    dlg.syncStage(config.width.tofloat(), config.height.tofloat());

    local clicked = mouseClicked() || keyPressed("Space") || keyPressed("Return") || touchTapped();

    if (!dlg.isWaitingChoice() && clicked) {
        tryAdvance();
    }

    refreshUI();
}

function eve_render() {
    gfx.clear();
    // soft stage floor
    gfx.drawSolidRect(0.0, config.height - 80.0, config.width.tofloat(), 80.0, 0.16, 0.18, 0.22, 1.0);
    avatar.render(gfx);
    ui.beginFrameAndRender();
}
