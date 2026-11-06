// Procedural 8-leg spider driven by eve.IK Skeleton3D + Solver3D (FABRIK).
// World metres on XZ ground (Y up). Visuals are lit Renderable3D meshes.
//
// Important: ik.hpp local yaw/pitch for 3D uses a Z-preferring basis from the
// parent forward. Seeding rest poses with yaw/pitch therefore does NOT match
// engine Y-up "azimuth on XZ". We seed hip/knee/foot in world space each frame,
// call updateRotations(), then solveChain + setPole.

function s3Clamp(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function s3Len2(x, z) {
    return sqrt(x * x + z * z);
}

function s3Make(ikMod, cx, cz) {
    local sk = ikMod.newSkeleton3D();
    local solver = ikMod.newSolver3D();
    solver.setMaxIterations(36);
    solver.setTolerance(0.0025);
    solver.setInfluence(1.0);

    // Even 45° spacing around the body (local azimuth on XZ).
    local restYaw = [];
    for (local i = 0; i < 8; i++)
        restYaw.push(-3.14159265 + 0.39269908 + i * 0.78539816);

    local hipLen = 0.30;
    local kneeLen = 0.42;
    local footLen = 0.36;
    local bodyY = 0.40;
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
        // Keep constraints very loose — world-space seeding + poles own the pose.
        // Tight yaw/pitch limits fight Y-up azimuth because ik.hpp treats +Z as
        // the preferred "up" when building the local basis.
        sk.clearConstraints(hip);
        sk.clearConstraints(knee);
        sk.clearConstraints(foot);
        local reach = hipLen + kneeLen + footLen;
        legs.push({
            index = i,
            hip = hip,
            knee = knee,
            foot = foot,
            restYaw = restYaw[i],
            plantR = reach * 0.82,
            stepHeight = 0.16,
            stepping = false,
            stepT = 0.0,
            stepDur = 0.28,
            fromX = 0.0, fromY = 0.0, fromZ = 0.0,
            toX = 0.0, toY = 0.0, toZ = 0.0,
            plantX = 0.0, plantY = 0.0, plantZ = 0.0,
            targetX = 0.0, targetY = 0.0, targetZ = 0.0,
            coxa = null,
            thigh = null,
            shin = null,
            hipJoint = null,
            kneeJoint = null,
            footPad = null
        });
    }

    local sp = {
        sk = sk,
        solver = solver,
        legs = legs,
        hipLen = hipLen,
        kneeLen = kneeLen,
        footLen = footLen,
        x = cx,
        y = bodyY,
        z = cz,
        facing = 0.0,
        speed = 0.80,
        turnSpeed = 1.9,
        stepTrigger = 0.22,
        stepOvershoot = 0.09,
        bodyBob = 0.0,
        waypointX = cx + 1.8,
        waypointY = 0.12,
        waypointZ = cz - 0.6,
        waypointTimer = 0.0,
        age = 0.0,
        steps = 0,
        arena = 5.0,
        meshSphere = null,
        meshCylinder = null,
        meshCube = null,
        cephalothorax = null,
        abdomen = null,
        eyeL = null,
        eyeR = null,
        waypointMark = null,
        floor = null,
        floorTrim = null,
        props = []
    };

    // Seed plants from ideal radial slots, then bind lengths from world pose.
    for (local i = 0; i < legs.len(); i++) {
        local leg = legs[i];
        local ideal = s3IdealPlant(sp, leg);
        leg.plantX = ideal.x; leg.plantY = 0.0; leg.plantZ = ideal.z;
        leg.targetX = ideal.x; leg.targetY = 0.0; leg.targetZ = ideal.z;
        leg.fromX = ideal.x; leg.fromY = 0.0; leg.fromZ = ideal.z;
        leg.toX = ideal.x; leg.toY = 0.0; leg.toZ = ideal.z;
    }
    s3SeedPose(sp);
    sk.bind();

    return sp;
}

function s3PickWaypoint(sp) {
    local tries = 0;
    local margin = 1.1;
    while (tries < 14) {
        local nx = (math.random() * 2.0 - 1.0) * (sp.arena - margin);
        local nz = (math.random() * 2.0 - 1.0) * (sp.arena - margin);
        if (s3Len2(nx - sp.x, nz - sp.z) > 1.6) {
            sp.waypointX = nx;
            sp.waypointZ = nz;
            sp.waypointY = 0.12;
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
        y = 0.0,
        z = sp.z + sin(a) * leg.plantR
    };
}

function s3BeginStep(sp, leg) {
    local ideal = s3IdealPlant(sp, leg);
    local fx = cos(sp.facing);
    local fz = sin(sp.facing);
    leg.fromX = leg.plantX; leg.fromY = 0.0; leg.fromZ = leg.plantZ;
    leg.toX = ideal.x + fx * sp.stepOvershoot;
    leg.toY = 0.0;
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
            leg.targetY = 0.0;
            leg.targetZ = leg.plantZ;
            continue;
        }
        leg.stepT += dt / leg.stepDur;
        local u = s3Clamp(leg.stepT, 0.0, 1.0);
        local s = u * u * (3.0 - 2.0 * u);
        leg.targetX = leg.fromX + (leg.toX - leg.fromX) * s;
        leg.targetZ = leg.fromZ + (leg.toZ - leg.fromZ) * s;
        leg.targetY = sin(u * 3.14159265) * leg.stepHeight;
        if (u >= 1.0) {
            leg.stepping = false;
            leg.plantX = leg.toX;
            leg.plantY = 0.0;
            leg.plantZ = leg.toZ;
            leg.targetX = leg.plantX;
            leg.targetY = 0.0;
            leg.targetZ = leg.plantZ;
        }
    }
}

// World-space rest pose: radial hips, arched knees, feet on targets.
function s3SeedPose(sp) {
    local sk = sp.sk;
    local bodyY = sp.y + sp.bodyBob;
    local fx = cos(sp.facing);
    local fz = sin(sp.facing);
    sk.setPosition(0, sp.x, bodyY, sp.z);
    // Root facing must be an orientation vector — setRotation on the root is
    // ignored once orientation is non-zero (ik.hpp FK path).
    sk.setOrientation(0, fx, 0.0, fz);

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local outA = sp.facing + leg.restYaw;
        local ox = cos(outA);
        local oz = sin(outA);
        local hx = sp.x + ox * sp.hipLen;
        // Slight vertical stagger so neighbouring coxae don't coplanar-intersect.
        local hy = bodyY - 0.02 + ((i % 2) * 0.03 - 0.015);
        local hz = sp.z + oz * sp.hipLen;
        local ty = leg.targetY;
        if (ty < 0.0) ty = 0.0;
        local mx = (hx + leg.targetX) * 0.5 + ox * 0.16;
        local mz = (hz + leg.targetZ) * 0.5 + oz * 0.16;
        local my = bodyY + 0.26;
        if (my < 0.16) my = 0.16;
        sk.setPosition(leg.hip, hx, hy, hz);
        sk.setPosition(leg.knee, mx, my, mz);
        sk.setPosition(leg.foot, leg.targetX, ty, leg.targetZ);
    }
    sk.updateRotations();
}

function s3SolveIK(sp) {
    local sk = sp.sk;
    local solver = sp.solver;
    s3SeedPose(sp);

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local hx = sk.getX(leg.hip);
        local hy = sk.getY(leg.hip);
        local hz = sk.getZ(leg.hip);
        local outX = cos(sp.facing + leg.restYaw);
        local outZ = sin(sp.facing + leg.restYaw);
        local poleX = (hx + leg.targetX) * 0.5 + outX * 0.28;
        local poleZ = (hz + leg.targetZ) * 0.5 + outZ * 0.28;
        local poleY = (sp.y + sp.bodyBob) + 0.55;
        solver.clearTargets();
        solver.clearTipRotation();
        solver.setPole(poleX, poleY, poleZ, 1.0);
        local ty = leg.targetY;
        if (ty < 0.0) ty = 0.0;
        solver.addTarget(leg.foot, leg.targetX, ty, leg.targetZ, 1.0);
        solver.solveChain(sk, leg.hip, leg.foot);

        // Ground clamp + keep knee clear of the floor / body.
        local fty = sk.getY(leg.foot);
        if (fty < 0.0) sk.setPosition(leg.foot, sk.getX(leg.foot), 0.0, sk.getZ(leg.foot));
        local kyy = sk.getY(leg.knee);
        if (kyy < 0.10) {
            sk.setPosition(leg.knee, sk.getX(leg.knee), 0.10, sk.getZ(leg.knee));
        }
        // Push knee outward if it collapsed toward the body centre.
        local kx = sk.getX(leg.knee);
        local kz = sk.getZ(leg.knee);
        local inward = (sp.x - kx) * outX + (sp.z - kz) * outZ;
        if (inward > -0.08) {
            sk.setPosition(leg.knee, kx + outX * 0.14, sk.getY(leg.knee), kz + outZ * 0.14);
        }
    }
    solver.clearPole();
    sk.updateRotations();
}

function s3Update(sp, dt) {
    if (dt > 0.05) dt = 0.05;
    sp.age += dt;
    sp.waypointTimer += dt;

    local dx = sp.waypointX - sp.x;
    local dz = sp.waypointZ - sp.z;
    local dist = s3Len2(dx, dz);
    if (dist < 0.55 || sp.waypointTimer > 7.0) {
        s3PickWaypoint(sp);
        dx = sp.waypointX - sp.x;
        dz = sp.waypointZ - sp.z;
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
    sp.x = s3Clamp(sp.x, -sp.arena + 0.9, sp.arena - 0.9);
    sp.z = s3Clamp(sp.z, -sp.arena + 0.9, sp.arena - 0.9);

    sp.bodyBob = sin(sp.age * 7.5) * 0.012;
    s3UpdateGait(sp, dt);
    s3SolveIK(sp);
}

function s3MakeProp(mesh, r, g, b, roughness, metallic, lit = true) {
    local e = eve.Renderable3D();
    e.setMesh(mesh);
    e.setTint(r, g, b, 1.0);
    e.setRoughness(roughness);
    e.setMetallic(metallic);
    e.setReceiveLight(lit);
    e.setCastShadow(lit);
    e.setReceiveShadow(true);
    e.setVisible(true);
    return e;
}

function s3Hide(e) {
    if (e == null) return;
    e.setVisible(false);
    e.setScale(0.001, 0.001, 0.001);
    e.setPosition(0.0, -20.0, 0.0);
}

function s3PlaceSphere(e, x, y, z, radius) {
    if (e == null) return;
    e.setVisible(true);
    e.setPosition(x, y, z);
    e.setScale(radius, radius, radius);
    e.setRotation(0.0, 0.0, 0.0);
}

// Unit cylinder is Y-up, height 2, radius 1. Align local +Y with A→B.
// Renderable3D applies yaw(Y) then pitch(X): dir = (sP*sY, cP, sP*cY).
function s3PlaceCylinder(e, ax, ay, az, bx, by, bz, radius) {
    if (e == null) return;
    local dx = bx - ax;
    local dy = by - ay;
    local dz = bz - az;
    local len = sqrt(dx * dx + dy * dy + dz * dz);
    if (len < 1e-4) {
        s3Hide(e);
        return;
    }
    local inv = 1.0 / len;
    dx *= inv; dy *= inv; dz *= inv;
    local pitch = acos(s3Clamp(dy, -1.0, 1.0));
    local yaw = 0.0;
    local horiz = sqrt(dx * dx + dz * dz);
    if (horiz > 1e-5) yaw = atan2(dx, dz);
    e.setVisible(true);
    e.setPosition((ax + bx) * 0.5, (ay + by) * 0.5, (az + bz) * 0.5);
    e.setScale(radius, len * 0.5, radius);
    e.setRotation(yaw, pitch, 0.0);
}

function s3BuildArena(sp) {
    local half = sp.arena;
    sp.floor = s3MakeProp(sp.meshCube, 0.18, 0.22, 0.20, 0.78, 0.06, true);
    sp.floor.setPosition(0.0, -0.06, 0.0);
    sp.floor.setScale(half * 2.0, 0.12, half * 2.0);
    sp.props.push(sp.floor);

    sp.floorTrim = s3MakeProp(sp.meshCube, 0.32, 0.38, 0.34, 0.70, 0.08, true);
    sp.floorTrim.setPosition(0.0, 0.006, 0.0);
    sp.floorTrim.setScale(half * 2.0 - 0.18, 0.012, half * 2.0 - 0.18);
    sp.props.push(sp.floorTrim);

    local curbH = 0.14;
    local curbT = 0.12;
    local curbs = [
        [0.0, curbH * 0.5, -half + curbT * 0.5, half * 2.0, curbH, curbT],
        [0.0, curbH * 0.5, half - curbT * 0.5, half * 2.0, curbH, curbT],
        [-half + curbT * 0.5, curbH * 0.5, 0.0, curbT, curbH, half * 2.0],
        [half - curbT * 0.5, curbH * 0.5, 0.0, curbT, curbH, half * 2.0]
    ];
    foreach (c in curbs) {
        local curb = s3MakeProp(sp.meshCube, 0.28, 0.34, 0.30, 0.72, 0.05, true);
        curb.setPosition(c[0], c[1], c[2]);
        curb.setScale(c[3], c[4], c[5]);
        sp.props.push(curb);
    }
}

function s3BuildVisuals(sp) {
    sp.meshSphere = gfx.newMeshSphere(24, 16);
    sp.meshCylinder = gfx.newMeshCylinder(20, 1, true);
    sp.meshCube = gfx.newMeshCube(1.0);

    s3BuildArena(sp);

    // Glossy carapace: low roughness + some metal so key-light specular reads.
    sp.cephalothorax = s3MakeProp(sp.meshSphere, 0.58, 0.24, 0.14, 0.22, 0.42, true);
    sp.abdomen = s3MakeProp(sp.meshSphere, 0.44, 0.17, 0.11, 0.28, 0.34, true);
    sp.eyeL = s3MakeProp(sp.meshSphere, 1.0, 0.94, 0.40, 0.08, 0.70, true);
    sp.eyeR = s3MakeProp(sp.meshSphere, 1.0, 0.94, 0.40, 0.08, 0.70, true);
    sp.waypointMark = s3MakeProp(sp.meshSphere, 0.22, 0.96, 0.72, 0.18, 0.20, true);
    sp.props.push(sp.cephalothorax);
    sp.props.push(sp.abdomen);
    sp.props.push(sp.eyeL);
    sp.props.push(sp.eyeR);
    sp.props.push(sp.waypointMark);

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local shade = 0.92 + (i % 2) * 0.08;
        leg.coxa = s3MakeProp(sp.meshCylinder, 0.52 * shade, 0.20, 0.12, 0.34, 0.22, true);
        leg.thigh = s3MakeProp(sp.meshCylinder, 0.58 * shade, 0.22, 0.13, 0.32, 0.20, true);
        leg.shin = s3MakeProp(sp.meshCylinder, 0.64 * shade, 0.26, 0.15, 0.30, 0.16, true);
        leg.hipJoint = s3MakeProp(sp.meshSphere, 0.60, 0.24, 0.14, 0.24, 0.28, true);
        leg.kneeJoint = s3MakeProp(sp.meshSphere, 0.68, 0.28, 0.16, 0.22, 0.24, true);
        leg.footPad = s3MakeProp(sp.meshSphere, 0.14, 0.11, 0.08, 0.48, 0.08, true);
        sp.props.push(leg.coxa);
        sp.props.push(leg.thigh);
        sp.props.push(leg.shin);
        sp.props.push(leg.hipJoint);
        sp.props.push(leg.kneeJoint);
        sp.props.push(leg.footPad);
    }

    s3SyncVisuals(sp);
}

function s3DestroyVisuals(sp) {
    if (sp == null) return;
    foreach (e in sp.props) {
        if (e != null) {
            e.setVisible(false);
            e.setScale(0.001, 0.001, 0.001);
            e.setPosition(0.0, -40.0, 0.0);
        }
    }
    sp.props = [];
    sp.cephalothorax = null;
    sp.abdomen = null;
    sp.eyeL = null;
    sp.eyeR = null;
    sp.waypointMark = null;
    sp.floor = null;
    sp.floorTrim = null;
    foreach (leg in sp.legs) {
        leg.coxa = null; leg.thigh = null; leg.shin = null;
        leg.hipJoint = null; leg.kneeJoint = null; leg.footPad = null;
    }
}

function s3SyncVisuals(sp) {
    local sk = sp.sk;
    local bodyY = sp.y + sp.bodyBob;
    local fx = cos(sp.facing);
    local fz = sin(sp.facing);
    local px = -sin(sp.facing);
    local pz = cos(sp.facing);

    // Body smaller than hipLen so sockets sit clearly outside the carapace.
    local bodyR = 0.092;
    s3PlaceSphere(sp.cephalothorax, sp.x, bodyY, sp.z, bodyR);
    s3PlaceSphere(sp.abdomen, sp.x - fx * 0.34, bodyY - 0.02, sp.z - fz * 0.34, 0.100);
    s3PlaceSphere(sp.eyeL,
        sp.x + fx * 0.078 + px * 0.036, bodyY + 0.038, sp.z + fz * 0.078 + pz * 0.036, 0.020);
    s3PlaceSphere(sp.eyeR,
        sp.x + fx * 0.078 - px * 0.036, bodyY + 0.038, sp.z + fz * 0.078 - pz * 0.036, 0.020);
    s3PlaceSphere(sp.waypointMark,
        sp.waypointX, 0.10 + 0.04 * sin(sp.age * 4.5), sp.waypointZ, 0.065);

    for (local i = 0; i < sp.legs.len(); i++) {
        local leg = sp.legs[i];
        local hx = sk.getX(leg.hip); local hy = sk.getY(leg.hip); local hz = sk.getZ(leg.hip);
        local kx = sk.getX(leg.knee); local ky = sk.getY(leg.knee); local kz = sk.getZ(leg.knee);
        local tx = sk.getX(leg.foot); local ty = sk.getY(leg.foot); local tz = sk.getZ(leg.foot);
        if (ty < 0.0) ty = 0.0;
        if (ky < 0.08) ky = 0.08;

        local socketAng = sp.facing + leg.restYaw;
        // Start coxa mid-hip (not on the carapace) so 8 thick cylinders don't
        // pack into each other at the body surface.
        local socketR = sp.hipLen * 0.52;
        if (socketR < bodyR + 0.04) socketR = bodyR + 0.04;
        local sx = sp.x + cos(socketAng) * socketR;
        local sy = bodyY - 0.01 + ((i % 2) * 0.02 - 0.01);
        local sz = sp.z + sin(socketAng) * socketR;
        s3PlaceCylinder(leg.coxa, sx, sy, sz, hx, hy, hz, 0.026);
        s3PlaceCylinder(leg.thigh, hx, hy, hz, kx, ky, kz, 0.030);
        s3PlaceCylinder(leg.shin, kx, ky, kz, tx, ty, tz, 0.024);
        s3PlaceSphere(leg.hipJoint, hx, hy, hz, 0.034);
        s3PlaceSphere(leg.kneeJoint, kx, ky, kz, 0.030);
        if (leg.footPad != null) {
            leg.footPad.setVisible(true);
            local padY = leg.stepping ? (ty + 0.012) : 0.014;
            leg.footPad.setPosition(tx, padY, tz);
            leg.footPad.setScale(0.048, 0.015, 0.048);
            leg.footPad.setRotation(0.0, 0.0, 0.0);
        }
    }
}
