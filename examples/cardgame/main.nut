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

if (!("card" in getroottable())) card <- null;
if (!("playerCfg" in getroottable())) playerCfg <- null;
if (!("enemyCfg" in getroottable())) enemyCfg <- null;
if (!("mana" in getroottable())) mana <- 10;
if (!("played" in getroottable())) played <- 0;
if (!("discarded" in getroottable())) discarded <- 0;
if (!("selected" in getroottable())) selected <- null;
if (!("logLines" in getroottable())) logLines <- [];
if (!("uiBuilt" in getroottable())) uiBuilt <- false;
if (!("prevKeys" in getroottable())) prevKeys <- {};

local defIds = ["flame.element", "frost.guard", "stone.golem", "jungle.drake",
                "shadow.assassin", "iron.knight", "fireball", "healing.light", "time.warp"];

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 6)
        logLines.remove(0);
}

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

// 注册卡牌类型定义（JSON）
function registerCards() {
    card.registerCardsFromJson(@"[
      {""id"":""flame.element"",""name"":""烈焰元素"",""kind"":""creature"",""cost"":2,""attack"":3,""health"":2,""tint"":[0.85,0.35,0.20]},
      {""id"":""frost.guard"",""name"":""寒霜守卫"",""kind"":""creature"",""cost"":3,""attack"":2,""health"":5,""tint"":[0.35,0.65,0.85]},
      {""id"":""stone.golem"",""name"":""石甲傀儡"",""kind"":""creature"",""cost"":4,""attack"":3,""health"":6,""tint"":[0.60,0.55,0.45]},
      {""id"":""jungle.drake"",""name"":""丛林幼龙"",""kind"":""creature"",""cost"":3,""attack"":4,""health"":3,""tint"":[0.40,0.75,0.35]},
      {""id"":""shadow.assassin"",""name"":""暗影刺客"",""kind"":""creature"",""cost"":2,""attack"":4,""health"":1,""tint"":[0.45,0.30,0.60]},
      {""id"":""iron.knight"",""name"":""钢铁骑士"",""kind"":""creature"",""cost"":5,""attack"":4,""health"":5,""tint"":[0.55,0.62,0.70]},
      {""id"":""fireball"",""name"":""火球术"",""kind"":""spell"",""cost"":3,""tint"":[0.95,0.55,0.25]},
      {""id"":""healing.light"",""name"":""治愈之光"",""kind"":""spell"",""cost"":2,""tint"":[0.95,0.85,0.55]},
      {""id"":""time.warp"",""name"":""时间扭曲"",""kind"":""spell"",""cost"":5,""tint"":[0.60,0.45,0.85]}
    ]");
}

// 用定义填充牌库（24 张，混合生物与法术）
function fillDeck() {
    local deck = card.getDeck();
    deck.clear();
    local n = 0;
    while (n < 24) {
        foreach (did in defIds) {
            deck.push(card.newCard(did));
            n += 1;
            if (n >= 24) break;
        }
    }
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
    for (local i = 0; i < 4; i += 1) card.drawCard("player");
    for (local i = 0; i < 5; i += 1) card.drawCard("enemy");
    pushLog("重置：洗牌并发牌。拖拽手牌到出牌区 / 弃牌区，或点按查看。");
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
    ui.sameLine();
    ui.button("重置 (R)", "reset");
    ui.button("偷看敌方 (2)", "peek");
    ui.sameLine();
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
    local info = "法力 " + mana + "   牌库 " + card.getDeck().count() +
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
    gfx.setBackgroundColor(0.09, 0.11, 0.16, 1.0);
    if (card == null) {
        card = eve.Card();
        registerCards();

        playerCfg = card.newConfig();
        playerCfg.setHandX(config.width * 0.5);
        playerCfg.setHandY(config.height - 80.0);
        playerCfg.setDeckX(config.width * 0.5 - 330.0);
        playerCfg.setDeckY(config.height - 100.0);
        card.setConfig(playerCfg);

        enemyCfg = card.newConfig();
        enemyCfg.setHandX(config.width * 0.5);
        enemyCfg.setHandY(80.0);
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
            playerCfg.getHandX() - 340.0, playerCfg.getHandY() + 40.0, 680.0, 70.0);
        hz.setColor(0.25, 0.60, 0.30); hz.setAlpha(0.14);

        local pz = card.newZone("play", "出牌区",
            config.width * 0.5 - 240.0, config.height * 0.32, 480.0, 170.0);
        pz.setColor(0.90, 0.55, 0.20); pz.setAlpha(0.14);

        local dz = card.newZone("discard", "弃牌区",
            config.width * 0.5 + 300.0, playerCfg.getHandY() + 40.0, 140.0, 70.0);
        dz.setColor(0.55, 0.35, 0.30); dz.setAlpha(0.14);

        resetRun();
    }
    if (!uiBuilt) buildPanel();
    syncHud();
};

eve_reload <- function() {
    syncHud();
};

eve_update = function(dt) {
    // 费用不足的牌自动置灰
    local ph = card.findHand("player");
    for (local i = 0; i < ph.count(); i += 1) {
        local c = ph.getCard(i);
        c.setDisabled(c.getCost() > mana);
    }

    // 面板按钮
    while (true) {
        local c = ui.consumeClick();
        if (c == "") break;
        if (c == "panel/draw") {
            if (card.drawCard("player") != null) pushLog("抽一张牌。");
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
        if (card.drawCard("player") != null) pushLog("抽一张牌。");
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
                        pushLog("法力不足，无法打出 " + c.getName() + "。");
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
    card.render(gfx);
    card.renderDeck(gfx);
    ui.beginFrameAndRender();
};
