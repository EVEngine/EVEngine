// ============================================================================
// EVEngine 通用载具系统示例
//
// 演示两套形态如何共用同一套 vehicle/weapon 模块：
//   RTS 坦克 tank.rts —— kinematic 移动 + attackMove 巡逻 + 炮塔自动瞄准；
//   FPS 吉普 car.fps —— 驾驶座读取玩家键盘输入，炮手座自动跟踪并开火。
//
// 数据全部在 JSON 定义里（见下方字符串），脚本只做输入/渲染/事件轮询。
// ============================================================================

// ---- 软热重载保护 ----
if (!("vehicle" in getroottable())) vehicle <- null;
if (!("weapon" in getroottable())) weapon <- null;
if (!("rtsTank" in getroottable())) rtsTank <- null;
if (!("fpsCar" in getroottable())) fpsCar <- null;
if (!("waypoint" in getroottable())) waypoint <- 0;
if (!("eventLog" in getroottable())) eventLog <- [];
if (!("fired" in getroottable())) fired <- false;

const PLAYER = 1;

waypoints <- [
    { x = 140.0, y = 140.0 },
    { x = 760.0, y = 140.0 },
    { x = 760.0, y = 500.0 },
    { x = 140.0, y = 500.0 }
];

weaponDefs <- @"[
  {""id"":""cannon.125"",""logic"":""projectile"",""damage"":320,""penetration"":260,
   ""range"":600,""spread"":1.0,""cooldown"":3.0,
   ""ammo"":{""mag"":1,""reserve"":40,""reload"":5.0},
   ""projectile"":{""type"":""shell"",""speed"":900,""gravity"":0.0,""aoe"":30.0}},
  {""id"":""mg.7.62"",""logic"":""hitscan"",""damage"":12,""range"":400,""spread"":2.0,
   ""fireMode"":""auto"",""cooldown"":0.12,
   ""ammo"":{""mag"":100,""reserve"":400,""reload"":2.5}}]";

vehicleDefs <- @"[
  {""id"":""tank.rts"",""category"":""tank"",""mobility"":""kinematic"",
   ""maxSpeed"":90,""accel"":60,""turnRate"":80,""radius"":22,""maxHealth"":500,
   ""armorZones"":[{""name"":""front"",""mult"":1.0},{""name"":""side"",""mult"":0.6}],
   ""mounts"":[{""name"":""turret"",""weapon"":""cannon.125"",""type"":""turret"",
                ""limits"":[-180,180,-8,20],""rotSpeed"":60,""aimMode"":""auto""}]},
  {""id"":""car.fps"",""category"":""car"",""mobility"":""kinematic"",
   ""maxSpeed"":160,""accel"":120,""turnRate"":140,""radius"":18,""maxHealth"":300,
   ""mounts"":[{""name"":""mg"",""weapon"":""mg.7.62"",""type"":""turret"",
                ""limits"":[-120,120,-10,20],""rotSpeed"":120,""aimMode"":""manual""}],
   ""seats"":[{""name"":""driver"",""cameraMode"":""third""},
              {""name"":""gunner"",""cameraMode"":""third"",""mountIndex"":0}]}]";

function logLine(text) {
    eventLog.push(text);
    while (eventLog.len() > 5) eventLog.remove(0);
}

function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

eve_init = function() {
    if (weapon == null) weapon = eve.Weapon();
    if (vehicle == null) vehicle = eve.Vehicle();

    weapon.registerWeaponsFromJson(weaponDefs);
    vehicle.registerVehiclesFromJson(vehicleDefs);

    if (rtsTank == null) {
        rtsTank = vehicle.newVehicle("tank.rts", 140.0, 500.0, 0.0, "red");
        vehicle.moveTo(rtsTank, waypoints[0].x, waypoints[0].y);
    }
    if (fpsCar == null) {
        fpsCar = vehicle.newVehicle("car.fps", 450.0, 320.0, 0.0, "blue");
        vehicle.enterSeat(fpsCar, 0, PLAYER);   // 驾驶座
        vehicle.enterSeat(fpsCar, 1, PLAYER);   // 炮手座（同一玩家）
    }
    logLine("初始化完成：RTS 坦克巡逻 + FPS 吉普（W/A/S/D 驾驶，Space 开火）");
};

eve_update = function(dt) {
    // ---- 玩家输入（FPS 吉普驾驶座）----
    local throttle = 0.0;
    local steer = 0.0;
    if (keyboard.isDown("w") || keyboard.isDown("W")) throttle = 1.0;
    if (keyboard.isDown("s") || keyboard.isDown("S")) throttle = -0.6;
    if (keyboard.isDown("a") || keyboard.isDown("A")) steer = -1.0;
    if (keyboard.isDown("d") || keyboard.isDown("D")) steer = 1.0;
    local fire = keyboard.isDown("Space");

    // 炮手座自动跟踪 RTS 坦克（演示 aimYaw 驱动挂点）
    local tx = vehicle.getX(rtsTank) - vehicle.getX(fpsCar);
    local ty = vehicle.getY(rtsTank) - vehicle.getY(fpsCar);
    local aimYaw = atan2(ty, tx) * 180.0 / 3.14159265;
    vehicle.setPlayerControls(PLAYER, throttle, steer, 0.0, fire, aimYaw, 0.0);

    // ---- 推进载具与武器（先 vehicle 后 weapon，炮塔转动才正确）----
    vehicle.update(dt);
    weapon.update(dt);

    // ---- 载具事件轮询 ----
    for (local i = 0; i < vehicle.getEventCount(); i += 1) {
        if (vehicle.getEventType(i) == "order_completed") {
            logLine("坦克抵达巡逻点");
        }
    }
    vehicle.clearEvents();

    // ---- RTS 坦克自动开火（炮塔已由 auto-aim 转向，这里按冷却开火）----
    local m = vehicle.getMount(rtsTank, 0);
    if (m != null) {
        local w = weapon.mountGetWeapon(m);
        if (w != null && weapon.canFire(w)) {
            if (weapon.fireAt(w, vehicle.getX(fpsCar), vehicle.getY(fpsCar), 0.0, 0)) {
                logLine("坦克主炮开火");
            }
        }
    }

    // ---- 武器事件轮询 ----
    local n = weapon.getEventCount();
    for (local i = 0; i < n; i += 1) {
        if (weapon.getEventType(i) == "fire") {
            if (weapon.getEventDefId(i) == "mg.7.62") logLine("吉普机枪开火");
        }
    }
    weapon.clearEvents();

    // 巡逻：到达航点后去下一个
    if (vehicle.isArrived(rtsTank)) {
        waypoint = (waypoint + 1) % waypoints.len();
        vehicle.attackMove(rtsTank, waypoints[waypoint].x, waypoints[waypoint].y);
    }
};

function drawVehicle(v, colorR, colorG, colorB) {
    local x = vehicle.getX(v);
    local y = vehicle.getY(v);
    local r = 18.0;
    local h = vehicle.getHeading(v) * 3.14159265 / 180.0;
    local dx = cos(h) * 12.0;
    local dy = sin(h) * 12.0;

    // 车体
    gfx.drawSolidRect(x - r, y - r * 0.62, r * 2.0, r * 1.24, colorR, colorG, colorB, 1.0);
    // 车头
    gfx.drawSolidRect(x + dx - 3.0, y + dy - 3.0, 6.0, 6.0, 1.0, 1.0, 1.0, 1.0);
    // 炮塔（朝挂点方向画一条线）
    for (local i = 0; i < vehicle.getMountCount(v); i += 1) {
        local m = vehicle.getMount(v, i);
        if (m == null) continue;
        local ay = (vehicle.getHeading(v) + m.getYaw()) * 3.14159265 / 180.0;
        local len = 26.0;
        // 炮管端点方块
        gfx.drawSolidRect(x + cos(ay) * len - 2.0, y + sin(ay) * len - 2.0,
                          4.0, 4.0, 0.9, 0.9, 0.9, 1.0);
    }
}

function drawHealthBar(v, x, y, w) {
    local hp = vehicle.getHealth(v);
    local maxHp = vehicle.getMaxHealth(v);
    local ratio = clampf(hp / maxHp, 0.0, 1.0);
    gfx.drawSolidRect(x, y, w, 5.0, 0.2, 0.2, 0.2, 1.0);
    gfx.drawSolidRect(x, y, w * ratio, 5.0, 0.25, 0.8, 0.25, 1.0);
}

eve_render = function() {
    gfx.setBackgroundColor(0.06, 0.08, 0.10, 1.0);

    drawVehicle(rtsTank, 0.85, 0.25, 0.2);
    drawVehicle(fpsCar, 0.25, 0.5, 0.9);
    drawHealthBar(rtsTank, vehicle.getX(rtsTank) - 26.0, vehicle.getY(rtsTank) + 26.0, 52.0);
    drawHealthBar(fpsCar, vehicle.getX(fpsCar) - 26.0, vehicle.getY(fpsCar) + 26.0, 52.0);

    // 事件日志
    local lineY = 30.0;
    gfx.drawSolidRect(0.0, 10.0, 420.0, 8.0 + eventLog.len() * 18.0, 0.1, 0.12, 0.16, 0.8);
    foreach (t in eventLog) {
        // 文本绘制需要字体模块；示例用色块代替，真实游戏用 eve.Font()
        gfx.drawSolidRect(10.0, lineY, 8.0 + t.len() * 7.0, 14.0, 0.35, 0.45, 0.55, 1.0);
        lineY += 18.0;
    }
};
