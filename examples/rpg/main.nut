// ============================================================================
// EVEngine RPG 模块示例游戏 —— 「地牢生存」
//
// 一个最小但完整的动作 RPG 循环，用来演示 eve.RPG() 五套子系统怎么协同工作：
//   属性 attributes  —— HP / MP / 耐力 / 攻击 / 防御，用 clampMin/clampMax
//                        modifier 把血条钳在 [0, max] 之间。
//   效果 effects     —— JSON 注册的效果模板（力量姿态 / 衰弱 / 灼烧 / 治疗药水 /
//                        被动回复），覆盖 instant / duration / period / stack
//                        四种策略。
//   状态 status      —— 运行时 buff/debuff 实例：剩余时间、叠加层数、周期 tick。
//   技能 skills      —— 学习 / 冷却 / 消耗 / 读条时间 / 施放事件轮询。
//   结算 settlement  —— 本例的伤害公式直接写在脚本里（见 computeDamage）：
//                        RPG 模块的 SettlementPipeline::registerStage 是 C++
//                        扩展点（未绑定到脚本），游戏项目通常会在原生插件里注册
//                        自己的伤害/治疗阶段，脚本侧改用 eve.RPG().runSettlement()
//                        触发；这里为了保持示例零原生依赖，改成纯脚本公式，
//                        详见 docs/RPG系统设计.md。
//
// 玩法：键盘 1/2/3/4 释放技能，撑过一波波强度递增的怪物，R 重开。
// 运行： make run/macosx-debug GAME=examples/rpg   （或对应平台的 run/<platform>-debug）
// ============================================================================

// ---- 软热重载状态保护：脚本被 dofile 重跑时不清空已创建的实体/UI ----
// （约定见 example/main.nut）
if (!("rpg" in getroottable())) rpg <- null;
if (!("player" in getroottable())) player <- null;
if (!("enemy" in getroottable())) enemy <- null;
if (!("wave" in getroottable())) wave <- 1;
if (!("score" in getroottable())) score <- 0;
if (!("gameOver" in getroottable())) gameOver <- false;
if (!("battleLog" in getroottable())) battleLog <- [];
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("hitFlash" in getroottable())) hitFlash <- { player = 0.0, enemy = 0.0 };

const PLAYER_MAX_HP = 100.0;
const PLAYER_MAX_MP = 50.0;
const PLAYER_MAX_STAMINA = 100.0;

// 效果 id -> 中文展示名，只用于状态栏文字。
effectNames <- {
    ["buff.power_stance"] = "力量姿态",
    ["debuff.weaken"] = "衰弱",
    ["dot.burn"] = "灼烧"
};

// 施法失败原因 -> 中文提示（对应 SkillSystem::canCast 的 reason 字符串）。
reasonText <- {
    no_actor = "目标无效",
    unknown_skill = "未知技能",
    not_learned = "尚未学会",
    already_casting = "正在读条",
    on_cooldown = "冷却中",
    insufficient_cost = "资源不足"
};

function randf(a, b) {
    return a + (b - a) * (rand().tofloat() / RAND_MAX.tofloat());
}

function roundi(v) {
    return floor(v + 0.5).tointeger();
}

function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function logLine(text) {
    battleLog.push(text);
    while (battleLog.len() > 6)
        battleLog.remove(0);
}

// ---------------------------------------------------------------------------
// 数据驱动内容：效果 / 技能定义。注册接口是幂等的（按 id 覆盖），软重载时
// 重复调用也没问题，方便边跑边改数值。
// ---------------------------------------------------------------------------
function registerContent() {
    rpg.clearEffectDefinitions();
    local effectsLoaded = rpg.registerEffectsFromJson(@"[
        {""id"":""buff.power_stance"",""durationPolicy"":""duration"",""duration"":6.0,
         ""stackPolicy"":""refresh"",
         ""modifiers"":[{""attribute"":""attack"",""op"":""add"",""value"":8.0}],
         ""tags"":[""buff""],
         ""extra"":{""icon"":""ui/power_stance.png""}},
        {""id"":""debuff.weaken"",""durationPolicy"":""duration"",""duration"":4.0,
         ""stackPolicy"":""refresh"",
         ""modifiers"":[{""attribute"":""defense"",""op"":""add"",""value"":-4.0}],
         ""tags"":[""debuff""],
         ""extra"":{""icon"":""ui/weaken.png""}},
        {""id"":""dot.burn"",""durationPolicy"":""duration"",""duration"":3.0,""period"":1.0,
         ""stackPolicy"":""stack"",""maxStacks"":3,""tags"":[""dot"",""debuff""],
         ""extra"":{""icon"":""ui/burn.png""}},
        {""id"":""instant.heal_potion"",""durationPolicy"":""instant"",
         ""modifiers"":[{""attribute"":""health"",""op"":""add"",""value"":25.0}]},
        {""id"":""passive.regen"",""durationPolicy"":""duration"",""duration"":999999.0,
         ""period"":2.0,""tags"":[""regen""]}
    ]");

    rpg.clearSkillDefinitions();
    local skillsLoaded = rpg.registerSkillsFromJson(@"[
        {""id"":""player.strike"",""cooldown"":0.4,""castTime"":0.0,
         ""costs"":[],""grantedEffects"":[],""tags"":[""attack""]},
        {""id"":""player.fireball"",""cooldown"":3.0,""castTime"":0.6,
         ""costs"":[{""attribute"":""mana"",""amount"":15.0}],
         ""grantedEffects"":[""dot.burn""],""tags"":[""attack"",""magic""]},
        {""id"":""player.power_stance"",""cooldown"":8.0,""castTime"":0.0,
         ""costs"":[{""attribute"":""stamina"",""amount"":10.0}],
         ""grantedEffects"":[""buff.power_stance""],""tags"":[""buff""]},
        {""id"":""player.potion"",""cooldown"":6.0,""castTime"":0.0,
         ""costs"":[],""grantedEffects"":[""instant.heal_potion""],""tags"":[""item""]},
        {""id"":""enemy.claw"",""cooldown"":1.2,""castTime"":0.0,
         ""costs"":[],""grantedEffects"":[],""tags"":[""attack""]},
        {""id"":""enemy.roar"",""cooldown"":6.0,""castTime"":0.5,
         ""costs"":[],""grantedEffects"":[""debuff.weaken""],""tags"":[""debuff""]}
    ]");

    print(format("RPG content loaded: %d effects, %d skills\n", effectsLoaded, skillsLoaded));
}

// ---------------------------------------------------------------------------
// Actor 构建：把 health/mana/stamina 的 base 值钳在 [0, max]。
// clampMin/clampMax 是永久 modifier（source="system"，高优先级），装一次
// 之后无论谁往 base 上加减，getFinalAttribute 读到的都是钳制后的值。
// ---------------------------------------------------------------------------
function setupVitals(actor, maxHp, maxMp, maxStamina) {
    actor.setBaseAttribute("health", maxHp);
    actor.setBaseAttribute("mana", maxMp);
    actor.setBaseAttribute("stamina", maxStamina);

    actor.addAttributeModifier("health", "system", "clampMin", 0.0, 900);
    actor.addAttributeModifier("health", "system", "clampMax", maxHp, 901);
    actor.addAttributeModifier("mana", "system", "clampMin", 0.0, 900);
    actor.addAttributeModifier("mana", "system", "clampMax", maxMp, 901);
    actor.addAttributeModifier("stamina", "system", "clampMin", 0.0, 900);
    actor.addAttributeModifier("stamina", "system", "clampMax", maxStamina, 901);
}

function makePlayer() {
    local p = rpg.newActor();
    setupVitals(p, PLAYER_MAX_HP, PLAYER_MAX_MP, PLAYER_MAX_STAMINA);
    p.setBaseAttribute("attack", 14.0);
    p.setBaseAttribute("defense", 4.0);
    p.learnSkill("player.strike");
    p.learnSkill("player.fireball");
    p.learnSkill("player.power_stance");
    p.learnSkill("player.potion");
    // 被动回复：duration 很长 + period=2s 的无限期 buff，纯靠 tick 事件在脚本里回血回蓝。
    p.applyEffect("passive.regen", "system");
    return p;
}

// 波次强度曲线：难度随波数线性增长，够撑十几波就会感到吃力。
function makeEnemy(w) {
    local e = rpg.newActor();
    local maxHp = 40.0 + w * 14.0;
    setupVitals(e, maxHp, 0.0, 0.0);
    e.setBaseAttribute("attack", 7.0 + w * 1.6);
    e.setBaseAttribute("defense", 1.0 + w * 0.8);
    e.learnSkill("enemy.claw");
    e.learnSkill("enemy.roar");
    return e;
}

function enemyMaxHp(w) { return 40.0 + w * 14.0; }

// ---------------------------------------------------------------------------
// 伤害结算（脚本侧公式，见文件头注释）。
// ---------------------------------------------------------------------------
function computeDamage(atk, def, critChance, critMult) {
    local raw = atk - def * 0.5;
    if (raw < 1.0) raw = 1.0;
    local crit = randf(0.0, 1.0) < critChance;
    if (crit) raw *= critMult;
    return { dmg = roundi(raw), crit = crit };
}

function applyDamage(target, isPlayerTarget, dmg) {
    target.modifyBaseAttribute("health", -dmg * 1.0);
    if (isPlayerTarget) hitFlash.player = 0.15; else hitFlash.enemy = 0.15;
}

// ---------------------------------------------------------------------------
// 玩家输入：边沿检测（仅在“刚按下”那一帧触发），避免长按重复施法。
// ---------------------------------------------------------------------------
function keyPressed(key) {
    local down = keyboard.isDown(key);
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function tryPlayerSkill(skillId, target, label) {
    if (player.canCastSkill(skillId)) {
        player.beginCastSkill(skillId, target);
    } else {
        local reason = player.canCastSkillReason(skillId);
        local text = (reason in reasonText) ? reasonText[reason] : reason;
        logLine(label + " 无法施放：" + text);
    }
}

// ---------------------------------------------------------------------------
// 敌人 AI：简单的冷却+概率决策，学到的两个技能都靠 SkillSystem 管理冷却/读条。
// ---------------------------------------------------------------------------
function updateEnemyAI() {
    if (enemy.isCastingSkill()) return;
    if (enemy.canCastSkill("enemy.roar") && randf(0.0, 1.0) < 0.35) {
        enemy.beginCastSkill("enemy.roar", player);
        return;
    }
    if (enemy.canCastSkill("enemy.claw")) {
        enemy.beginCastSkill("enemy.claw", player);
    }
}

// ---------------------------------------------------------------------------
// 每帧轮询 RPG 模块产生的事件：技能命中结算 + 周期性 tick（灼烧/回复）。
// ---------------------------------------------------------------------------
function processEvents() {
    local n = rpg.getTickEventCount();
    for (local i = 0; i < n; i += 1) {
        local actor = rpg.getTickEventActor(i);
        local effectId = rpg.getTickEventEffectId(i);
        local stacks = rpg.getTickEventStacks(i);
        local isPlayer = (actor == player);

        if (effectId == "dot.burn") {
            local dmg = 4 * stacks;
            applyDamage(actor, isPlayer, dmg);
            logLine((isPlayer ? "玩家" : "敌人") + " 受到灼烧伤害 " + dmg + (stacks > 1 ? " (x" + stacks + ")" : ""));
        } else if (effectId == "passive.regen") {
            actor.modifyBaseAttribute("health", 3.0);
            actor.modifyBaseAttribute("mana", 2.0);
            actor.modifyBaseAttribute("stamina", 6.0);
        }
    }

    local m = rpg.getCastEventCount();
    for (local i = 0; i < m; i += 1) {
        local caster = rpg.getCastEventCaster(i);
        local target = rpg.getCastEventTarget(i);
        local skillId = rpg.getCastEventSkillId(i);
        local isPlayerCaster = (caster == player);

        if (skillId == "player.strike" || skillId == "enemy.claw") {
            local r = computeDamage(caster.getFinalAttribute("attack"), target.getFinalAttribute("defense"), 0.15, 1.6);
            applyDamage(target, !isPlayerCaster, r.dmg);
            logLine((isPlayerCaster ? "玩家" : "敌人") + " 普通攻击造成 " + r.dmg + " 伤害" + (r.crit ? "（暴击！）" : ""));
        } else if (skillId == "player.fireball") {
            local r = computeDamage(caster.getFinalAttribute("attack") + 10.0, target.getFinalAttribute("defense"), 0.2, 1.6);
            applyDamage(target, !isPlayerCaster, r.dmg);
            logLine("玩家 火球术造成 " + r.dmg + " 伤害" + (r.crit ? "（暴击！）" : "") + "，并点燃目标");
        } else if (skillId == "player.power_stance") {
            logLine("玩家 进入力量姿态，攻击力提升");
        } else if (skillId == "player.potion") {
            logLine("玩家 使用治疗药水，恢复生命");
        } else if (skillId == "enemy.roar") {
            logLine("敌人 发出怒吼，玩家防御被削弱");
        }
    }
}

// ---------------------------------------------------------------------------
// 胜负判定 / 波次推进
// ---------------------------------------------------------------------------
function checkOutcomes() {
    if (enemy != null && enemy.getFinalAttribute("health") <= 0.0) {
        score += 1;
        logLine("敌人被击败！进入第 " + (wave + 1) + " 波");
        enemy.release();
        wave += 1;
        enemy = makeEnemy(wave);
    }
    if (player != null && player.getFinalAttribute("health") <= 0.0 && !gameOver) {
        gameOver = true;
        logLine("玩家倒下了……");
        showGameOver(true);
    }
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
function buildUI() {
    local pad = 14.0;

    ui.beginBuild();
    ui.beginWindow("Player", "root");
    ui.text("玩家", "title");
    ui.progress(1.0, "hpbar", "");
    ui.text("HP 0/0", "hptext");
    ui.progress(1.0, "mpbar", "");
    ui.text("MP 0/0", "mptext");
    ui.progress(1.0, "stbar", "");
    ui.text("耐力 0/0", "sttext");
    ui.text("", "pstatus");
    ui.separator("sep1");
    ui.text("[1] 普通攻击", "s1");
    ui.text("[2] 火球术 (MP15)", "s2");
    ui.text("[3] 力量姿态 (耐力10)", "s3");
    ui.text("[4] 治疗药水", "s4");
    ui.end();
    ui.mountBuildAs("player");
    ui.select("player");
    ui.setHostOverlay(true);
    ui.setHostPos(pad, pad, 0.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Enemy", "root");
    ui.text("敌人 - 第 1 波", "title");
    ui.progress(1.0, "ehpbar", "");
    ui.text("HP 0/0", "ehptext");
    ui.text("", "estatus");
    ui.end();
    ui.mountBuildAs("enemy");
    ui.select("enemy");
    ui.setHostOverlay(true);
    ui.setHostPos(config.width - pad, pad, 1.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Log", "root");
    ui.text("战斗记录：", "logtitle");
    ui.text("", "log");
    ui.end();
    ui.mountBuildAs("log");
    ui.select("log");
    ui.setHostOverlay(true);
    ui.setHostPos(pad, config.height - pad, 0.0, 1.0);

    ui.beginBuild();
    ui.beginWindow("Game Over", "root");
    ui.text("玩家倒下", "msg");
    ui.text("", "final");
    ui.button("重新开始 (R)", "restart");
    ui.end();
    ui.mountBuildAs("gameover");
    ui.select("gameover");
    ui.setHostVisible(false);
    ui.setHostPos(config.width * 0.5, config.height * 0.45, 0.5, 0.5);
}

function statusLines(actor) {
    local text = "";
    local n = actor.getStatusCount();
    for (local i = 0; i < n; i += 1) {
        local effectId = actor.getStatusEffectId(i);
        local name = (effectId in effectNames) ? effectNames[effectId] : effectId;
        local stacks = actor.getStatusStacks(i);
        local remaining = actor.getStatusRemaining(i);
        local line = name + (stacks > 1 ? " x" + stacks : "") + " " + format("%.1f", remaining) + "s";
        text = (text == "") ? line : (text + "\n" + line);
    }
    return text;
}

function refreshHud() {
    ui.select("player");
    local hp = player.getFinalAttribute("health");
    local mp = player.getFinalAttribute("mana");
    local st = player.getFinalAttribute("stamina");
    ui.setValue("hpbar", (hp / PLAYER_MAX_HP).tofloat());
    ui.setText("hptext", "HP " + roundi(hp) + "/" + roundi(PLAYER_MAX_HP));
    ui.setValue("mpbar", (mp / PLAYER_MAX_MP).tofloat());
    ui.setText("mptext", "MP " + roundi(mp) + "/" + roundi(PLAYER_MAX_MP));
    ui.setValue("stbar", (st / PLAYER_MAX_STAMINA).tofloat());
    ui.setText("sttext", "耐力 " + roundi(st) + "/" + roundi(PLAYER_MAX_STAMINA));
    ui.setText("pstatus", statusLines(player));

    local hint1 = player.isCastingSkill() ? format("读条中 %.0f%%", player.getCastProgress() * 100.0) : "";
    ui.setText("s1", "[1] 普通攻击" + (player.getSkillCooldown("player.strike") > 0.0 ? format(" (CD %.1fs)", player.getSkillCooldown("player.strike")) : ""));
    ui.setText("s2", "[2] 火球术 (MP15)" + (player.getSkillCooldown("player.fireball") > 0.0 ? format(" (CD %.1fs)", player.getSkillCooldown("player.fireball")) : "") + (hint1 != "" ? "  " + hint1 : ""));
    ui.setText("s3", "[3] 力量姿态 (耐力10)" + (player.getSkillCooldown("player.power_stance") > 0.0 ? format(" (CD %.1fs)", player.getSkillCooldown("player.power_stance")) : ""));
    ui.setText("s4", "[4] 治疗药水" + (player.getSkillCooldown("player.potion") > 0.0 ? format(" (CD %.1fs)", player.getSkillCooldown("player.potion")) : ""));

    ui.select("enemy");
    if (enemy != null) {
        local maxHp = enemyMaxHp(wave);
        local ehp = enemy.getFinalAttribute("health");
        ui.setText("title", "敌人 - 第 " + wave + " 波");
        ui.setValue("ehpbar", (ehp / maxHp).tofloat());
        ui.setText("ehptext", "HP " + roundi(ehp) + "/" + roundi(maxHp));
        ui.setText("estatus", statusLines(enemy));
    }

    ui.select("log");
    local logText = "得分 " + score + "   第 " + wave + " 波\n";
    foreach (line in battleLog)
        logText += line + "\n";
    ui.setText("log", logText);
}

function showGameOver(show) {
    ui.select("gameover");
    ui.setHostVisible(show);
    if (show) {
        ui.setHostModal(true);
        ui.setText("final", "存活 " + wave + " 波，击败 " + score + " 个敌人");
    } else {
        ui.setHostModal(false);
    }
}

// ---------------------------------------------------------------------------
// 游戏生命周期
// ---------------------------------------------------------------------------
function startNewGame() {
    if (player != null) player.release();
    if (enemy != null) enemy.release();

    wave = 1;
    score = 0;
    gameOver = false;
    battleLog = [];
    hitFlash.player = 0.0;
    hitFlash.enemy = 0.0;

    player = makePlayer();
    enemy = makeEnemy(wave);
    showGameOver(false);
    logLine("第 1 波开始，按 1/2/3/4 施放技能");
}

eve_init = function() {
    gfx.setBackgroundColor(0.06, 0.05, 0.09, 1.0);
    if (rpg == null) rpg = eve.RPG();
    registerContent();
    buildUI();
    if (player == null)
        startNewGame();
    else
        refreshHud();
};

// 软重载后调用（仅重跑脚本时触发，不重建实体/UI）。
eve_reload <- function() {
    registerContent();
};

eve_update = function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "gameover/restart") startNewGame();
        id = ui.consumeClick();
    }

    if (hitFlash.player > 0.0) hitFlash.player -= dt;
    if (hitFlash.enemy > 0.0) hitFlash.enemy -= dt;

    if (!gameOver) {
        if (keyPressed("1")) tryPlayerSkill("player.strike", enemy, "普通攻击");
        if (keyPressed("2")) tryPlayerSkill("player.fireball", enemy, "火球术");
        if (keyPressed("3")) tryPlayerSkill("player.power_stance", null, "力量姿态");
        if (keyPressed("4")) tryPlayerSkill("player.potion", null, "治疗药水");

        updateEnemyAI();

        rpg.update(dt);
        processEvents();
        checkOutcomes();
    } else if (keyPressed("R")) {
        startNewGame();
    }

    refreshHud();
};

eve_render = function() {
    gfx.clear();

    // 地面
    gfx.drawSolidRect(0.0, config.height - 60.0, config.width * 1.0, 60.0, 0.12, 0.1, 0.14, 1.0);

    // 玩家（左侧，蓝色系）
    local px = config.width * 0.28;
    local py = config.height - 160.0;
    local pf = hitFlash.player > 0.0 ? 0.6 : 0.0;
    gfx.drawSolidRect(px - 30.0, py, 60.0, 90.0, 0.25 + pf, 0.45 + pf, 0.8 + pf, 1.0);
    gfx.drawSolidRect(px - 16.0, py - 34.0, 32.0, 32.0, 0.35 + pf, 0.55 + pf, 0.85 + pf, 1.0);
    if (player != null && player.isCastingSkill()) {
        local prog = player.getCastProgress();
        gfx.drawSolidRect(px - 30.0, py - 46.0, 60.0, 6.0, 0.2, 0.2, 0.25, 1.0);
        gfx.drawSolidRect(px - 30.0, py - 46.0, 60.0 * prog, 6.0, 0.95, 0.75, 0.25, 1.0);
    }

    // 敌人（右侧，红色系，随波数略微变大）
    local ex = config.width * 0.72;
    local ey = config.height - 170.0;
    local ef = hitFlash.enemy > 0.0 ? 0.6 : 0.0;
    local esize = 70.0 + (wave.tofloat() * 3.0);
    if (esize > 130.0) esize = 130.0;
    gfx.drawSolidRect(ex - esize * 0.5, ey, esize, esize, 0.75 + ef, 0.25 + ef, 0.2 + ef, 1.0);
    gfx.drawSolidRect(ex - esize * 0.22, ey + esize * 0.25, esize * 0.14, esize * 0.14, 1.0, 1.0, 0.6, 1.0);
    gfx.drawSolidRect(ex + esize * 0.08, ey + esize * 0.25, esize * 0.14, esize * 0.14, 1.0, 1.0, 0.6, 1.0);
    if (enemy != null && enemy.isCastingSkill()) {
        gfx.drawSolidRect(ex - esize * 0.5, ey - 12.0, esize, 6.0, 0.2, 0.2, 0.25, 1.0);
        gfx.drawSolidRect(ex - esize * 0.5, ey - 12.0, esize * enemy.getCastProgress(), 6.0, 0.9, 0.35, 0.35, 1.0);
    }

    ui.beginFrameAndRender();
};

eve_quit = function() {
};
