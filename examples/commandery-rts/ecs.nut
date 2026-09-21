// Script ECS for Commandery: units and projectiles are entities. Economy
// points, authority, and production stay in engine modules (facts, not actors).

function destroyEcs(cls) {
    local snap = [];
    foreach (e in eve.view(cls)) snap.push(e);
    foreach (e in snap) e.destroy();
}

class Unit extends eve.Entity {
    id = ""
    faction = ""
    kind = ""
    x = 0.0
    y = 0.0
    tx = 0.0
    ty = 0.0
    hp = 0.0
    maxHp = 0.0
    damage = 0.0
    speed = 0.0
    range = 0.0
    cooldown = 0.0
    selected = false
    alive = true
    radius = 0.0
    holdFire = false
    repairing = false
    repairRange = 0.0
    repairRate = 0.0
    repairTarget = null
    orderId = ""
    commander = ""
    stats = null
    queue = null
    aimX = 1.0
    aimY = 0.0
}

class Shell extends eve.Entity {
    x = 0.0
    y = 0.0
    vx = 0.0
    vy = 0.0
    target = null
    damage = 0.0
    heavy = false
    alive = true
    life = 2.2
}

class CombatSystem extends eve.System {
    constructor() { base.constructor(Unit); }
    function update(dt) { updateCombat(dt); }
}

class HospitalRegenSystem extends eve.System {
    constructor() { base.constructor(Unit); }
    function update(dt) { updateHospitalRegen(dt); }
}

class RepairSystem extends eve.System {
    constructor() { base.constructor(Unit); }
    function update(dt) { updateRepair(dt); }
}

class ShellSystem extends eve.System {
    constructor() { base.constructor(Shell); }
    function update(dt) { updateProjectiles(dt); }
}
