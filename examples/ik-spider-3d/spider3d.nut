// Procedural 8-leg spider driven by eve.IK Skeleton3D + Solver3D (FABRIK).
// World units are metres on the XZ ground plane (Y up). Knees bend upward via
// per-chain pole vectors; even/odd legs alternate in a tetrapod gait.

function s3Clamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function s3Len2(x, z) {
    return sqrt(x * x + z * z);
}

function s3Require(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}

function s3RequireUpdate(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
}

function s3Translation(x, y, z) {
    // Column-major 4x4; translation in the last column.
    return [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        x, y, z, 1.0
    ];
}

function s3Make(ikMod, cx, cz) {
    local sk = ikMod.newSkeleton3D();
    local solver = ikMod.newSolver3D();
    solver.setMaxIterations(28);
    solver.setTolerance(0.004);
    solver.setInfluence(1.0);

    // Rest yaw offsets around +Y (body forward = local +X after root yaw).
    local restYaw = [
        -2.70, -1.95, -1.20, -0.45,
         0.45,  1.20,  1.95,  2.70
    ];
    local hipLen = 0.22;
    local kneeLen = 0.34;
    local footLen = 0.30;
    local bodyY = 0.28;
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
        local yaw = restYaw[i];
        sk.setConstraints(hip, yaw - 0.9, -0.85, yaw + 0.9, 0.35);
        sk.setConstraints(knee, -0.6, -1.55, 0.6, -0.05);
        sk.setConstraints(foot, -0.7, -0.6, 0.7, 0.7);
        local reach = hipLen + kneeLen + footLen;
        legs.push({
            index = i,
            hip = hip,
            knee = knee,
            foot = foot,
            restYaw = yaw,
            plantR = reach * 0.70,
            stepHeight = 0.16,
            stepping = false,
            stepT = 0.0,
            stepDur = 0.24,
            fromX = 0.0, fromY = 0.0, fromZ = 0.0,
            toX = 0.0, toY = 0.0, toZ = 0.0,
            plantX = 0.0, plantY = 0.02, plantZ = 0.0,
            targetX = 0.0, targetY = 0.02, targetZ = 0.0,
            // Visuals filled in by spiderBuildVisuals().
            polyline = null,
            hipBall = null,
            kneeBall = null,
            footBall = null
        });
    }

    sk.setPosition(0, cx, bodyY, cz);
    sk.setRotation(0, 0.0, 0.0);
    for (local i = 0; i < legs.len(); i++) {
        local leg = legs[i];
        sk.setRotation(leg.hip, leg.restYaw, -0.40);
        sk.setRotation(leg.knee, 0.0, -0.95);
        sk.setRotation(leg.foot, 0.0, 0.25);
    }
    sk.forwardKinematics();
    sk.bind();

    for (local i = 0; i < legs.len(); i++) {
        local leg = legs[i];
        // Drop the initial plant onto the ground under the FK tip.
        leg.plantX = sk.getX(leg.foot);
        leg.plantZ = sk.getZ(leg.foot);
        leg.plantY = 0.02;
        leg.targetX = leg.plantX;
        leg.targetY = leg.plantY;
        leg.targetZ = leg.plantZ;
        leg.fromX = leg.plantX; leg.fromY = leg.plantY; leg.fromZ = leg.plantZ;
        leg.toX = leg.plantX; leg.toY = leg.plantY; leg.toZ = leg.plantZ;
    }

    return {
        sk = sk,
        solver = solver,
        legs = legs,
        x = cx,
        y = bodyY,
        z = cz,
        facing = 0.0,
        speed = 0.95,
        turnSpeed = 2.2,
        stepTrigger = 0.22,
        stepOvershoot = 0.12,
        bodyBob = 0.0,
        waypointX = cx + 1.8,
        waypointY = 0.05,
        waypointZ = cz - 0.6,
        waypointTimer = 0.0,
        age = 0.0,
        steps = 0,
        arena = 4.2,
        cephalothorax = null,
        abdomen = null,
        eyeL = null,
        eyeR = null,
        waypointMark = null,
        trail = [],
        trailLine = null
    };
}

function s3PickWaypoint(sp) {
    local tries = 0;
    local margin = 0.9;
    while (tries < 14) {
        local nx = (math.random() * 2.0 - 1.0) * (sp.arena - margin);
        local nz = (math.random() * 2.0 - 1.0) * (sp.arena - margin);
        if (s3Len2(nx - sp.x, nz - sp.z) > 1.4) {
            sp.waypointX = nx;
            sp.waypointZ = nz;
            sp.waypointY = 0.05;
            sp.waypointTimer = 0.0;
            return;
        }
        tries += 1;
    }
    sp.waypointX = 0.0;
    sp.waypointZ = 0.0;
    sp.waypointTimer = 0.0;
}

function s3IdealPlant(sp, leg) {
    local a = sp.facing + leg.restYaw;
    return {
        x = sp.x + cos(a) * leg.plantR,
        y = 0.02,
        z = sp.z + sin(a) * leg.plantR
    };
}

function s3BeginStep(sp, leg) {
    local ideal = s3IdealPlant(sp, leg);
    local fx = cos(sp.facing);
    local fz = sin(sp.facing);
    leg.fromX = leg.plantX; leg.fromY = leg.plantY; leg.fromZ = leg.plantZ;
    leg.toX = ideal.x + fx * sp.stepOvershoot;
    leg.toY = 0.02;
    leg.toZ = ideal.z + fz * sp.stepOvershoot;
    leg.stepping = true;
    leg.stepT = 0.0;
    sp.steps += 1;
}

function s3UpdateGait(sp, dt) {
    local groupNeed = [false, false];
    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        if (leg.stepping) continue;
        local ideal = s3IdealPlant(sp, leg);
        if (s3Len2(leg.plantX - ideal.x, leg.plantZ - ideal.z) > sp.stepTrigger)
            groupNeed[i % 2] = true;
    }

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
            local ideal = s3IdealPlant(sp, leg);
            if (s3Len2(leg.plantX - ideal.x, leg.plantZ - ideal.z) > sp.stepTrigger * 0.55)
                s3BeginStep(sp, leg);
        }
    }

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        if (!leg.stepping) {
            leg.targetX = leg.plantX;
            leg.targetY = leg.plantY;
            leg.targetZ = leg.plantZ;
            continue;
        }
        leg.stepT += dt / leg.stepDur;
        local u = s3Clamp(leg.stepT, 0.0, 1.0);
        local s = u * u * (3.0 - 2.0 * u);
        leg.targetX = leg.fromX + (leg.toX - leg.fromX) * s;
        leg.targetZ = leg.fromZ + (leg.toZ - leg.fromZ) * s;
        leg.targetY = 0.02 + sin(u * 3.14159265) * leg.stepHeight;
        if (u >= 1.0) {
            leg.stepping = false;
            leg.plantX = leg.toX;
            leg.plantY = leg.toY;
            leg.plantZ = leg.toZ;
            leg.targetX = leg.plantX;
            leg.targetY = leg.plantY;
            leg.targetZ = leg.plantZ;
        }
    }
}

function s3SolveIK(sp) {
    local sk = sp.sk;
    local solver = sp.solver;
    local bodyY = sp.y + sp.bodyBob;
    sk.setPosition(0, sp.x, bodyY, sp.z);
    sk.setRotation(0, sp.facing, 0.0);
    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        sk.setRotation(leg.hip, leg.restYaw, -0.40);
        sk.setRotation(leg.knee, 0.0, -0.95);
        sk.setRotation(leg.foot, 0.0, 0.25);
    }
    sk.forwardKinematics();

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local hx = sk.getX(leg.hip);
        local hz = sk.getZ(leg.hip);
        // Pole above the hip→foot mid-span pulls the knee into a spider arch.
        local poleX = (hx + leg.targetX) * 0.5;
        local poleZ = (hz + leg.targetZ) * 0.5;
        local poleY = bodyY + 0.45;
        solver.clearTargets();
        solver.clearTipRotation();
        solver.setPole(poleX, poleY, poleZ, 0.9);
        solver.addTarget(leg.foot, leg.targetX, leg.targetY, leg.targetZ, 1.0);
        solver.solveChain(sk, leg.hip, leg.foot);
    }
    solver.clearPole();
}

function s3Update(sp, dt) {
    if (dt > 0.05) dt = 0.05;
    sp.age += dt;
    sp.waypointTimer += dt;

    local dx = sp.waypointX - sp.x;
    local dz = sp.waypointZ - sp.z;
    local dist = s3Len2(dx, dz);
    if (dist < 0.45 || sp.waypointTimer > 7.0) {
        s3PickWaypoint(sp);
        dx = sp.waypointX - sp.x;
        dz = sp.waypointZ - sp.z;
        dist = s3Len2(dx, dz);
    }

    local desired = atan2(dz, dx);
    local err = desired - sp.facing;
    while (err > 3.14159265) err -= 6.2831853;
    while (err < -3.14159265) err += 6.2831853;
    local maxTurn = sp.turnSpeed * dt;
    sp.facing += s3Clamp(err, -maxTurn, maxTurn);

    local turnFactor = 1.0 - s3Clamp(fabs(err) / 1.2, 0.0, 0.55);
    local step = sp.speed * turnFactor * dt;
    sp.x += cos(sp.facing) * step;
    sp.z += sin(sp.facing) * step;
    sp.x = s3Clamp(sp.x, -sp.arena + 0.6, sp.arena - 0.6);
    sp.z = s3Clamp(sp.z, -sp.arena + 0.6, sp.arena - 0.6);

    sp.bodyBob = sin(sp.age * 9.0) * 0.012;
    s3UpdateGait(sp, dt);
    s3SolveIK(sp);
}

function s3MakeSphere(x, y, z, radius, r, g, b, a, width) {
    local s = s3Require(gfx.newPrimitiveSphere3D(x, y, z, radius, r, g, b, a, width), "sphere");
    s3RequireUpdate(s.setPaintMode("fill"), "sphere fill");
    s3RequireUpdate(s.setDepthMode("test"), "sphere depth");
    return s;
}

function s3MakePolyline(pts, r, g, b, a, width) {
    local line = s3Require(gfx.newPrimitivePolyline3D(pts, false, r, g, b, a, width), "polyline");
    s3RequireUpdate(line.setDepthMode("test"), "polyline depth");
    s3RequireUpdate(line.setWidthSpace("world"), "polyline width space");
    s3RequireUpdate(line.setLineCap("round"), "polyline cap");
    s3RequireUpdate(line.setLineJoin("round"), "polyline join");
    return line;
}

function s3DestroyVisuals(sp) {
    if (sp == null) return;
    local balls = [sp.cephalothorax, sp.abdomen, sp.eyeL, sp.eyeR, sp.waypointMark, sp.trailLine];
    foreach (p in balls) {
        if (p != null && !p.isStale()) p.remove();
    }
    sp.cephalothorax = null; sp.abdomen = null; sp.eyeL = null; sp.eyeR = null;
    sp.waypointMark = null; sp.trailLine = null;
    foreach (leg in sp.legs) {
        if (leg.polyline != null && !leg.polyline.isStale()) leg.polyline.remove();
        if (leg.hipBall != null && !leg.hipBall.isStale()) leg.hipBall.remove();
        if (leg.kneeBall != null && !leg.kneeBall.isStale()) leg.kneeBall.remove();
        if (leg.footBall != null && !leg.footBall.isStale()) leg.footBall.remove();
        leg.polyline = null; leg.hipBall = null; leg.kneeBall = null; leg.footBall = null;
    }
}

function s3BuildVisuals(sp) {
    local sk = sp.sk;
    local bodyY = sp.y;
    // Author spheres at the origin; s3MoveSphere applies world translation.
    sp.cephalothorax = s3MakeSphere(0.0, 0.0, 0.0, 0.14, 0.55, 0.22, 0.16, 1.0, 1.5);
    sp.abdomen = s3MakeSphere(0.0, 0.0, 0.0, 0.12, 0.45, 0.18, 0.14, 1.0, 1.5);
    sp.eyeL = s3MakeSphere(0.0, 0.0, 0.0, 0.025, 0.95, 0.85, 0.35, 1.0, 1.2);
    sp.eyeR = s3MakeSphere(0.0, 0.0, 0.0, 0.025, 0.95, 0.85, 0.35, 1.0, 1.2);
    sp.waypointMark = s3MakeSphere(0.0, 0.0, 0.0, 0.06, 0.25, 0.85, 0.55, 1.0, 1.5);

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local shade = 0.55 + (i % 2) * 0.12;
        local hx = sk.getX(leg.hip); local hy = sk.getY(leg.hip); local hz = sk.getZ(leg.hip);
        local kx = sk.getX(leg.knee); local ky = sk.getY(leg.knee); local kz = sk.getZ(leg.knee);
        local fx = sk.getX(leg.foot); local fy = sk.getY(leg.foot); local fz = sk.getZ(leg.foot);
        local pts = [sp.x, bodyY, sp.z, hx, hy, hz, kx, ky, kz, fx, fy, fz];
        leg.polyline = s3MakePolyline(pts, 0.62 * shade, 0.24, 0.16, 1.0, 0.055);
        leg.hipBall = s3MakeSphere(0.0, 0.0, 0.0, 0.035, 0.78, 0.36, 0.22, 1.0, 1.2);
        leg.kneeBall = s3MakeSphere(0.0, 0.0, 0.0, 0.030, 0.85, 0.42, 0.24, 1.0, 1.2);
        leg.footBall = s3MakeSphere(0.0, 0.0, 0.0, 0.032, 0.95, 0.55, 0.28, 1.0, 1.2);
    }

    sp.trailLine = s3MakePolyline([sp.x, 0.03, sp.z, sp.x, 0.03, sp.z + 0.01],
                                  0.20, 0.55, 0.40, 0.55, 0.02);
    s3SyncVisuals(sp);
}

function s3MoveSphere(sphere, x, y, z) {
    if (sphere == null || sphere.isStale()) return;
    s3RequireUpdate(sphere.setTransform(s3Translation(x, y, z)), "move sphere");
}

function s3SyncVisuals(sp) {
    local sk = sp.sk;
    local bodyY = sp.y + sp.bodyBob;
    local fx = cos(sp.facing);
    local fz = sin(sp.facing);
    local px = -sin(sp.facing);
    local pz = cos(sp.facing);

    s3MoveSphere(sp.cephalothorax, sp.x, bodyY, sp.z);
    s3MoveSphere(sp.abdomen, sp.x - fx * 0.26, bodyY, sp.z - fz * 0.26);
    s3MoveSphere(sp.eyeL, sp.x + fx * 0.12 + px * 0.05, bodyY + 0.05, sp.z + fz * 0.12 + pz * 0.05);
    s3MoveSphere(sp.eyeR, sp.x + fx * 0.12 - px * 0.05, bodyY + 0.05, sp.z + fz * 0.12 - pz * 0.05);
    s3MoveSphere(sp.waypointMark, sp.waypointX, sp.waypointY + 0.04 + 0.02 * sin(sp.age * 5.0), sp.waypointZ);

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local hx = sk.getX(leg.hip); local hy = sk.getY(leg.hip); local hz = sk.getZ(leg.hip);
        local kx = sk.getX(leg.knee); local ky = sk.getY(leg.knee); local kz = sk.getZ(leg.knee);
        local tx = sk.getX(leg.foot); local ty = sk.getY(leg.foot); local tz = sk.getZ(leg.foot);
        if (leg.polyline != null && !leg.polyline.isStale()) {
            local pts = [sp.x, bodyY, sp.z, hx, hy, hz, kx, ky, kz, tx, ty, tz];
            s3RequireUpdate(leg.polyline.setPolyline(pts, false), "leg polyline");
            local a = leg.stepping ? 0.65 : 1.0;
            s3RequireUpdate(leg.polyline.setColor(0.62, 0.24, 0.16, a), "leg color");
        }
        s3MoveSphere(leg.hipBall, hx, hy, hz);
        s3MoveSphere(leg.kneeBall, kx, ky, kz);
        s3MoveSphere(leg.footBall, tx, ty, tz);
        if (leg.footBall != null && !leg.footBall.isStale()) {
            local fa = leg.stepping ? 0.45 : 1.0;
            s3RequireUpdate(leg.footBall.setColor(0.95, 0.55, 0.28, fa), "foot color");
        }
    }

    // Body trail on the ground.
    if ((sp.steps > 0) && (floor(sp.age * 20.0).tointeger() % 2 == 0)) {
        sp.trail.push(sp.x);
        sp.trail.push(0.025);
        sp.trail.push(sp.z);
        while (sp.trail.len() > 180) {
            sp.trail.remove(0); sp.trail.remove(0); sp.trail.remove(0);
        }
        if (sp.trailLine != null && !sp.trailLine.isStale() && sp.trail.len() >= 6) {
            s3RequireUpdate(sp.trailLine.setPolyline(sp.trail, false), "trail");
        }
    }
}

function s3BuildArena(arena) {
    local half = arena;
    local ground = s3Require(
        gfx.newPrimitiveAabb3D(-half, -0.04, -half, half, 0.0, half, 0.18, 0.24, 0.20, 1.0, 1.5),
        "ground");
    s3RequireUpdate(ground.setPaintMode("fill"), "ground fill");
    s3RequireUpdate(ground.setDepthMode("test"), "ground depth");

    local grid = s3Require(
        gfx.newPrimitiveGrid3D([-half, 0.01, -half, half * 2.0, 0.0, 0.0, 0.0, 0.0, half * 2.0],
                               12, 12, 0.35, 0.48, 0.38, 0.75, 1.6),
        "grid");
    s3RequireUpdate(grid.setDepthMode("test"), "grid depth");

    // Soft rim walls so the spider's arena reads in perspective.
    local wallH = 0.28;
    local walls = [
        [-half, 0.0, -half, half, wallH, -half + 0.05],
        [-half, 0.0, half - 0.05, half, wallH, half],
        [-half, 0.0, -half, -half + 0.05, wallH, half],
        [half - 0.05, 0.0, -half, half, wallH, half]
    ];
    foreach (w in walls) {
        local wall = s3Require(
            gfx.newPrimitiveAabb3D(w[0], w[1], w[2], w[3], w[4], w[5], 0.28, 0.36, 0.30, 0.95, 1.2),
            "wall");
        s3RequireUpdate(wall.setPaintMode("fill"), "wall fill");
        s3RequireUpdate(wall.setDepthMode("test"), "wall depth");
    }
}
