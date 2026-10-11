// Script ECS for KayKit tactics. The tactics module still owns occupancy and
// turn order; HP, skills, and presentation live on Combatant entities.

function destroyEcs(cls) {
    local snap = [];
    foreach (e in eve.view(cls)) snap.push(e);
    foreach (e in snap) e.destroy();
}

class Combatant extends eve.Entity {
    id = ""
    name = ""
    role = ""
    hero = false
    hp = 0
    maxHp = 0
    x = 0
    z = 0
    renderables = null
    skins = null
    bindPose = null
    attachments = null
    skeleton = null
    player = null
    clips = null
    state = "idle"
    oneShot = 0.0
    visualWorldX = 0.0
    visualWorldZ = 0.0
    moving = false
    fromX = 0
    fromZ = 0
    toX = 0
    toZ = 0
    moveTime = 0.0
    movePath = null
    moveIndex = 0
    skillCursor = 0
    attacks = null
    damage = 0
    range = 0
    row = 0
}

class Hero extends Combatant {
}

class Foe extends Combatant {
}

class ActorAnimSystem extends eve.System {
    constructor() { base.constructor(Combatant); }
    function update(dt) { updateActors(dt); }
}
