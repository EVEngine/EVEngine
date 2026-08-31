// Runnable climbing/parkour course composed from EVEngine-native systems.
// CC0 glTF clips drive real Motion Matching and skinned rendering. Climbing owns
// probes, deterministic selection and capsule-constrained action execution.

persist physics = null
persist animation = null
persist climbing = null
persist model3d = null
persist world = null
persist runtime = null
persist mannequin = null
persist skeleton = null
persist matcher = null
persist motionDb = null
persist characterParts = []
persist characterSkins = []
persist scenery = []
persist clips = {}
persist actionPlayer = null
persist activeAction = ""
persist playerFeet = [0.0, 0.0, 0.0]
persist lane = 1
persist tick = 0
persist accumulator = 0.0
persist movingTime = 0.0
persist movingBody = null
persist movingVisual = null
persist previousKeys = {}
persist status = "loading"
persist debugMotion = true
persist screenshotSaved = false
persist frame = 0

const FIXED_DT = 0.016666667;
const WALK_SPEED = 1.7;
const RUN_SPEED = 4.2;

function clampf(value, minimum, maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !was;
}

function keyEither(lower, upper) {
    return keyboard.isDown(lower) || keyboard.isDown(upper);
}

function findAnimation(model, name) {
    for (local i = 0; i < model.getAnimationCount(); ++i)
        if (model.getAnimationName(i) == name) return i;
    throw "KayKit animation not found: " + name;
}

function clipFrom(model, name, loop) {
    local clip = animation.newClipFromModel(model, skeleton, findAnimation(model, name));
    clip.setLoop(loop);
    return clip;
}

function makeVisual(x, y, z, sx, sy, sz, r, g, b) {
    local visual = eve.Renderable3D();
    visual.setMesh(gfx.newMeshCube(1.0));
    visual.setPosition(x, y, z);
    visual.setScale(sx, sy, sz);
    visual.setTint(r, g, b, 1.0);
    visual.setRoughness(0.82);
    visual.setCastShadow(true);
    visual.setReceiveShadow(true);
    scenery.push(visual);
    return visual;
}

function makeObstacle(x, z, height, r, g, b) {
    local body = world.newBody("static", x, height * 0.5, z);
    body.newBoxShape(2.1, height, 0.72, 1.0, 0.76, 0.0);
    makeVisual(x, height * 0.5, z, 2.1, height, 0.72, r, g, b);
}

function loadCharacterAndMotion() {
    mannequin = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_MovementBasic.glb");
    local advanced = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_MovementAdvanced.glb");
    skeleton = animation.newSkeletonFromModel(mannequin);

    clips.walkA <- clipFrom(mannequin, "Walking_A", true);
    clips.walkB <- clipFrom(mannequin, "Walking_B", true);
    clips.runA <- clipFrom(mannequin, "Running_A", true);
    clips.runB <- clipFrom(mannequin, "Running_B", true);
    clips.strafeL <- clipFrom(advanced, "Running_Strafe_Left", true);
    clips.strafeR <- clipFrom(advanced, "Running_Strafe_Right", true);
    clips.vaultLow <- clipFrom(advanced, "Dodge_Forward", false);
    clips.vaultHigh <- clipFrom(mannequin, "Jump_Full_Short", false);
    clips.mantle <- clipFrom(mannequin, "Jump_Full_Long", false);

    motionDb = animation.newMotionDatabase(skeleton);
    motionDb.setRootBoneByName("root");
    motionDb.addFeatureBoneByName("foot.l");
    motionDb.addFeatureBoneByName("foot.r");
    motionDb.addClip(clips.walkA);
    motionDb.addClip(clips.walkB);
    motionDb.addClip(clips.runA);
    motionDb.addClip(clips.runB);
    motionDb.addClip(clips.strafeL);
    motionDb.addClip(clips.strafeR);
    motionDb.bake();
    matcher = animation.newMotionMatcher(skeleton, motionDb);
    matcher.setSearchInterval(0.08);
    matcher.setBlendTime(0.14);
    matcher.setTrajectoryWeight(1.0);
    matcher.setPoseWeight(0.42);
    matcher.setVelocityWeight(0.8);

    actionPlayer = animation.newPlayer(skeleton);
    for (local mesh = 0; mesh < mannequin.getMeshCount(); ++mesh) {
        local part = model3d.createRenderable(gfx, mannequin, mesh);
        part.setTint(0.92, 0.95, 1.0, 1.0);
        part.setRoughness(0.72);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        characterParts.push(part);
        if (mannequin.hasBones(mesh))
            characterSkins.push({skin=animation.newSkinFromModel(mannequin, mesh, skeleton), part=part});
    }
}

function bindAction(id, kind, minHeight, maxHeight, landing, apex, clip, notifies) {
    local duration = clip.getDuration();
    local added = runtime.upsertActionKind(
        id, kind, minHeight, maxHeight, 0.0, duration, landing, apex, 0,
        0.35, 1.55, 0.42, 0.0, 1.0);
    if (!added.ok) throw added.error.message;
    local notifyTime = duration * 0.22;
    foreach (notify in notifies) {
        clip.addEvent(notifyTime, notify, "");
        notifyTime += duration * 0.18;
    }
    local validated = runtime.validateActionClip(id, clip);
    if (!validated.ok) throw validated.error.message;
}

function configureClimbing() {
    local created = climbing.newRuntime();
    if (!created.ok) throw created.error.message;
    runtime = created.value;
    local profile = runtime.setProfile(0.30, 1.80, 0.03, 1.45, 1.80, 0.65, 0.30, -1);
    if (!profile.ok) throw profile.error.message;
    runtime.setDebugCapture(true);
    bindAction("parkour:vault_low", "vault", 0.40, 0.78, 0.78, 0.34,
               clips.vaultLow, ["contact.left_hand", "land"]);
    bindAction("parkour:vault_high", "vault", 0.79, 1.15, 0.66, 0.56,
               clips.vaultHigh, ["contact.left_hand", "land"]);
    bindAction("climbing:mantle", "mantle", 1.16, 1.62, 0.58, 0.78,
               clips.mantle, ["contact.left_hand", "contact.right_hand", "land"]);
}

function buildCourse() {
    world = physics.newWorld3D(0.0, 0.0, 0.0, false);
    local ground = world.newBody("static", 0.0, -0.15, 13.0);
    ground.newBoxShape(10.0, 0.3, 30.0, 1.0, 0.84, 0.0);
    makeVisual(0.0, -0.15, 13.0, 10.0, 0.3, 30.0, 0.10, 0.13, 0.17);

    makeObstacle(-2.8, 5.0, 0.62, 0.12, 0.55, 0.82);
    makeObstacle(0.0, 5.0, 1.00, 0.20, 0.72, 0.48);
    makeObstacle(2.8, 5.0, 1.45, 0.90, 0.52, 0.16);
    makeObstacle(-2.8, 12.0, 1.42, 0.64, 0.34, 0.82);
    makeObstacle(0.0, 12.0, 0.65, 0.18, 0.68, 0.75);

    movingBody = world.newBody("kinematic", 2.8, 0.52, 12.0);
    movingBody.newBoxShape(2.1, 0.82, 0.72, 1.0, 0.75, 0.0);
    movingVisual = makeVisual(2.8, 0.52, 12.0, 2.1, 0.82, 0.72, 0.88, 0.30, 0.34);

    // Elevated landing decks make the three action heights visually legible.
    makeVisual(-2.8, 0.18, 18.5, 2.6, 0.36, 7.0, 0.14, 0.20, 0.28);
    makeVisual(0.0, 0.34, 18.5, 2.6, 0.68, 7.0, 0.16, 0.25, 0.22);
    makeVisual(2.8, 0.55, 18.5, 2.6, 1.10, 7.0, 0.28, 0.20, 0.15);
}

function resetCourse() {
    if (runtime != null) {
        local inspected = runtime.inspect();
        if (inspected.ok && inspected.value.phase != "idle" && inspected.value.phase != "completed" &&
            inspected.value.phase != "cancelled" && inspected.value.phase != "failed") {
            tick += 1;
            runtime.cancel("player_request", tick);
        }
    }
    playerFeet = [lane == 1 ? -2.8 : (lane == 2 ? 0.0 : 2.8), 0.0, 0.0];
    activeAction = "";
    status = "lane " + lane + " ready";
}

function beginClimbingAction() {
    if (activeAction != "") return;
    tick += 1;
    local started = runtime.tryBeginMode(world, playerFeet[0], playerFeet[1], playerFeet[2],
                                         0.0, 1.0, RUN_SPEED, -1, 0.0, true, tick);
    if (!started.ok) {
        status = started.error.message;
        return;
    }
    activeAction = started.value.actionId;
    local clip = activeAction == "parkour:vault_low" ? clips.vaultLow :
                 (activeAction == "parkour:vault_high" ? clips.vaultHigh : clips.mantle);
    actionPlayer.play(clip);
    actionPlayer.setLoop(false);
    status = activeAction;
}

function updateLocomotion(dt) {
    local x = 0.0;
    local z = 0.0;
    if (keyEither("a", "A")) x -= 1.0;
    if (keyEither("d", "D")) x += 1.0;
    if (keyEither("w", "W")) z += 1.0;
    if (keyEither("s", "S")) z -= 1.0;
    local length = sqrt(x * x + z * z);
    if (length > 0.0) { x /= length; z /= length; }
    local running = keyEither("lshift", "LShift") || keyEither("rshift", "RShift");
    local speed = running ? RUN_SPEED : WALK_SPEED;
    local vx = x * speed;
    local vz = z * speed;
    matcher.setDesiredVelocity(vx, vz);
    matcher.setDesiredYaw(0.0);
    matcher.update(dt);
    playerFeet[0] = clampf(playerFeet[0] + vx * dt, -4.1, 4.1);
    playerFeet[2] = clampf(playerFeet[2] + vz * dt, -1.0, 27.0);
    status = length > 0.0 ? (running ? "motion matching: run" : "motion matching: walk") :
                            "motion matching: idle blend";
}

function updateAction() {
    tick += 1;
    local advanced = runtime.advance(world, tick, FIXED_DT);
    if (!advanced.ok) {
        status = advanced.error.message;
        activeAction = "";
        return;
    }
    actionPlayer.update(FIXED_DT);
    playerFeet = [advanced.value.feet.x, advanced.value.feet.y, advanced.value.feet.z];
    status = activeAction + " · " + advanced.value.phase;
    if (advanced.value.phase == "completed" || advanced.value.phase == "cancelled" ||
        advanced.value.phase == "failed") activeAction = "";
}

function applyCharacterPose() {
    local pose = activeAction == "" ? matcher.getPose() : actionPlayer.getPose();
    pose.computeWorld(skeleton);
    foreach (binding in characterSkins)
        binding.skin.applyToMesh(gfx, binding.part.getMesh(), pose);
    foreach (part in characterParts) {
        part.setPosition(playerFeet[0], playerFeet[1], playerFeet[2]);
        part.setYaw(0.0);
        part.setTint(0.90, 0.94, 1.0, 1.0);
    }
}

function fixedUpdate() {
    movingTime += FIXED_DT;
    local movingY = 0.52 + sin(movingTime * 1.35) * 0.24;
    movingBody.setPosition(2.8, movingY, 12.0);
    movingVisual.setPosition(2.8, movingY, 12.0);
    world.update(FIXED_DT);
    if (activeAction == "") updateLocomotion(FIXED_DT); else updateAction();
    applyCharacterPose();
}

eve_init = function() {
    gfx.setBackgroundColor(0.035, 0.055, 0.085, 1.0);
    camera = eve.Camera3D();
    camera.setEye(10.5, 8.0, -4.5);
    camera.setTarget(0.0, 0.9, 8.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(48.0);
    camera.setAmbient(0.28, 0.31, 0.37);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.32, 1.25, 1.18, 1.08);

    physics = eve.Physics();
    animation = eve.Animation();
    climbing = eve.Climbing();
    model3d = eve.Model3D();
    buildCourse();
    loadCharacterAndMotion();
    configureClimbing();
    resetCourse();
    applyCharacterPose();
    print("climbing-motion-matching: W/A/S/D move | Shift run | Space parkour | Q/E lane | R reset\n");
    print("climbing-motion-matching: clips=9 motionFrames=" + motionDb.getFrameCount() +
          " whiteModelMeshes=" + mannequin.getMeshCount() + " license=CC0-1.0\n");
};

eve_update = function(dt) {
    if (edgePressed("Space") || edgePressed("space")) beginClimbingAction();
    if (edgePressed("q") || edgePressed("Q")) { lane = lane == 1 ? 3 : lane - 1; resetCourse(); }
    if (edgePressed("e") || edgePressed("E")) { lane = lane == 3 ? 1 : lane + 1; resetCourse(); }
    if (edgePressed("r") || edgePressed("R")) resetCourse();
    if (edgePressed("F1") || edgePressed("f1")) {
        debugMotion = !debugMotion;
        runtime.setDebugCapture(debugMotion);
    }
    accumulator += dt;
    while (accumulator >= FIXED_DT) {
        fixedUpdate();
        accumulator -= FIXED_DT;
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    gfx.drawSolidRect(18.0, 18.0, 470.0, 82.0, 0.02, 0.035, 0.06, 0.88);
    local color = activeAction == "" ? [0.18, 0.68, 0.92] : [0.96, 0.56, 0.16];
    gfx.drawSolidRect(30.0, 30.0, 20.0, 20.0, color[0], color[1], color[2], 1.0);
    gfx.drawSolidRect(62.0, 30.0, 390.0, 8.0, 0.18, 0.24, 0.32, 1.0);
    gfx.drawSolidRect(62.0, 30.0, 390.0 * (playerFeet[2] + 1.0) / 28.0, 8.0,
                      color[0], color[1], color[2], 1.0);
    gfx.drawSolidRect(62.0, 52.0, debugMotion ? 180.0 : 55.0, 8.0,
                      debugMotion ? 0.25 : 0.32, debugMotion ? 0.86 : 0.36, 0.48, 1.0);
    frame += 1;
    if (!screenshotSaved && frame > 120 && gfx.saveFramePng("climbing-motion-matching.png")) {
        screenshotSaved = true;
        print("climbing-motion-matching: saved climbing-motion-matching.png status=" + status + "\n");
    }
};
