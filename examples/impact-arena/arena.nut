function fighter(index, x, y, hero, hp) {
    local id = format("00000000-0000-0000-0000-%012d", index);
    local owner = "fighter:" + index;
    local actor = { id = id, owner = owner, hero = hero, view = null, action = null,
        faceX = hero ? 1.0 : -1.0, faceY = 0.0, flash = 0.0, counted = false,
        light = 0, heavy = 0, dodge = 0, index = index };
    actor.view = checked(arena.registerFighter(id, owner, x, y, hp, 50.0,
        hero ? 600.0 : 82.0 + gameState.wave * 7.0, hero ? 6000.0 : 2500.0));
    actor.light = checked(arena.grantAbility(owner, "combat-ability:light-attack")).grantId;
    actor.heavy = checked(arena.grantAbility(owner, "combat-ability:heavy-attack")).grantId;
    actor.dodge = checked(arena.grantAbility(owner, "combat-ability:dodge")).grantId;
    actors.append(actor);
    return actor;
}
function beginAction(actor, name) {
    if (actor.action != null || actor.view.health <= 0.0) return;
    local grant = name == "heavy" ? actor.heavy : (name == "dodge" ? actor.dodge : actor.light);
    if (checked(arena.abilityGrant(grant)).cooldownSeconds > 0.0) return;
    local activated = checked(arena.activateAbility(grant, gameState.tick));
    actor.action = { id = activated.activationId, name = name, phase = "windup", hit = false,
        x = actor.faceX, y = actor.faceY, elapsed = 0.0 };
    if (actor.hero && name == "dodge") gameState.dodges++;
}
function nextWave() {
    gameState.wave++;
    if (gameState.wave > 3) { gameState.phase = "victory"; return; }
    for (local i = 0; i < gameState.wave + 2; ++i) {
        local angle = i * 6.283185 / (gameState.wave + 2) + gameState.wave * 0.8;
        fighter(gameState.wave * 10 + i, 640.0 + cos(angle) * 400.0,
            390.0 + sin(angle) * 185.0, false, 48.0 + gameState.wave * 12.0);
    }
}
function vectorTo(a, b) {
    local dx = b.view.position.x - a.view.position.x;
    local dy = b.view.position.z - a.view.position.z;
    local distance = sqrt(dx * dx + dy * dy);
    return { x = distance > 0.01 ? dx / distance : 1.0,
        y = distance > 0.01 ? dy / distance : 0.0, distance = distance };
}
function popup(amount, actor, kind, merge) {
    checked(numberView.spawn(amount, viewX + actor.view.position.x * viewScale,
        viewY + (actor.view.position.z - 43.0) * viewScale, kind, merge ? actor.id : "",
        ((gameState.hits % 3) - 1) * 35.0 * viewScale));
}
function strike(attacker) {
    local move = attacker.action;
    local heavy = move.name == "heavy";
    local range = attacker.hero ? (heavy ? 140.0 : 100.0) : 94.0;
    foreach (target in actors) {
        if (target.hero == attacker.hero || target.view.health <= 0.0) continue;
        local direction = vectorTo(attacker, target);
        if (direction.distance > range || direction.x * move.x + direction.y * move.y < 0.05) continue;
        if (target.action != null && target.action.name == "dodge" && target.action.phase == "active") {
            popup(0.0, target, "miss", false); gameState.avoided++; continue;
        }
        local critical = attacker.hero && heavy;
        local amount = attacker.hero ? (heavy ? 46.0 : 22.0) : 10.0 + gameState.wave * 2.0;
        local outcome = checked(arena.applyDamage(attacker.id, target.id, "Damage.Physical.Slash",
            amount, heavy ? 22.0 : 8.0, 0.0, 0.0, 0.0, critical, gameState.tick));
        // Presentation consumes the committed canonical result, including overkill clamp.
        local settled = outcome.settlement;
        target.view = checked(arena.state(target.id));
        if (settled.applied > 0.0) {
            gameState.hits++;
            if (settled.critical) gameState.criticals++;
            popup(settled.applied, target, settled.critical ? "critical" : "damage", true);
            target.flash = 0.22;
            effects.append({x = target.view.position.x, y = target.view.position.z,
                life = 0.32, critical = critical});
        }
        gameState.lastApplied = settled.applied;
        gameState.lastRequested = settled.requested;
        gameState.lastStages = settled.stages.len();
    }
}
function moveActor(actor, dx, dy, speed) {
    // Stop at the arena edge; simulation position remains owned by CombatRuntime.
    local p = actor.view.position;
    if ((p.x <= 80.0 && dx < 0.0) || (p.x >= 1200.0 && dx > 0.0)) dx = 0.0;
    if ((p.z <= 190.0 && dy < 0.0) || (p.z >= 615.0 && dy > 0.0)) dy = 0.0;
    checked(arena.setMoveIntent(actor.id, dx, dy, speed));
}
function botControl() {
    local nearest = null, best = 1.0e9;
    foreach (a in actors) {
        if (a.hero || a.view.health <= 0.0) continue;
        local d = vectorTo(player, a);
        if (d.distance < best) { best = d.distance; nearest = a; }
    }
    if (nearest == null) return {x = 0.0, y = 0.0};
    local d = vectorTo(player, nearest);
    player.faceX = d.x; player.faceY = d.y;
    if (nearest.action != null && nearest.action.phase == "windup" && d.distance < 115.0) {
        player.faceX = -d.y; player.faceY = d.x;
        beginAction(player, "dodge");
    } else if (d.distance < 125.0) {
        beginAction(player, "heavy");
        if (d.distance < 88.0) beginAction(player, "light");
    }
    return {x = best > 85.0 ? d.x : 0.0, y = best > 85.0 ? d.y : 0.0};
}
function simulateArena(dt) {
    checked(numberView.update(dt));
    for (local i = effects.len() - 1; i >= 0; --i) {
        effects[i].life -= dt;
        if (effects[i].life <= 0.0) effects.remove(i);
    }
    gameState.popupCount = numberView.count();
    if (gameState.phase != "playing") return;
    gameState.tick++; gameState.time += dt;
    local input = {x = commands.x, y = commands.y};
    if (gameState.demo) input = botControl();
    else {
        if (keyboard.isDown("a") || keyboard.isDown("left")) input.x -= 1.0;
        if (keyboard.isDown("d") || keyboard.isDown("right")) input.x += 1.0;
        if (keyboard.isDown("w") || keyboard.isDown("up")) input.y -= 1.0;
        if (keyboard.isDown("s") || keyboard.isDown("down")) input.y += 1.0;
        local length = sqrt(input.x * input.x + input.y * input.y);
        if (length > 0.01) { player.faceX = input.x / length; player.faceY = input.y / length; }
        if (commands.dodge) beginAction(player, "dodge");
        else if (commands.heavy) beginAction(player, "heavy");
        else if (commands.attack) beginAction(player, "light");
    }
    commands.attack = false; commands.heavy = false; commands.dodge = false;
    local phases = checked(arena.advanceAbilities(gameState.tick, dt));
    foreach (actor in actors) {
        actor.flash = arenaMax(0.0, actor.flash - dt);
        if (actor.action != null) {
            foreach (phase in phases.advances) {
                if (phase.activationId != actor.action.id) continue;
                actor.action.phase = phase.phase; actor.action.elapsed = phase.elapsedSeconds;
                if (phase.phase == "active" && !actor.action.hit) {
                    actor.action.hit = true;
                    if (actor.action.name != "dodge" && actor.view.health > 0.0) strike(actor);
                }
                if (phase.phase == "completed" || phase.phase == "cancelled") actor.action = null;
                break;
            }
        }
    }
    local speed = 0.43;
    if (player.action != null) {
        if (player.action.name == "dodge" && player.action.phase == "active") {
            input.x = player.action.x; input.y = player.action.y; speed = 1.0;
        } else speed = 0.2;
    }
    moveActor(player, input.x, input.y, speed);
    local alive = 0;
    foreach (a in actors) {
        if (a.hero) continue;
        if (a.view.health <= 0.0) {
            checked(arena.stop(a.id));
            if (!a.counted) { a.counted = true; gameState.kills++; }
            continue;
        }
        alive++;
        local d = vectorTo(a, player);
        if (a.action == null) {
            a.faceX = d.x; a.faceY = d.y;
            if (d.distance < 90.0) beginAction(a, "heavy");
        }
        if (a.action != null || d.distance < 72.0) checked(arena.stop(a.id));
        else moveActor(a, d.x, d.y, 1.0);
    }
    checked(arena.advance(gameState.tick, dt));
    foreach (a in actors) a.view = checked(arena.state(a.id));
    gameState.playerHealth = player.view.health;
    gameState.enemyCount = alive;
    if (player.view.health <= 0.0) gameState.phase = "defeat";
    else if (alive == 0) {
        gameState.nextWave -= dt;
        if (gameState.nextWave <= 0.0) { nextWave(); gameState.nextWave = 1.4; }
    }
}
