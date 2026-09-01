dofile("poker_rules.nut");

// Playable heads-up Texas Hold'em example.
// Controls: C check/call, R raise, A all-in, F fold, N next hand.

local pokerCard = null;
local playerCfg = null;
local aiCfg = null;
local boardCfg = null;
local artTextures = {};
local allCardObjects = [];

persist playerStack: dynamic = 1000
persist aiStack: dynamic = 1000
persist pot: dynamic = 0
persist playerStreetBet: dynamic = 0
persist aiStreetBet: dynamic = 0
persist currentBet: dynamic = 0
persist handNumber: dynamic = 0
persist dealer: dynamic = "ai"
persist street: dynamic = "preflop"
persist actor: dynamic = ""
persist actionsSinceRaise: dynamic = 0
persist handState: dynamic = "idle"
persist aiThinkTimer: dynamic = 0.0
persist playerHole: dynamic = []
persist aiHole: dynamic = []
persist boardCards: dynamic = []
persist burnCount: dynamic = 0
persist resultText: dynamic = ""
persist logLines: dynamic = []
persist pokerUiVersion: dynamic = 0

local smallBlind = 10;
local bigBlind = 20;
local suits = ["Clovers", "Hearts", "Pikes", "Tiles"];
local ranks = ["A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "Jack", "Queen", "King"];
local rankValues = [14, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13];
local cardInfoById = {};
local defIds = [];

foreach (suit in suits) {
    for (local i = 0; i < ranks.len(); i += 1) {
        local id = suit + "_" + ranks[i];
        defIds.push(id);
        cardInfoById.rawset(id, { id = id, suit = suit, rank = rankValues[i] });
    }
}

function keyPressed(name, alternate = "") {
    return key_just_pressed(name, alternate);
}

function readTextFile(path) {
    local handle = file(path, "r");
    if (handle == null) return null;
    local content = handle.read();
    handle.close();
    return content;
}

function pushLog(text) {
    logLines.push(text);
    while (logLines.len() > 9) logLines.remove(0);
}

function streetName() {
    if (street == "preflop") return "翻牌前";
    if (street == "flop") return "翻牌";
    if (street == "turn") return "转牌";
    return "河牌";
}

function otherPlayer(owner) {
    return owner == "player" ? "ai" : "player";
}

function ownerName(owner) {
    return owner == "player" ? "你" : "AI";
}

function getCardTexture(defId) {
    if (defId in artTextures) return artTextures[defId];
    local texture = gfx.newTextureFromFile("assets/playing-cards/" + defId + "_black.png");
    artTextures.rawset(defId, texture);
    return texture;
}

function registerCards() {
    local json = readTextFile("data/cards.json");
    if (json == null) throw "cardgame: missing data/cards.json";
    if (pokerCard.registerCardsFromJson(json) != 52) throw "cardgame: expected 52 card definitions";
}

function configureLayout(cfg, cardW, cardH, spacing, x, y) {
    cfg.setCardW(cardW);
    cfg.setCardH(cardH);
    cfg.setSpacing(spacing);
    cfg.setHandX(x);
    cfg.setHandY(y);
    cfg.setArcHeight(0.0);
    cfg.setRotationAngle(0.0);
    cfg.setHoverScale(1.08);
    cfg.setHoverLift(10.0);
    cfg.setShowZones(false);
}

function createPokerTable() {
    pokerCard = eve.Card();
    registerCards();

    playerCfg = pokerCard.newConfig();
    configureLayout(playerCfg, 115.0, 163.0, 18.0, 790.0, 616.0);
    playerCfg.setDeckX(1162.0);
    playerCfg.setDeckY(355.0);
    pokerCard.setConfig(playerCfg);

    aiCfg = pokerCard.newConfig();
    configureLayout(aiCfg, 92.0, 131.0, 12.0, 790.0, 108.0);
    aiCfg.setHoverScale(1.0);
    aiCfg.setHoverLift(0.0);

    boardCfg = pokerCard.newConfig();
    configureLayout(boardCfg, 96.0, 137.0, 12.0, 790.0, 358.0);
    boardCfg.setHoverScale(1.0);
    boardCfg.setHoverLift(0.0);

    pokerCard.newDeck();

    local playerHand = pokerCard.newHand(playerCfg);
    playerHand.setOwner("player");
    playerHand.setInteractive(false);

    local aiHand = pokerCard.newHand(aiCfg);
    aiHand.setOwner("ai");
    aiHand.setFaceDown(true);
    aiHand.setInteractive(false);

    local boardHand = pokerCard.newHand(boardCfg);
    boardHand.setOwner("board");
    boardHand.setInteractive(false);

    foreach (id in defIds) allCardObjects.push(pokerCard.newCard(id));
}

function resetDeck() {
    local deck = pokerCard.getDeck();
    pokerCard.findHand("player").clear();
    pokerCard.findHand("ai").clear();
    pokerCard.findHand("board").clear();
    pokerCard.findHand("ai").setPeek(false);
    deck.clear();
    foreach (c in allCardObjects) {
        c.setFaceUp(true);
        c.setDisabled(false);
        c.setState("deck");
        deck.push(c);
    }
    deck.shuffle();
}

function drawTo(owner) {
    local c = pokerCard.getDeck().draw();
    if (c == null) throw "cardgame: deck exhausted";
    pokerCard.findHand(owner).addCard(c);
    local info = cardInfoById[c.getDefinitionId()];
    if (owner == "player") playerHole.push(info);
    else if (owner == "ai") aiHole.push(info);
    else boardCards.push(info);
    return c;
}

function burnCard() {
    local c = pokerCard.getDeck().draw();
    if (c == null) throw "cardgame: cannot burn from empty deck";
    c.setState("discarded");
    burnCount += 1;
}

function stackFor(owner) {
    return owner == "player" ? playerStack : aiStack;
}

function betFor(owner) {
    return owner == "player" ? playerStreetBet : aiStreetBet;
}

function setStack(owner, value) {
    if (owner == "player") playerStack = value;
    else aiStack = value;
}

function setBet(owner, value) {
    if (owner == "player") playerStreetBet = value;
    else aiStreetBet = value;
}

function commitChips(owner, requested) {
    local paid = pokerMin(requested, stackFor(owner));
    setStack(owner, stackFor(owner) - paid);
    setBet(owner, betFor(owner) + paid);
    pot += paid;
    return paid;
}

function callAmount(owner) {
    return pokerMax(0, currentBet - betFor(owner));
}

function maxEffectiveTarget(owner) {
    local opponent = otherPlayer(owner);
    return pokerMin(betFor(owner) + stackFor(owner), betFor(opponent) + stackFor(opponent));
}

function suggestedRaiseTarget(owner) {
    local increment = pokerMax(bigBlind, (pot / 2).tointeger());
    return pokerMin(currentBet + increment, maxEffectiveTarget(owner));
}

function allInfoFor(owner) {
    local result = [];
    local hole = owner == "player" ? playerHole : aiHole;
    foreach (c in hole) result.push(c);
    foreach (c in boardCards) result.push(c);
    return result;
}

function finishHand(winner, reason) {
    if (winner == "player") playerStack += pot;
    else aiStack += pot;
    resultText = reason + "\n" + ownerName(winner) + "赢得 " + pot + " 筹码";
    pushLog(resultText);
    pot = 0;
    actor = "";
    handState = "hand_over";
}

function showdown() {
    pokerCard.findHand("ai").setPeek(true);
    local playerResult = pokerEvaluate(allInfoFor("player"));
    local aiResult = pokerEvaluate(allInfoFor("ai"));
    local comparison = pokerCompare(playerResult, aiResult);
    local contestedPot = pot;

    if (comparison > 0) {
        playerStack += pot;
        resultText = "你以「" + playerResult.name + "」击败 AI 的「" + aiResult.name + "」\n赢得 " + pot + " 筹码";
    } else if (comparison < 0) {
        aiStack += pot;
        resultText = "AI 以「" + aiResult.name + "」击败你的「" + playerResult.name + "」\nAI 赢得 " + pot + " 筹码";
    } else {
        local half = (pot / 2).tointeger();
        playerStack += half;
        aiStack += half;
        if (pot - half * 2 > 0) {
            // The odd chip goes to the first active seat left of the button.
            if (dealer == "player") aiStack += 1;
            else playerStack += 1;
        }
        resultText = "双方都是「" + playerResult.name + "」，平分底池 " + pot;
    }
    pushLog("摊牌：你 " + playerResult.name + " / AI " + aiResult.name + "。");
    pot = 0;
    actor = "";
    handState = "hand_over";
    if (contestedPot <= 0) throw "cardgame: showdown without a pot";
}

function runOutBoard() {
    if (boardCards.len() == 0) {
        burnCard();
        for (local i = 0; i < 3; i += 1) drawTo("board");
    }
    if (boardCards.len() == 3) {
        burnCard();
        drawTo("board");
    }
    if (boardCards.len() == 4) {
        burnCard();
        drawTo("board");
    }
    street = "river";
    showdown();
}

function beginBettingRound() {
    playerStreetBet = 0;
    aiStreetBet = 0;
    currentBet = 0;
    actionsSinceRaise = 0;
    actor = dealer == "player" ? "ai" : "player";
    if (playerStack == 0 || aiStack == 0) {
        runOutBoard();
        return;
    }
    if (actor == "ai") aiThinkTimer = 0.65;
}

function advanceStreet() {
    if (street == "preflop") {
        street = "flop";
        burnCard();
        for (local i = 0; i < 3; i += 1) drawTo("board");
        pushLog("翻牌发出。");
    } else if (street == "flop") {
        street = "turn";
        burnCard();
        drawTo("board");
        pushLog("转牌发出。");
    } else if (street == "turn") {
        street = "river";
        burnCard();
        drawTo("board");
        pushLog("河牌发出。");
    } else {
        showdown();
        return;
    }
    beginBettingRound();
}

function afterAction(owner) {
    if (handState != "betting") return;
    if (playerStreetBet == aiStreetBet && actionsSinceRaise >= 2) {
        advanceStreet();
        return;
    }
    actor = otherPlayer(owner);
    if (actor == "ai") aiThinkTimer = 0.65;
}

function performFold(owner) {
    if (handState != "betting" || actor != owner) return;
    finishHand(otherPlayer(owner), ownerName(owner) + "弃牌。");
}

function performCallOrCheck(owner) {
    if (handState != "betting" || actor != owner) return;
    local needed = callAmount(owner);
    if (needed > 0) {
        local paid = commitChips(owner, needed);
        pushLog(ownerName(owner) + "跟注 " + paid + "。");
    } else {
        pushLog(ownerName(owner) + "过牌。");
    }
    actionsSinceRaise += 1;
    afterAction(owner);
}

function performRaise(owner, allIn) {
    if (handState != "betting" || actor != owner) return;
    local target = allIn ? maxEffectiveTarget(owner) : suggestedRaiseTarget(owner);
    if (target <= currentBet) {
        performCallOrCheck(owner);
        return;
    }
    commitChips(owner, target - betFor(owner));
    currentBet = betFor(owner);
    actionsSinceRaise = 1;
    pushLog(ownerName(owner) + (allIn ? "全押到 " : "加注到 ") + currentBet + "。");
    afterAction(owner);
}

function aiStrength() {
    if (boardCards.len() == 0) return pokerPreflopStrength(aiHole);
    return pokerMadeHandStrength(allInfoFor("ai"));
}

function performAiAction() {
    if (handState != "betting" || actor != "ai") return;
    local needed = callAmount("ai");
    local strength = aiStrength();
    local noise = ((handNumber * 37 + pot * 7 + boardCards.len() * 19 + aiStack) % 100).tofloat() / 100.0;
    local pressure = needed > 0 ? needed.tofloat() / pokerMax(1, pot + needed).tofloat() : 0.0;
    local canRaise = suggestedRaiseTarget("ai") > currentBet;

    if (needed > 0 && strength + noise * 0.18 < 0.22 + pressure * 0.82) {
        performFold("ai");
    } else if (canRaise && (strength > 0.70 || (strength > 0.48 && noise > 0.76))) {
        performRaise("ai", strength > 0.92);
    } else {
        performCallOrCheck("ai");
    }
}

function startNewHand() {
    if (playerStack < bigBlind || aiStack < bigBlind) {
        playerStack = 1000;
        aiStack = 1000;
        pushLog("一方筹码不足，双方自动补码到 1000。");
    }

    handNumber += 1;
    dealer = handNumber % 2 == 1 ? "player" : "ai";
    street = "preflop";
    handState = "betting";
    resultText = "";
    playerHole.clear();
    aiHole.clear();
    boardCards.clear();
    burnCount = 0;
    pot = 0;
    playerStreetBet = 0;
    aiStreetBet = 0;
    currentBet = bigBlind;
    actionsSinceRaise = 0;
    resetDeck();

    for (local i = 0; i < 2; i += 1) {
        drawTo(dealer);
        drawTo(otherPlayer(dealer));
    }

    commitChips(dealer, smallBlind);
    commitChips(otherPlayer(dealer), bigBlind);
    actor = dealer;
    pushLog("第 " + handNumber + " 手牌：" + ownerName(dealer) + "坐庄，盲注 " + smallBlind + "/" + bigBlind + "。");
    if (actor == "ai") aiThinkTimer = 0.65;
}

function playerActionAllowed() {
    return handState == "betting" && actor == "player";
}

function requestPlayerAction(action) {
    if (!playerActionAllowed()) return;
    if (action == "fold") performFold("player");
    else if (action == "call") performCallOrCheck("player");
    else if (action == "raise") performRaise("player", false);
    else if (action == "allin") performRaise("player", true);
}

function buildPokerPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("单机德州扑克", "root");
    ui.text("", "status");
    ui.separator("status_sep");
    ui.text("", "hand_info");
    ui.text("", "result");
    ui.separator("action_sep");
    ui.button("过牌 / 跟注 [C]", "call");
    ui.sameLine("same_call");
    ui.button("加注 [R]", "raise");
    ui.button("弃牌 [F]", "fold");
    ui.sameLine("same_fold");
    ui.button("全押 [A]", "allin");
    ui.button("开始下一手 [N]", "new_hand");
    ui.separator("log_sep");
    ui.text("牌局记录", "log_title");
    ui.text("", "log");
    ui.textWrapped("标准烧牌、四轮下注与七选五摊牌。", 270.0, "help");
    ui.end();
    ui.mountBuildAs("poker");
    ui.select("poker");
    ui.setHostOverlay(true);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    ui.setHostSize(310.0, 696.0);
    pokerUiVersion = 1;
}

function cardNames(cards) {
    local text = "";
    foreach (c in cards) {
        if (text != "") text += "  ";
        text += pokerCard.getCardDefinitionName(c.id);
    }
    return text;
}

function syncPokerUi() {
    if (pokerUiVersion != 1) return;
    ui.select("poker");
    local role = dealer == "player" ? "庄家 / 小盲" : "大盲";
    local turnText = handState == "hand_over" ? "本手结束" : (actor == "player" ? "轮到你行动" : "AI 思考中…");
    ui.setText("status", "第 " + handNumber + " 手牌 · " + streetName() + "\n" +
        "底池  " + pot + "    盲注  " + smallBlind + "/" + bigBlind + "\n" +
        "你  " + playerStack + "（" + role + "）\nAI  " + aiStack + "\n" + turnText);

    local handText = "你的底牌：" + cardNames(playerHole);
    if (boardCards.len() >= 3) {
        local made = pokerEvaluate(allInfoFor("player"));
        handText += "\n当前牌型：" + made.name;
    }
    handText += "\n本轮下注：你 " + playerStreetBet + " / AI " + aiStreetBet;
    ui.setText("hand_info", handText);
    ui.setText("result", resultText);

    local needed = callAmount("player");
    ui.setText("call", needed > 0 ? "跟注 " + needed + " [C]" : "过牌 [C]");
    local target = suggestedRaiseTarget("player");
    ui.setText("raise", target > currentBet ? "加注到 " + target + " [R]" : "无法加注 [R]");
    local showActions = playerActionAllowed();
    ui.setVisible("call", showActions);
    ui.setVisible("raise", showActions);
    ui.setVisible("fold", showActions);
    ui.setVisible("allin", showActions);
    ui.setVisible("new_hand", handState == "hand_over");

    local logText = "";
    foreach (line in logLines) logText += line + "\n";
    ui.setText("log", logText);
}

function handContains(hand, instanceId) {
    return hand != null && hand.findCard(instanceId) != null;
}

function renderPlayingCardBacks() {
    pokerCard.capturePresentation();
    local aiHand = pokerCard.findHand("ai");
    local aiHidden = aiHand != null && aiHand.isFaceDown() && !aiHand.isPeek();
    if (!aiHidden) return;

    for (local i = 0; i < pokerCard.getPresentationCount(); i += 1) {
        local snap = pokerCard.getPresentation(i);
        local instanceId = snap.getInstanceId();
        local inAi = handContains(aiHand, instanceId);
        if (inAi) {
            local backW = snap.getW() * snap.getScale();
            local backH = snap.getH() * snap.getScale();
            gfx.drawSolidRect(snap.getX() - backW * 0.25, snap.getY() - 3.0,
                backW * 0.5, 6.0, 0.82, 0.62, 0.30, snap.getAlpha());
            gfx.drawSolidRect(snap.getX() - backW * 0.5 + 9.0, snap.getY() - backH * 0.5 + 9.0,
                backW - 18.0, backH - 18.0, 0.12, 0.035, 0.075, snap.getAlpha());
            gfx.drawSolidRect(snap.getX() - backW * 0.5 + 6.0, snap.getY() - backH * 0.5 + 6.0,
                backW - 12.0, backH - 12.0, 0.82, 0.62, 0.30, snap.getAlpha());
            gfx.drawSolidRect(snap.getX() - backW * 0.5, snap.getY() - backH * 0.5,
                backW, backH, 0.34, 0.055, 0.10, snap.getAlpha());
        }
    }
}

function renderPlayingCardFaces() {
    pokerCard.capturePresentation();
    local playerHand = pokerCard.findHand("player");
    local aiHand = pokerCard.findHand("ai");
    local boardHand = pokerCard.findHand("board");
    local aiHidden = aiHand != null && aiHand.isFaceDown() && !aiHand.isPeek();

    for (local i = 0; i < pokerCard.getPresentationCount(); i += 1) {
        local snap = pokerCard.getPresentation(i);
        local instanceId = snap.getInstanceId();
        local inPlayer = handContains(playerHand, instanceId);
        local inAi = handContains(aiHand, instanceId);
        local inBoard = handContains(boardHand, instanceId);
        if ((inPlayer || inAi || inBoard) && snap.isFaceUp() && !(inAi && aiHidden)) {
            local scale = snap.getScale();
            gfx.drawTexturedRectRotated(getCardTexture(snap.getDefinitionId()),
                snap.getX(), snap.getY(), snap.getW() * scale, snap.getH() * scale,
                snap.getAngle(), 1.0, 1.0, 1.0, snap.getAlpha());
        }
    }
}

function renderPokerTable() {
    local dealerY = dealer == "player" ? 535.0 : 168.0;
    gfx.drawSolidRect(920.0, dealerY, 36.0, 36.0, 0.96, 0.78, 0.28, 1.0);
    gfx.drawSolidRect(926.0, dealerY + 6.0, 24.0, 24.0, 0.20, 0.12, 0.05, 1.0);

    local slotW = 96.0;
    local slotH = 137.0;
    local gap = 12.0;
    local totalW = slotW * 5.0 + gap * 4.0;
    local left = 790.0 - totalW * 0.5;
    for (local i = 0; i < 5; i += 1) {
        gfx.drawSolidRect(left + i * (slotW + gap), 358.0 - slotH * 0.5,
            slotW, slotH, 0.08, 0.29, 0.27, 0.72);
    }

    // Solid rectangles at equal depth keep the first submitted pixel, so build
    // the nested table from foreground details toward the outer frame.
    gfx.drawSolidRect(380.0, 253.0, 836.0, 210.0, 0.025, 0.24, 0.16, 0.72);
    gfx.drawSolidRect(365.0, 49.0, 866.0, 622.0, 0.035, 0.34, 0.22, 1.0);
    gfx.drawSolidRect(350.0, 34.0, 896.0, 652.0, 0.025, 0.16, 0.105, 1.0);
    gfx.drawSolidRect(338.0, 22.0, 920.0, 676.0, 0.42, 0.16, 0.075, 1.0);
    gfx.drawSolidRect(326.0, 10.0, 944.0, 700.0, 0.055, 0.025, 0.035, 1.0);
}

eve_init = function() {
    pokerRulesSelfTest();
    gfx.setBackgroundColor(0.018, 0.020, 0.028, 1.0);
    createPokerTable();
    startNewHand();
    if (pokerUiVersion != 1) buildPokerPanel();
    syncPokerUi();
};

eve_reload <- function() {
    playerStack = 1000;
    aiStack = 1000;
    handNumber = 0;
    logLines.clear();
    pokerCard = null;
    playerCfg = null;
    aiCfg = null;
    boardCfg = null;
    artTextures.clear();
    allCardObjects.clear();
    createPokerTable();
    startNewHand();
    pushLog("脚本已热重载，牌桌重置为 1000 / 1000。");
    buildPokerPanel();
    syncPokerUi();
};

eve_update = function(dt) {
    while (true) {
        local click = ui.consumeClick();
        if (click == "") break;
        if (click == "poker/call") requestPlayerAction("call");
        else if (click == "poker/raise") requestPlayerAction("raise");
        else if (click == "poker/fold") requestPlayerAction("fold");
        else if (click == "poker/allin") requestPlayerAction("allin");
        else if (click == "poker/new_hand" && handState == "hand_over") startNewHand();
    }

    if (!ui.wantCaptureKeyboard()) {
        if (keyPressed("c", "C")) requestPlayerAction("call");
        if (keyPressed("r", "R")) requestPlayerAction("raise");
        if (keyPressed("a", "A")) requestPlayerAction("allin");
        if (keyPressed("f", "F")) requestPlayerAction("fold");
        if (keyPressed("n", "N") && handState == "hand_over") startNewHand();
    }

    if (handState == "betting" && actor == "ai") {
        aiThinkTimer -= dt;
        if (aiThinkTimer <= 0.0) performAiAction();
    }

    pokerCard.update(dt, mouse.getX(), mouse.getY(), false);
    pokerCard.clearEvents();
    syncPokerUi();
};

eve_render = function() {
    gfx.clear();
    renderPlayingCardBacks();
    pokerCard.render(gfx);
    pokerCard.renderDeck(gfx);
    renderPokerTable();
    renderPlayingCardFaces();
    ui.beginFrameAndRender();
};
