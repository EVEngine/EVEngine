// Climbing playground: real World3D probes, candidate selection and capsule-constrained execution.
// 1/2/3 = 0.6/1.0/1.4 m obstacle, 4 = airborne ledge grab,
// 5 = blocked-clearance lane, 6 = moving platform, Space = start, R = reset.

persist physics = null
persist animation = null
persist climbing = null
persist world = null
persist runtime = null
persist player = null
persist obstacles = []
persist movingBody = null
persist movingVisual = null
persist movingTime = 0.0
persist lane = 1
persist playerFeet = [0.0, 0.0, 0.0]
persist tick = 0
persist accumulator = 0.0
persist active = false
persist lastPhase = "idle"
persist lastCode = "ready"
persist previousKeys = {}

const FIXED_DT = 0.016666667;

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !was;
}

function makeVisual(x, y, z, sx, sy, sz, r, g, b) {
    local visual = eve.Renderable3D();
    visual.setMesh(gfx.newMeshCube(1.0));
    visual.setPosition(x, y, z);
    visual.setScale(sx, sy, sz);
    visual.setTint(r, g, b, 1.0);
    visual.setRoughness(0.75);
    visual.setCastShadow(true);
    visual.setReceiveShadow(true);
    obstacles.push(visual);
    return visual;
}

function makeObstacle(x, height, color) {
    local body = world.newBody("static", x, height * 0.5, 2.0);
    body.newBoxShape(1.35, height, 0.65, 1.0, 0.7, 0.0);
    makeVisual(x, height * 0.5, 2.0, 1.35, height, 0.65,
               color[0], color[1], color[2]);
    return body;
}

function addAction(id, kind, minHeight, maxHeight, landing, apex, notifies) {
    local result = runtime.upsertActionKind(
        id, kind, minHeight, maxHeight, 0.0, 0.65, landing, apex, 0,
        0.35, 1.55, 0.42, 0.0, 1.0);
    if (!result.ok) throw result.error.message;
    local clip = animation.newClip("playground_" + id);
    clip.setDuration(0.65);
    local time = 0.16;
    foreach (notify in notifies) {
        clip.addEvent(time, notify, "");
        time += 0.12;
    }
    local validated = runtime.validateActionClip(id, clip);
    if (!validated.ok) throw validated.error.message;
}

function configureRuntime() {
    local created = climbing.newRuntime();
    if (!created.ok) throw created.error.message;
    runtime = created.value;
    local profile = runtime.setProfile(0.30, 1.80, 0.03, 1.35, 1.80, 0.65, 0.25, -1);
    if (!profile.ok) throw profile.error.message;
    addAction("parkour:vault_low", "vault", 0.40, 0.78, 0.75, 0.35,
              ["contact.left_hand", "land"]);
    addAction("parkour:vault_high", "vault", 0.79, 1.18, 0.65, 0.55,
              ["contact.left_hand", "land"]);
    addAction("climbing:mantle", "mantle", 1.19, 1.60, 0.55, 0.75,
              ["contact.left_hand", "contact.right_hand", "land"]);
    addAction("climbing:ledge_grab", "ledge_grab", 0.50, 1.60, 0.0, 0.10,
              ["contact.left_hand", "contact.right_hand"]);
}

function cancelActive() {
    if (runtime == null) return true;
    local inspected = runtime.inspect();
    if (!inspected.ok) {
        lastCode = inspected.error.message;
        return false;
    }
    local phase = inspected.value.phase;
    if (phase == "idle" || phase == "completed" || phase == "cancelled" || phase == "failed")
        return true;
    tick += 1;
    local cancelled = runtime.cancel("player_request", tick);
    if (!cancelled.ok) {
        lastCode = cancelled.error.message;
        return false;
    }
    return true;
}

function resetPlayer() {
    if (!cancelActive()) return false;
    local laneX = [-4.0, -2.0, 0.0, 2.0, 4.0, 6.0];
    playerFeet = [laneX[lane - 1], lane == 4 ? 0.65 : 0.0, 0.0];
    active = false;
    lastPhase = "idle";
    lastCode = "lane " + lane;
    player.setPosition(playerFeet[0], playerFeet[1] + 0.9, playerFeet[2]);
    return true;
}

function selectLane(value) {
    local oldLane = lane;
    lane = value;
    if (!resetPlayer()) lane = oldLane;
}

function startAction() {
    if (active) return;
    tick += 1;
    local airborne = lane == 4;
    local result = runtime.tryBeginMode(
        world, playerFeet[0], playerFeet[1], playerFeet[2],
        0.0, 1.0, airborne ? 2.5 : 4.5, -1,
        airborne ? -2.0 : 0.0, !airborne, tick);
    if (result.ok) {
        active = true;
        lastCode = result.value.actionId;
    } else {
        lastCode = result.error.message;
        local debug = runtime.inspect();
        if (debug.ok) {
            print("climbing playground reject: " + lastCode +
                  " queries=" + debug.value.queryCount + "\n");
            foreach (evidence in debug.value.evidence)
                print("  " + evidence.actionId + " -> " + evidence.code + "\n");
        }
    }
}

function fixedUpdate() {
    movingTime += FIXED_DT;
    local movingY = 0.65 + sin(movingTime * 1.4) * 0.28;
    movingBody.setPosition(6.0, movingY, 2.0);
    movingVisual.setPosition(6.0, movingY, 2.0);
    world.update(FIXED_DT);

    if (!active) return;
    tick += 1;
    local step = runtime.advance(world, tick, FIXED_DT);
    if (!step.ok) {
        active = false;
        lastCode = step.error.message;
        return;
    }
    playerFeet = [step.value.feet.x, step.value.feet.y, step.value.feet.z];
    player.setPosition(playerFeet[0], playerFeet[1] + 0.9, playerFeet[2]);
    lastPhase = step.value.phase;
    if (lastPhase == "completed" || lastPhase == "cancelled" || lastPhase == "failed")
        active = false;
}

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.075, 0.11, 1.0);
    camera = eve.Camera3D();
    camera.setEye(10.5, 7.0, 11.5);
    camera.setTarget(1.0, 0.9, 2.2);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.32, 0.34, 0.38);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.35, 1.2, 1.15, 1.05);

    if (physics == null) physics = eve.Physics();
    if (animation == null) animation = eve.Animation();
    if (climbing == null) climbing = eve.Climbing();
    if (world == null) {
        world = physics.newWorld3D(0.0, 0.0, 0.0, false);
        local ground = world.newBody("static", 1.0, -0.15, 2.4);
        ground.newBoxShape(14.0, 0.3, 7.0, 1.0, 0.8, 0.0);
        makeVisual(1.0, -0.15, 2.4, 14.0, 0.3, 7.0, 0.16, 0.19, 0.23);
        makeObstacle(-4.0, 0.6, [0.20, 0.60, 0.82]);
        makeObstacle(-2.0, 1.0, [0.28, 0.72, 0.48]);
        makeObstacle(0.0, 1.4, [0.88, 0.58, 0.20]);
        makeObstacle(2.0, 1.2, [0.62, 0.38, 0.82]);
        makeObstacle(4.0, 1.0, [0.78, 0.28, 0.28]);
        local ceiling = world.newBody("static", 4.0, 2.05, 2.45);
        ceiling.newBoxShape(1.6, 0.35, 1.5, 1.0, 0.8, 0.0);
        makeVisual(4.0, 2.05, 2.45, 1.6, 0.35, 1.5, 0.50, 0.12, 0.12);

        movingBody = world.newBody("kinematic", 6.0, 0.65, 2.0);
        movingBody.newBoxShape(1.35, 1.0, 0.65, 1.0, 0.7, 0.0);
        movingVisual = makeVisual(6.0, 0.65, 2.0, 1.35, 1.0, 0.65,
                                  0.12, 0.72, 0.72);
    }
    if (runtime == null) configureRuntime();
    if (player == null) {
        player = eve.Renderable3D();
        player.setMesh(gfx.newMeshCube(1.0));
        player.setScale(0.6, 1.8, 0.6);
        player.setTint(0.94, 0.94, 0.98, 1.0);
    }
    resetPlayer();
    print("climbing-playground: 1..6 lane | Space start | R reset\n");
};

eve_update = function(dt) {
    if (edgePressed("1")) selectLane(1);
    if (edgePressed("2")) selectLane(2);
    if (edgePressed("3")) selectLane(3);
    if (edgePressed("4")) selectLane(4);
    if (edgePressed("5")) selectLane(5);
    if (edgePressed("6")) selectLane(6);
    if (edgePressed("Space")) startAction();
    if (edgePressed("R") || edgePressed("r")) resetPlayer();
    accumulator += dt;
    while (accumulator >= FIXED_DT) {
        fixedUpdate();
        accumulator -= FIXED_DT;
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    local activeColor = active ? [0.18, 0.78, 0.38] : [0.25, 0.35, 0.48];
    gfx.drawSolidRect(20.0, 20.0, 390.0, 74.0, 0.04, 0.055, 0.08, 0.90);
    gfx.drawSolidRect(30.0, 30.0, 18.0, 18.0,
                      activeColor[0], activeColor[1], activeColor[2], 1.0);
};
