// ============================================================================
// EVEngine 通用载具系统框架演示
//
// 这个示例的目标是"走框架"，每块都在调用 vehicle/weapon/physics 模块的
// 真实功能，而不是在脚本里重写一遍：
//
//   RTS 命令队列  —— 左键 moveTo / 右键 attack / S=stop / H=hold / T=巡逻(attackMove)
//   RTS 自动瞄准  —— attack 命令让坦克炮塔自动转向目标（autoAim）
//   武器管线      —— 冷却/弹药/装填事件；hitscan(MG) 与 projectile(火炮) 两种逻辑
//   伤害管线      —— 开火事件 → applyDamage → 装甲区（front/side/rear）倍率 → 事件
//   物理移动      —— 两辆车 attachPhysics2D（wheel 移动模型 + Box2D 墙体碰撞）
//   座位系统      —— 驾驶座/炮手座分属两个玩家控制源；E 键进出驾驶座
//   ECS 视图      —— eve.view(eve.VehicleEntity) 遍历所有载具统一渲染
//
// 操作：左键点地=坦克移动；右键点敌车=攻击；W/A/S/D=开蓝车；Space=机枪；
//       E=上下车；S=坦克停；H=坦克原地待命；T=坦克开始/停止巡逻。
// ============================================================================

// ---- 软热重载保护 ----
if (!("vehicle" in getroottable())) vehicle <- null;
if (!("weapon" in getroottable())) weapon <- null;
if (!("physics" in getroottable())) physics <- null;
if (!("world" in getroottable())) world <- null;
if (!("rtsTank" in getroottable())) rtsTank <- null;
if (!("fpsCar" in getroottable())) fpsCar <- null;
if (!("enemyCar" in getroottable())) enemyCar <- null;
if (!("waypoint" in getroottable())) waypoint <- 0;
if (!("patrol" in getroottable())) patrol <- false;
if (!("enemySteer" in getroottable())) enemySteer <- 0.0;
if (!("enemySteerTimer" in getroottable())) enemySteerTimer <- 0.0;
if (!("carRespawn" in getroottable())) carRespawn <- -1.0;
if (!("enemyRespawn" in getroottable())) enemyRespawn <- -1.0;
if (!("eventLog" in getroottable())) eventLog <- [];
if (!("tracers" in getroottable())) tracers <- [];
if (!("prevKeys" in getroottable())) prevKeys <- {};
if (!("prevMouse" in getroottable())) prevMouse <- { left = false, right = false };

const PLAYER_DRIVER = 1;
const PLAYER_GUNNER = 2;
const PI = 3.14159265;

waypoints <- [
    { x = 140.0, y = 140.0 },
    { x = 760.0, y = 140.0 },
    { x = 760.0, y = 500.0 },
    { x = 140.0, y = 500.0 }
];

weaponDefs <- @"[
  {""id"":""cannon.120"",""logic"":""projectile"",""damage"":60,""penetration"":260,
   ""range"":620,""spread"":1.0,""cooldown"":3.0,
   ""ammo"":{""mag"":1,""reserve"":40,""reload"":5.0},
   ""projectile"":{""type"":""shell"",""speed"":900,""gravity"":0.0,""aoe"":30.0}},
  {""id"":""cannon.60"",""logic"":""projectile"",""damage"":25,""penetration"":180,
   ""range"":520,""spread"":1.5,""cooldown"":2.5,
   ""ammo"":{""mag"":1,""reserve"":60,""reload"":4.0},
   ""projectile"":{""type"":""shell"",""speed"":800,""gravity"":0.0,""aoe"":22.0}},
  {""id"":""mg.7.62"",""logic"":""hitscan"",""damage"":6,""range"":420,""spread"":2.0,
   ""fireMode"":""auto"",""cooldown"":0.12,
   ""ammo"":{""mag"":100,""reserve"":400,""reload"":2.5}}]";

vehicleDefs <- @"[
  {""id"":""tank.rts"",""category"":""tank"",""mobility"":""kinematic"",
   ""maxSpeed"":90,""accel"":60,""turnRate"":80,""radius"":22,""maxHealth"":800,
   ""armorZones"":[{""name"":""front"",""mult"":1.0},{""name"":""side"",""mult"":0.6},{""name"":""rear"",""mult"":0.4}],
   ""mounts"":[{""name"":""turret"",""weapon"":""cannon.120"",""type"":""turret"",
                ""limits"":[-180,180,-8,20],""rotSpeed"":60,""aimMode"":""auto""}]},
  {""id"":""car.fps"",""category"":""car"",""mobility"":""wheel"",
   ""maxSpeed"":160,""accel"":120,""turnRate"":150,""radius"":18,""maxHealth"":600,
   ""armorZones"":[{""name"":""front"",""mult"":1.0},{""name"":""side"",""mult"":0.6},{""name"":""rear"",""mult"":0.4}],
   ""mounts"":[{""name"":""mg"",""weapon"":""mg.7.62"",""type"":""turret"",
                ""limits"":[-120,120,-10,20],""rotSpeed"":120,""aimMode"":""manual""}],
   ""seats"":[{""name"":""driver"",""cameraMode"":""third""},
              {""name"":""gunner"",""cameraMode"":""third"",""mountIndex"":0}]},
  {""id"":""car.enemy"",""category"":""car"",""mobility"":""wheel"",
   ""maxSpeed"":120,""accel"":90,""turnRate"":130,""radius"":18,""maxHealth"":500,
   ""armorZones"":[{""name"":""front"",""mult"":1.0},{""name"":""side"",""mult"":0.6},{""name"":""rear"",""mult"":0.4}],
   ""mounts"":[{""name"":""turret"",""weapon"":""cannon.60"",""type"":""turret"",
                ""limits"":[-180,180,-8,20],""rotSpeed"":50,""aimMode"":""manual""}]}]";

function logLine(text) {
    eventLog.push(text);
    while (eventLog.len() > 6) eventLog.remove(0);
}

function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function randf(a, b) {
    return a + (b - a) * (rand().tofloat() / 2147483647.0);
}

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local key = "k_" + name;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function mousePressed(button) {
    local down = mouse.isDown(button);
    local was = false;
    if (button == 0) was = prevMouse.left;
    if (button == 2) was = prevMouse.right;
    if (button == 0) prevMouse.left = down;
    if (button == 2) prevMouse.right = down;
    return down && !was;
}

function spawnPlayerCar() {
    fpsCar = vehicle.newVehicle("car.fps", 450.0, 320.0, 0.0, "blue");
    vehicle.enterSeat(fpsCar, 0, PLAYER_DRIVER);
    vehicle.enterSeat(fpsCar, 1, PLAYER_GUNNER);
    vehicle.attachPhysics2D(fpsCar, world);
}

function spawnEnemyCar() {
    enemyCar = vehicle.newVehicle("car.enemy", 760.0, 140.0, 180.0, "red");
    vehicle.attachPhysics2D(enemyCar, world);
}

eve_init = function() {
    if (weapon == null) weapon = eve.Weapon();
    if (vehicle == null) vehicle = eve.Vehicle();
    if (physics == null) physics = eve.Physics();

    weapon.registerWeaponsFromJson(weaponDefs);
    vehicle.registerVehiclesFromJson(vehicleDefs);

    // 2D 物理世界 + 墙体 + 障碍物（演示 attachPhysics2D 与碰撞）
    if (world == null) {
        world = physics.newWorld(0.0, 0.0, true);
        local w = config.width.tofloat();
        local h = config.height.tofloat();
        local t = 24.0;
        local wall = world.newBody("static", w * 0.5, -t * 0.5);
        wall.newRectangleFixture(w + t, t, 1.0, 0.6, 0.0);
        wall = world.newBody("static", w * 0.5, h + t * 0.5);
        wall.newRectangleFixture(w + t, t, 1.0, 0.6, 0.0);
        wall = world.newBody("static", -t * 0.5, h * 0.5);
        wall.newRectangleFixture(t, h + t, 1.0, 0.6, 0.0);
        wall = world.newBody("static", w + t * 0.5, h * 0.5);
        wall.newRectangleFixture(t, h + t, 1.0, 0.6, 0.0);
        local rock = world.newBody("static", 300.0, 260.0);
        rock.newRectangleFixture(70.0, 70.0, 1.0, 0.8, 0.0);
        rock = world.newBody("static", 620.0, 420.0);
        rock.newRectangleFixture(80.0, 60.0, 1.0, 0.8, 0.0);
    }

    if (rtsTank == null) {
        rtsTank = vehicle.newVehicle("tank.rts", 140.0, 500.0, 0.0, "yellow");
        vehicle.moveTo(rtsTank, 300.0, 420.0);
    }
    if (fpsCar == null) spawnPlayerCar();
    if (enemyCar == null) spawnEnemyCar();

    logLine("左键=坦克移动 右键=攻击  W/A/S/D=开车  Space=机枪  E=上下车");
    logLine("S=坦克停  H=待命  T=巡逻  （蓝车带白色角标=玩家）");
};

// 命中装甲区：按攻击方向与目标朝向的相对角判断 front/side/rear
function hitZone(attacker, target) {
    local dx = vehicle.getX(target) - vehicle.getX(attacker);
    local dy = vehicle.getY(target) - vehicle.getY(attacker);
    local worldAng = atan2(dy, dx) * 180.0 / PI;
    local rel = worldAng - vehicle.getHeading(target);
    while (rel > 180.0) rel -= 360.0;
    while (rel < -180.0) rel += 360.0;
    if (rel > -60.0 && rel < 60.0) return "front";
    if (rel > 60.0 && rel < 120.0 || rel < -60.0 && rel > -120.0) return "side";
    return "rear";
}

function aimAt(mount, fromX, fromY, toX, toY, baseHeading) {
    local worldAng = atan2(toY - fromY, toX - fromX) * 180.0 / PI;
    local localAng = worldAng - baseHeading;
    while (localAng > 180.0) localAng -= 360.0;
    while (localAng < -180.0) localAng += 360.0;
    weapon.mountAimAt(mount, localAng, 0.0);
    return localAng;
}

eve_update = function(dt) {
    // ============ RTS 指挥（命令队列 + 自动瞄准） ============
    if (mousePressed(0)) {
        vehicle.moveTo(rtsTank, mouse.getX().tofloat(), mouse.getY().tofloat());
        logLine("命令：移动到 (" + mouse.getX() + ", " + mouse.getY() + ")");
    }
    if (mousePressed(2)) {
        vehicle.attack(rtsTank, vehicle.getX(enemyCar), vehicle.getY(enemyCar), 0);
        logLine("命令：攻击敌车");
    }
    if (keyPressed("s") || keyPressed("S")) { vehicle.stop(rtsTank); logLine("命令：停止"); }
    if (keyPressed("h") || keyPressed("H")) { vehicle.hold(rtsTank); logLine("命令：待命"); }
    if (keyPressed("t") || keyPressed("T")) {
        patrol = !patrol;
        logLine(patrol ? "巡逻开启（attackMove）" : "巡逻关闭");
    }

    // ============ 玩家车输入（驾驶座 + 炮手座） ============
    local throttle = 0.0;
    local steer = 0.0;
    if (keyboard.isDown("w") || keyboard.isDown("W")) throttle = 1.0;
    if (keyboard.isDown("s") || keyboard.isDown("S")) throttle = -0.6;
    if (keyboard.isDown("a") || keyboard.isDown("A")) steer = -1.0;
    if (keyboard.isDown("d") || keyboard.isDown("D")) steer = 1.0;
    local fire = keyboard.isDown("Space");
    if (keyPressed("e") || keyPressed("E")) {
        if (vehicle.isSeatOccupied(fpsCar, 0)) {
            vehicle.exitSeat(fpsCar, 0);
            logLine("离开驾驶座（车停了）");
        } else {
            vehicle.enterSeat(fpsCar, 0, PLAYER_DRIVER);
            logLine("进入驾驶座");
        }
    }

    // 炮手座自动跟踪敌车（独立控制源）
    local aimYaw = aimAt(vehicle.getSeatMount(fpsCar, 1), vehicle.getX(fpsCar),
                         vehicle.getY(fpsCar), vehicle.getX(enemyCar),
                         vehicle.getY(enemyCar), vehicle.getHeading(fpsCar));
    vehicle.setPlayerControls(PLAYER_DRIVER, throttle, steer, 0.0, fire, aimYaw, 0.0);
    vehicle.setPlayerControls(PLAYER_GUNNER, 0.0, 0.0, 0.0, true, aimYaw, 0.0);

    // ============ 敌车 AI（物理驱动 + 直连 weapon 模块瞄准/开火） ============
    if (!vehicle.isDestroyed(enemyCar)) {
        enemySteerTimer -= dt;
        if (enemySteerTimer <= 0.0) {
            enemySteer = randf(-1.0, 1.0);
            enemySteerTimer = 1.0 + rand() % 300 / 100.0;
        }
        vehicle.setInput(enemyCar, 0.5, enemySteer, 0.0, false);
        local em = vehicle.getMount(enemyCar, 0);
        if (em != null) {
            aimAt(em, vehicle.getX(enemyCar), vehicle.getY(enemyCar),
                  vehicle.getX(fpsCar), vehicle.getY(fpsCar), vehicle.getHeading(enemyCar));
            local ew = weapon.mountGetWeapon(em);
            if (ew != null && weapon.canFire(ew)) {
                weapon.fireAt(ew, vehicle.getX(fpsCar), vehicle.getY(fpsCar), 0.0, 0);
            }
        }
    }

    // ============ 推进（vehicle → 物理 → weapon，顺序即框架约定） ============
    vehicle.update(dt);
    if (world != null) world.update(dt);
    weapon.update(dt);

    // ============ 载具事件（命令完成/击毁） ============
    for (local i = 0; i < vehicle.getEventCount(); i += 1) {
        if (vehicle.getEventType(i) == "order_completed") {
            local t = vehicle.getEventOrderType(i);
            if (t == "attack_move") logLine("巡逻点到达");
            else if (t == "move") logLine("移动命令完成");
        }
        if (vehicle.getEventType(i) == "destroyed") {
            logLine("一辆载具被击毁");
        }
    }
    vehicle.clearEvents();

    // ============ 武器事件 → 伤害结算（框架管线） ============
    local n = weapon.getEventCount();
    for (local i = 0; i < n; i += 1) {
        local type = weapon.getEventType(i);
        local defId = weapon.getEventDefId(i);
        if (type == "fire") {
            if (defId == "cannon.120" && !vehicle.isDestroyed(enemyCar)) {
                vehicle.applyDamage(enemyCar, 60.0, hitZone(rtsTank, enemyCar), 0);
                logLine("坦克主炮命中（" + hitZone(rtsTank, enemyCar) + " 装甲）");
            }
            if (defId == "cannon.60" && !vehicle.isDestroyed(fpsCar)) {
                vehicle.applyDamage(fpsCar, 25.0, hitZone(enemyCar, fpsCar), 0);
                logLine("敌车火炮命中");
            }
            if (defId == "mg.7.62" && !vehicle.isDestroyed(enemyCar)) {
                vehicle.applyDamage(enemyCar, 6.0, hitZone(fpsCar, enemyCar), 0);
            }
            tracers.push({ x = vehicle.getX(fpsCar), y = vehicle.getY(fpsCar),
                           life = 0.12 });
        } else if (type == "reload_start") {
            logLine(defId + " 装填中");
        } else if (type == "reload_end") {
            logLine(defId + " 装填完成");
        } else if (type == "empty") {
            logLine(defId + " 弹药耗尽");
        }
    }
    weapon.clearEvents();

    // ============ 巡逻（attackMove 命令循环） ============
    if (patrol && vehicle.isArrived(rtsTank)) {
        waypoint = (waypoint + 1) % waypoints.len();
        vehicle.attackMove(rtsTank, waypoints[waypoint].x, waypoints[waypoint].y);
    }

    // ============ 重生 ============
    if (vehicle.isDestroyed(fpsCar)) {
        if (carRespawn < 0.0) {
            carRespawn = 2.0;
            logLine("蓝车被击毁，2 秒后重生");
        }
        carRespawn -= dt;
        if (carRespawn <= 0.0) {
            spawnPlayerCar();
            carRespawn = -1.0;
            logLine("蓝车重生");
        }
    }
    if (vehicle.isDestroyed(enemyCar)) {
        if (enemyRespawn < 0.0) {
            enemyRespawn = 3.0;
            logLine("敌车被击毁，3 秒后重生");
        }
        enemyRespawn -= dt;
        if (enemyRespawn <= 0.0) {
            spawnEnemyCar();
            enemyRespawn = -1.0;
            logLine("敌车重生");
        }
    }

    // 曳光弹寿命
    for (local i = tracers.len() - 1; i >= 0; i -= 1) {
        tracers[i].life -= dt;
        if (tracers[i].life <= 0.0) tracers.remove(i);
    }
};

// ============ ECS 视图渲染：遍历 eve.view(eve.VehicleEntity) 统一绘制 ============
eve_render = function() {
    gfx.setBackgroundColor(0.06, 0.08, 0.10, 1.0);

    // 墙体与障碍物
    gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), 24.0, 0.22, 0.24, 0.28, 1.0);
    gfx.drawSolidRect(0.0, config.height.tofloat() - 24.0, config.width.tofloat(), 24.0, 0.22, 0.24, 0.28, 1.0);
    gfx.drawSolidRect(0.0, 0.0, 24.0, config.height.tofloat(), 0.22, 0.24, 0.28, 1.0);
    gfx.drawSolidRect(config.width.tofloat() - 24.0, 0.0, 24.0, config.height.tofloat(), 0.22, 0.24, 0.28, 1.0);
    gfx.drawSolidRect(300.0 - 35.0, 260.0 - 35.0, 70.0, 70.0, 0.18, 0.20, 0.22, 1.0);
    gfx.drawSolidRect(620.0 - 40.0, 420.0 - 30.0, 80.0, 60.0, 0.18, 0.20, 0.22, 1.0);

    foreach (e in eve.view(eve.VehicleEntity)) {
        local x = e.getX();
        local y = e.getY();
        local h = e.getHeading() * PI / 180.0;
        local r = 18.0;
        local colorR = 0.4, colorG = 0.4, colorB = 0.4;
        if (e.getFaction() == "yellow") { colorR = 0.9; colorG = 0.8; colorB = 0.25; }
        if (e.getFaction() == "blue") { colorR = 0.25; colorG = 0.5; colorB = 0.95; }
        if (e.getFaction() == "red") { colorR = 0.9; colorG = 0.25; colorB = 0.2; }

        gfx.drawSolidRect(x - r, y - r * 0.62, r * 2.0, r * 1.24, colorR, colorG, colorB, 1.0);
        // 车头
        gfx.drawSolidRect(x + cos(h) * 12.0 - 3.0, y + sin(h) * 12.0 - 3.0, 6.0, 6.0, 1.0, 1.0, 1.0, 1.0);
        // 炮塔
        for (local i = 0; i < e.getMountCount(); i += 1) {
            local m = e.getMount(i);
            if (m == null) continue;
            local ay = (e.getHeading() + m.getYaw()) * PI / 180.0;
            gfx.drawSolidRect(x + cos(ay) * 26.0 - 2.0, y + sin(ay) * 26.0 - 2.0, 4.0, 4.0, 0.95, 0.95, 0.95, 1.0);
        }
        // 血条
        local ratio = clampf(e.getHealth() / e.getMaxHealth(), 0.0, 1.0);
        gfx.drawSolidRect(x - 26.0, y + 26.0, 52.0, 5.0, 0.2, 0.2, 0.2, 1.0);
        gfx.drawSolidRect(x - 26.0, y + 26.0, 52.0 * ratio, 5.0, 0.25, 0.8, 0.25, 1.0);
    }

    // 玩家车白色角标
    if (!vehicle.isDestroyed(fpsCar)) {
        local cx = vehicle.getX(fpsCar);
        local cy = vehicle.getY(fpsCar);
        gfx.drawSolidRect(cx - 28.0, cy - 28.0, 9.0, 2.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx - 28.0, cy - 28.0, 2.0, 9.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx + 19.0, cy - 28.0, 9.0, 2.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx + 26.0, cy - 28.0, 2.0, 9.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx - 28.0, cy + 26.0, 9.0, 2.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx - 28.0, cy + 19.0, 2.0, 9.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx + 19.0, cy + 26.0, 9.0, 2.0, 1.0, 1.0, 1.0, 1.0);
        gfx.drawSolidRect(cx + 26.0, cy + 19.0, 2.0, 9.0, 1.0, 1.0, 1.0, 1.0);
    }

    // 曳光弹
    foreach (t in tracers) {
        gfx.drawSolidRect(t.x - 3.0, t.y - 3.0, 6.0, 6.0, 1.0, 0.9, 0.4, 1.0);
    }

    // 事件日志（色块代替文字；真实游戏用 eve.Font() 绘制文本）
    local lineY = 34.0;
    gfx.drawSolidRect(0.0, 12.0, 460.0, 10.0 + eventLog.len() * 18.0, 0.10, 0.12, 0.16, 0.85);
    foreach (t in eventLog) {
        gfx.drawSolidRect(10.0, lineY, 8.0 + t.len() * 7.0, 14.0, 0.35, 0.45, 0.55, 1.0);
        lineY += 18.0;
    }
};
