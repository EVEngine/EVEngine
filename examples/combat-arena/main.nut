// Combat arena vertical slice: compose the script-owned CombatRuntime into a
// short scripted duel. Geometric melee, cancel graphs and lock-on are covered by
// C++ unit tests; this example proves the playable loop boots and advances
// without script-owned HP copies.

persist arena = null;
persist playerId = "11121314-1516-1718-991a-1b1c1d1e1f20";
persist enemyId = "01020304-0506-0708-890a-0b0c0d0e0f10";
persist tick = 0;
persist elapsed = 0.0;
persist attacked = false;
persist done = false;

eve_init = function() {
    if (arena != null) return;
    local combat = eve.Combat();
    local created = combat.newRuntime();
    if (!created.ok) {
        print("Run failed: combat runtime create");
        return;
    }
    arena = created.value;

    local player = arena.registerFighter(playerId, "fighter:player", 0.0, 0.0, 100.0, 40.0, 5.0, 18.0);
    local enemy = arena.registerFighter(enemyId, "fighter:enemy", 2.0, 0.0, 80.0, 30.0, 4.0, 16.0);
    if (!player.ok || !enemy.ok) {
        print("Run failed: register fighters");
        return;
    }

    local granted = arena.grantAbility("fighter:player", "combat-ability:light-attack");
    if (!granted.ok) {
        print("Run failed: grant ability");
        return;
    }
    print("combat-arena ready");
};

eve_update = function(dt) {
    if (arena == null || done) return;
    elapsed += dt;
    tick += 1;

    if (tick == 1) {
        local moved = arena.setMoveIntent(playerId, 1.0, 0.0, 1.0);
        if (!moved.ok) print("frame error: move intent");
    }

    local advanced = arena.advance(tick, dt);
    if (!advanced.ok) {
        print("frame error: combat advance");
        return;
    }

    if (!attacked && elapsed > 0.35) {
        attacked = true;
        local hit = arena.applyDamage(playerId, enemyId, "Damage.Physical.Slash", 12.0, 6.0, 0.0, 0.0, -1.0);
        if (!hit.ok) print("frame error: apply damage");
    }

    if (elapsed > 3.0) {
        local state = arena.state(enemyId);
        if (state.ok) print("combat-arena duel complete enemyHealth=" + state.value.health);
        done = true;
        eve.quit();
    }
};

eve_render = function() {
    gfx.clear();
};
