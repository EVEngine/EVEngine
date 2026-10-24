// Utility planner for three AI companions. Same reservation / ETA ideas as companion-tactics,
// adapted to metres on the XZ plane and EQS-selected positions.

function sbDanger(sim, u) {
    local worst = null;
    foreach (h in sim.hazards) {
        if (h.remaining > 0.0 && h.remaining < 1.25 && sbDistanceXZ(u, h) < h.radius + 0.45) {
            if (worst == null || h.remaining < worst.remaining) worst = h;
        }
    }
    return worst;
}

function sbMoveSpeed(sim, u) {
    return sbDistanceXZ(u, sim.units[0]) > sbSupportBand(u).max
        ? SB_POSITION.catchupSpeed
        : SB_POSITION.speed;
}

function sbSupportEta(sim, u, target, skill) {
    local s = SB_SKILLS[skill];
    if (u.pending != null && u.pending.skill == skill && u.pending.target == target.id)
        return u.pending.remaining;
    local locked = u.pending == null ? 0.0 : u.pending.remaining;
    local travel = sbMax(0.0, sbDistanceXZ(u, target) - (s.range - SB_POSITION.rangeMargin))
        / sbMoveSpeed(sim, u);
    local resource = sbMax(0.0, s.cost - u.mana) / sim.manaRegen;
    return locked + sbMax(travel, sbMax(u.cooldowns[skill] - locked, resource - locked))
        + s.cast + SB_POSITION.reaction;
}

function sbCastReach(u, target, skill) {
    local s = SB_SKILLS[skill];
    return sbDistanceXZ(u, target) <= s.range - 0.15;
}

function sbCanAttackHere(sim, u) {
    local band = sbSupportBand(u);
    return sbDistanceXZ(u, sim.units[0]) <= band.max + 0.2;
}

function sbPlan(sim, u) {
    if (u.hp <= 0.0) return sbCandidate("down", u.id, 1000.0, "Incapacitated");
    if (sbDanger(sim, u) != null) {
        local c = sbCandidate("dodge", u.id, 1000.0, "Predicted boss telegraph");
        local p = sbEqsPickDodge(sim, u);
        c.x = p.x; c.z = p.z;
        return c;
    }
    if (sim.command == "RETREAT")
        return sbCandidate("follow", 0, 500.0, "Regroup - offense locked");
    if (u.pending != null && SB_SKILLS[u.pending.skill].kind == "attack"
        && sbDistanceXZ(u, sim.units[0]) > sbSupportBand(u).max)
        return sbCandidate("follow", 0, 450.0, "Cancel offense - restore support");
    if (u.pending != null) return u.choice;

    local best = sbCandidate("follow", 0, 8.0, "Maintain support spacing");
    foreach (k in u.skills) {
        local s = SB_SKILLS[k];
        if (u.cooldowns[k] > 0.0 || u.mana < s.cost) continue;
        foreach (t in sim.units) {
            if (t.hp <= 0.0) continue;
            if ((s.kind == "attack") == (t.team == u.team)) continue;
            local d = sbDistanceXZ(u, t);
            if (d > s.range + 2.5) continue;
            if (sim.command == "PROTECT" && t.team != u.team
                && sbDistanceXZ(t, sim.units[0]) > 5.0) continue;
            local score = 0.0;
            local reason = "";
            if (s.kind == "heal") {
                local missing = t.maxHp - t.hp;
                if (missing < 14.0) continue;
                if (sim.claims[t.id] != -1 && sim.claims[t.id] != u.id) continue;
                score = sbMin(missing, s.power) * u.healWeight + (t.hp < 35.0 ? 50.0 : 0.0);
                reason = "Missing health + urgency";
            } else if (s.kind == "guard") {
                local threat = sbDanger(sim, t);
                if (t.shield > 5.0 || threat == null) continue;
                if (sbSupportEta(sim, u, t, k) + 0.05 >= threat.remaining) continue;
                score = 90.0 * u.guardWeight + (t.id == 0 ? 28.0 : 0.0);
                reason = "Ally inside boss telegraph";
            } else {
                score = 18.0 * u.attackWeight + (t.id == sim.bossId ? 24.0 : 0.0);
                if (sim.command == "FOCUS") score += 32.0;
                reason = "Damage window on Colossus";
            }
            score -= sbSupportEta(sim, u, t, k) * 10.0;
            if (sim.command == "PROTECT" && s.kind != "attack") score += 30.0;
            if (u.choice != null && u.choice.kind == s.kind && u.choice.target == t.id) score += 7.0;
            if (score > best.score) best = sbCandidate(s.kind, t.id, score, reason, k);
        }
    }
    return best;
}
