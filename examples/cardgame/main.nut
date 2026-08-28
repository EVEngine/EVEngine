// ============================================================================
// EVEngine 卡牌工具模块示例 —— 功能参考 ycarowr/UiCard
//
// 演示 C++ 模块 eve.Card()（src/modules/card/）：
//   扇形手牌布局       间距 / 弧高 / 悬浮放大 / 悬浮上移 / 运动速度
//   抽牌 / 洗牌        牌库堆可视化 + 剩余张数
//   敌方手牌           背面朝上，可“偷看”翻面
//   拖拽出牌           拖到出牌区打出、弃牌区弃掉、手牌区松手归位
//   费用系统           法力不足的牌自动置灰（disabledAlpha）且不可拖拽
//   配置面板           左侧 ImGui 面板实时调节全部布局参数（UiCard Configs）
//
// 操作：左键拖拽 / 点按查看；按键 1 抽牌、2 偷看敌方、R 重置。
// 运行： make run/<platform>-debug GAME=examples/cardgame
// ============================================================================

persist card = null
persist playerCfg = null
persist enemyCfg = null
persist mana = 10
persist played = 0
persist discarded = 0
persist deckRemaining = 0
persist selected = null
persist logLines = []
persist uiBuilt = false
persist artTextures = {}

local suits = ["Clovers", "Hearts", "Pikes", "Tiles"];
local ranks = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King"];
local defIds = [];
foreach (suit in suits) {
    foreach (rank in ranks) {
        defIds.push(suit + "_" + rank);
    }
}

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 6)
        logLines.remove(0);
}

function keyPressed(name) {
    return key_just_pressed(name);
}

function readTextFile(path) {
    local handle = file(path, "r");
    if (handle == null) return null;
    local content = handle.read();
    handle.close();
    return content;
}

function getCardTexture(defId) {
    if (defId in artTextures) return artTextures[defId];
    local path = "assets/playing-cards/" + defId + "_black.png";
    local texture = gfx.newTextureFromFile(path);
    artTextures.rawset(defId, texture);
    return texture;
}

function renderPlayingCardFaces() {
    card.capturePresentation();
    local playerHand = card.findHand("player");
    local enemyHand = card.findHand("enemy");
    local enemyHidden = enemyHand != null && enemyHand.isFaceDown() && !enemyHand.isPeek();

    for (local i = 0; i < card.getPresentationCount(); i += 1) {
        local snap = card.getPresentation(i);
        local instanceId = snap.getInstanceId();
        local inPlayerHand = playerHand != null && playerHand.findCard(instanceId) != null;
        local inEnemyHand = enemyHand != null && enemyHand.findCard(instanceId) != null;
        if ((inPlayerHand || inEnemyHand) && snap.isFaceUp() && !(inEnemyHand && enemyHidden)) {
            local scale = snap.getScale();
            local shade = snap.isDisabled() ? 0.42 : 1.0;
            gfx.drawTexturedRectRotated(getCardTexture(snap.getDefinitionId()),
                snap.getX(), snap.getY(), snap.getW() * scale, snap.getH() * scale,
                snap.getAngle(), shade, shade, shade, snap.getAlpha());
        }
    }
}

// 注册卡牌类型定义（JSON，从 data/cards.json 读取）
function registerCards() {
    local json = readTextFile("data/cards.json");
    if (json == null) {
        print("cardgame: missing data/cards.json\n");
        return;
    }
    card.registerCardsFromJson(json);
}

// 用完整 52 张扑克牌定义填充牌库
function fillDeck() {
    local deck = card.getDeck();
    deck.clear();
    foreach (did in defIds)
        deck.push(card.newCard(did));
    deck.shuffle();
}

function resetRun() {
    card.findHand("player").clear();
    card.findHand("enemy").clear();
    mana = 10;
    played = 0;
    discarded = 0;
    selected = null;
    fillDeck();
    deckRemaining = defIds.len();
    for (local i = 0; i < 4; i += 1) card.drawCard("player");
    for (local i = 0; i < 5; i += 1) card.drawCard("enemy");
    deckRemaining -= 9;
    pushLog("重置：洗好 52 张牌并发牌。拖拽手牌到出牌区 / 弃牌区，或点按查看。");
}

function buildPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("卡牌工具配置（UiCard）", "root");
    ui.text("手牌布局", "h1");
    ui.slider("间距 spacing", playerCfg.getSpacing(), 0.0, 90.0, "spacing");
    ui.slider("弧高 arcHeight", playerCfg.getArcHeight(), 0.0, 140.0, "arc");
    ui.slider("旋转角 rotationAngle", playerCfg.getRotationAngle(), 0.0, 30.0, "rotation");
    ui.slider("悬浮缩放 hoverScale", playerCfg.getHoverScale(), 1.0, 2.2, "hscale");
    ui.slider("悬浮上移 hoverLift", playerCfg.getHoverLift(), 0.0, 120.0, "hlift");
    ui.slider("悬浮速度 hoverSpeed", playerCfg.getHoverSpeed(), 0.02, 0.6, "hspeed");
    ui.slider("运动速度 motionSpeed", playerCfg.getMotionSpeed(), 0.02, 0.6, "mspeed");
    ui.slider("禁用透明度 disabledAlpha", playerCfg.getDisabledAlpha(), 0.05, 1.0, "dalpha");
    ui.slider("手牌高度 Y handY", playerCfg.getHandY(), 200.0, config.height.tofloat(), "handy");
    ui.checkbox("绘制落牌区", playerCfg.getShowZones(), "showzones");
    ui.checkbox("悬浮时转正", playerCfg.getHoverRotation(), "hoverrotation");
    ui.text("操作", "h1");
    ui.button("抽一张牌 (1)", "draw");
    ui.sameLine("sl_draw");
    ui.button("重置 (R)", "reset");
    ui.button("偷看敌方 (2)", "peek");
    ui.sameLine("sl_peek");
    ui.button("洗牌", "shuffle");
    ui.text("", "hud");
    ui.text("", "log");
    ui.end();
    ui.mountBuildAs("panel");
    ui.select("panel");
    ui.setHostOverlay(true);
    ui.setHostPos(14.0, 14.0, 0.0, 0.0);
    uiBuilt = true;
}

function syncHud() {
    if (!uiBuilt) return;
    ui.select("panel");
    local deck = card.getDeck();
    local deckCount = (deck != null && ("count" in deck)) ? deck.count() : deckRemaining;
    local info = "行动点 " + mana + "   牌库 " + deckCount +
        "   已出 " + played + "   弃掉 " + discarded;
    if (selected != null)
        info += "\n选中: " + selected.describe();
    ui.setText("hud", info);
    local txt = "";
    foreach (l in logLines)
        txt += l + "\n";
    ui.setText("log", txt);
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.030, 0.040, 1.0);
    if (card == null) {
        card = eve.Card();
        registerCards();

        playerCfg = card.newConfig();
        playerCfg.setCardW(112.0);
        playerCfg.setCardH(159.0);
        playerCfg.setSpacing(20.0);
        playerCfg.setHandX(790.0);
        playerCfg.setHandY(config.height - 76.0);
        playerCfg.setDeckX(1160.0);
        playerCfg.setDeckY(config.height - 108.0);
        card.setConfig(playerCfg);

        enemyCfg = card.newConfig();
        enemyCfg.setCardW(98.0);
        enemyCfg.setCardH(139.0);
        enemyCfg.setHandX(790.0);
        enemyCfg.setHandY(82.0);
        enemyCfg.setArcHeight(28.0);
        enemyCfg.setSpacing(30.0);

        local deck = card.newDeck();

        local ph = card.newHand(playerCfg);
        ph.setOwner("player");

        local eh = card.newHand(enemyCfg);
        eh.setOwner("enemy");
        eh.setFaceDown(true);
        eh.setInteractive(false);

        local hz = card.newZone("hand", "手牌区（松手归位）",
            playerCfg.getHandX() - 355.0, playerCfg.getHandY() + 38.0, 710.0, 70.0);
        hz.setColor(0.25, 0.72, 0.62); hz.setAlpha(0.11);

        local pz = card.newZone("play", "出牌区",
            530.0, 270.0, 520.0, 170.0);
        pz.setColor(0.94, 0.55, 0.34); pz.setAlpha(0.11);

        local dz = card.newZone("discard", "弃牌区",
            1015.0, playerCfg.getHandY() + 38.0, 128.0, 70.0);
        dz.setColor(0.78, 0.30, 0.34); dz.setAlpha(0.11);

        resetRun();
    }
    if (!uiBuilt) buildPanel();
    syncHud();
};

eve_reload <- function() {
    syncHud();
};

eve_update = function(dt) {
    // 行动点不足的牌自动置灰
    local ph = card.findHand("player");
    if (ph != null && ("count" in ph)) {
        for (local i = 0; i < ph.count(); i += 1) {
            local c = ph.getCard(i);
            if (c != null && ("setDisabled" in c))
                c.setDisabled(c.getCost() > mana);
        }
    }

    // 面板按钮
    while (true) {
        local c = ui.consumeClick();
        if (c == "") break;
        if (c == "panel/draw") {
            if (card.drawCard("player") != null) {
                deckRemaining -= 1;
                pushLog("抽一张牌。");
            }
            else pushLog("牌库空了。");
        } else if (c == "panel/reset") {
            resetRun();
        } else if (c == "panel/peek") {
            local eh = card.findHand("enemy");
            eh.setPeek(!eh.isPeek());
            pushLog(eh.isPeek() ? "偷看敌方手牌。" : "收回敌方手牌。");
        } else if (c == "panel/shuffle") {
            card.getDeck().shuffle();
            pushLog("洗牌。");
        }
    }

    // 面板滑块变更
    while (true) {
        local ch = ui.consumeChange();
        if (ch == "") break;
        if (ch == "panel/spacing") playerCfg.setSpacing(ui.getValue("spacing"));
        else if (ch == "panel/arc") playerCfg.setArcHeight(ui.getValue("arc"));
        else if (ch == "panel/rotation") playerCfg.setRotationAngle(ui.getValue("rotation"));
        else if (ch == "panel/hscale") playerCfg.setHoverScale(ui.getValue("hscale"));
        else if (ch == "panel/hlift") playerCfg.setHoverLift(ui.getValue("hlift"));
        else if (ch == "panel/hspeed") playerCfg.setHoverSpeed(ui.getValue("hspeed"));
        else if (ch == "panel/mspeed") playerCfg.setMotionSpeed(ui.getValue("mspeed"));
        else if (ch == "panel/dalpha") playerCfg.setDisabledAlpha(ui.getValue("dalpha"));
        else if (ch == "panel/handy") playerCfg.setHandY(ui.getValue("handy"));
        else if (ch == "panel/showzones") playerCfg.setShowZones(ui.getChecked("showzones"));
        else if (ch == "panel/hoverrotation") playerCfg.setHoverRotation(ui.getChecked("hoverrotation"));
    }

    // 同步滑块显示
    ui.select("panel");
    ui.setValue("spacing", playerCfg.getSpacing());
    ui.setValue("arc", playerCfg.getArcHeight());
    ui.setValue("rotation", playerCfg.getRotationAngle());
    ui.setValue("hscale", playerCfg.getHoverScale());
    ui.setValue("hlift", playerCfg.getHoverLift());
    ui.setValue("hspeed", playerCfg.getHoverSpeed());
    ui.setValue("mspeed", playerCfg.getMotionSpeed());
    ui.setValue("dalpha", playerCfg.getDisabledAlpha());
    ui.setValue("handy", playerCfg.getHandY());
    ui.setChecked("showzones", playerCfg.getShowZones());
    ui.setChecked("hoverrotation", playerCfg.getHoverRotation());

    // 键盘快捷键
    if (keyPressed("1")) {
        if (card.drawCard("player") != null) {
            deckRemaining -= 1;
            pushLog("抽一张牌。");
        }
        else pushLog("牌库空了。");
    }
    if (keyPressed("2")) {
        local eh = card.findHand("enemy");
        eh.setPeek(!eh.isPeek());
        pushLog(eh.isPeek() ? "偷看敌方手牌。" : "收回敌方手牌。");
    }
    if (keyPressed("r") || keyPressed("R")) resetRun();

    // 鼠标悬停 / 拖拽（ImGui 面板上时交给 UI 处理）
    local overUi = ui.wantCaptureMouse();
    card.update(dt, mouse.getX(), mouse.getY(), mouse.isDown(1) && !overUi);

    // 交互事件
    for (local i = 0; i < card.getEventCount(); i += 1) {
        local type = card.getEventType(i);
        local zoneId = card.getEventZone(i);
        local cardId = card.getEventCardId(i);
        if (type == "dropRejected") {
            pushLog("无法放置：" + card.getEventReason(i));
        } else if (type == "drop") {
            local c = ph.findCard(cardId);
            if (c != null) {
                if (zoneId == "play") {
                    if (c.getCost() > mana) {
                        pushLog("行动点不足，无法打出 " + c.getName() + "。");
                    } else {
                        ph.removeCard(c);
                        c.setState("played");
                        mana -= c.getCost();
                        played += 1;
                        pushLog("打出：" + c.describe());
                    }
                } else if (zoneId == "discard") {
                    ph.removeCard(c);
                    c.setState("discarded");
                    discarded += 1;
                    pushLog("弃掉：" + c.describe());
                }
            }
            selected = null;
        } else if (type == "click") {
            local c = ph.findCard(cardId);
            if (c != null) {
                selected = c;
                pushLog("点按：" + c.describe());
            }
        }
    }
    card.clearEvents();

    syncHud();
};

eve_render = function() {
    gfx.clear();
    // Casino-style table that keeps the configuration panel visually separate.
    gfx.drawSolidRect(330.0, 18.0, 932.0, 684.0, 0.08, 0.035, 0.055, 1.0);
    gfx.drawSolidRect(340.0, 28.0, 912.0, 664.0, 0.48, 0.16, 0.22, 1.0);
    gfx.drawSolidRect(352.0, 40.0, 888.0, 640.0, 0.035, 0.18, 0.19, 1.0);
    gfx.drawSolidRect(364.0, 52.0, 864.0, 616.0, 0.045, 0.245, 0.25, 1.0);
    gfx.drawSolidRect(378.0, 242.0, 836.0, 2.0, 0.94, 0.55, 0.34, 0.28);
    gfx.drawSolidRect(378.0, 470.0, 836.0, 2.0, 0.94, 0.55, 0.34, 0.28);
    card.render(gfx);
    renderPlayingCardFaces();
    card.renderDeck(gfx);
    ui.beginFrameAndRender();
};
