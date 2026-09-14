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

persist dlg = null
persist i18n = eve.I18n()
persist aliceAv = null
persist bobAv = null
persist vnGen = null
persist vnDone = false
persist waitingResume = false
// mouse / touch 边沿检测仍用 prevKeys（edgePressed）。
persist prevKeys = {}
persist uiReady = false
persist dialogueView = null
// Nine-patch loading borrows the Image provider; keep it alive with the view.
persist dialogueImages = null
// Textures outlive the Avatar layers that borrow them.
persist portraitTextures = {}
persist viewStyle = 0
persist viewTime = 0.0
persist curSceneName = "town"
persist sceneReady = false

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

function makePortrait(name) {
    if (!(name in portraitTextures))
        portraitTextures[name] <- gfx.newTextureFromFile("assets/" + name + ".png");
    local av = avatar.newImageAvatar();
    av.addLayer("portrait", portraitTextures[name], 0);
    av.setLayerSize("portrait", 256.0, 384.0);
    av.setLayerOffset("portrait", -128.0, 0.0);
    // This demo has one still per actor; expression names retain the same image.
    av.defineExpression("neutral", "portrait=1");
    av.defineExpression("happy", "portrait=1");
    av.defineExpression("shy", "portrait=1");
    av.applyExpression("neutral");
    av.setPosition(0.0, 20.0);
    av.setLayer(20);
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
    dlg.setLipSyncEnabled(false);
    dlg.setSlotX("left", 0.25);
    dlg.setSlotX("right", 0.75);
    bobAv.setPosition(0.0, 20.0);
    aliceAv.setPosition(0.0, 20.0);
    aliceAv.setVisible(false);
    bobAv.setVisible(false);
    vnGen = scene_intro();
    vnDone = false;
    waitingResume = false;
    resumeVn();
}

// Presentation is disposable; switching skins keeps the current line and choices.
function buildUI() {
    if (dialogueView == null)
        dialogueView = make_default_dialogue_ui(dlg, dialogueUX, ui, "dlgbox");
    local options = {
        x = 40.0, y = 224.0, width = 864.0, height = 296.0,
        hint = tr("hint.styles")
    };
    if (viewStyle == 0) options.skin <- "skins/moonlight.9.png";
    else if (viewStyle == 1) {
        options.skin <- "skins/letter.9.png";
        options.x = 180.0;
        options.width = 600.0;
    } else {
        options.x = 64.0;
        options.y = 300.0;
        options.width = 820.0;
        // Script painting can animate, follow speakers, or draw scene-space bubbles.
        // Text and accessible choice buttons remain ordinary UI widgets here.
        options.drawBackground <- function(frame) {
            local blue = frame.speakerId == "bob";
            gfx.drawSolidRect(40.0, 276.0, 880.0, 244.0, 0.025, 0.06, 0.09, 0.94);
            gfx.drawSolidRect(40.0, 276.0, 4.0, 244.0,
                              blue ? 0.48 : 0.23, 0.82, blue ? 1.0 : 0.65, 1.0);
            for (local i = 0; i < 24; ++i) {
                local h = frame.typing ? 3.0 + (sin(viewTime * 9.0 + i * 0.8) + 1.0) * 7.0 : 3.0;
                gfx.drawSolidRect(740.0 + i * 6.0, 290.0 - h * 0.5, 2.0, h, 0.3, 0.85, 0.72, 0.8);
            }
        };
    }
    dialogueView.setPresentation(options);
    dialogueView.mount();
    uiReady = true;
}

function refreshUI(dt) {
    if (!uiReady) return;
    if (dialogueView.update(dt)) resumeVn();
    ui.select("dlgbox");
    local action = dlg.isWaitingChoice() ? tr("hint.choose")
                  : (vnDone ? tr("hint.restart") : tr("hint.advance"));
    ui.setText("hint", action + "\n" + tr("hint.styles"));
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
    gfx.setBackgroundColor(0.20, 0.18, 0.19, 1.0);
    if (dlg == null) dlg = dialogue;
    if (dialogueImages == null) dialogueImages = eve.Image();
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
    if (aliceAv == null) aliceAv = makePortrait("alice");
    if (bobAv == null) bobAv = makePortrait("bob");
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
    viewTime += dt;
    if (keyPressed("4")) { viewStyle = 0; buildUI(); }
    if (keyPressed("5")) { viewStyle = 1; buildUI(); }
    if (keyPressed("6")) { viewStyle = 2; buildUI(); }
    local picked = dialogueView.consumeChoice();
    if (picked >= 0) tryChoice(picked);
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

    local pointerClick = mouseClicked();
    local space = keyPressed("Space");
    local enter = keyPressed("Return");
    local tapped = touchTapped();
    local clicked = pointerClick || space || enter || tapped;

    if (picked < 0 && !dlg.isWaitingChoice() && clicked) {
        tryAdvance();
    }

    refreshUI(dt);
}

function eve_render() {
    gfx.clear();
    // The warm neutral stage keeps the portrait silhouettes readable.
    avatar.render(gfx);
    dialogueView.render();
}
