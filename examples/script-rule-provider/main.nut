// ============================================================================
// 用脚本接管一个原生扩展点（不需要改任何 C++ 文件）
//
// 载具的伤害管线在扣血前会遍历 IVehicleDamageModifier 这个"插座"：
//
//     Vehicle::applyDamage()  →  cap::forEach<IVehicleDamageModifier>
//
// 这个示例把三条游戏规则用脚本挂到那个插座上，并断言它们真的生效：
//
//   1 front-resist  正面减伤 50%（科技 / 护盾）
//   2 rear-crit     背面暴击 ×2（打法规则）
//   3 armor-plating 再减固定 5 点（永不把伤害压到 0）
//
// 关键点：
//   * 接缝由拥有该接口的模块（vehicle）声明，脚本只负责"实现 + 发布"；
//   * 同优先级的规则按发布顺序依次调用，装甲区倍率在全部规则之后应用；
//   * 脚本拿不到 VehicleEntity*，只拿到稳定 id、模板 id 与标量；
//   * 规则抛错时按原值放行（不会静默吞掉这一击），并在 eve.log 里报一次；
//   * 规则可以随时撤销 / 重发（热重载友好），退役的句柄永不复用。
//
// 操作：1 / 2 / 3 开关三条规则   4 开关"故意写错的规则"   R 重置目标
// ============================================================================

const SOCKET = "IVehicleDamageModifier";
const HIT_INTERVAL = 0.7;
const HIT_AMOUNT = 100.0;
const MAX_HP = 1000.0;

persist vehicle = null;
persist target = null;
persist rules = null;        // 名字 -> 是否已发布
persist ruleNames = null;
persist hitTimer = 0.0;
persist respawnTimer = -1.0;
persist lastLine = "";
persist prevKeys = {};
persist hudBuilt = false;
persist selfChecked = false;

// ---------------------------------------------------------------------------
// 三条游戏规则：每个都只是一个普通 Squirrel 类
// ---------------------------------------------------------------------------

class FrontResistRule {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) {
        if (zone == "front") return amount * 0.5;
        return amount;
    }
}

class RearCritRule {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) {
        if (zone == "rear") return amount * 2.0;
        return amount;
    }
}

class ArmorPlatingRule {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) {
        local reduced = amount - 5.0;
        // 规则只能改数值，压到 0 会让这一击彻底消失，所以自己兜住下限。
        if (reduced < amount * 0.5) return amount * 0.5;
        return reduced;
    }
}

// 故意写错的规则：用来演示"规则抛错时会发生什么"。
class BrokenRule {
    function modifyDamage(vehicleId, defId, amount, zone, sourceId) {
        throw "this rule is intentionally broken";
    }
}

// 名字 -> 规则类。注意 `const` 只接受标量初值，表要用普通根表槽位。
RULE_CLASSES <- {
    ["front-resist"] = FrontResistRule,
    ["rear-crit"] = RearCritRule,
    ["armor-plating"] = ArmorPlatingRule,
    ["broken"] = BrokenRule,
};

// ---------------------------------------------------------------------------
// 发布 / 撤销：eve.provider 是唯一入口，全部返回结构化结果表
//   { ok, code, status, diagnostics, value }
// ---------------------------------------------------------------------------

function publishRule(name) {
    local result = eve.provider.publishFor(SOCKET, name, RULE_CLASSES[name]());
    if (!result.ok) {
        print("[script-rule] publish '" + name + "' refused: " + result.code + " - " + result.status.summary + "\n");
        return false;
    }
    rules[name] = true;
    return true;
}

function unpublishRule(name) {
    local result = eve.provider.unload(name);
    if (!result.ok) {
        print("[script-rule] unload '" + name + "' refused: " + result.code + " - " + result.status.summary + "\n");
        return false;
    }
    rules[name] = false;
    return true;
}

// ---------------------------------------------------------------------------
// 目标与伤害：脚本里可观测的唯一事实是 health
// ---------------------------------------------------------------------------

function spawnTarget() {
    target = vehicle.newVehicle("tank.demo", 450.0, 330.0, 0.0, "demo");
    if (target == null) return false;
    vehicle.clearEvents();
    return true;
}

function resetTarget() {
    if (target == null || vehicle.isDestroyed(target)) {
        spawnTarget();
        return;
    }
    vehicle.setHealth(target, MAX_HP);
    vehicle.clearEvents();
}

// 打一枪，返回实际扣掉的血量（= 全部规则 + 装甲区的最终结果）。
function hitOnce(zone, amount) {
    local before = vehicle.getHealth(target);
    vehicle.applyDamage(target, amount, zone, 7);
    vehicle.clearEvents();
    return before - vehicle.getHealth(target);
}

function near(a, b) {
    local d = a - b;
    if (d < 0.0) d = -d;
    return d < 0.01;
}

// ---------------------------------------------------------------------------
// 自检：脚本注册的规则到底有没有生效？把算术打印出来，而不是"看起来对"。
// ---------------------------------------------------------------------------

function selfCheck() {
    local ok = true;
    local lines = [];

    // 0) 这个扩展点确实存在，而且是脚本能发现的
    local found = false;
    foreach (name in eve.provider.capabilities()) {
        if (name == SOCKET) found = true;
    }
    if (!found) {
        lines.push("socket '" + SOCKET + "' is not declared");
        ok = false;
    }

    // 1) 没有任何规则时：100 正面 × 装甲 1.0 = 100
    local unmodified = hitOnce("front", HIT_AMOUNT);
    lines.push("no rule               : front 100 -> " + unmodified);
    if (!near(unmodified, 100.0)) ok = false;

    // 2) 三条规则按发布顺序生效：(100×0.5) - 5 = 45，再乘正面装甲 1.0
    //    背面：(100×2.0) - 5 = 195，再乘背面装甲 1.5 = 292.5
    publishRule("front-resist");
    publishRule("rear-crit");
    publishRule("armor-plating");
    local front = hitOnce("front", HIT_AMOUNT);
    local rear = hitOnce("rear", HIT_AMOUNT);
    lines.push("3 rules               : front 100 -> " + front + "   (x0.5, -5, armor x1.0)");
    lines.push("3 rules               : rear  100 -> " + rear + "  (x2.0, -5, armor x1.5)");
    if (!near(front, 45.0)) ok = false;
    if (!near(rear, 292.5)) ok = false;

    // 3) 撤销固定减伤后：(100×0.5) = 50 —— 撤销真的生效了
    unpublishRule("armor-plating");
    local withoutPlating = hitOnce("front", HIT_AMOUNT);
    lines.push("plating off           : front 100 -> " + withoutPlating);
    if (!near(withoutPlating, 50.0)) ok = false;
    unpublishRule("front-resist");
    unpublishRule("rear-crit");

    // 4) 重复撤销是"结构化失败"，不是静默成功
    local twice = eve.provider.unload("front-resist");
    lines.push("unload twice          : ok=" + twice.ok + " code=" + twice.code);
    if (twice.ok) ok = false;

    // 5) 规则抛错 -> 按原值放行（这一击不会消失），并在 eve.log 里报一次
    publishRule("broken");
    local brokenHit = hitOnce("front", HIT_AMOUNT);
    lines.push("raising rule          : front 100 -> " + brokenHit + "  (incoming amount kept, see eve.log)");
    if (!near(brokenHit, 100.0)) ok = false;
    unpublishRule("broken");

    print("[script-rule] capabilities: " + eve.provider.capabilities().len() + ", providers live: " +
          eve.provider.size() + "\n");
    foreach (line in lines) print("[script-rule]   " + line + "\n");
    if (ok) {
        print("SCRIPT_RULE_PASS script-registered game rules changed the native damage pipeline\n");
    } else {
        print("SCRIPT_RULE_FAIL see the lines above\n");
    }
    return ok;
}

// ---------------------------------------------------------------------------
// 输入
// ---------------------------------------------------------------------------

function ruleKeyPressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function toggleRule(name, label) {
    if (rules[name]) {
        unpublishRule(name);
        lastLine = "off: " + label;
    } else {
        publishRule(name);
        lastLine = "on : " + label;
    }
}

// ---------------------------------------------------------------------------
// 帧循环
// ---------------------------------------------------------------------------

eve_init = function() {
    vehicle = eve.Vehicle();
    vehicle.registerVehiclesFromJson(@"[{""id"":""tank.demo"",""category"":""tank"",""mobility"":""kinematic"",
        ""maxSpeed"":0,""accel"":0,""turnRate"":0,""radius"":22,""maxHealth"":1000,
        ""armorZones"":[{""name"":""front"",""mult"":1.0},{""name"":""rear"",""mult"":1.5}]}]");

    rules = {};
    ruleNames = [];
    foreach (name, cls in RULE_CLASSES) {
        rules[name] <- false;
        ruleNames.push(name);
    }

    // 顺序很重要：接缝由 Vehicle 模块声明，所以必须先拿到模块实例，再发布规则。
    if (!spawnTarget()) {
        print("[script-rule] target spawn failed\n");
        return;
    }

    if (!selfChecked) {
        // 自检会反复开关规则；跑完把演示用的三条规则恢复成"开启"。
        selfCheck();
        selfChecked = true;
    }
    publishRule("front-resist");
    publishRule("rear-crit");
    publishRule("armor-plating");
    resetTarget();

    if (!hudBuilt) {
        ui.setTheme("dark");
        ui.beginBuild();
        ui.beginWindow("脚本扩展点：" + SOCKET, "hud");
        ui.text("规则由脚本发布，C++ 侧零改动", "intro");
        ui.separator("sep1");
        ui.text("", "rules");
        ui.separator("sep2");
        ui.text("", "log");
        ui.text("1/2/3 开关规则   4 开关故意写错的规则   R 重置目标", "help");
        ui.end();
        ui.mountBuildAs("scriptrule");
        hudBuilt = true;
    }
    lastLine = "三条规则已发布，目标每 " + HIT_INTERVAL + " 秒挨一枪";
    hitTimer = HIT_INTERVAL;
    respawnTimer = -1.0;
};

eve_update = function(dt) {
    if (ruleKeyPressed("1")) toggleRule("front-resist", "front-resist（正面减伤 50%）");
    if (ruleKeyPressed("2")) toggleRule("rear-crit", "rear-crit（背面暴击 x2）");
    if (ruleKeyPressed("3")) toggleRule("armor-plating", "armor-plating（固定 -5）");
    if (ruleKeyPressed("4")) toggleRule("broken", "broken（故意抛错，按原值放行）");
    if (ruleKeyPressed("r")) {
        resetTarget();
        lastLine = "目标已重置";
    }

    if (target != null && vehicle.isDestroyed(target)) {
        if (respawnTimer < 0.0) respawnTimer = 1.0;
        respawnTimer -= dt;
        if (respawnTimer <= 0.0) {
            spawnTarget();
            respawnTimer = -1.0;
        }
    }

    hitTimer -= dt;
    if (hitTimer <= 0.0 && target != null && !vehicle.isDestroyed(target)) {
        hitTimer = HIT_INTERVAL;
        // 正面 / 背面轮流打，让"装甲区倍率在规则之后应用"看得出来。
        local zone = ((rand() % 2) == 0) ? "front" : "rear";
        local applied = hitOnce(zone, HIT_AMOUNT);
        lastLine = "shot(" + zone + ") 100 -> 实际扣血 " + applied;
    }

    if (hudBuilt) {
        local ruleText = "";
        for (local i = 0; i < ruleNames.len(); i += 1) {
            local name = ruleNames[i];
            ruleText += (rules[name] ? "[on ] " : "[off] ") + name + "\n";
        }
        ui.setText("rules", ruleText);

        local publishedText = "";
        foreach (entry in eve.provider.published()) {
            publishedText += "provider " + entry.name + "  gen=" + entry.generation + "\n";
        }
        ui.setText("log", lastLine + "\n\n" + publishedText + "socket " + SOCKET);
    }
};

eve_render = function() {
    gfx.setBackgroundColor(0.07, 0.08, 0.11, 1.0);
    gfx.clear();

    if (target != null) {
        local x = vehicle.getX(target);
        local y = vehicle.getY(target);
        local ratio = vehicle.getHealth(target) / vehicle.getMaxHealth(target);
        if (ratio < 0.0) ratio = 0.0;

        // 车体（正面朝右），两侧色条标出正面 / 背面，方便对照规则命中的 zone
        gfx.drawSolidRect(x - 26.0, y - 16.0, 52.0, 32.0, 0.85, 0.75, 0.30, 1.0);
        gfx.drawSolidRect(x + 20.0, y - 4.0, 14.0, 8.0, 0.95, 0.95, 0.95, 1.0);
        gfx.drawSolidRect(x + 26.0, y - 16.0, 3.0, 32.0, 0.35, 0.75, 0.95, 1.0);
        gfx.drawSolidRect(x - 29.0, y - 16.0, 3.0, 32.0, 0.95, 0.35, 0.35, 1.0);
        // 血条
        gfx.drawSolidRect(x - 40.0, y + 30.0, 80.0, 7.0, 0.20, 0.20, 0.22, 1.0);
        gfx.drawSolidRect(x - 40.0, y + 30.0, 80.0 * ratio, 7.0, 0.30, 0.80, 0.35, 1.0);
    }

    ui.beginFrameAndRender();
};
