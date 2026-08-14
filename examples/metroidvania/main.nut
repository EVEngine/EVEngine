dofile("tuning.nut");

const CAT_TERRAIN = 1;
const CAT_PLAYER = 2;
const CAT_ENEMY = 4;
const CAT_PROP = 8;
const CAT_PLAYER_ATTACK = 16;
const CAT_PICKUP = 32;
const CAT_HAZARD = 64;

if (!("game" in getroottable())) game <- null;
if (!("prevKeys" in getroottable())) prevKeys <- {};

function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function absf(v) { return v < 0.0 ? -v : v; }

function startsWith(text, prefix) {
    return text.len() >= prefix.len() && text.slice(0, prefix.len()) == prefix;
}

function keyDown(primary, alternate = "") {
    return keyboard.isDown(primary) || (alternate != "" && keyboard.isDown(alternate));
}

function keyPressed(primary, alternate = "") {
    local now = keyDown(primary, alternate);
    local key = primary + ":" + alternate;
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- now;
    return now && !was;
}

function bodyPair(a, b) {
    return a < b ? (a + ":" + b) : (b + ":" + a);
}

function setFixtureFilter(fx, category, mask, tag, sensor = false) {
    fx.setCategoryBits(category);
    fx.setMaskBits(mask);
    fx.setTag(tag);
    fx.setSensor(sensor);
}

function makeSpine(model, animationName, scale) {
    local modelPath = "spine/" + model + "/export/";
    local data = anim.newSpineSkeletonDataFromFile(modelPath + model + ".json");
    local atlas = anim.newSpineAtlasFromFile(modelPath + model + ".atlas");
    if (data == null || atlas == null) return null;
    local skeleton = anim.newSpineSkeleton(data);
    local player = anim.newSpineAnim(skeleton);
    player.setAtlas(atlas);
    local textures = [];
    for (local i = 0; i < atlas.getPageCount(); i += 1) {
        local tex = gfx.newTextureFromFile(modelPath + atlas.getPageName(i));
        player.setPageTexture(i, tex);
        textures.push(tex);
    }
    player.setScale(scale, scale);
    player.setLoop(true);
    player.play(animationName);
    return { data = data, atlas = atlas, skeleton = skeleton, player = player,
             textures = textures, current = animationName };
}

function playSpine(holder, name, loop = true, restart = false, speed = 1.0) {
    if (holder == null || (holder.current == name && !restart)) return;
    holder.current = name;
    holder.player.setSpeed(speed);
    holder.player.setLoop(loop);
    holder.player.play(name);
}

function addBodyEntity(e) {
    game.byBody[e.body.getId()] <- e;
    game.entities.push(e);
    return e;
}

function spawnPlayer(x, y) {
    local body = game.world.newBody("dynamic", x, y - 28.0);
    body.setFixedRotation(true);
    body.setBullet(true);
    local main = body.newRectangleFixture(30.0, 54.0, 1.0, 0.0, 0.0);
    setFixtureFilter(main, CAT_PLAYER, CAT_TERRAIN | CAT_ENEMY | CAT_PROP | CAT_HAZARD,
                     "player");
    local foot = body.newRectangleFixtureAt(22.0, 7.0, 0.0, 29.0, 0.0, 0.0, 0.0);
    setFixtureFilter(foot, CAT_PLAYER, CAT_TERRAIN | CAT_PROP, "player_foot", true);
    local wallL = body.newRectangleFixtureAt(6.0, 34.0, -18.0, 0.0, 0.0, 0.0, 0.0);
    setFixtureFilter(wallL, CAT_PLAYER, CAT_TERRAIN, "player_wall_l", true);
    local wallR = body.newRectangleFixtureAt(6.0, 34.0, 18.0, 0.0, 0.0, 0.0, 0.0);
    setFixtureFilter(wallR, CAT_PLAYER, CAT_TERRAIN, "player_wall_r", true);
    local attackL = body.newRectangleFixtureAt(48.0, 42.0, -37.0, -2.0, 0.0, 0.0, 0.0);
    local attackR = body.newRectangleFixtureAt(48.0, 42.0, 37.0, -2.0, 0.0, 0.0, 0.0);
    setFixtureFilter(attackL, CAT_PLAYER_ATTACK, 0, "player_attack", true);
    setFixtureFilter(attackR, CAT_PLAYER_ATTACK, 0, "player_attack", true);
    local e = {
        kind = "player", body = body, fixtures = [main, foot, wallL, wallR, attackL, attackR],
        attackL = attackL, attackR = attackR, hp = TUNE.playerHp, maxHp = TUNE.playerHp,
        facing = 1, grounded = 0, wallLeft = 0, wallRight = 0,
        groundContacts = {}, wallLeftContacts = {}, wallRightContacts = {}, coyote = 0.0,
        jumpBuffer = 0.0, dashTimer = 0.0, dashReady = true, attackTimer = 0.0,
        wallJumpLock = 0.0, attackStartup = 0.0, attackActive = 0.0,
        attackEnabled = false, attackWindowDone = false, attackKind = "none", attackFixture = null,
        queuedAttack = false, queuedKick = false, combo = 0, comboGrace = 0.0, attackHits = {},
        invulnerable = 0.0, spine = makeSpine("hero", "Idle", TUNE.heroVisualScale)
    };
    return addBodyEntity(e);
}

function spawnEnemy(kind, x, y) {
    local body = game.world.newBody("dynamic", x + 16.0, y - 27.0);
    body.setFixedRotation(true);
    body.setBullet(true);
    local main = body.newRectangleFixture(31.0, 52.0, TUNE.enemyDensity, 0.0, 0.02);
    setFixtureFilter(main, CAT_ENEMY, CAT_TERRAIN | CAT_PLAYER | CAT_PROP | CAT_ENEMY |
                     CAT_PLAYER_ATTACK, "enemy");
    local foot = body.newRectangleFixtureAt(21.0, 7.0, 0.0, 28.0, 0.0, 0.0, 0.0);
    setFixtureFilter(foot, CAT_ENEMY, CAT_TERRAIN | CAT_PROP, "enemy_foot", true);
    local wall = body.newRectangleFixtureAt(37.0, 30.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    setFixtureFilter(wall, CAT_ENEMY, CAT_TERRAIN, "enemy_wall", true);
    local e = {
        kind = kind, body = body, fixtures = [main, foot, wall], hp = TUNE.enemyHp, maxHp = TUNE.enemyHp,
        stagger = 0.0, staggerDelay = 0.0, state = "normal", stateTimer = 0.0,
        attackCooldown = 0.3, attackDealt = false, impactCooldown = 0.0, facing = -1,
        grounded = 0, groundContacts = {}, wallContacts = {},
        spine = makeSpine("spineboy", "idle", TUNE.enemyVisualScale)
    };
    return addBodyEntity(e);
}

function spawnProp(kind, x, y) {
    local isRock = kind == "prop_rock";
    local body = game.world.newBody("dynamic", x + 16.0, y - 17.0);
    body.setBullet(true);
    local fx = isRock
        ? body.newCircleFixture(15.0, TUNE.rockDensity, TUNE.frictionRock, TUNE.restitutionProp)
        : body.newRectangleFixture(31.0, 31.0, TUNE.crateDensity, TUNE.frictionCrate,
                                   TUNE.restitutionProp);
    setFixtureFilter(fx, CAT_PROP, CAT_TERRAIN | CAT_PLAYER | CAT_ENEMY | CAT_PROP |
                     CAT_PLAYER_ATTACK, isRock ? "prop_rock" : "prop_crate");
    local e = {
        kind = kind, body = body, fixtures = [fx], hp = isRock ? 999999.0 : TUNE.crateHp,
        maxHp = isRock ? 999999.0 : TUNE.crateHp, impactCooldown = 0.0,
        texture = isRock ? game.rockTexture : game.crateTexture, dead = false
    };
    return addBodyEntity(e);
}

function spawnSensor(kind, x, y, w, h) {
    local body = game.world.newBody("static", x + w * 0.5, y - h * 0.5);
    local fx = body.newRectangleFixture(w, h, 0.0, 0.0, 0.0);
    setFixtureFilter(fx, kind == "hazard" ? CAT_HAZARD : CAT_PICKUP, CAT_PLAYER, kind, true);
    return addBodyEntity({ kind = kind, body = body, fixtures = [fx], dead = false });
}

function spawnMovingPlatform(x, y, w, h) {
    local body = game.world.newBody("kinematic", x + w * 0.5, y - h * 0.5);
    local fx = body.newRectangleFixture(w, h, 0.0, TUNE.frictionTerrain, 0.0);
    setFixtureFilter(fx, CAT_TERRAIN, CAT_PLAYER | CAT_ENEMY | CAT_PROP, "terrain");
    return addBodyEntity({ kind = "moving_platform", body = body, fixtures = [fx], time = 0.0,
                           speed = 130.0 });
}

function spawnBoss(x, y) {
    local body = game.world.newBody("kinematic", x + 48.0, y - 48.0);
    local fx = body.newRectangleFixture(94.0, 70.0, 1.0, 0.3, 0.03);
    setFixtureFilter(fx, CAT_ENEMY, CAT_PLAYER | CAT_PROP | CAT_PLAYER_ATTACK, "boss");
    local e = {
        kind = "boss", body = body, fixtures = [fx], hp = TUNE.bossHp, maxHp = TUNE.bossHp,
        stagger = 0.0, staggerDelay = 0.0, state = "normal", stateTimer = 0.0,
        impactCooldown = 0.0, phase = 1, time = 0.0,
        homeX = x + 48.0, homeY = y - 48.0,
        spine = makeSpine("dragon", "flying", TUNE.bossVisualScale)
    };
    game.boss = e;
    return addBodyEntity(e);
}

function buildTerrain(layer) {
    local w = layer.getMapWidth();
    local h = layer.getMapHeight();
    local used = [];
    for (local i = 0; i < w * h; i += 1) used.push(false);
    for (local y = 0; y < h; y += 1) {
        for (local x = 0; x < w; x += 1) {
            local idx = y * w + x;
            if (used[idx] || layer.getTile(x, y) == 0) continue;
            local rw = 1;
            while (x + rw < w && !used[y * w + x + rw] && layer.getTile(x + rw, y) != 0)
                rw += 1;
            local rh = 1;
            local extend = true;
            while (y + rh < h && extend) {
                for (local xx = 0; xx < rw; xx += 1) {
                    if (used[(y + rh) * w + x + xx] || layer.getTile(x + xx, y + rh) == 0) {
                        extend = false;
                        break;
                    }
                }
                if (extend) rh += 1;
            }
            for (local yy = 0; yy < rh; yy += 1)
                for (local xx = 0; xx < rw; xx += 1)
                    used[(y + yy) * w + x + xx] = true;
            local tw = layer.getTileWidth();
            local th = layer.getTileHeight();
            local body = game.world.newBody("static", (x + rw * 0.5) * tw, (y + rh * 0.5) * th);
            local fx = body.newRectangleFixture(rw * tw, rh * th, 0.0, TUNE.frictionTerrain, 0.0);
            setFixtureFilter(fx, CAT_TERRAIN, CAT_PLAYER | CAT_ENEMY | CAT_PROP, "terrain");
            game.staticBodies.push({ body = body, fixture = fx });
        }
    }
}

function spawnMapObjects() {
    for (local i = 0; i < map.getObjectCount(); i += 1) {
        local kind = map.getObjectType(i);
        local x = map.getObjectX(i);
        local y = map.getObjectY(i);
        local w = map.getObjectWidth(i);
        local h = map.getObjectHeight(i);
        if (kind == "player") game.player = spawnPlayer(x + 16.0, y);
        else if (kind == "enemy_melee" || kind == "enemy_leaper") spawnEnemy(kind, x, y);
        else if (kind == "prop_crate" || kind == "prop_rock") spawnProp(kind, x, y);
        else if (kind == "moving_platform") spawnMovingPlatform(x, y, w, h);
        else if (kind == "boss") spawnBoss(x, y);
        else if (kind == "checkpoint" || kind == "ability_walljump" || kind == "ability_dash" ||
                 kind == "exit")
            spawnSensor(kind, x, y, w, h);
    }
    spawnSensor("hazard", 84.0 * 32.0, 20.0 * 32.0, 4.0 * 32.0, 32.0);
}

function resetRun(preserveAbilities) {
    local wall = preserveAbilities && game.hasWallJump;
    local dash = preserveAbilities && game.hasDash;
    if (game.world != null) game.world.destroy();
    game.world = physics.newWorld(0.0, TUNE.gravity, true);
    game.entities = [];
    game.byBody = {};
    game.staticBodies = [];
    game.activeImpacts = {};
    game.contactCounts = {};
    game.effects = [];
    game.boss = null;
    game.hasWallJump = wall;
    game.hasDash = dash;
    buildTerrain(game.collisionLayer);
    spawnMapObjects();
    game.player.body.setPosition(game.checkpointX, game.checkpointY);
}

function beginAttack(stage, kick) {
    local p = game.player;
    p.attackL.setMaskBits(0);
    p.attackR.setMaskBits(0);
    p.combo = stage;
    p.attackKind = kick ? "kick" : "attack";
    p.attackTimer = kick ? TUNE.kickDuration : TUNE.attackDuration[stage - 1];
    p.attackStartup = kick ? TUNE.kickStartup : TUNE.attackStartup[stage - 1];
    p.attackActive = kick ? TUNE.kickActive : TUNE.attackActive[stage - 1];
    p.attackEnabled = false;
    p.attackWindowDone = false;
    p.attackFixture = p.facing > 0 ? p.attackR : p.attackL;
    p.comboGrace = 0.0;
    p.attackHits = {};
    p.attackFixture.setTag(kick ? "player_kick" : ("player_attack_" + stage));
    local animSpeed = kick ? 0.88 : (stage == 1 ? 1.15 : (stage == 2 ? 1.0 : 0.82));
    playSpine(p.spine, "Attack", false, true, animSpeed);
}

function damageActor(e, damage, stagger, impulseX, impulseY) {
    if (e == null || e.hp <= 0.0 || e.state == "dying" || e.state == "dead" ||
        e.impactCooldown > 0.0) return;
    e.hp -= damage;
    e.stagger += stagger;
    e.staggerDelay = TUNE.staggerDecayDelay;
    e.impactCooldown = TUNE.impactInvulnerability;
    game.hitStop = damage >= 12.0 ? 0.04 : 0.018;
    if (e.hp <= 0.0) {
        e.hp = 0.0;
        if (e.kind == "boss") {
            e.state = "dead";
            game.message = "Dragon defeated - reach the opened gate";
            game.messageTimer = 6.0;
        } else {
            e.state = "dying";
            e.stateTimer = TUNE.enemyDeathTime;
            e.body.setLinearVelocity(0.0, 0.0);
            playSpine(e.spine, "death", false, true);
        }
        return;
    }
    if (e.kind == "boss") {
        e.body.applyLinearImpulse(impulseX, impulseY);
        if (e.stagger >= TUNE.staggerThreshold * 2.0) {
            e.state = "knocked_down";
            e.stateTimer = 2.4;
            e.stagger = 0.0;
        }
        return;
    }
    if (e.stagger >= TUNE.staggerThreshold) {
        e.body.applyLinearImpulse(impulseX, impulseY);
        e.state = "launched";
        e.stateTimer = 0.0;
        e.stagger = 0.0;
        playSpine(e.spine, "jump", true, true);
    } else {
        e.body.applyLinearImpulse(impulseX * 0.55, impulseY * 0.22);
        e.state = "hit_stun";
        e.stateTimer = 0.18;
        playSpine(e.spine, "hit", false, true);
    }
}

function handleAttackContact(aId, aTag, bId, bTag) {
    local attackId = 0;
    local targetId = 0;
    local tag = "";
    if (startsWith(aTag, "player_attack") || aTag == "player_kick") {
        attackId = aId; targetId = bId; tag = aTag;
    } else if (startsWith(bTag, "player_attack") || bTag == "player_kick") {
        attackId = bId; targetId = aId; tag = bTag;
    }
    if (attackId == 0 || attackId != game.player.body.getId()) return;
    if (!(targetId in game.byBody) || (targetId in game.player.attackHits)) return;
    local target = game.byBody[targetId];
    if (target.kind == "checkpoint" || target.kind == "ability_walljump" ||
        target.kind == "ability_dash" || target.kind == "exit") return;
    game.player.attackHits[targetId] <- true;
    local dir = game.player.facing.tofloat();
    if (tag == "player_kick") {
        if (target.kind == "prop_crate" || target.kind == "prop_rock") {
            target.body.applyLinearImpulse(TUNE.kickImpulseX * dir, TUNE.kickImpulseY);
        } else if (target.kind == "enemy_melee" || target.kind == "enemy_leaper") {
            damageActor(target, 5.0, 36.0, TUNE.kickImpulseX * dir, TUNE.kickImpulseY);
        }
        return;
    }
    local stage = game.player.combo;
    if (target.kind == "enemy_melee" || target.kind == "enemy_leaper" || target.kind == "boss")
        damageActor(target, TUNE.attackDamage[stage - 1], TUNE.attackStagger[stage - 1],
                    TUNE.attackImpulseX[stage - 1] * dir, TUNE.attackImpulseY[stage - 1]);
}

function tagsAre(a, b, ta, tb) {
    return (a == ta && b == tb) || (a == tb && b == ta);
}

function contactOtherId(aId, aTag, bId, tag) { return aTag == tag ? bId : aId; }

function addContact(table, id) { if (!(id in table)) table[id] <- true; }
function removeContact(table, id) { if (id in table) delete table[id]; }

function processContactEvents() {
    local p = game.player;
    for (local i = 0; i < game.world.getBeginContactCount(); i += 1) {
        local aId = game.world.getBeginContactBodyAId(i);
        local bId = game.world.getBeginContactBodyBId(i);
        local a = game.world.getBeginContactFixtureATag(i);
        local b = game.world.getBeginContactFixtureBTag(i);
        local pairKey = bodyPair(aId, bId);
        game.contactCounts[pairKey] <- (pairKey in game.contactCounts) ?
            game.contactCounts[pairKey] + 1 : 1;
        if (tagsAre(a, b, "player_foot", "terrain") ||
            tagsAre(a, b, "player_foot", "prop_crate") || tagsAre(a, b, "player_foot", "prop_rock"))
            addContact(p.groundContacts, contactOtherId(aId, a, bId, "player_foot"));
        if (tagsAre(a, b, "player_wall_l", "terrain"))
            addContact(p.wallLeftContacts, contactOtherId(aId, a, bId, "player_wall_l"));
        if (tagsAre(a, b, "player_wall_r", "terrain"))
            addContact(p.wallRightContacts, contactOtherId(aId, a, bId, "player_wall_r"));
        if (a == "enemy_foot" || b == "enemy_foot") {
            local enemyId = a == "enemy_foot" ? aId : bId;
            if (enemyId in game.byBody)
                addContact(game.byBody[enemyId].groundContacts,
                           contactOtherId(aId, a, bId, "enemy_foot"));
        }
        if (a == "enemy_wall" || b == "enemy_wall") {
            local enemyId = a == "enemy_wall" ? aId : bId;
            if (enemyId in game.byBody)
                addContact(game.byBody[enemyId].wallContacts,
                           contactOtherId(aId, a, bId, "enemy_wall"));
        }
        handleAttackContact(aId, a, bId, b);
        local other = aId == p.body.getId() ? bId : (bId == p.body.getId() ? aId : 0);
        if (other != 0 && other in game.byBody) {
            local e = game.byBody[other];
            if (e.kind == "checkpoint") {
                game.checkpointX = e.body.getX(); game.checkpointY = e.body.getY() - 22.0;
                game.message = "Checkpoint awakened"; game.messageTimer = 2.0;
            } else if (e.kind == "ability_walljump" && !game.hasWallJump) {
                game.hasWallJump = true; e.dead = true; e.body.setActive(false);
                game.message = "Wall Jump acquired"; game.messageTimer = 3.0;
            } else if (e.kind == "ability_dash" && !game.hasDash) {
                game.hasDash = true; e.dead = true; e.body.setActive(false);
                game.message = "Air Dash acquired"; game.messageTimer = 3.0;
            } else if (e.kind == "exit") {
                if (game.boss != null && game.boss.state == "dead") game.won = true;
                else { game.message = "The dragon seals this gate"; game.messageTimer = 1.5; }
            } else if (e.kind == "hazard") {
                p.hp -= 20.0; p.body.setPosition(game.checkpointX, game.checkpointY);
                p.body.setLinearVelocity(0.0, 0.0);
                p.groundContacts.clear(); p.wallLeftContacts.clear(); p.wallRightContacts.clear();
                p.grounded = 0; p.wallLeft = 0; p.wallRight = 0; p.coyote = 0.0;
            }
        }
    }
    for (local i = 0; i < game.world.getEndContactCount(); i += 1) {
        local aId = game.world.getEndContactBodyAId(i);
        local bId = game.world.getEndContactBodyBId(i);
        local a = game.world.getEndContactFixtureATag(i);
        local b = game.world.getEndContactFixtureBTag(i);
        if (tagsAre(a, b, "player_foot", "terrain") ||
            tagsAre(a, b, "player_foot", "prop_crate") || tagsAre(a, b, "player_foot", "prop_rock"))
            removeContact(p.groundContacts, contactOtherId(aId, a, bId, "player_foot"));
        if (tagsAre(a, b, "player_wall_l", "terrain"))
            removeContact(p.wallLeftContacts, contactOtherId(aId, a, bId, "player_wall_l"));
        if (tagsAre(a, b, "player_wall_r", "terrain"))
            removeContact(p.wallRightContacts, contactOtherId(aId, a, bId, "player_wall_r"));
        if (a == "enemy_foot" || b == "enemy_foot") {
            local enemyId = a == "enemy_foot" ? aId : bId;
            if (enemyId in game.byBody)
                removeContact(game.byBody[enemyId].groundContacts,
                              contactOtherId(aId, a, bId, "enemy_foot"));
        }
        if (a == "enemy_wall" || b == "enemy_wall") {
            local enemyId = a == "enemy_wall" ? aId : bId;
            if (enemyId in game.byBody)
                removeContact(game.byBody[enemyId].wallContacts,
                              contactOtherId(aId, a, bId, "enemy_wall"));
        }
        local key = bodyPair(aId, bId);
        if (key in game.contactCounts) {
            game.contactCounts[key] -= 1;
            if (game.contactCounts[key] <= 0) {
                delete game.contactCounts[key];
                if (key in game.activeImpacts) delete game.activeImpacts[key];
            }
        }
    }
    p.grounded = p.groundContacts.len();
    p.wallLeft = p.wallLeftContacts.len();
    p.wallRight = p.wallRightContacts.len();
    foreach (e in game.entities)
        if ("groundContacts" in e && e.kind != "player") e.grounded = e.groundContacts.len();
}

function impactCarrier(e, preSolveSpeed) {
    if (e == null) return false;
    if (e.kind == "prop_crate" || e.kind == "prop_rock")
        return preSolveSpeed > TUNE.impactSpeedThreshold;
    return ("state" in e) && e.state == "launched";
}

function massDamageFactor(target, other) {
    if (target == null || other == null) return 1.0;
    local targetMass = target.body.getMass();
    local otherMass = other.body.getMass();
    if (targetMass <= 0.0 || otherMass <= 0.0) return 1.0;
    return clampf(otherMass / targetMass, 0.45, 1.8);
}

function processImpacts() {
    for (local i = 0; i < game.world.getImpactCount(); i += 1) {
        local aId = game.world.getImpactBodyAId(i);
        local bId = game.world.getImpactBodyBId(i);
        local key = bodyPair(aId, bId);
        if (key in game.activeImpacts) continue;
        local speed = game.world.getImpactRelativeNormalSpeed(i);
        local impulse = game.world.getImpactNormalImpulse(i);
        if (speed < TUNE.impactSpeedThreshold || impulse < TUNE.impactImpulseThreshold) continue;
        local a = (aId in game.byBody) ? game.byBody[aId] : null;
        local b = (bId in game.byBody) ? game.byBody[bId] : null;
        if (!impactCarrier(a, speed) && !impactCarrier(b, speed)) continue;
        local aWasLaunched = a != null && "state" in a && a.state == "launched";
        local bWasLaunched = b != null && "state" in b && b.state == "launched";
        game.activeImpacts[key] <- true;
        local damage = clampf(impulse * (speed - TUNE.impactSpeedThreshold) * TUNE.impactScale,
                              1.0, TUNE.impactDamageCap);
        local nx = game.world.getImpactNormalX(i);
        local ny = game.world.getImpactNormalY(i);
        if (a != null && (a.kind == "enemy_melee" || a.kind == "enemy_leaper" || a.kind == "boss"))
            damageActor(a, damage * massDamageFactor(a, b), damage * 2.2,
                        -nx * impulse * 0.25, -ny * impulse * 0.25);
        if (b != null && (b.kind == "enemy_melee" || b.kind == "enemy_leaper" || b.kind == "boss"))
            damageActor(b, damage * massDamageFactor(b, a), damage * 2.2,
                        nx * impulse * 0.25, ny * impulse * 0.25);
        if (a != null && a.kind == "prop_crate") {
            a.hp -= damage;
            if (impulse >= TUNE.crateBreakImpulse) a.hp = 0.0;
        }
        if (b != null && b.kind == "prop_crate") {
            b.hp -= damage;
            if (impulse >= TUNE.crateBreakImpulse) b.hp = 0.0;
        }
        if (aWasLaunched && a.state != "dying" && a.state != "dead") {
            a.state = "knocked_down"; a.stateTimer = TUNE.knockdownTime;
            playSpine(a.spine, "hit", false, true);
        }
        if (bWasLaunched && b.state != "dying" && b.state != "dead") {
            b.state = "knocked_down"; b.stateTimer = TUNE.knockdownTime;
            playSpine(b.spine, "hit", false, true);
        }
        game.shake = clampf(damage * 0.35, 2.0, 10.0);
        game.hitStop = damage > 18.0 ? 0.05 : (damage > 7.0 ? 0.022 : game.hitStop);
        local px = game.world.getImpactPointX(i);
        local py = game.world.getImpactPointY(i);
        for (local spark = 0; spark < 6; spark += 1) {
            local spread = spark.tofloat() - 2.5;
            game.effects.push({ x = px, y = py, vx = -nx * 120.0 + spread * 38.0,
                                vy = -ny * 120.0 - 75.0, life = 0.18 + spark * 0.015,
                                heavy = damage > 12.0 });
        }
    }
}

function updatePlayer(dt) {
    local p = game.player;
    if (p.hp <= 0.0 || p.body.getY() > 780.0) {
        resetRun(true);
        game.message = "Returned to checkpoint"; game.messageTimer = 2.0;
        return;
    }
    if (p.grounded > 0) { p.coyote = TUNE.coyoteTime; p.dashReady = true; }
    else p.coyote -= dt;
    if (p.wallJumpLock > 0.0) p.wallJumpLock -= dt;
    if (keyPressed("Space")) p.jumpBuffer = TUNE.jumpBuffer;
    else p.jumpBuffer -= dt;
    local move = 0.0;
    if (keyDown("A", "Left")) move -= 1.0;
    if (keyDown("D", "Right")) move += 1.0;
    // Keep the visual and the already-selected attack/dash fixture pointing in
    // the same direction until that action completes.
    if (move != 0.0 && p.attackTimer <= 0.0 && p.dashTimer <= 0.0)
        p.facing = move > 0.0 ? 1 : -1;

    if (p.jumpBuffer > 0.0) {
        if (p.coyote > 0.0) {
            p.body.setLinearVelocity(p.body.getLinearVelocityX(), -TUNE.jumpSpeed);
            p.groundContacts.clear(); p.grounded = 0;
            p.jumpBuffer = 0.0; p.coyote = 0.0;
        } else if (game.hasWallJump && (p.wallLeft > 0 || p.wallRight > 0)) {
            local dir = p.wallLeft > 0 ? 1.0 : -1.0;
            p.body.setLinearVelocity(TUNE.wallJumpX * dir, -TUNE.wallJumpY);
            p.facing = dir > 0.0 ? 1 : -1;
            p.wallJumpLock = TUNE.wallJumpLockTime;
            p.wallLeftContacts.clear(); p.wallRightContacts.clear();
            p.wallLeft = 0; p.wallRight = 0;
            p.jumpBuffer = 0.0; p.coyote = 0.0;
        }
    }
    if (game.hasDash && p.dashReady && keyPressed("Left Shift", "Right Shift")) {
        p.dashTimer = TUNE.dashTime; p.dashReady = false;
        p.body.setLinearVelocity(TUNE.dashSpeed * p.facing, 0.0);
    }
    if (p.dashTimer > 0.0) {
        p.dashTimer -= dt;
        p.body.setLinearVelocity(TUNE.dashSpeed * p.facing, 0.0);
    } else {
        local target = move * TUNE.moveSpeed;
        local vx = p.body.getLinearVelocityX();
        local acceleration = p.grounded > 0 ? TUNE.groundAcceleration : TUNE.airAcceleration;
        local nextVx = p.wallJumpLock > 0.0 ? vx :
            vx + (target - vx) * clampf(dt * acceleration, 0.0, 1.0);
        local nextVy = p.body.getLinearVelocityY();
        if (!keyDown("Space") && nextVy < -80.0)
            nextVy += TUNE.jumpCutAcceleration * dt;
        p.body.setLinearVelocity(nextVx, nextVy);
        local pressingIntoWall = (p.wallLeft > 0 && move < 0.0) || (p.wallRight > 0 && move > 0.0);
        if (game.hasWallJump && p.grounded == 0 && pressingIntoWall &&
            p.body.getLinearVelocityY() > TUNE.wallSlideSpeed)
            p.body.setLinearVelocity(p.body.getLinearVelocityX(), TUNE.wallSlideSpeed);
    }

    if (p.attackTimer <= 0.0) p.comboGrace -= dt;
    if (keyPressed("J")) {
        if (p.attackTimer > 0.0) p.queuedAttack = true;
        else beginAttack(p.comboGrace > 0.0 ? (p.combo % 3) + 1 : 1, false);
    }
    if (keyPressed("K")) {
        if (p.attackTimer > 0.0) p.queuedKick = true;
        else beginAttack(1, true);
    }
    if (p.attackTimer > 0.0) {
        p.attackTimer -= dt;
        if (!p.attackEnabled && !p.attackWindowDone) {
            p.attackStartup -= dt;
            if (p.attackStartup <= 0.0) {
                p.attackEnabled = true;
                p.attackFixture.setMaskBits(CAT_ENEMY | CAT_PROP);
            }
        } else {
            p.attackActive -= dt;
            if (p.attackActive <= 0.0) {
                p.attackEnabled = false;
                p.attackWindowDone = true;
                p.attackL.setMaskBits(0); p.attackR.setMaskBits(0);
            }
        }
        if (p.attackTimer <= 0.0) {
            p.attackL.setMaskBits(0); p.attackR.setMaskBits(0);
            p.attackEnabled = false;
            p.comboGrace = TUNE.comboGrace;
            if (p.queuedKick) {
                p.queuedKick = false; p.queuedAttack = false;
                beginAttack(1, true);
            } else if (p.queuedAttack) {
                p.queuedAttack = false;
                beginAttack((p.combo % 3) + 1, false);
            }
        }
    }
    if (p.invulnerable > 0.0) p.invulnerable -= dt;

    if (p.attackTimer <= 0.0) {
        p.spine.player.setSpeed(1.0);
        local vy = p.body.getLinearVelocityY();
        if (p.grounded > 0) playSpine(p.spine, absf(p.body.getLinearVelocityX()) > 25.0 ? "Run" : "Idle");
        else playSpine(p.spine, vy < 0.0 ? "Jump" : "Fall");
    }
}

function updateEnemies(dt) {
    local px = game.player.body.getX();
    local py = game.player.body.getY();
    foreach (e in game.entities) {
        if (!("state" in e) || e.kind == "player" || e.kind == "boss" || e.state == "dead") continue;
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

function updateBoss(dt) {
    local b = game.boss;
    if (b == null || b.state == "dead") return;
    if (b.impactCooldown > 0.0) b.impactCooldown -= dt;
    b.time += dt;
    b.phase = b.hp < b.maxHp * 0.33 ? 3 : (b.hp < b.maxHp * 0.66 ? 2 : 1);
    if (b.state == "knocked_down") {
        b.stateTimer -= dt;
        if (b.stateTimer <= 0.0) b.state = "normal";
        return;
    }
    local radiusX = b.phase == 1 ? 115.0 : 175.0;
    local radiusY = b.phase == 3 ? 105.0 : 65.0;
    b.body.setPosition(b.homeX + sin(b.time * (0.7 + b.phase * 0.16)) * radiusX,
                       b.homeY + sin(b.time * 1.35) * radiusY);
    if (absf(game.player.body.getX() - b.body.getX()) < 70.0 &&
        absf(game.player.body.getY() - b.body.getY()) < 65.0 && game.player.invulnerable <= 0.0) {
        game.player.hp -= 14.0 + b.phase * 2.0;
        game.player.invulnerable = 0.7;
        game.player.body.applyLinearImpulse((game.player.body.getX() < b.body.getX() ? -420.0 : 420.0), -180.0);
    }
}

function updatePlatforms(dt) {
    foreach (e in game.entities) {
        if (e.kind != "moving_platform") continue;
        e.time += dt;
        e.body.setLinearVelocity(cos(e.time * 1.35) * e.speed, 0.0);
    }
}

function updateEffects(dt) {
    for (local i = game.effects.len() - 1; i >= 0; i -= 1) {
        local effect = game.effects[i];
        effect.life -= dt;
        if (effect.life <= 0.0) { game.effects.remove(i); continue; }
        effect.x += effect.vx * dt;
        effect.y += effect.vy * dt;
        effect.vy += 620.0 * dt;
    }
}

function removeBrokenProps() {
    foreach (e in game.entities) {
        if (e.kind == "prop_crate" && !e.dead && e.hp <= 0.0) {
            e.dead = true; e.body.setActive(false);
            game.message = "Crate shattered by momentum"; game.messageTimer = 1.4;
        }
    }
}

function updateCamera(dt) {
    local worldWidth = 120.0 * 32.0;
    local worldHeight = 22.0 * 32.0;
    local targetX = worldWidth <= config.width ? worldWidth * 0.5 :
        clampf(game.player.body.getX(), config.width * 0.5, worldWidth - config.width * 0.5);
    local targetY = worldHeight <= config.height ? worldHeight * 0.5 :
        clampf(game.player.body.getY(), config.height * 0.5, worldHeight - config.height * 0.5);
    game.cameraX += (targetX - game.cameraX) * clampf(dt * 5.5, 0.0, 1.0);
    game.cameraY += (targetY - game.cameraY) * clampf(dt * 5.5, 0.0, 1.0);
    local shakeX = game.shake > 0.0 ? sin(game.time * 97.0) * game.shake : 0.0;
    local shakeY = game.shake > 0.0 ? cos(game.time * 83.0) * game.shake * 0.55 : 0.0;
    game.camera.setPosition(game.cameraX + shakeX, game.cameraY + shakeY);
    if (game.shake > 0.0) game.shake = clampf(game.shake - dt * 24.0, 0.0, 10.0);
}

function buildHud() {
    ui.beginBuild();
    ui.beginWindow("Momentum Ruins", "root");
    ui.progress(1.0, "hp", "");
    ui.text("HP", "hptext");
    ui.text("", "ability");
    ui.text("A/D move  Space jump  J combo  K kick  Shift dash", "help");
    ui.text("", "message");
    ui.progress(1.0, "boss", "");
    ui.text("", "bosstext");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostPos(16.0, 16.0, 0.0, 0.0);
}

function refreshHud() {
    ui.select("hud");
    ui.setValue("hp", clampf(game.player.hp / game.player.maxHp, 0.0, 1.0));
    ui.setText("hptext", "HP " + game.player.hp.tointeger() + "/" + game.player.maxHp.tointeger());
    ui.setText("ability", "Abilities: Wall Jump " + (game.hasWallJump ? "ON" : "--") +
        "   Air Dash " + (game.hasDash ? "ON" : "--"));
    ui.setText("message", game.won ? "RUINS CLEARED - press R to restart" : game.message);
    if (game.boss != null && game.boss.hp > 0.0 && game.player.body.getX() > 104.0 * 32.0) {
        ui.setValue("boss", game.boss.hp / game.boss.maxHp);
        ui.setText("bosstext", "Dragon " + game.boss.hp.tointeger() + "/" + game.boss.maxHp.tointeger());
    } else {
        ui.setValue("boss", 0.0); ui.setText("bosstext", "");
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.075, 0.11, 1.0);
    physics.setMeter(30.0);
    local spineRoot = fs.getSourceBaseDirectory() + "/../test/assets/spine";
    fs.allowMountingForPath(spineRoot);
    if (!fs.mountPath(spineRoot, "spine", false))
        print("warning: could not mount test Spine assets from " + spineRoot + "\n");
    map.loadFromFile("maps/world.json");
    local camera = eve.Camera2D();
    for (local i = 0; i < 4; i += 1) {
        local layer = map.getLayer(i);
        if (layer != null) layer.setCamera(camera);
    }
    game = {
        world = null, entities = [], byBody = {}, staticBodies = [], player = null, boss = null,
        collisionLayer = map.getLayer(1), camera = camera, cameraX = 640.0, cameraY = 360.0,
        checkpointX = 112.0, checkpointY = 548.0, hasWallJump = false, hasDash = false,
        activeImpacts = {}, contactCounts = {}, crateTexture = gfx.newTextureFromFile("assets/crate.png"),
        rockTexture = gfx.newTextureFromFile("assets/rock.png"),
        backgrounds = [gfx.newTextureFromFile("assets/background_forest.png"),
                       gfx.newTextureFromFile("assets/background_ruins.png"),
                       gfx.newTextureFromFile("assets/background_mine.png")],
        message = "Reach the ruins",
        messageTimer = 3.0, shake = 0.0, time = 0.0, hitStop = 0.0, accumulator = 0.0,
        effects = [], won = false
    };
    buildHud();
    resetRun(false);
};

function simulateStep(dt) {
    game.time += dt;
    if (game.won && keyPressed("R")) { game.won = false; resetRun(false); }
    game.world.clearContactEvents();
    updatePlayer(dt);
    updateEnemies(dt);
    updateBoss(dt);
    updatePlatforms(dt);
    game.world.updateFull(dt, 8, 3);
    processContactEvents();
    processImpacts();
    removeBrokenProps();
    updateEffects(dt);
    anim.update(dt);
    map.update(dt);
    updateCamera(dt);
    if (game.messageTimer > 0.0) game.messageTimer -= dt;
    else game.message = "";
    refreshHud();
}

eve_update = function(dt) {
    if (game.hitStop > 0.0) { game.hitStop -= clampf(dt, 0.0, 0.05); return; }
    game.accumulator += clampf(dt, 0.0, 0.05);
    local steps = 0;
    while (game.accumulator >= 1.0 / 60.0 && steps < 4) {
        simulateStep(1.0 / 60.0);
        game.accumulator -= 1.0 / 60.0;
        steps += 1;
        if (game.hitStop > 0.0) break;
    }
};

function screenX(wx) {
    return game.camera.worldToScreenX(wx, 0.0, config.width.tofloat(), config.height.tofloat());
}
function screenY(wy) {
    return game.camera.worldToScreenY(0.0, wy, config.width.tofloat(), config.height.tofloat());
}

eve_render = function() {
    gfx.clear();
    local region = game.player.body.getX() < 40.0 * 32.0 ? 0 : (game.player.body.getX() < 80.0 * 32.0 ? 1 : 2);
    local tintR = region == 0 ? 0.08 : (region == 1 ? 0.10 : 0.13);
    local tintG = region == 0 ? 0.16 : (region == 1 ? 0.10 : 0.09);
    gfx.drawTexturedRect(game.backgrounds[region], 0.0, 0.0,
                         config.width.tofloat(), config.height.tofloat(),
                         0.72, 0.72, 0.78, 1.0);
    gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), config.height.tofloat(),
                      tintR, tintG, 0.18, 0.45);
    map.render(gfx);

    foreach (e in game.entities) {
        if ("dead" in e && e.dead) continue;
        if (e.kind == "prop_crate" || e.kind == "prop_rock") {
            local size = e.kind == "prop_rock" ? 42.0 : 40.0;
            if (e.body.getLinearSpeed() > TUNE.impactSpeedThreshold) {
                local trailX = e.body.getX() - e.body.getLinearVelocityX() * 0.045;
                local trailY = e.body.getY() - e.body.getLinearVelocityY() * 0.045;
                gfx.drawSolidRect(screenX(trailX) - size * 0.35, screenY(trailY) - size * 0.35,
                                  size * 0.7, size * 0.7, 0.9, 0.7, 0.35, 0.30);
            }
            gfx.drawTexturedRect(e.texture, screenX(e.body.getX()) - size * 0.5,
                screenY(e.body.getY()) - size * 0.5, size, size, 1.0, 1.0, 1.0, 1.0);
        } else if (e.kind == "moving_platform") {
            gfx.drawSolidRect(screenX(e.body.getX()) - 48.0, screenY(e.body.getY()) - 12.0,
                              96.0, 24.0, 0.36, 0.42, 0.48, 1.0);
        } else if ("spine" in e && e.spine != null && (!("state" in e) || e.state != "dead")) {
            local sx = screenX(e.body.getX());
            local sy = screenY(e.body.getY());
            local facing = ("facing" in e) ? e.facing : 1;
            local baseScale = e.kind == "player" ? TUNE.heroVisualScale :
                (e.kind == "boss" ? TUNE.bossVisualScale : TUNE.enemyVisualScale);
            e.spine.player.setScale(baseScale * facing, baseScale);
            e.spine.player.setPosition(sx, sy + (e.kind == "boss" ? 20.0 : TUNE.actorVisualYOffset));
            if ((e.kind == "player" && e.invulnerable > 0.0) ||
                ("impactCooldown" in e && e.impactCooldown > 0.0))
                e.spine.player.setColor(1.0, 0.55, 0.48, 1.0);
            else e.spine.player.setColor(1.0, 1.0, 1.0, 1.0);
            e.spine.player.draw(gfx);
        }
    }
    foreach (effect in game.effects) {
        local radius = effect.heavy ? 7.0 : 4.0;
        gfx.drawSolidRect(screenX(effect.x) - radius, screenY(effect.y) - radius,
                          radius * 2.0, radius * 2.0, 1.0,
                          effect.heavy ? 0.42 : 0.76, 0.22,
                          clampf(effect.life * 4.0, 0.0, 1.0));
    }
    if (config.debug) game.world.drawDebug(gfx);
    ui.beginFrameAndRender();
};

eve_quit = function() {
    if (game != null && game.world != null) game.world.destroy();
};
