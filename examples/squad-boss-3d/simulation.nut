// Single owner of health, resources, hazards and companion coordination.
// Player abilities admit through AbilityRuntime (GAS); companion heal/guard stay host-owned.
// EQS proposes positions; NpcAiWorld owns intent → state transitions.

class SquadBossSimulation {
    world = null;
    sensingWorld = null;
    combat = null;
    abilities = null;
    playerStats = null;
    bossStats = null;
    effects = null;
    units = null;
    hazards = null;
    events = null;
    claims = null;
    playerSkills = null;
    pendingGas = null;
    tick = 0;
    time = 0.0;
    command = "BALANCED";
    bossId = 4;
    auto = true;
    manaRegen = 8.0;
    stats = null;
    eqsLastCount = 0;
    phase = "ready";
    message = "SQUAD ONLINE";
    lastDamage = 0.0;
    effectEventCount = 0;
    playerSubject = "aaaaaaaa-0001-4000-8000-000000000001";
    bossSubject = "bbbbbbbb-0002-4000-8000-000000000002";
    companionSubjects = [
        "cccccccc-0003-4000-8000-000000000003",
        "dddddddd-0004-4000-8000-000000000004",
        "eeeeeeee-0005-4000-8000-000000000005"
    ];

    constructor() {
        units = [];
        hazards = [];
        events = [];
        claims = [-1, -1, -1, -1, -1];
        pendingGas = {};
        stats = {
            heals = 0, guards = 0, absorbed = 0, dodges = 0, hits = 0,
            cancels = 0, transitions = 0, gas_hits = 0, eqs_queries = 0, boss_slams = 0
        };
        world = sbRequire(npc_ai.newWorld(256, 0), "create AI world");
        sbRequire(world.registerBehavior(sbBehavior()), "register squad behavior");
        sbEqsRequire(this);

        combat = sbRequire(eve.Combat().newRuntime(), "create combat runtime");
        abilities = sbRequire(eve.Action().newRuntime(), "create ability runtime");
        effects = sbRequire(eve.Effects().newContainer(), "create effects");
        sbRequire(combat.registerFighter(playerSubject, "fighter:player", -3.2, 1.5, 140.0, 40.0, 3.6, 18.0),
            "register player fighter");
        sbRequire(combat.registerFighter(companionSubjects[0], "fighter:bastion", -4.4, 0.2, 160.0, 50.0, 3.2, 16.0),
            "register bastion");
        sbRequire(combat.registerFighter(companionSubjects[1], "fighter:lumen", -4.0, 2.6, 110.0, 30.0, 3.3, 16.0),
            "register lumen");
        sbRequire(combat.registerFighter(companionSubjects[2], "fighter:vesper", -2.0, 3.2, 120.0, 35.0, 3.5, 17.0),
            "register vesper");
        sbRequire(combat.registerFighter(bossSubject, "fighter:boss", 2.8, -0.4, 900.0, 120.0, 1.4, 8.0),
            "register boss fighter");
        playerStats = attributes.newSet(playerSubject);
        playerStats.setBase("Attribute.Energy", 100.0);
        playerStats.setBase("Attribute.Attack", 14.0);
        bossStats = attributes.newSet(bossSubject);
        bossStats.setBase("Attribute.DamageTaken", 1.0);

        addUnit("YOU", 0, -3.2, 1.5, 140.0, [2], 0.0, 0.0, 1.0, false, playerSubject);
        addUnit("BASTION", 0, -4.4, 0.2, 160.0, [1, 0, 2], 0.7, 1.6, 0.75, true, companionSubjects[0]);
        addUnit("LUMEN", 0, -4.0, 2.6, 110.0, [0, 2], 2.2, 0.0, 0.7, true, companionSubjects[1]);
        addUnit("VESPER", 0, -2.0, 3.2, 120.0, [2, 0], 0.4, 0.2, 1.5, true, companionSubjects[2]);
        addUnit("COLOSSUS", 1, 2.8, -0.4, 900.0, [], 0.0, 0.0, 1.0, false, bossSubject);
        units[bossId].enemyClock = 2.4;
        units[bossId].radius = 1.1;

        playerSkills = [
            {
                key = "j", label = "SLASH", definition = "slash", cost = 12.0, damage = 1.0,
                cd = 1.6, tag = "Ability.Attack.Slash", grant = 0, cooldown = 0.0
            },
            {
                key = "k", label = "BREAKER", definition = "breaker", cost = 32.0, damage = 2.2,
                cd = 4.2, tag = "Ability.Attack.Heavy", grant = 0, cooldown = 0.0
            },
            {
                key = "l", label = "RALLY", definition = "rally", cost = 28.0, damage = 0.0,
                cd = 12.0, tag = "Ability.Buff.Rally", grant = 0, cooldown = 0.0
            }
        ];
        foreach (s in playerSkills) {
            local id = "squad-ability:" + s.definition;
            sbRequire(abilities.registerTimelineAbility(
                id, fs.readText("data/" + s.definition + ".json"), s.cd,
                "per-execution", "exclusive-blocking", s.tag), "register " + s.definition);
            s.grant = sbRequire(abilities.grantAbility("fighter:player", id), "grant " + s.definition).grantId;
        }
        sbNote(this, "Four-man squad online. EQS positioning + GAS player kit.");
    }

    function addUnit(name, team, x, z, hp, skills, heal, guard, attack, ai, subject) {
        local id = units.len();
        units.append({
            id = id, name = name, team = team, x = x.tofloat(), z = z.tofloat(),
            hp = hp.tofloat(), maxHp = hp.tofloat(), mana = 100.0, shield = 0.0, shieldTime = 0.0,
            skills = skills, healWeight = heal, guardWeight = guard, attackWeight = attack,
            cooldowns = [0.0, 0.0, 0.0], pending = null, choice = null, state = "follow",
            agent = null, vx = 0.0, vz = 0.0, position = null, positionReason = "EQS",
            band = null, positionTick = 0, dodgeTime = 0.0, dodgeX = 0.0, dodgeZ = 0.0,
            enemyClock = 0.0, flash = 0.0, yaw = 0.0, strike = 0.0, radius = 0.35,
            subject = subject
        });
        if (ai) {
            units[id].agent = sbRequire(world.createAgent("squad"), "create companion agent");
            units[id].band = sbSupportBand(units[id]);
        }
    }

    function close() {
        foreach (u in units) if (u.agent != null) {
            sbRequire(world.destroyAgent(u.agent), "destroy companion");
            if (!world.isAgentStale(u.agent)) throw "destroyed AI handle remained live";
            u.agent = null;
        }
        hazards.clear();
        claims = [-1, -1, -1, -1, -1];
        pendingGas = {};
        if (effects != null) { effects.release(); effects = null; }
    }

    function order(value) {
        command = value;
        foreach (u in units) if (u.agent != null) cancel(u);
        sbNote(this, "COMMAND / " + value);
    }

    function cancel(u) {
        if (u.pending != null) { stats.cancels += 1; u.pending = null; }
        foreach (i, owner in claims) if (owner == u.id) claims[i] = -1;
    }

    function move(u, x, z, speed, dt) {
        local target = sbClampArena(x, z);
        local dx = target.x - u.x;
        local dz = target.z - u.z;
        local n = sqrt(dx * dx + dz * dz);
        if (n > 0.01) {
            local step = sbMin(n, speed * dt);
            u.x += dx / n * step;
            u.z += dz / n * step;
            u.yaw = atan2(dx, dz);
        }
        local c = sbClampArena(u.x, u.z);
        u.x = c.x; u.z = c.z;
    }

    function damage(source, target, power) {
        local u = units[target];
        if (u.hp <= 0.0) return 0.0;
        local blocked = sbMin(u.shield, power);
        u.shield -= blocked;
        if (blocked > 0.0) {
            stats.absorbed += blocked.tointeger();
            sbNote(this, u.name + " / AEGIS absorbed " + blocked.tointeger());
        }
        local dealt = power - blocked;
        u.hp = sbMax(0.0, u.hp - dealt);
        u.flash = 0.18;
        if (u.hp <= 0.0) {
            cancel(u);
            sbNote(this, u.name + " / down");
            foreach (other in units)
                if (other.pending != null && other.pending.target == target) cancel(other);
        }
        return dealt;
    }

    function start(u, c) {
        local s = SB_SKILLS[c.skill];
        local t = units[c.target];
        if (u.pending != null || u.hp <= 0.0 || t.hp <= 0.0 || u.cooldowns[c.skill] > 0.0
            || u.mana < s.cost || sbDistanceXZ(u, t) > s.range) return;
        if (u.agent != null && !sbCastReach(u, t, c.skill)) return;
        if (s.kind == "heal") {
            if (claims[t.id] != -1 && claims[t.id] != u.id) return;
            claims[t.id] = u.id;
        }
        u.choice = c;
        local threat = s.kind == "guard" ? sbDanger(this, t) : null;
        u.pending = {
            skill = c.skill, target = c.target, remaining = s.cast,
            deadline = threat == null ? null : time + threat.remaining
        };
    }

    function finish(u) {
        local p = u.pending;
        local s = SB_SKILLS[p.skill];
        local t = units[p.target];
        if (u.hp <= 0.0 || t.hp <= 0.0 || sbDistanceXZ(u, t) > s.range + 0.25 || u.mana < s.cost) {
            cancel(u); return;
        }
        if (p.deadline != null && time >= p.deadline) { cancel(u); return; }
        u.mana -= s.cost;
        u.cooldowns[p.skill] = s.cool;
        if (s.kind == "heal") {
            local amount = sbMin(s.power, t.maxHp - t.hp);
            t.hp += amount;
            stats.heals += 1;
            sbNote(this, u.name + " / MEND " + t.name + " +" + amount.tointeger());
        } else if (s.kind == "guard") {
            t.shield = s.power;
            t.shieldTime = 3.0;
            stats.guards += 1;
            sbNote(this, u.name + " / AEGIS protects " + t.name);
        } else if (t.id == bossId) {
            // Mirror companion bolts through native DamageRuntime so GAS/combat
            // and the raid simulation stay on one boss health ledger.
            local hit = sbRequire(combat.applyDamage(u.subject, bossSubject,
                "Damage.Physical.Bolt", s.power, 4.0, 0.0, 0.0, 0.0), "companion bolt");
            local dealt = damage(u.id, t.id, hit.appliedHealthDamage);
            if (dealt > 0.0) stats.hits += 1;
        } else {
            local dealt = damage(u.id, t.id, s.power);
            if (dealt > 0.0) stats.hits += 1;
        }
        u.pending = null;
        foreach (i, owner in claims) if (owner == u.id) claims[i] = -1;
    }

    function buffRemaining() {
        if (effects.taggedCount(playerSubject, "State.Empowered") == 0) return 0.0;
        return effects.taggedAt(playerSubject, "State.Empowered", 0).getRemaining();
    }

    function drainEffectEvents() {
        for (local i = 0; i < effects.eventCount(); ++i) {
            local event = effects.eventAt(i);
            local kind = event.getKind();
            local id = event.getEffectId();
            local source = "effect:" + id;
            effectEventCount += 1;
            if (kind == "applied" || kind == "refreshed") {
                local effect = effects.find(id);
                if (effect == null) continue;
                if (effect.hasTag("State.Empowered")) {
                    local multiplier = effect.getPayload().getJson("attackMultiplier").tofloat();
                    if (!playerStats.addModifier(source, "Attribute.Attack", source, "multiply", multiplier, 10))
                        throw "Apply attack modifier failed";
                } else if (effect.hasTag("State.Exposed")) {
                    local additive = effect.getPayload().getJson("damageTakenAdd").tofloat();
                    if (!bossStats.addModifier(source, "Attribute.DamageTaken", source, "add", additive, 0))
                        throw "Apply vulnerability modifier failed";
                }
            } else if (kind == "removed" || kind == "expired") {
                playerStats.removeBySource(source, "Attribute.Attack");
                bossStats.removeBySource(source, "Attribute.DamageTaken");
            }
        }
        effects.clearEvents();
    }

    function applyRally() {
        local id = sbRequire(effects.apply(playerSubject, "Effect.Buff.Rally", "ability:rally", 10, 10.0,
            "rally", "refresh"), "apply rally");
        local effect = effects.find(id);
        sbRequire(effect.addTag("State.Empowered"), "tag rally");
        effect.getPayload().setNumber("attackMultiplier", 1.35);
        drainEffectEvents();
        return id;
    }

    function applyExposed() {
        if (effects.taggedCount(bossSubject, "State.Exposed") >= 3) {
            local oldest = effects.taggedAt(bossSubject, "State.Exposed", 0).getId();
            sbRequire(effects.remove(oldest, "stack-cap"), "replace oldest exposure");
            drainEffectEvents();
        }
        local id = sbRequire(effects.apply(bossSubject, "Effect.Debuff.Exposed", "ability:melee", 1, 8.0,
            "exposed", "stack"), "apply exposure");
        local effect = effects.find(id);
        sbRequire(effect.addTag("State.Exposed"), "tag exposure");
        effect.getPayload().setNumber("damageTakenAdd", 0.08);
        drainEffectEvents();
    }

    function activatePlayerSkill(index) {
        local s = playerSkills[index];
        local hero = units[0];
        local boss = units[bossId];
        if (hero.hp <= 0.0) { message = "Player down / press R"; return false; }
        if (boss.hp <= 0.0) { message = "Boss defeated / press R"; return false; }
        if (index == 2 && buffRemaining() > 0.0) { message = "Rally already active"; return false; }
        if (playerStats.getBase("Attribute.Energy", 0.0) < s.cost) {
            message = "Insufficient energy"; return false;
        }
        local matching = sbRequire(abilities.matchingAbilities("fighter:player", s.tag), "match tag");
        if (matching.len() != 1 || matching[0] != s.grant) throw "Gameplay event must resolve one grant";
        local result = abilities.activateAbility(matching[0], tick);
        if (!result.ok) { message = "Blocked: cooldown or group"; return false; }
        playerStats.modifyBase("Attribute.Energy", -s.cost);
        pendingGas[result.value.activationId] <- { skill = index, consumed = false };
        message = s.tag + " / cost " + s.cost.tointeger();
        return true;
    }

    function consumeGas(record) {
        local s = playerSkills[record.skill];
        local hero = units[0];
        local boss = units[bossId];
        if (record.skill == 2) {
            applyRally();
            sbNote(this, "RALLY / Attack x1.35 for 10s");
            return;
        }
        if (sbDistanceXZ(hero, boss) > 3.0) {
            sbNote(this, "Miss / Colossus outside 3m");
            return;
        }
        local amount = playerStats.getFinal("Attribute.Attack", 0.0) * s.damage
            * bossStats.getFinal("Attribute.DamageTaken", 1.0);
        local hit = sbRequire(combat.applyDamage(playerSubject, bossSubject, "Damage.Physical.Slash",
            amount, 8.0, 0.0, 0.0, 0.0), "apply GAS damage");
        lastDamage = hit.appliedHealthDamage;
        stats.gas_hits += 1;
        // Simulation remains HP authority for the raid; CombatRuntime records the native hit.
        damage(0, bossId, lastDamage);
        hero.strike = 1.0;
        if (lastDamage > 0.0) applyExposed();
        sbNote(this, format("GAS hit %.1f / boss %.0f HP", lastDamage, boss.hp));
    }

    function decide() {
        foreach (u in units) if (u.agent != null) {
            local c = sbPlan(this, u);
            if (c.kind == "follow" && u.pending != null && SB_SKILLS[u.pending.skill].kind == "attack")
                cancel(u);
            if (c.kind == "dodge" && u.dodgeTime <= 0.0) {
                cancel(u);
                u.dodgeTime = 0.42;
                u.dodgeX = c.x;
                u.dodgeZ = c.z;
                stats.dodges += 1;
                sbNote(this, u.name + " / EQS evade " + format("(%.1f,%.1f)", c.x, c.z));
            }
            if (u.dodgeTime > 0.0 && u.hp > 0.0)
                c = sbCandidate("dodge", u.id, 1000.0, "Finish evasive movement");
            local changed = u.choice == null || u.choice.kind != c.kind || u.choice.target != c.target;
            u.choice = c;
            if (c.kind != "dodge" && c.kind != "down" && u.pending == null
                && (changed || u.position == null || tick >= u.positionTick)) {
                local pick = sbEqsPickSupport(this, u, c);
                u.position = { x = pick.x, z = pick.z };
                u.positionReason = pick.reason;
                u.positionTick = tick + 18;
            }
            foreach (i, owner in claims) if (owner == u.id) claims[i] = -1;
            if (c.kind == "heal") claims[c.target] = u.id;
            sbRequire(world.setBlackboardString(u.agent, "intent", c.kind), "publish intent");
        }
        local report = sbRequire(world.tick(tick, 0.1, 8, 2), "tick AI");
        stats.transitions += report.transitionsApplied;
        foreach (u in units) if (u.agent != null) {
            u.state = sbRequire(world.snapshot(u.agent), "read AI state").activeState;
            if (u.state != u.choice.kind) {
                // Budgeted ticks can lag one agent; republish intent and keep host choice.
                sbRequire(world.setBlackboardString(u.agent, "intent", u.choice.kind), "republish intent");
                u.state = u.choice.kind;
            }
        }
    }

    function bossAct(dt) {
        local boss = units[bossId];
        local hero = units[0];
        if (boss.hp <= 0.0 || hero.hp <= 0.0) return;
        boss.enemyClock -= dt;
        if (boss.enemyClock <= 0.0) {
            boss.enemyClock = 3.8;
            // Prefer the densest living ally cluster; fall back to the player.
            local target = hero;
            local bestCrowd = -1.0;
            foreach (u in units) {
                if (u.team != 0 || u.hp <= 0.0) continue;
                local crowd = 0.0;
                foreach (other in units)
                    if (other.team == 0 && other.hp > 0.0 && sbDistanceXZ(u, other) < 2.4)
                        crowd += 1.0;
                if (crowd > bestCrowd) { bestCrowd = crowd; target = u; }
            }
            hazards.append({
                source = bossId, x = target.x, z = target.z,
                radius = 2.35, remaining = 1.2, total = 1.2, resolved = false
            });
            stats.boss_slams += 1;
            sbNote(this, "COLOSSUS / slam telegraph on " + target.name);
        }
        if (sbDistanceXZ(boss, hero) > 4.5) move(boss, hero.x, hero.z, 0.9, dt);
        boss.yaw = atan2(hero.x - boss.x, hero.z - boss.z);
    }

    function step(dt, input) {
        tick += 1;
        time += dt;
        sbRequire(effects.update(dt), "advance effects");
        drainEffectEvents();
        playerStats.setBase("Attribute.Energy",
            sbClamp(playerStats.getBase("Attribute.Energy", 0.0) + dt * 5.0, 0.0, 100.0));

        foreach (u in units) {
            foreach (i, cd in u.cooldowns) u.cooldowns[i] = sbMax(0.0, cd - dt);
            u.mana = sbMin(100.0, u.mana + manaRegen * dt);
            u.flash = sbMax(0.0, u.flash - dt);
            u.strike = sbMax(0.0, u.strike - dt * 2.5);
            u.shieldTime -= dt;
            if (u.shieldTime <= 0.0) u.shield = 0.0;
        }

        local hero = units[0];
        local oldX = hero.x;
        local oldZ = hero.z;
        if (hero.hp > 0.0) {
            if (input.x != 0.0 || input.z != 0.0) {
                auto = false;
                move(hero, hero.x + input.x * 4.0, hero.z + input.z * 4.0, 3.2, dt);
            } else if (auto) {
                local boss = units[bossId];
                // Close to melee range so the GAS showcase lands; weave slightly on approach.
                local ax = boss.x + cos(time * 0.7) * 2.2;
                local az = boss.z + sin(time * 0.55) * 2.2;
                move(hero, ax, az, 2.6, dt);
                if (sbDistanceXZ(hero, boss) < 2.8) {
                    if (tick % 90 == 30) activatePlayerSkill(0);
                    if (tick % 180 == 90) activatePlayerSkill(2);
                    if (tick % 150 == 60) activatePlayerSkill(1);
                }
            }
            if (input.attack) activatePlayerSkill(0);
        }
        hero.vx = (hero.x - oldX) / dt;
        hero.vz = (hero.z - oldZ) / dt;

        bossAct(dt);
        if (tick % 6 == 0) decide();

        foreach (u in units) {
            if (u.hp <= 0.0) continue;
            local startX = u.x;
            local startZ = u.z;
            if (u.agent != null && u.choice != null) {
                local c = u.choice;
                if (u.state == "dodge") {
                    move(u, u.dodgeX, u.dodgeZ, 6.2, dt);
                    u.dodgeTime = sbMax(0.0, u.dodgeTime - dt);
                } else if (u.pending == null && u.state == "follow") {
                    if (u.position != null && sbDistanceXZ(u, u.position) > 0.2)
                        move(u, u.position.x, u.position.z, sbMoveSpeed(this, u), dt);
                } else if (u.pending == null
                    && (u.state == "attack" || u.state == "heal" || u.state == "guard")
                    && c.skill >= 0 && c.target >= 0 && c.target < units.len()) {
                    local t = units[c.target];
                    local s = SB_SKILLS[c.skill];
                    if (t.hp <= 0.0) { cancel(u); continue; }
                    local ready = sbCastReach(u, t, c.skill)
                        && (s.kind != "attack" || sbCanAttackHere(this, u));
                    if (ready) start(u, c);
                    if (u.pending == null && u.position != null && sbDistanceXZ(u, u.position) > 0.2)
                        move(u, u.position.x, u.position.z, sbMoveSpeed(this, u), dt);
                }
            }
            if (u.team == 0 && u.id != 0) {
                u.vx = (u.x - startX) / dt;
                u.vz = (u.z - startZ) / dt;
            }
            if (u.pending != null) {
                u.pending.remaining -= dt;
                if (u.pending.remaining <= 0.0) finish(u);
            }
        }

        for (local i = hazards.len() - 1; i >= 0; i--) {
            local h = hazards[i];
            h.remaining -= dt;
            if (units[h.source].hp <= 0.0 && !h.resolved) { hazards.remove(i); continue; }
            if (h.remaining <= 0.0 && !h.resolved) {
                h.resolved = true;
                foreach (u in units)
                    if (u.team == 0 && sbDistanceXZ(u, h) < h.radius + 0.25)
                        damage(h.source, u.id, 34.0);
            }
            if (h.remaining < -0.4) hazards.remove(i);
        }

        local advanced = sbRequire(abilities.advanceAbilities(tick, dt), "advance abilities");
        phase = "ready";
        foreach (execution in advanced.advances) {
            phase = execution.phase;
            if (execution.phase == "active") units[0].strike = 1.0;
            if (execution.activationId in pendingGas) {
                local record = pendingGas[execution.activationId];
                local reached = execution.phase == "active" || execution.phase == "recover"
                    || execution.phase == "completed";
                if (!record.consumed && reached) {
                    record.consumed = true;
                    consumeGas(record);
                }
                if (execution.phase == "completed" || execution.phase == "cancelled"
                    || execution.phase == "failed")
                    delete pendingGas[execution.activationId];
            }
        }
        foreach (s in playerSkills)
            s.cooldown = sbRequire(abilities.abilityGrant(s.grant), "read cooldown").cooldownSeconds;

        // Keep CombatRuntime fighters near the simulation authority for native damage reads.
        sbRequire(combat.stop(playerSubject), "stop player combat mover");
        sbRequire(combat.stop(bossSubject), "stop boss combat mover");
        sbRequire(combat.advance(tick, dt), "advance combat");
    }
}
