// Script ECS for Armored Command 3D. Renderables stay on Graphics; tanks,
// shells, and command posts are queryable entities.

function destroyEcs(cls) {
    local snap = [];
    foreach (e in eve.view(cls)) snap.push(e);
    foreach (e in snap) e.destroy();
}

class Tank extends eve.Entity {
    type = 0
    team = 0
    x = 0.0
    z = 0.0
    yaw = 0.0
    tx = 0.0
    tz = 0.0
    hp = 100.0
    dead = false
    selected = false
    death = 0.0
    recoil = 0.0
    hit = 0.0
    cooldown = 0.5
    clip = 1
    parts = null
    skins = null
    gun = null
    player = null
}

class Shell extends eve.Entity {
    ent = null
    x = 0.0
    z = 0.0
    dx = 0.0
    dz = 0.0
    target = null
    life = 0.0
}

class CommandPost extends eve.Entity {
    x = 0.0
    z = 0.0
    team = 0
    hp = 160.0
    dead = false
    time = 0.0
    pieces = null
}

class TankSystem extends eve.System {
    constructor() { base.constructor(Tank); }
    function update(dt) { updateUnits(dt); }
}

class ShellSystem extends eve.System {
    constructor() { base.constructor(Shell); }
    function update(dt) { updateShells(dt); }
}

class BuildingSystem extends eve.System {
    constructor() { base.constructor(CommandPost); }
    function update(dt) { updateBuildings(dt); }
}
