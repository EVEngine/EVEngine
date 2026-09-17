// Play mode.
//
// Entering play builds a runtime world from the authored level (terrain height
// field + one static box per whitebox piece), spawns the player at the start
// point and drives the character with the Motion Matching controller. Leaving
// play tears the runtime down and restores the editor camera; authored data is
// never mutated by a play session.

function levelPlayGravity() { return -18.0; }
function levelPlayerRadius() { return 0.36; }
function levelPlayerHalfHeight() { return 0.55; }

function levelCreatePlayState() {
    return {
        world = null, rig = null, characterName = "", motionSetName = "",
        positionX = 0.0, positionY = 0.0, positionZ = 0.0,
        velocityY = 0.0, grounded = false, facing = 0.0,
        focusX = 0.0, focusY = 0.0, focusZ = 0.0,
        accumulator = 0.0, steps = 0, lastCost = 0.0,
        bodies = [],
        // Third-person view: the camera orbits the player, and locomotion is
        // camera-relative, so mouse look also steers where "forward" points.
        cameraYaw = 0.0, cameraPitch = 0.26, cameraDistance = 6.4,
        lastMouseX = 0.0, lastMouseY = 0.0,
        // Script- and AI-drivable movement input. When `inputDriven` is false the
        // keyboard is read; setting it lets a script (or an AI controller, or a
        // smoke test) drive the same Motion Matching locomotion path.
        inputDriven = false, inputForward = 0.0, inputStrafe = 0.0, inputRunning = false
    };
}

/** @brief Normalized movement intent: forward/strafe in [-1,1] plus run flag. */
function levelPlayReadInput() {
    local play = level.play;
    if (play.inputDriven)
        return {forward = play.inputForward, strafe = play.inputStrafe, running = play.inputRunning};
    local forward = 0.0, strafe = 0.0;
    if (!ui.wantCaptureKeyboard()) {
        if (levelKeyEither("w", "W")) forward += 1.0;
        if (levelKeyEither("s", "S")) forward -= 1.0;
        if (levelKeyEither("a", "A")) strafe -= 1.0;
        if (levelKeyEither("d", "D")) strafe += 1.0;
    }
    return {forward = forward, strafe = strafe,
            running = levelKeyEither("lshift", "LShift") || levelKeyEither("rshift", "RShift")};
}

function levelPlayerFeet() {
    local play = level.play;
    return [play.positionX, play.positionY, play.positionZ];
}

/** @brief Runtime world: terrain collider plus one static body per piece. */
function levelBuildPlayWorld() {
    local play = level.play;
    play.world = level.physics.newWorld3D(0.0, levelPlayGravity(), 0.0, true);
    if (play.world == null) { level.status = "physics world creation failed"; return false; }
    local terrainBody = terrainBuildCollider(play.world);
    if (terrainBody != null) play.bodies.push(terrainBody);
    foreach (id, piece in level.pieces) {
        local body = whiteboxBuildCollider(play.world, id);
        if (body != null) play.bodies.push(body);
    }
    return true;
}

function levelSpawnPlayerAt(spawnId) {
    local play = level.play;
    local spawn = (spawnId != "" && (spawnId in level.spawns)) ? level.spawns[spawnId] : null;
    local object = spawnId != "" ? levelObject(spawnId) : null;
    if (object != null) {
        local t = object.transform;
        play.positionX = t.x;
        play.positionY = t.y;
        play.positionZ = t.z;
    }
    play.facing = spawn != null ? spawn.yaw : 0.0;
    if (spawn != null) {
        if (spawn.character != "") play.characterName = spawn.character;
        if (spawn.motionSet != "") play.motionSetName = spawn.motionSet;
    }
    if (play.characterName == "") play.characterName = levelActiveCharacterName();
    if (play.motionSetName == "") play.motionSetName = levelActiveMotionSetName();
    return true;
}

function levelShowPlayer(visible) {
    local play = level.play;
    if (play == null || play.rig == null) return;
    foreach (part in play.rig.parts) part.setVisible(visible);
}

/** @brief Rebind the rig when the player switches character or motion set. */
function levelEnsurePlayRig() {
    local play = level.play;
    if (play.rig != null && play.characterName == play.rig.name &&
        play.motionSetName == play.rig.motionSet) return true;
    local rig = levelBuildRig(play.characterName, play.motionSetName);
    if (rig == null) return false;
    if (play.rig != null && play.rig != rig) foreach (part in play.rig.parts) part.setVisible(false);
    play.rig = rig;
    return true;
}

function levelSwitchPlayCharacter(name) {
    local play = level.play;
    if (play == null) { levelSetCharacter(name); return; }
    play.characterName = name;
    level.characterName = name;
    levelShowPlayer(false);
    if (levelEnsurePlayRig()) levelShowPlayer(true);
    levelMarkPanelsDirty();
}

function levelSwitchPlayMotionSet(name) {
    local play = level.play;
    if (play == null) { levelSetMotionSet(name); return; }
    play.motionSetName = name;
    level.motionSetName = name;
    levelShowPlayer(false);
    if (levelEnsurePlayRig()) levelShowPlayer(true);
    levelMarkPanelsDirty();
}

function levelTogglePlay() {
    if (level.mode == "play") levelStopPlay(); else levelStartPlay();
}

function levelStartPlay() {
    if (level.terrain == null) { level.status = "no terrain to play on"; return; }
    level.play = levelCreatePlayState();
    level.play.characterName = levelActiveCharacterName();
    level.play.motionSetName = levelActiveMotionSetName();
    local spawnId = spawnResolveStart();
    if (spawnId == "") {
        // A level may legitimately have no authored spawn point yet; start at the
        // terrain centre so Play still previews the geometry.
        local extent = terrainWorldExtent(level.terrain);
        level.play.positionX = (extent.minX + extent.maxX) * 0.5;
        level.play.positionZ = (extent.minZ + extent.maxZ) * 0.5;
        level.play.positionY = terrainSnapHeight(level.play.positionX, level.play.positionZ) + 0.2;
    } else {
        levelSpawnPlayerAt(spawnId);
        level.play.positionY += 0.2;
    }
    if (!levelBuildPlayWorld()) { level.play = null; return; }
    if (!levelEnsurePlayRig()) { levelStopPlay(); return; }

    level.play.world.setMoverUp(0.0, 1.0, 0.0);
    level.play.world.setMoverSlopeLimit(52.0);

    // Spawn markers are authoring aids; hide them while playing.
    foreach (id, spawn in level.spawns) {
        if (spawn.entity != null) spawn.entity.setVisible(false);
        if (spawn.facing != null) spawn.facing.setVisible(false);
    }
    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(false);
    levelShowPlayer(true);

    if (level.controller == null) {
        level.controller = eve.CameraController();
        level.controller.setCamera(level.camera);
        level.controller.setMode("follow");
        level.controller.setSmooth(7.0);
        level.controller.setPositionSmooth(9.0);
        level.controller.setTargetSmooth(11.0);
        level.controller.setFov(58.0);
        level.controller.setCollisionEnabled(false);
    }
    // Start the view where the player is facing, and seed the mouse-look history
    // so the first frame of play does not snap the camera.
    level.play.cameraYaw = level.play.facing;
    level.play.focusX = level.play.positionX;
    level.play.focusY = level.play.positionY + 1.0;
    level.play.focusZ = level.play.positionZ;
    level.play.lastMouseX = mouse.getX();
    level.play.lastMouseY = mouse.getY();
    try {
        ui.select("viewport");
        if (ui.viewportHovered("level-vp")) {
            level.play.lastMouseX = ui.viewportMouseX("level-vp");
            level.play.lastMouseY = ui.viewportMouseY("level-vp");
        }
    } catch (error) {}
    levelApplyPlayCameraOffset();
    level.controller.setTarget(level.play.focusX, level.play.focusY, level.play.focusZ);
    level.controller.snap();
    level.controller.update(0.016);

    level.mode = "play";
    level.status = "playing " + level.play.characterName + " / " + level.play.motionSetName +
                   " (" + level.play.rig.clipCount + " clips)";
    levelMarkPanelsDirty();
    levelMountPanels();
}

function levelStopPlay() {
    if (level.play != null) levelShowPlayer(false);
    level.mode = "edit";
    level.play = null;
    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(true);
    foreach (id, spawn in level.spawns) {
        if (spawn.entity != null) spawn.entity.setVisible(true);
        if (spawn.facing != null) spawn.facing.setVisible(true);
    }
    level.status = "edit mode";
    levelMarkPanelsDirty();
    levelUpdateOrbitCamera();
    levelMountPanels();
}

/**
 * @brief Rotate the third-person view by a cursor delta, in pixels.
 *
 * Split out from the input handler so the look behaviour is a named, testable
 * operation rather than inline arithmetic.
 */
function levelPlayLook(deltaX, deltaY) {
    local play = level.play;
    if (play == null) return;
    play.cameraYaw -= deltaX * 0.0045;
    play.cameraPitch = levelClamp(play.cameraPitch + deltaY * 0.0045 * 0.7, -0.35, 1.15);
}

/**
 * @brief Heading the player's "forward" input means.
 *
 * Locomotion is camera-relative, so this follows the third-person camera rather
 * than a fixed world axis: turning the view turns the direction WASD runs.
 */
function levelPlayInputYaw() {
    return level.play != null ? level.play.cameraYaw : level.yaw;
}

/**
 * @brief Place the follow camera behind the player from yaw/pitch/distance.
 *
 * `CameraController` in `follow` mode evaluates `eye = target + offset`, so the
 * orbit is expressed entirely as an offset vector; the same `(sin, cos)` heading
 * drives both the camera and the movement basis above.
 */
function levelApplyPlayCameraOffset() {
    local play = level.play;
    if (play == null || level.controller == null) return;
    local horizontal = cos(play.cameraPitch) * play.cameraDistance;
    level.controller.setOffset(-sin(play.cameraYaw) * horizontal,
                               sin(play.cameraPitch) * play.cameraDistance,
                               -cos(play.cameraYaw) * horizontal);
}

function levelFixedPlayStep(dt) {
    local play = level.play;
    if (play == null || play.rig == null) return;

    local intent = levelPlayReadInput();
    local forward = intent.forward, strafe = intent.strafe;
    local magnitude = sqrt(forward * forward + strafe * strafe);
    if (magnitude > 0.0) { forward /= magnitude; strafe /= magnitude; }

    local running = intent.running;
    local speed = running ? 4.4 : (magnitude > 0.0 ? 1.9 : 0.0);
    local yaw = levelPlayInputYaw();
    local rightX = cos(yaw), rightZ = -sin(yaw);
    local forwardX = sin(yaw), forwardZ = cos(yaw);
    local velocityX = (forwardX * forward + rightX * strafe) * speed;
    local velocityZ = (forwardZ * forward + rightZ * strafe) * speed;

    local matcher = play.rig.matcher;
    matcher.setDesiredVelocity(velocityX, velocityZ);
    if (magnitude > 0.0) play.facing = atan2(velocityX, velocityZ);
    matcher.setDesiredYaw(play.facing);
    matcher.update(dt);
    play.lastCost = matcher.getLastSearchCost();

    play.velocityY += levelPlayGravity() * dt;
    if (play.grounded && play.velocityY < 0.0) play.velocityY = 0.0;

    local radius = levelPlayerRadius();
    local halfHeight = levelPlayerHalfHeight();
    local ax = play.positionX, ay = play.positionY + radius, az = play.positionZ;
    local bx = ax, by = ay + halfHeight * 2.0, bz = az;
    local dx = velocityX * dt, dy = play.velocityY * dt, dz = velocityZ * dt;
    play.world.moveCapsule(ax, ay, az, bx, by, bz, radius, dx, dy, dz);
    play.positionX += play.world.getMoverDeltaX();
    play.positionY += play.world.getMoverDeltaY();
    play.positionZ += play.world.getMoverDeltaZ();
    play.grounded = play.world.isMoverGrounded();
    if (play.grounded && play.velocityY < 0.0) play.velocityY = 0.0;

    play.focusX = play.positionX;
    play.focusY = play.positionY + 1.1;
    play.focusZ = play.positionZ;
    play.steps += 1;
}

function levelApplyPlayPose() {
    local play = level.play;
    if (play == null || play.rig == null) return;
    local pose = play.rig.matcher.getPose();
    pose.computeWorld(play.rig.skeleton);
    foreach (binding in play.rig.skins)
        binding.skin.applyToMesh(gfx, binding.part.getMesh(), pose);
    foreach (part in play.rig.parts) {
        part.setPosition(play.positionX, play.positionY, play.positionZ);
        part.setYaw(play.facing);
    }
}

function levelUpdatePlay(dt) {
    local play = level.play;
    if (play == null) return;
    play.accumulator += dt;
    local fixed = 1.0 / 60.0;
    local guard = 0;
    while (play.accumulator >= fixed && guard < 8) {
        levelFixedPlayStep(fixed);
        levelApplyPlayPose();
        play.accumulator -= fixed;
        guard += 1;
    }
    // The follow camera is owned by the editor and reused across play sessions.
    if (level.controller != null) {
        level.controller.setTarget(play.focusX, play.focusY, play.focusZ);
        levelApplyPlayCameraOffset();
        level.controller.update(dt);
    }
}
