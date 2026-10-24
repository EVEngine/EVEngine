// Play mode.
//
// Play owns one World3D containing the terrain, exact static mesh colliders,
// dynamic convex pieces and the dynamic character capsule. Motion Matching is
// visual animation only; Box3D solves contacts and bilateral impulses. Fixed
// 60 Hz stepping never changes authored state, and Stop destroys this world.

function levelPlayGravity() { return -18.0; }
function levelPlayerRadius() { return level.play!=null ? level.play.radius : 0.36; }
function levelPlayerHalfHeight() { return level.play!=null ? level.play.halfHeight : 0.55; }

function levelCreatePlayState() {
    return {
        world = null, rig = null, characterName = "", motionSetName = "",
        positionX = 0.0, positionY = 0.0, positionZ = 0.0,
        velocityY = 0.0, grounded = false, facing = 0.0,
        radius = 0.36, halfHeight = 0.55, scale = [1.0,1.0,1.0],
        focusX = 0.0, focusY = 0.0, focusZ = 0.0,
        accumulator = 0.0, steps = 0, lastCost = 0.0,
        bodies = [], dynamics = [], npcs = [], playerSpawn = "", playerBody = null, paused = false,
        inputJump = false,
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

/** @brief Stage the runtime terrain and live pieces in one caller-owned world. */
function levelBuildPlayWorld() {
    local play = level.play;
    play.world = level.physics.newWorld3D(0.0, levelPlayGravity(), 0.0, true);
    if (play.world == null) { level.status = "physics world creation failed"; return false; }
    local terrainBody = terrainBuildCollider(play.world);
    if (terrainBody == null) throw "Terrain collider creation failed";
    play.bodies.push(terrainBody);
    foreach (id, piece in level.pieces) {
        if (levelObject(id) == null) continue;
        local body = whiteboxBuildCollider(play.world, id);
        if (body == null) throw "Collider missing: " + id;
        play.bodies.push(body);
        if (piece.motion == "dynamic") {
            local t=levelTransformRelativeTo(id, "");
            play.dynamics.push({body=body,entity=piece.entity,scale=[t.scaleX,t.scaleY,t.scaleZ],
                offset=[0.0,0.0,0.0]});
        }
    }
    foreach(id,spawn in level.spawns) {
        if(id==play.playerSpawn || levelObject(id)==null) continue;
        spawnApplyTransform(spawn,levelObject(id));
        local point=scene.localToWorldAt(levelHost(),id,0.0,0.0,0.0);
        local t=levelTransformRelativeTo(id,"");
        local radius=0.36*(t.scaleX>t.scaleZ ? t.scaleX : t.scaleZ), half=0.55*t.scaleY;
        local body=play.world.newBody("dynamic",point[0],point[1]+radius+half+0.05,point[2]);
        local capsule=body.newCapsuleShape(half*2.0,radius,70.0,0.0,0.0);
        if(capsule==null) throw "NPC capsule creation failed";
        body.setFixedRotation(true);
        play.bodies.push(body);play.npcs.push({body=body,rig=spawn.rig,offset=radius+half});
    }
    return true;
}

function levelSpawnPlayerAt(spawnId) {
    local play = level.play;
    local spawn = (spawnId != "" && (spawnId in level.spawns)) ? level.spawns[spawnId] : null;
    local object = spawnId != "" ? levelObject(spawnId) : null;
    if (object != null) {
        local point = scene.localToWorldAt(levelHost(),object.id,0.0,0.0,0.0);
        play.positionX = point[0];
        play.positionY = point[1];
        play.positionZ = point[2];
        local t=levelTransformRelativeTo(object.id,"");play.scale=[t.scaleX,t.scaleY,t.scaleZ];
        play.radius=0.36*(t.scaleX>t.scaleZ ? t.scaleX : t.scaleZ);play.halfHeight=0.55*t.scaleY;
    }
    play.facing = 0.0;
    if(spawn!=null && object!=null) {
        local a=scene.localToWorldAt(levelHost(),object.id,0.0,0.0,0.0);
        local b=scene.localToWorldAt(levelHost(),object.id,sin(spawn.yaw),0.0,cos(spawn.yaw));
        play.facing=atan2(b[0]-a[0],b[2]-a[2]);
    }
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
    if (level.mode == "play") return;
    terrainEndStroke();
    if(level.gizmo.isDragging()) { level.gizmo.endDrag();levelSyncGizmo(); }
    scene.updateTransformsAll();
    if (level.terrain == null) { level.status = "no terrain to play on"; return; }
    level.play = levelCreatePlayState();
    level.play.characterName = levelActiveCharacterName();
    level.play.motionSetName = levelActiveMotionSetName();
    local spawnId = spawnResolveStart();
    level.play.playerSpawn=spawnId;
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
    try {
        if (!levelBuildPlayWorld()) throw level.status;
        local play=level.play, offset=levelPlayerRadius()+levelPlayerHalfHeight();
        play.playerBody=play.world.newBody("dynamic",play.positionX,play.positionY+offset,play.positionZ);
        local capsule=play.playerBody.newCapsuleShape(levelPlayerHalfHeight()*2.0,levelPlayerRadius(),70.0,0.0,0.0);
        if(capsule==null) throw "Player capsule creation failed";
        play.playerBody.setFixedRotation(true);play.playerBody.setSleepEnabled(false);play.playerBody.setBullet(true);
    } catch(error) {
        if(level.play.world!=null) level.play.world.destroy();
        level.play=null;level.status="Play failed: " + error;print(level.status + "\n");return;
    }
    if (!levelEnsurePlayRig()) { levelStopPlay(); return; }

    level.play.world.setMoverUp(0.0, 1.0, 0.0);
    level.play.world.setMoverSlopeLimit(52.0);

    // Spawn markers are authoring aids; hide them while playing.
    foreach (id, spawn in level.spawns) {
        if (spawn.entity != null) spawn.entity.setVisible(false);
        if (spawn.facing != null) spawn.facing.setVisible(false);
        if(spawn.rig!=null) foreach(part in spawn.rig.parts) part.setVisible(false);
    }
    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(levelObject(id) != null);
    levelShowPlayer(true);
    foreach(npc in level.play.npcs) foreach(part in npc.rig.parts) part.setVisible(true);

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
    if (level.play != null) {
        levelShowPlayer(false);
        if (level.play.world != null) level.play.world.destroy();
    }
    level.mode = "edit";
    level.play = null;
    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(levelObject(id) != null);
    foreach (id, spawn in level.spawns) {
        if (spawn.entity != null) spawn.entity.setVisible(levelObject(id) != null);
        if (spawn.facing != null) spawn.facing.setVisible(levelObject(id) != null);
    }
    level.status = "edit mode";
    levelMarkPanelsDirty();
    whiteboxSyncTransforms();spawnSyncTransforms();
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

    local body=play.playerBody;
    local vy=body.getLinearVelocityY();
    if(play.inputJump && play.grounded) vy=6.5;
    play.inputJump=false;
    body.setLinearVelocity(velocityX,vy,velocityZ);
    play.world.update(dt);
    play.positionX=body.getX();play.positionY=body.getY()-levelPlayerRadius()-levelPlayerHalfHeight();
    play.positionZ=body.getZ();play.velocityY=body.getLinearVelocityY();
    play.world.setQueryIgnoredBodyId(body.getId());
    local hits=play.world.rayCast(play.positionX,play.positionY+0.15,play.positionZ,
        play.positionX,play.positionY-0.18,play.positionZ);
    play.grounded=hits>0 && play.world.getRayHitNormalY()>cos(52.0/57.2957795) && play.velocityY<1.0;
    play.world.clearQueryIgnores();
    levelSyncDynamicPieces();
    foreach(npc in play.npcs) foreach(part in npc.rig.parts)
        part.setPosition(npc.body.getX(),npc.body.getY()-npc.offset,npc.body.getZ());

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
        part.setScale(play.scale[0],play.scale[1],play.scale[2]);
    }
}

function levelUpdatePlay(dt) {
    local play = level.play;
    if (play == null || play.paused) return;
    play.accumulator += levelClamp(dt, 0.0, 0.25);
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

/** @brief Rotate a local offset using the runtime body's quaternion. */
function levelRotateQuaternion(q,v) {
    local u=[q[0],q[1],q[2]], cross=[u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]];
    local cross2=[u[1]*cross[2]-u[2]*cross[1],u[2]*cross[0]-u[0]*cross[2],u[0]*cross[1]-u[1]*cross[0]];
    return [v[0]+2.0*(q[3]*cross[0]+cross2[0]),v[1]+2.0*(q[3]*cross[1]+cross2[1]),
            v[2]+2.0*(q[3]*cross[2]+cross2[2])];
}
function levelSyncDynamicPieces() {
    foreach(entry in level.play.dynamics) {
        local body=entry.body,q=[body.getRotX(),body.getRotY(),body.getRotZ(),body.getRotW()];
        local offset=levelRotateQuaternion(q,entry.offset);
        local basis=[[0.0,0.0,0.0]];
        foreach(axis in [[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]) basis.push(levelRotateQuaternion(q,axis));
        local t=levelBasisTransform(basis);
        entry.entity.setPosition(body.getX()-offset[0],body.getY()-offset[1],body.getZ()-offset[2]);
        entry.entity.setRotation(t.rotationY,t.rotationX,t.rotationZ);
        entry.entity.setScale(entry.scale[0],entry.scale[1],entry.scale[2]);
    }
}
function levelStepPlay() {
    if(level.play==null) return;
    level.play.paused=true;
    levelFixedPlayStep(1.0/60.0);levelApplyPlayPose();
    levelMarkPanelsDirty();
}
