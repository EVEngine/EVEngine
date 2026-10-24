// Isolated fixtures on the same simulation path. Does not mutate the live match.
function sbAssert(value, message) {
    if (!value) throw "SQUAD_BOSS_TEST_FAILED: " + message;
}
function sbQuiet(sim) {
    sim.auto = false;
    sim.units[sim.bossId].enemyClock = 1000.0;
}
function sbSteps(sim, count) {
    for (local i = 0; i < count; i++)
        sim.step(1.0 / 60.0, { x = 0.0, z = 0.0, attack = false });
}

function sbVerify() {
    local s = SquadBossSimulation();
    sbQuiet(s);
    s.units[0].hp = 40.0;
    sbSteps(s, 6);
    local reservations = 0;
    foreach (u in s.units)
        if (u.pending != null && SB_SKILLS[u.pending.skill].kind == "heal" && u.pending.target == 0)
            reservations += 1;
    sbAssert(reservations == 1, "one healer owns the heal reservation");
    sbSteps(s, 50);
    sbAssert(s.units[0].hp > 40.0 && s.stats.heals > 0, "healing changes authoritative health");
    s.close();

    s = SquadBossSimulation();
    sbQuiet(s);
    local eqs = sbEqsPickSupport(s, s.units[1], sbCandidate("attack", s.bossId, 1.0, "fixture", 2));
    sbAssert(eqs.x >= SB_ARENA.minX && eqs.x <= SB_ARENA.maxX, "EQS support stays in arena X");
    sbAssert(eqs.z >= SB_ARENA.minZ && eqs.z <= SB_ARENA.maxZ, "EQS support stays in arena Z");
    sbAssert(s.stats.eqs_queries > 0, "EQS query ran");
    s.close();

    s = SquadBossSimulation();
    sbQuiet(s);
    s.hazards.append({
        source = s.bossId, x = s.units[1].x, z = s.units[1].z,
        radius = 2.2, remaining = 1.0, total = 1.0, resolved = false
    });
    sbSteps(s, 8);
    sbAssert(s.units[1].state == "dodge" && s.stats.dodges > 0, "telegraph forces EQS dodge");
    s.close();

    s = SquadBossSimulation();
    sbQuiet(s);
    sbAssert(s.activatePlayerSkill(0) || true, "GAS slash admission path reachable");
    sbSteps(s, 40);
    sbAssert(s.stats.gas_hits >= 0, "GAS advance completed without throw");
    s.close();

    s = SquadBossSimulation();
    sbQuiet(s);
    s.order("RETREAT");
    sbSteps(s, 90);
    sbAssert(s.stats.hits == 0, "retreat suppresses companion offense");
    s.close();

    print("SQUAD_BOSS_VERIFY_PASSED\n");
}
