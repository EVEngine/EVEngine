// Environment Query System adapter.
// sensing.queryPositions is World2D (x,y); this raid maps it onto the arena XZ plane.
// The query proposes locations; the host scores them against support bands, peers and hazards.

function sbEqsRequire(sim) {
    if (sim.sensingWorld != null) return;
    local sensing = eve.Sensing();
    sim.sensingWorld = sbRequire(sensing.newWorld(), "create sensing world");
}

function sbEqsQueryRing(sim, selfX, selfZ, targetX, targetZ, radius, samples, limit) {
    sbEqsRequire(sim);
    local queried = sim.sensingWorld.queryPositions(
        selfX, selfZ, targetX, targetZ, radius, samples, limit, false);
    if (!queried.ok) throw "EQS query failed: " + queried.status.summary;
    sim.eqsLastCount = queried.value.ranked.len();
    sim.stats.eqs_queries += 1;
    return queried.value.ranked;
}

function sbEqsScoreSupport(sim, u, candidate, choice) {
    local p = { x = candidate.location.x, z = candidate.location.y };
    local clamped = sbClampArena(p.x, p.z);
    p.x = clamped.x; p.z = clamped.z;
    local hero = sim.units[0];
    local boss = sim.units[sim.bossId];
    local band = sbSupportBand(u);
    local gap = sbDistanceXZ(p, hero);
    local score = candidate.score * 40.0;
    score -= sbMax(0.0, band.min - gap) * 18.0;
    score -= sbMax(0.0, gap - band.max) * 18.0;
    score -= fabs(gap - band.ideal) * 2.0;
    score -= sbDistanceXZ(u, p) * 1.2;
    foreach (t in sim.units) {
        if (t.hp <= 0.0 || t.id == u.id) continue;
        local peer = sbDistanceXZ(p, t);
        if (t.team == u.team && t.id != 0)
            score -= sbMax(0.0, SB_POSITION.peerMinimum - peer) * 16.0;
        else if (t.team != u.team)
            score -= sbMax(0.0, 2.2 - peer) * 5.0;
    }
    if (choice.skill >= 0 && sim.units[choice.target].hp > 0.0) {
        local skill = SB_SKILLS[choice.skill];
        local excess = sbMax(0.0, sbDistanceXZ(p, sim.units[choice.target]) - (skill.range - 0.35));
        score -= skill.kind == "attack" ? sbMin(40.0, excess * 8.0) : excess * 14.0;
    }
    foreach (h in sim.hazards) {
        if (h.remaining <= 0.0) continue;
        local clear = sbDistanceXZ(p, h) - h.radius - 0.35;
        if (clear < 0.0) score -= 200.0 - clear * 40.0;
    }
    // Prefer a ring around the boss when attacking so the squad flanks instead of stacking.
    if (choice.kind == "attack" && boss.hp > 0.0) {
        local orbit = fabs(sbDistanceXZ(p, boss) - 3.2);
        score -= orbit * 6.0;
    }
    return { x = p.x, z = p.z, score = score };
}

function sbEqsPickSupport(sim, u, choice) {
    local hero = sim.units[0];
    local boss = sim.units[sim.bossId];
    local anchor = choice.kind == "attack" && boss.hp > 0.0 ? boss : hero;
    local radius = choice.kind == "attack" ? 3.4 : sbSupportBand(u).ideal;
    local ranked = sbEqsQueryRing(sim, u.x, u.z, anchor.x, anchor.z, radius, 16, 16);
    local best = null;
    local bestScore = -1.0e9;
    foreach (candidate in ranked) {
        local scored = sbEqsScoreSupport(sim, u, candidate, choice);
        if (u.position != null) {
            local stay = sbDistanceXZ(scored, u.position);
            if (stay < SB_POSITION.hysteresis) scored.score += 8.0;
        }
        if (scored.score > bestScore) {
            bestScore = scored.score;
            best = scored;
        }
    }
    if (best == null) {
        local fallback = sbClampArena(hero.x + 2.0, hero.z);
        return { x = fallback.x, z = fallback.z, reason = "EQS empty / fallback" };
    }
    return { x = best.x, z = best.z, reason = "EQS support ring" };
}

function sbEqsPickDodge(sim, u) {
    local threat = null;
    foreach (h in sim.hazards) {
        if (h.remaining > 0.0 && h.remaining < 1.2 && sbDistanceXZ(u, h) < h.radius + 0.55) {
            if (threat == null || h.remaining < threat.remaining) threat = h;
        }
    }
    if (threat == null) {
        local hold = sbClampArena(u.x, u.z);
        return { x = hold.x, z = hold.z, reason = "no active telegraph" };
    }
    // Ring around the hazard centre; host keeps the safest in-band candidate.
    local ranked = sbEqsQueryRing(sim, u.x, u.z, threat.x, threat.z, threat.radius + 2.4, 20, 20);
    local best = null;
    local bestScore = -1.0e9;
    local hero = sim.units[0];
    local band = sbSupportBand(u);
    foreach (candidate in ranked) {
        local p = sbClampArena(candidate.location.x, candidate.location.y);
        local clear = sbDistanceXZ(p, threat) - threat.radius - 0.4;
        local score = clear * 20.0 - sbDistanceXZ(u, p) * 2.0;
        local gap = sbDistanceXZ(p, hero);
        score -= sbMax(0.0, gap - band.max) * 4.0;
        score -= sbMax(0.0, band.min - gap) * 2.0;
        foreach (t in sim.units) {
            if (t.team == u.team && t.id != u.id && t.hp > 0.0)
                score -= sbMax(0.0, SB_POSITION.peerMinimum - sbDistanceXZ(p, t)) * 8.0;
        }
        if (clear < 0.0) score -= 500.0;
        if (score > bestScore) {
            bestScore = score;
            best = p;
        }
    }
    if (best == null) {
        local away = sbClampArena(u.x + (u.x - threat.x), u.z + (u.z - threat.z));
        return { x = away.x, z = away.z, reason = "EQS dodge fallback" };
    }
    return { x = best.x, z = best.z, reason = "EQS dodge ring" };
}
