// Main-thread simulation owner. Native bodies/fixtures live as long as this game.
// Rendering and MCP receive derived observations; only Box2D owns player motion.
class SkiGame {
    course = null;
    physicsOwner = null;
    world = null;
    body = null;
    contacts = null;
    obstacles = null;
    obstacleByTag = null;
    controls = null;
    particles = null;
    trails = null;
    phase = "ready";
    stepSeconds = 1.0 / 120.0;
    accumulator = 0.0;
    tick = 0;
    elapsed = 0.0;
    x = 0.0;
    y = 0.0;
    speed = 0.0;
    vx = 0.0;
    vy = 0.0;
    altitude = 0.0;
    cameraY = 0.0;
    grounded = false;
    airborne = false;
    coyote = 0.0;
    jumpBuffer = 0.0;
    jumpLock = 0.0;
    flightTime = 0.0;
    takeoffAngle = 0.0;
    takeoffRamp = -1;
    lastGroundRamp = -1;
    rotation = 0.0;
    poseAngle = 0.0;
    coins = 0;
    flips = 0;
    crashes = 0;
    jumps = 0;
    rampJumps = 0;
    bestScore = 0;
    stage = 0;
    avalanche = 680.0;
    invincible = 0.0;
    message = "";
    messageTime = 0.0;
    lastTrailX = -100.0;

    constructor(layout) {
        course = layout;
        contacts = {};
        obstacles = [];
        obstacleByTag = {};
        controls = { tuck = false, flip = false, brake = false };
        particles = [];
        trails = [];
        buildWorld();
        reset("ready");
    }

    function buildWorld() {
        physicsOwner = eve.Physics();
        world = physicsOwner.newWorld(0.0, 980.0, false);
        world.setMeter(course.pixelsPerMeter);
        local width = course.segmentWidth;
        for (local px = -800.0; px < course.finish + 1800.0; px += width) {
            local mid = px + width * 0.5;
            local y1 = course.segmentHeight(px, mid);
            local y2 = course.segmentHeight(px + width, mid);
            local angle = atan2(y2 - y1, width);
            local snow = world.newBody("static", mid - sin(angle) * 20.0,
                (y1 + y2) * 0.5 + cos(angle) * 20.0);
            snow.setAngle(angle);
            snow.newRectangleFixture(sqrt(width * width + (y2-y1) * (y2-y1)) + 0.3,
                40.0, 0.0, 0.035, 0.0).setTag("snow");
        }
        body = world.newBody("dynamic", 0.0, course.height(0.0) - 18.0);
        body.newCircleFixture(12.0, 1.0, 0.015, 0.025).setTag("skier");
        body.setFixedRotation(true);
        body.setBullet(true);

        for (local i = 0; i < 57; ++i) {
            local px = 1100.0 + i * 620.0;
            local kind = i % 4 == 2 ? "ramp" : (i % 3 == 0 ? "tree" : "rock");
            local item = { x = px, kind = kind, hit = false, collected = false };
            if (kind != "ramp") {
                local obstacle = world.newBody("static", px,
                    course.height(px) - (kind == "tree" ? 38.0 : 16.0));
                item.fixture <- kind == "tree"
                    ? obstacle.newRectangleFixture(20.0, 76.0, 0.0, 0.25, 0.05)
                    : obstacle.newCircleFixture(24.0, 0.0, 0.15, 0.12);
                local tag = "obstacle" + i;
                item.fixture.setTag(tag);
                obstacleByTag[tag] <- item;
            }
            obstacles.append(item);
            for (local j = 0; j < 5; ++j) {
                obstacles.append({ x = px - 180.0 + j * 65.0, kind = "coin",
                    height = 58.0 + sin(j * SKI_PI / 4.0) * 80.0,
                    hit = false, collected = false });
            }
        }
    }

    function reset(nextPhase) {
        phase = nextPhase;
        accumulator = 0.0; tick = 0; elapsed = 0.0;
        coins = 0; flips = 0; crashes = 0; jumps = 0; rampJumps = 0; stage = 0;
        rotation = 0.0; flightTime = 0.0; takeoffAngle = atan(course.slope(0.0));
        poseAngle = takeoffAngle;
        airborne = false; grounded = false; coyote = 0.0; jumpBuffer = 0.0; jumpLock = 0.0;
        takeoffRamp = -1; lastGroundRamp = -1;
        avalanche = 680.0; invincible = 0.0;
        controls.tuck = false; controls.flip = false; controls.brake = false;
        particles.clear(); trails.clear(); contacts.clear(); lastTrailX = -100.0;
        // Deactivate first: restart on the same snow segment must regenerate contacts.
        body.setActive(false);
        body.setPosition(0.0, course.height(0.0) - 18.0);
        body.setLinearVelocity(130.0, course.slope(0.0) * 130.0);
        body.setAngularVelocity(0.0);
        foreach (item in obstacles) {
            item.hit = false; item.collected = false;
            if ("fixture" in item) item.fixture.setSensor(false);
        }
        body.setActive(true);
        world.clearContactEvents();
        refreshMotion();
        cameraY = course.height(0.0) - 360.0;
        announce("LET GRAVITY LEAD THE WAY");
    }

    function refreshMotion() {
        x = body.getX(); y = body.getY();
        vx = body.getLinearVelocityX(); vy = body.getLinearVelocityY();
        speed = body.getLinearSpeed();
        altitude = skiMax(0.0, course.height(x) - y - 13.0);
    }

    function score() {
        return (skiMin(course.finish, skiMax(0.0, x)) / course.pixelsPerMeter).tointeger()
            + coins * 25 + flips * 250;
    }

    function announce(text) { message = text; messageTime = 2.0; }

    function togglePause() {
        if (phase == "riding") phase = "paused";
        else if (phase == "paused") phase = "riding";
    }

    function pressJump() {
        if (phase == "ready" || phase == "won" || phase == "lost") { reset("riding"); return; }
        if (phase == "riding") jumpBuffer = 0.14;
    }

    function launch() {
        local angle = grounded ? atan(course.slope(x)) : takeoffAngle;
        local mass = body.getMass();
        body.applyLinearImpulse(sin(angle) * 520.0 * mass, -cos(angle) * 520.0 * mass);
        grounded = false; airborne = true; coyote = 0.0; jumpBuffer = 0.0; jumpLock = 0.08;
        flightTime = 0.0; rotation = 0.0; takeoffAngle = angle; takeoffRamp = -1;
        poseAngle = angle;
        jumps += 1;
        burst(x, y + 12.0, 10, false);
    }

    function burst(px, py, count, gold) {
        // A bounded, deterministic cosmetic stream; it never affects physics or scoring.
        for (local i = 0; i < count; ++i) {
            if (particles.len() >= 160) particles.remove(0);
            local angle = (i * 2.399963 + tick * 0.17);
            particles.append({ x = px, y = py, vx = cos(angle) * (35.0 + i * 6.0),
                vy = -70.0 - skiAbs(sin(angle)) * 95.0, life = 0.65, gold = gold });
        }
    }

    function crash(reason) {
        if (invincible > 0.0) return;
        crashes += 1;
        body.setLinearVelocity(skiMax(120.0, body.getLinearVelocityX() * 0.58),
            skiMin(body.getLinearVelocityY(), -220.0));
        avalanche -= 150.0; invincible = 1.8;
        burst(x, y, 20, false);
        announce(reason + " / KEEP GOING");
    }

    function consumeContacts() {
        // All fixture changes occur after update(), never from a physics callback.
        for (local i = 0; i < world.getEndContactCount(); ++i) {
            local a = world.getEndContactFixtureATag(i), b = world.getEndContactFixtureBTag(i);
            if ((a == "skier" && b == "snow") || (b == "skier" && a == "snow")) {
                local id = a == "skier" ? world.getEndContactBodyBId(i) : world.getEndContactBodyAId(i);
                if (id in contacts) delete contacts[id];
            }
        }
        for (local i = 0; i < world.getBeginContactCount(); ++i) {
            local a = world.getBeginContactFixtureATag(i), b = world.getBeginContactFixtureBTag(i);
            if (a != "skier" && b != "skier") continue;
            local tag = a == "skier" ? b : a;
            if (tag == "snow") {
                local id = a == "skier" ? world.getBeginContactBodyBId(i) : world.getBeginContactBodyAId(i);
                contacts[id] <- true;
            } else if (tag in obstacleByTag) {
                local item = obstacleByTag[tag];
                if (!item.hit) {
                    item.hit = true;
                    item.fixture.setSensor(true);
                    crash(item.kind == "tree" ? "WATCH THE PINES" : "WATCH THE ROCKS");
                }
            }
        }
        world.clearContactEvents();
    }

    function step() {
        tick += 1; elapsed += stepSeconds;
        messageTime = skiMax(0.0, messageTime - stepSeconds);
        invincible = skiMax(0.0, invincible - stepSeconds);
        jumpLock = skiMax(0.0, jumpLock - stepSeconds);
        coyote = grounded ? 0.08 : skiMax(0.0, coyote - stepSeconds);
        if (jumpBuffer > 0.0 && coyote > 0.0) launch();
        jumpBuffer = skiMax(0.0, jumpBuffer - stepSeconds);

        local wasGrounded = grounded;
        local drag = controls.brake ? 3.0 : (controls.tuck ? 0.3 : 0.62);
        local mass = body.getMass();
        body.applyForce(-body.getLinearVelocityX() * drag * mass,
            -body.getLinearVelocityY() * drag * mass * 0.1);
        world.update(stepSeconds);
        refreshMotion();
        consumeContacts();
        grounded = contacts.len() > 0 && jumpLock <= 0.0;

        if (wasGrounded && !grounded && !airborne) {
            takeoffAngle = atan(course.slope(x - vx * stepSeconds));
            takeoffRamp = lastGroundRamp;
            rotation = 0.0; flightTime = 0.0;
            poseAngle = takeoffAngle;
        }
        if (!grounded) {
            flightTime += stepSeconds;
            if (flightTime > 0.08) {
                airborne = true;
                if (takeoffRamp >= 0) { rampJumps += 1; takeoffRamp = -1; announce("NATURAL TAKEOFF"); }
            }
            if (controls.flip && airborne) {
                rotation += 6.4 * stepSeconds;
                poseAngle = takeoffAngle - rotation;
            } else {
                // Releasing the trick input lets the skier recover towards the coming slope.
                poseAngle += skiAngle(atan(course.slope(x)) - poseAngle) * stepSeconds * 6.0;
            }
        } else {
            lastGroundRamp = course.rampAt(x);
            if (airborne && flightTime > 0.08) {
                local landingError = skiAngle(poseAngle - atan(course.slope(x)));
                local turns = (rotation / (2.0 * SKI_PI)).tointeger();
                if (skiAbs(landingError) > 0.85) crash("BAD LANDING");
                else if (turns > 0) {
                    flips += turns; avalanche = skiMin(920.0, avalanche + turns * 100.0);
                    announce("BACKFLIP +" + (turns * 250));
                    burst(x, y, 16, true);
                }
                burst(x, y + 12.0, 8, false);
            }
            airborne = false; flightTime = 0.0; rotation = 0.0;
            takeoffAngle = atan(course.slope(x));
            poseAngle = takeoffAngle;
        }

        foreach (item in obstacles) {
            if (item.kind != "coin" || item.collected || skiAbs(item.x - x) > 42.0) continue;
            local coinY = course.height(item.x) - item.height;
            if (skiAbs(y - 18.0 - coinY) < 38.0) {
                item.collected = true; coins += 1;
                avalanche = skiMin(920.0, avalanche + 12.0);
                burst(item.x, coinY, 5, true);
            }
        }
        if (grounded && x - lastTrailX >= 9.0) {
            trails.append({ x = x, y = course.height(x), angle = atan(course.slope(x)) });
            lastTrailX = x;
        }
        while (trails.len() > 60 || (trails.len() > 0 && trails[0].x < x - 420.0)) trails.remove(0);
        for (local i = particles.len() - 1; i >= 0; --i) {
            local p = particles[i];
            p.life -= stepSeconds; p.vy += 300.0 * stepSeconds;
            p.x += p.vx * stepSeconds; p.y += p.vy * stepSeconds;
            if (p.life <= 0.0) particles.remove(i);
        }

        refreshMotion(); // Contact responses may have changed velocity after the solver.
        avalanche = skiMin(920.0, avalanche + (skiMax(0.0, vx) - (285.0 + skiMin(120.0, elapsed))) * stepSeconds);
        local nextStage = skiMin(2, (skiMax(0.0, x) / 12000.0).tointeger());
        if (nextStage > stage) { stage = nextStage; announce(course.stages[stage]); }
        cameraY += (course.baseHeight(x) - 360.0 - cameraY) * skiMin(1.0, stepSeconds * 8.0);
        if (avalanche <= 0.0) finishRun("lost");
        else if (x >= course.finish) finishRun("won");
    }

    function finishRun(outcome) {
        phase = outcome;
        bestScore = skiMax(bestScore, score());
        announce(outcome == "won" ? "THE LODGE IS IN SIGHT" : "THE AVALANCHE CAUGHT YOU");
        print("SKI_ADVENTURE_FINISH outcome=" + outcome + " score=" + score()
            + " coins=" + coins + " flips=" + flips + "\n");
    }

    function update(dt) {
        if (phase != "riding") return;
        // Bounded catch-up: overload slows simulation instead of tunnelling through snow.
        accumulator += skiMin(0.1, skiMax(0.0, dt));
        while (accumulator >= stepSeconds && phase == "riding") {
            accumulator -= stepSeconds;
            step();
        }
    }

    function snapshot() {
        local nextKind = "finish", nextDistance = course.finish - x;
        foreach (item in obstacles) {
            if (item.kind != "coin" && !item.hit && item.x > x + 28.0 && item.x - x < nextDistance) {
                nextKind = item.kind; nextDistance = item.x - x;
            }
        }
        return { phase = phase, tick = tick, elapsed = elapsed, distance = x, worldY = y,
            speed = speed, velocityX = vx, velocityY = vy, altitude = altitude, grounded = grounded,
            rotation = rotation, coins = coins, flips = flips, crashes = crashes, jumps = jumps,
            rampJumps = rampJumps, avalanche = avalanche, stage = stage + 1, score = score(),
            best = bestScore, playing = phase == "riding" || phase == "paused", paused = phase == "paused",
            finished = phase == "won" || phase == "lost", won = phase == "won", slope = course.slope(x),
            nextKind = nextKind, nextDistance = nextDistance, jumpQueued = jumpBuffer > 0.0 };
    }
}
