// Companion skill catalogue (utility AI) and player GAS ability table.
// Heal / guard stay host-owned because CombatRuntime has no heal admission path.
// Player attack / buff go through AbilityRuntime + AttributeSet + EffectContainer.

SB_SKILLS <- [
    { name = "MEND", kind = "heal", range = 4.2, cast = 0.55, cool = 3.4, cost = 22.0, power = 28.0 },
    { name = "AEGIS", kind = "guard", range = 3.6, cast = 0.16, cool = 4.0, cost = 18.0, power = 32.0 },
    { name = "BOLT", kind = "attack", range = 4.6, cast = 0.32, cool = 0.85, cost = 4.0, power = 9.0 },
];

SB_POSITION <- {
    minimum = 2.0,
    peerMinimum = 1.8,
    rangeMargin = 0.45,
    speed = 3.4,
    catchupSpeed = 4.2,
    reaction = 0.1,
    hysteresis = 0.35,
};

function sbCandidate(kind, target, score, reason, skill = -1) {
    return { kind = kind, target = target, score = score, reason = reason, skill = skill, x = 0.0, z = 0.0 };
}

function sbSupportSkill(u) {
    foreach (k in u.skills) if (SB_SKILLS[k].kind == "guard") return k;
    foreach (k in u.skills) if (SB_SKILLS[k].kind == "heal") return k;
    return u.skills.len() > 0 ? u.skills[0] : 0;
}

function sbSupportBand(u) {
    if (u.band != null) return u.band;
    local range = SB_SKILLS[sbSupportSkill(u)].range;
    return {
        min = SB_POSITION.minimum,
        ideal = sbMin(3.1, range - 1.0),
        max = range - SB_POSITION.rangeMargin
    };
}
