// Script ECS types for Momentum Ruins. Physics bodies stay in Box2D; this file
// owns entity identity, component-shaped fields, and the per-frame systems.

function destroyEcs(cls) {
    local snap = [];
    foreach (e in eve.view(cls)) snap.push(e);
    foreach (e in snap) e.destroy();
}

class Actor extends eve.Entity {
    kind = ""
    body = null
    fixtures = null
    dead = false
    spine = null
}

class Player extends Actor {
    hp = 0.0
    maxHp = 0.0
    facing = 1
    grounded = 0
    wallLeft = 0
    wallRight = 0
    groundContacts = null
    wallLeftContacts = null
    wallRightContacts = null
    coyote = 0.0
    jumpBuffer = 0.0
    airJumpsRemaining = 0
    wallCoyote = 0.0
    wallJumpDir = 0.0
    wallJumpFeedback = 0.0
    dashTimer = 0.0
    dashReady = true
    attackTimer = 0.0
    wallJumpLock = 0.0
    attackStartup = 0.0
    attackActive = 0.0
    attackEnabled = false
    attackWindowDone = false
    attackKind = "none"
    attackFixture = null
    attackL = null
    attackR = null
    queuedAttack = false
    queuedKick = false
    combo = 0
    comboGrace = 0.0
    attackHits = null
    invulnerable = 0.0
}

class Enemy extends Actor {
    hp = 0.0
    maxHp = 0.0
    stagger = 0.0
    staggerDelay = 0.0
    state = "normal"
    stateTimer = 0.0
    attackCooldown = 0.3
    attackDealt = false
    impactCooldown = 0.0
    facing = -1
    grounded = 0
    groundContacts = null
    wallContacts = null
}

class Boss extends Actor {
    hp = 0.0
    maxHp = 0.0
    stagger = 0.0
    staggerDelay = 0.0
    state = "normal"
    stateTimer = 0.0
    impactCooldown = 0.0
    phase = 1
    time = 0.0
    homeX = 0.0
    homeY = 0.0
}

class Prop extends Actor {
    hp = 0.0
    maxHp = 0.0
    impactCooldown = 0.0
    texture = null
}

class Sensor extends Actor {
}

class MovingPlatform extends Actor {
    time = 0.0
    speed = 130.0
}

class PlayerControlSystem extends eve.System {
    constructor() { base.constructor(Player); }
    function update(dt) { updatePlayer(dt); }
}

class EnemyAISystem extends eve.System {
    constructor() { base.constructor(Enemy); }
    function update(dt) {
        local px = game.player.body.getX();
        local py = game.player.body.getY();
        foreach (e in entities()) {
            if (e.state == "dead") continue;
            if (e.impactCooldown > 0.0) e.impactCooldown -= dt;
            if (e.attackCooldown > 0.0) e.attackCooldown -= dt;
            if (e.staggerDelay > 0.0) e.staggerDelay -= dt;
            else e.stagger = clampf(e.stagger - TUNE.staggerDecay * dt, 0.0, TUNE.staggerThreshold);
            if (e.state == "dying") {
                e.stateTimer -= dt;
                if (e.stateTimer <= 0.0) { e.state = "dead"; e.body.setActive(false); }
                continue;
            } else if (e.state == "attacking") {
                e.stateTimer -= dt;
                e.body.setLinearVelocity(0.0, e.body.getLinearVelocityY());
                if (!e.attackDealt && e.stateTimer <= TUNE.enemyAttackDuration - TUNE.enemyAttackHitTime) {
                    e.attackDealt = true;
                    local dxNow = game.player.body.getX() - e.body.getX();
                    local dyNow = game.player.body.getY() - e.body.getY();
                    if (absf(dxNow) < 54.0 && absf(dyNow) < 52.0 && game.player.invulnerable <= 0.0) {
                        game.player.hp -= 9.0;
                        game.player.invulnerable = 0.55;
                        game.player.body.applyLinearImpulse(e.facing * 240.0, -80.0);
                    }
                }
                if (e.stateTimer <= 0.0) { e.state = "normal"; e.attackCooldown = 0.72; }
                continue;
            } else if (e.state == "hit_stun") {
                e.stateTimer -= dt;
                if (e.stateTimer <= 0.0) e.state = "normal";
            } else if (e.state == "launched") {
                if (e.grounded > 0 && absf(e.body.getLinearVelocityY()) < TUNE.launchedLandSpeed) {
                    e.state = "knocked_down";
                    e.stateTimer = TUNE.knockdownTime;
                    e.body.setLinearVelocity(e.body.getLinearVelocityX() * 0.55, 0.0);
                    playSpine(e.spine, "hit", false, true);
                }
            } else if (e.state == "knocked_down") {
                e.stateTimer -= dt;
                if (e.stateTimer <= 0.0) {
                    e.state = "getting_up"; e.stateTimer = TUNE.getupTime;
                    playSpine(e.spine, "idle", true, true);
                }
            } else if (e.state == "getting_up") {
                e.stateTimer -= dt;
                if (e.stateTimer <= 0.0) e.state = "normal";
            }
            if (e.state != "normal") continue;
            local dx = px - e.body.getX();
            local dy = py - e.body.getY();
            e.facing = dx >= 0.0 ? 1 : -1;
            if (absf(dx) < 48.0 && absf(dy) < 52.0 && e.attackCooldown <= 0.0) {
                e.state = "attacking";
                e.stateTimer = TUNE.enemyAttackDuration;
                e.attackDealt = false;
                e.body.setLinearVelocity(0.0, e.body.getLinearVelocityY());
                playSpine(e.spine, "shoot", false, true);
            } else if (absf(dx) < 430.0) {
                local speed = e.kind == "enemy_leaper" ? 105.0 : 78.0;
                local blocked = e.wallContacts.len() > 0;
                e.body.setLinearVelocity(blocked ? 0.0 : e.facing * speed,
                                         e.body.getLinearVelocityY());
                if (e.kind == "enemy_leaper" && e.grounded > 0 && absf(dx) < 180.0 &&
                    e.attackCooldown <= 0.0 && (!blocked || absf(dx) < 90.0)) {
                    e.body.applyLinearImpulse(e.facing * 190.0, -420.0);
                    e.attackCooldown = 1.8;
                    playSpine(e.spine, "jump", true);
                } else playSpine(e.spine, "run");
            } else playSpine(e.spine, "idle");
        }
    }
}

class BossSystem extends eve.System {
    constructor() { base.constructor(Boss); }
    function update(dt) {
        foreach (b in entities()) {
            if (b.state == "dead") continue;
            if (b.impactCooldown > 0.0) b.impactCooldown -= dt;
            b.time += dt;
            b.phase = b.hp < b.maxHp * 0.33 ? 3 : (b.hp < b.maxHp * 0.66 ? 2 : 1);
            if (b.state == "knocked_down") {
                b.stateTimer -= dt;
                if (b.stateTimer <= 0.0) b.state = "normal";
                continue;
            }
            local radiusX = b.phase == 1 ? 115.0 : 175.0;
            local radiusY = b.phase == 3 ? 105.0 : 65.0;
            b.body.setPosition(b.homeX + sin(b.time * (0.7 + b.phase * 0.16)) * radiusX,
                               b.homeY + sin(b.time * 1.35) * radiusY);
            if (absf(game.player.body.getX() - b.body.getX()) < 70.0 &&
                absf(game.player.body.getY() - b.body.getY()) < 65.0 && game.player.invulnerable <= 0.0) {
                game.player.hp -= 14.0 + b.phase * 2.0;
                game.player.invulnerable = 0.7;
                game.player.body.applyLinearImpulse(
                    (game.player.body.getX() < b.body.getX() ? -420.0 : 420.0), -180.0);
            }
        }
    }
}

class PlatformSystem extends eve.System {
    constructor() { base.constructor(MovingPlatform); }
    function update(dt) {
        foreach (e in entities()) {
            e.time += dt;
            e.body.setLinearVelocity(cos(e.time * 1.35) * e.speed, 0.0);
        }
    }
}

class PropBreakSystem extends eve.System {
    constructor() { base.constructor(Prop); }
    function update(_dt) {
        foreach (e in entities()) {
            if (e.kind == "prop_crate" && !e.dead && e.hp <= 0.0) {
                e.dead = true; e.body.setActive(false);
                game.message = "Crate shattered by momentum"; game.messageTimer = 1.4;
            }
        }
    }
}
