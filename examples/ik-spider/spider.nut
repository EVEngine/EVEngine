// Procedural 8-leg spider driven by eve.IK Skeleton2D + Solver2D (FABRIK).
// Alternating tetrapod gait: even / odd legs step in opposite phases while the
// body wanders between random waypoints.

function spiderClamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function spiderLen(x, y) {
    return sqrt(x * x + y * y);
}

function spiderNorm(x, y) {
    local L = spiderLen(x, y);
    if (L < 1e-5) return { x = 1.0, y = 0.0 };
    return { x = x / L, y = y / L };
}

function spiderMake(ikMod, cx, cy) {
    local sk = ikMod.newSkeleton2D();
    local solver = ikMod.newSolver2D();
    solver.setMaxIterations(24);
    solver.setTolerance(0.35);
    solver.setInfluence(1.0);

    // Rest pose: 8 radial legs, each hip → knee → foot (3 bones).
    // createBone reorders ids — build all bones, then re-read via getChild.
    local restAngles = [
        -2.70, -1.95, -1.20, -0.45,
         0.45,  1.20,  1.95,  2.70
    ];
    local hipLen = 22.0;
    local kneeLen = 34.0;
    local footLen = 30.0;
    for (local i = 0; i < 8; i++) {
        local hip = sk.createBone(0, hipLen);
        local knee = sk.createBone(hip, kneeLen);
        sk.createBone(knee, footLen);
    }

    local legs = [];
    for (local i = 0; i < 8; i++) {
        local hip = sk.getChild(0, i);
        local knee = sk.getChild(hip, 0);
        local foot = sk.getChild(knee, 0);
        local ang = restAngles[i];
        // Soft hinge limits keep the FABRIK pose spider-like.
        sk.setConstraints(hip, ang - 0.85, ang + 0.85);
        sk.setConstraints(knee, -1.55, -0.15);
        sk.setConstraints(foot, -0.9, 0.9);
        local reach = hipLen + kneeLen + footLen;
        local plantR = reach * 0.72;
        legs.push({
            index = i,
            hip = hip,
            knee = knee,
            foot = foot,
            restAngle = ang,
            plantR = plantR,
            stepHeight = 18.0,
            stepping = false,
            stepT = 0.0,
            stepDur = 0.22,
            fromX = 0.0,
            fromY = 0.0,
            toX = 0.0,
            toY = 0.0,
            plantX = 0.0,
            plantY = 0.0,
            targetX = 0.0,
            targetY = 0.0
        });
    }

    sk.setPosition(0, cx, cy);
    sk.setRotation(0, 0.0);
    for (local i = 0; i < legs.len(); i++) {
        local leg = legs[i];
        sk.setRotation(leg.hip, leg.restAngle);
        sk.setRotation(leg.knee, -0.85);
        sk.setRotation(leg.foot, 0.15);
    }
    sk.forwardKinematics();
    sk.bind();

    for (local i = 0; i < legs.len(); i++) {
        local leg = legs[i];
        leg.plantX = sk.getX(leg.foot);
        leg.plantY = sk.getY(leg.foot);
        leg.targetX = leg.plantX;
        leg.targetY = leg.plantY;
        leg.toX = leg.plantX;
        leg.toY = leg.plantY;
        leg.fromX = leg.plantX;
        leg.fromY = leg.plantY;
    }

    return {
        sk = sk,
        solver = solver,
        legs = legs,
        x = cx,
        y = cy,
        facing = 0.0,
        speed = 78.0,
        turnSpeed = 2.4,
        stepTrigger = 26.0,
        stepOvershoot = 14.0,
        bodyBob = 0.0,
        waypointX = cx + 180.0,
        waypointY = cy - 40.0,
        waypointTimer = 0.0,
        age = 0.0,
        steps = 0
    };
}

function spiderPickWaypoint(sp, width, height, margin) {
    local tries = 0;
    while (tries < 12) {
        local nx = margin + math.random() * (width - 2.0 * margin);
        local ny = margin + math.random() * (height - 2.0 * margin);
        local dx = nx - sp.x;
        local dy = ny - sp.y;
        if (spiderLen(dx, dy) > 140.0) {
            sp.waypointX = nx;
            sp.waypointY = ny;
            sp.waypointTimer = 0.0;
            return;
        }
        tries += 1;
    }
    sp.waypointX = width * 0.5;
    sp.waypointY = height * 0.5;
    sp.waypointTimer = 0.0;
}

function spiderIdealPlant(sp, leg) {
    local a = sp.facing + leg.restAngle;
    return {
        x = sp.x + cos(a) * leg.plantR,
        y = sp.y + sin(a) * leg.plantR
    };
}

function spiderBeginStep(sp, leg) {
    local ideal = spiderIdealPlant(sp, leg);
    local fx = cos(sp.facing);
    local fy = sin(sp.facing);
    // Plant slightly ahead of the rest offset so the push carries the body.
    leg.fromX = leg.plantX;
    leg.fromY = leg.plantY;
    leg.toX = ideal.x + fx * sp.stepOvershoot;
    leg.toY = ideal.y + fy * sp.stepOvershoot;
    leg.stepping = true;
    leg.stepT = 0.0;
    sp.steps += 1;
}

function spiderUpdateGait(sp, dt) {
    // Two groups: even legs step together, odd legs opposite.
    local groupNeed = [false, false];
    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        if (leg.stepping) continue;
        local ideal = spiderIdealPlant(sp, leg);
        local dx = leg.plantX - ideal.x;
        local dy = leg.plantY - ideal.y;
        if (spiderLen(dx, dy) > sp.stepTrigger) {
            groupNeed[i % 2] = true;
        }
    }

    // Never start a step for a group while the other group is mid-air —
    // keeps at least four feet planted (tetrapod).
    local otherBusy = [false, false];
    for (local i = 0; i < sp.legs.len(); i++) {
        if (sp.legs[i].stepping) otherBusy[1 - (i % 2)] = true;
    }

    for (local g = 0; g < 2; g++) {
        if (!groupNeed[g] || otherBusy[g]) continue;
        for (local i = 0; i < sp.legs.len(); i++) {
            if ((i % 2) != g) continue;
            local leg = sp.legs[i];
            if (leg.stepping) continue;
            local ideal = spiderIdealPlant(sp, leg);
            local dx = leg.plantX - ideal.x;
            local dy = leg.plantY - ideal.y;
            if (spiderLen(dx, dy) > sp.stepTrigger * 0.55) spiderBeginStep(sp, leg);
        }
    }

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        if (!leg.stepping) {
            leg.targetX = leg.plantX;
            leg.targetY = leg.plantY;
            continue;
        }
        leg.stepT += dt / leg.stepDur;
        local u = spiderClamp(leg.stepT, 0.0, 1.0);
        // Smoothstep for the horizontal plant; sin lift for the arc.
        local s = u * u * (3.0 - 2.0 * u);
        leg.targetX = leg.fromX + (leg.toX - leg.fromX) * s;
        leg.targetY = leg.fromY + (leg.toY - leg.fromY) * s;
        // 2D top-down: lift is a short inward tuck toward the body.
        local lift = sin(u * 3.14159265) * leg.stepHeight;
        local inward = spiderNorm(sp.x - leg.targetX, sp.y - leg.targetY);
        leg.targetX += inward.x * lift * 0.35;
        leg.targetY += inward.y * lift * 0.35;
        if (u >= 1.0) {
            leg.stepping = false;
            leg.plantX = leg.toX;
            leg.plantY = leg.toY;
            leg.targetX = leg.plantX;
            leg.targetY = leg.plantY;
        }
    }
}

function spiderSolveIK(sp) {
    local sk = sp.sk;
    local solver = sp.solver;
    sk.setPosition(0, sp.x, sp.y);
    sk.setRotation(0, sp.facing);
    // Seed a radial rest pose so FABRIK has a good starting configuration.
    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        sk.setRotation(leg.hip, leg.restAngle);
        sk.setRotation(leg.knee, -0.85);
        sk.setRotation(leg.foot, 0.15);
    }
    sk.forwardKinematics();

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        solver.clearTargets();
        solver.addTarget(leg.foot, leg.targetX, leg.targetY, 1.0);
        // Chain root = hip keeps the body root pinned and isolates each leg.
        solver.solveChain(sk, leg.hip, leg.foot);
    }
}

function spiderUpdate(sp, dt, width, height) {
    if (dt > 0.05) dt = 0.05;
    sp.age += dt;
    sp.waypointTimer += dt;

    local dx = sp.waypointX - sp.x;
    local dy = sp.waypointY - sp.y;
    local dist = spiderLen(dx, dy);
    if (dist < 48.0 || sp.waypointTimer > 7.5) {
        spiderPickWaypoint(sp, width, height, 90.0);
        dx = sp.waypointX - sp.x;
        dy = sp.waypointY - sp.y;
        dist = spiderLen(dx, dy);
    }

    local desired = atan2(dy, dx);
    local err = desired - sp.facing;
    while (err > 3.14159265) err -= 6.2831853;
    while (err < -3.14159265) err += 6.2831853;
    local maxTurn = sp.turnSpeed * dt;
    sp.facing += spiderClamp(err, -maxTurn, maxTurn);

    // Slow down while turning hard so feet can catch up.
    local turnFactor = 1.0 - spiderClamp(fabs(err) / 1.2, 0.0, 0.55);
    local step = sp.speed * turnFactor * dt;
    sp.x += cos(sp.facing) * step;
    sp.y += sin(sp.facing) * step;
    sp.x = spiderClamp(sp.x, 70.0, width - 70.0);
    sp.y = spiderClamp(sp.y, 70.0, height - 70.0);

    sp.bodyBob = sin(sp.age * 9.0) * 1.5;
    spiderUpdateGait(sp, dt);
    spiderSolveIK(sp);
}

function spiderDrawBone(gfx, ax, ay, bx, by, r, g, b, a, thickness) {
    local dx = bx - ax;
    local dy = by - ay;
    local L = spiderLen(dx, dy);
    if (L < 0.5) return;
    local steps = spiderClamp(floor(L / 3.0).tointeger(), 2, 48);
    local hx = thickness * 0.5;
    for (local i = 0; i <= steps; i++) {
        local t = i.tofloat() / steps.tofloat();
        local x = ax + dx * t;
        local y = ay + dy * t;
        gfx.drawSolidRect(x - hx, y - hx, thickness, thickness, r, g, b, a);
    }
}

function spiderDraw(sp, gfx) {
    local sk = sp.sk;
    // Soft ground shadow under the body.
    gfx.drawSolidRect(sp.x - 28.0, sp.y + 10.0, 56.0, 18.0, 0.05, 0.07, 0.09, 0.35);

    // Legs first (under body).
    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local hx = sk.getX(leg.hip);
        local hy = sk.getY(leg.hip);
        local kx = sk.getX(leg.knee);
        local ky = sk.getY(leg.knee);
        local fx = sk.getX(leg.foot);
        local fy = sk.getY(leg.foot);
        local shade = 0.55 + (i % 2) * 0.12;
        spiderDrawBone(gfx, sp.x, sp.y + sp.bodyBob, hx, hy, 0.42 * shade, 0.18, 0.14, 1.0, 7.0);
        spiderDrawBone(gfx, hx, hy, kx, ky, 0.55 * shade, 0.22, 0.16, 1.0, 6.0);
        spiderDrawBone(gfx, kx, ky, fx, fy, 0.68 * shade, 0.28, 0.18, 1.0, 5.0);
        // Joint knobs.
        gfx.drawSolidRect(hx - 4.0, hy - 4.0, 8.0, 8.0, 0.75, 0.35, 0.22, 1.0);
        gfx.drawSolidRect(kx - 3.5, ky - 3.5, 7.0, 7.0, 0.82, 0.40, 0.24, 1.0);
        local footA = leg.stepping ? 0.55 : 1.0;
        gfx.drawSolidRect(fx - 4.5, fy - 4.5, 9.0, 9.0, 0.95, 0.55, 0.28, footA);
    }

    // Cephalothorax + abdomen.
    local bobY = sp.y + sp.bodyBob;
    gfx.drawSolidRect(sp.x - 22.0, bobY - 16.0, 44.0, 32.0, 0.55, 0.22, 0.16, 1.0);
    local bx = sp.x - cos(sp.facing) * 28.0;
    local by = bobY - sin(sp.facing) * 28.0;
    gfx.drawSolidRect(bx - 16.0, by - 12.0, 32.0, 24.0, 0.45, 0.18, 0.14, 1.0);
    // Eyes.
    local ex = sp.x + cos(sp.facing) * 14.0;
    local ey = bobY + sin(sp.facing) * 14.0;
    local px = -sin(sp.facing);
    local py = cos(sp.facing);
    gfx.drawSolidRect(ex + px * 7.0 - 3.0, ey + py * 7.0 - 3.0, 6.0, 6.0, 0.95, 0.85, 0.35, 1.0);
    gfx.drawSolidRect(ex - px * 7.0 - 3.0, ey - py * 7.0 - 3.0, 6.0, 6.0, 0.95, 0.85, 0.35, 1.0);
}

function spiderDrawGuides(sp, gfx) {
    // Destination marker.
    gfx.drawSolidRect(sp.waypointX - 5.0, sp.waypointY - 5.0, 10.0, 10.0, 0.25, 0.75, 0.55, 0.85);
    gfx.drawSolidRect(sp.waypointX - 1.0, sp.waypointY - 14.0, 2.0, 28.0, 0.25, 0.75, 0.55, 0.45);
    gfx.drawSolidRect(sp.waypointX - 14.0, sp.waypointY - 1.0, 28.0, 2.0, 0.25, 0.75, 0.55, 0.45);
}
