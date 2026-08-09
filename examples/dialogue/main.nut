// ============================================================================
// EVEngine Dialogue + Avatar demo — visual-novel style, still pure Squirrel.
//
//   eve.Dialogue  —— 台词打字机 / 选项 / 舞台槽位
//   eve.Avatar    —— Image 分层人物（Live2D / VRoid API 见设计文档）
//
// 剧情用 Squirrel generator 编写（yield "wait" / yield "choice"），
// 不引入第二套脚本 DSL。
//
// 操作：空格 / 鼠标左键 / 触屏 推进；选项阶段按 1 / 2。
// 运行：make run/linux-debug GAME=examples/dialogue
// ============================================================================

if (!("dlg" in getroottable())) dlg <- null;
if (!("aliceAv" in getroottable())) aliceAv <- null;
if (!("bobAv" in getroottable())) bobAv <- null;
if (!("vnGen" in getroottable())) vnGen <- null;
if (!("vnDone" in getroottable())) vnDone <- false;
if (!("waitingResume" in getroottable())) waitingResume <- false;
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("uiReady" in getroottable())) uiReady <- false;

function edgePressed(key, down) {
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function keyPressed(name) {
    return edgePressed("k_" + name, keyboard.isDown(name));
}

function mouseClicked() {
    return edgePressed("m0", mouse.isDown(0));
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
function scene_intro() {
    dlg.show("alice", "left");
    dlg.setExpression("alice", "happy");
    dlg.say("alice", "你好！我是 Alice。");
    yield "wait";

    dlg.show("bob", "right");
    dlg.say("bob", "我是 Bob。这是用 Squirrel 写的对话脚本。");
    yield "wait";

    dlg.setExpression("alice", "shy");
    dlg.say("alice", "神态和动作都走 Avatar 分层；没有另发明脚本语言。");
    yield "wait";

    dlg.narrate("要不要继续听他们聊天？");
    yield "wait";

    dlg.clearChoices();
    dlg.addChoice("yes", "继续");
    dlg.addChoice("no", "先这样");
    dlg.presentChoices();
    yield "choice";

    if (dlg.getSelectedChoiceId() == "yes") {
        dlg.setExpression("alice", "happy");
        dlg.setMotion("alice", "wave");
        dlg.say("alice", "太好了！Live2D / VRoid 也可以挂到同一套 API 上。");
        yield "wait";
        dlg.say("bob", "Image 层是基础；其它后端按需接入。");
        yield "wait";
    } else {
        dlg.say("bob", "好的，下次再见。");
        yield "wait";
    }

    dlg.narrate("（演示结束 —— 空格可重开）");
    yield "wait";
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
    dlg.registerCharacter("alice", "Alice");
    dlg.registerCharacter("bob", "Bob");
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
    ui.text("空格 / 点击 继续", "hint");
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
        ui.setText("hint", "按 1 / 2 选择");
    } else {
        ui.setText("c1", "");
        ui.setText("c2", "");
        ui.setText("hint", vnDone ? "空格重开" : "空格 / 点击 继续");
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
    if (dlg == null) dlg = dialogue;
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
    dlg.update(dt);
    avatar.update(dt);
    dlg.syncStage(config.width.tofloat(), config.height.tofloat());

    local clicked = mouseClicked() || keyPressed("Space") || keyPressed("Return") || touchTapped();

    if (dlg.isWaitingChoice()) {
        if (keyPressed("1")) tryChoice(0);
        if (keyPressed("2")) tryChoice(1);
    } else if (clicked) {
        tryAdvance();
    }

    refreshUI();
}

function eve_render() {
    gfx.clear(0.12, 0.14, 0.18, 1.0);
    // soft stage floor
    gfx.drawSolidRect(0.0, config.height - 80.0, config.width.tofloat(), 80.0, 0.16, 0.18, 0.22, 1.0);
    avatar.render(gfx);
}
