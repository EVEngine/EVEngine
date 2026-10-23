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
persist contactConstraints = null
persist contactPose = null
persist contactRig = []
persist contactBlend = []
persist lastContactAdvance = null
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
persist contactLabs = []
persist contactLabIndex = -1
persist contactLabPhase = ""
persist contactEvidenceSaved = false
persist contactLabAge = 0.0
persist contactLabTransferred = false
persist hudFont = null

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

// Presentation state only: never feeds positions, collisions or gameplay selection.
function emptyContactBlend() {
    return [{weight=0.0, target=null}, {weight=0.0, target=null},
            {weight=0.0, target=null}, {weight=0.0, target=null}];
}

function blendContactPose(previous, desired, dt) {
    if (!(dt > 0.0) || dt > 1.0 || previous.len() != 4 || desired.len() != 4)
        throw "invalid contact blend step";
    local next = [];
    local step = dt / 0.12;
    for (local i = 0; i < 4; ++i) {
        local item = desired[i];
        if (item.weight != item.weight || abs(item.weight) > 1.0 || item.weight < 0.0 ||
            (item.weight > 0.0 && item.target == null)) throw "invalid contact target";
        if (item.target != null) {
            foreach (value in [item.target.x, item.target.y, item.target.z])
                if (value != value || abs(value) > 1.0e20) throw "nonfinite contact target";
        }
        local weight = previous[i].weight + clampf(item.weight - previous[i].weight, -step, step);
        local target = item.weight > 0.0 ? item.target : previous[i].target;
        next.push({weight=weight, target=weight > 0.0 ? target : null});
    }
    return next; // Publish only after validating all four limbs.
}

function configureContactPose() {
    contactConstraints = animation.newConstraintStack(skeleton);
    contactPose = animation.newPose(skeleton.getBoneCount());
    contactRig = [];
    foreach (names in [["upperarm.l", "lowerarm.l", "wrist.l"],
                       ["upperarm.r", "lowerarm.r", "wrist.r"],
                       ["upperleg.l", "lowerleg.l", "foot.l"],
                       ["upperleg.r", "lowerleg.r", "foot.r"]]) {
        local chain = [];
        foreach (name in names) {
            local index = skeleton.findBone(name);
            if (index < 0) throw "missing contact bone: " + name;
            chain.push(index);
        }
        if (skeleton.getParent(chain[1]) != chain[0] || skeleton.getParent(chain[2]) != chain[1])
            throw "contact chain must have consecutive parents";
        contactRig.push(chain);
    }
    contactBlend = emptyContactBlend();
}

function updateContactPose(dt) {
    local desired = emptyContactBlend();
    if (lastContactAdvance != null) {
        local a = lastContactAdvance;
        desired[0] = {weight=a.leftHandWeight, target=a.leftHandAnchor};
        desired[1] = {weight=a.rightHandWeight, target=a.rightHandAnchor};
        desired[2] = {weight=a.hasLeftFootAnchor ? a.leftFootWeight : 0.0, target=a.leftFootAnchor};
        desired[3] = {weight=a.hasRightFootAnchor ? a.rightFootWeight : 0.0, target=a.rightFootAnchor};
    }
    contactBlend = blendContactPose(contactBlend, desired, dt);
}

function loadCharacterAndMotion() {
    mannequin = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_MovementBasic.glb");
    local advanced = model3d.newModelDataFromFile("assets/kaykit/Rig_Medium_MovementAdvanced.glb");
    skeleton = animation.newSkeletonFromModel(mannequin);
    configureContactPose();

    clips.walkA <- clipFrom(mannequin, "Walking_A", true);
    clips.walkB <- clipFrom(mannequin, "Walking_B", true);
    clips.runA <- clipFrom(mannequin, "Running_A", true);
    clips.runB <- clipFrom(mannequin, "Running_B", true);
    clips.strafeL <- clipFrom(advanced, "Running_Strafe_Left", true);
    clips.strafeR <- clipFrom(advanced, "Running_Strafe_Right", true);
    clips.vaultLow <- clipFrom(advanced, "Dodge_Forward", false);
    clips.vaultHigh <- clipFrom(mannequin, "Jump_Full_Short", false);
    clips.mantle <- clipFrom(mannequin, "Jump_Full_Long", false);
    clips.hang <- clipFrom(mannequin, "Jump_Idle", true);

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
        part.setMetallic(0.0);
        if (part.getMaterial() != null) {
            part.getMaterial().setTint(0.85, 0.90, 1.0, 1.0);
            part.getMaterial().setRoughness(0.8);
        }
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
    local profile = runtime.setProfileJson(eve.Filesystem().readText("assets/contact-profile.json"));
    if (!profile.ok) throw profile.error.message;
    runtime.setDebugCapture(true);
    bindAction("parkour:vault_low", "vault", 0.40, 0.78, 0.78, 0.34,
               clips.vaultLow, ["contact.left_hand", "land"]);
    bindAction("parkour:vault_high", "vault", 0.79, 1.15, 0.66, 0.56,
               clips.vaultHigh, ["contact.left_hand", "land"]);
    bindAction("climbing:mantle", "mantle", 1.16, 1.62, 0.58, 0.78,
               clips.mantle, ["contact.left_hand", "contact.right_hand", "land"]);
}

function buildContactLabs() {
    contactLabs = [];
    local names = ["wall", "free", "narrow", "ladder", "climb_up", "corner_outer", "corner_inner", "back_jump"];
    for (local i = 0; i < names.len(); ++i) {
        local x = 12.0 + i * 6.0;
        local y = i == 1 ? 2.45 : 1.0;
        local corner = i == 5 || i == 6;
        local depth = corner ? 2.0 : 0.5;
        local width = i == 5 ? 2.0 : (i == 6 ? 6.0 : (i == 2 ? 0.2 : (i == 3 ? 0.6 : 4.0)));
        local height = i == 1 ? 0.1 : (i == 3 ? 4.2 : 3.0);
        local bodyX = x - (i == 2 ? 0.15 : 0.0);
        local body = world.newBody("static", bodyX, y, 4.0);
        body.newBoxShape(width, height, depth, 1.0, 0.76, 0.0);
        if (i == 6) {
            body.newBoxShape(2.0, 3.0, 4.0, 1.0, 0.76, 0.0).setLocalPosition(2.0, 0.0, -3.0);
            makeVisual(x + 2.0, y, 1.0, 2.0, 3.0, 4.0, 0.3, 0.58, 0.5);
        }
        if (i == 7) {
            body.newBoxShape(4.0, 3.0, 0.5, 1.0, 0.76, 0.0).setLocalPosition(0.0, 0.0, -3.0);
            makeVisual(x, y, 1.0, 4.0, 3.0, 0.5, 0.3, 0.58, 0.5);
        }
        if (i == 3) {
            foreach (side in [-1.0, 1.0])
                makeVisual(x + side * 0.26, 1.6, 4.0, 0.08, 3.0, 0.12, 0.30, 0.42, 0.52);
            for (local rung = 0; rung < 10; ++rung)
                makeVisual(x, 0.4 + rung * 0.3, 3.75, 0.6, 0.06, 0.10, 0.80, 0.65, 0.28);
        } else makeVisual(bodyX, y, 4.0, width, height, depth, 0.24, 0.52, 0.58);
        makeVisual(x, -0.1, 3.0, 5.0, 0.2, 5.0, 0.10, 0.13, 0.17);
        local graph = climbing.newAnchorGraphJson(
            eve.Filesystem().readText("assets/contact-" + names[i] + ".json"), world, body);
        if (!graph.ok) throw graph.status.summary;
        contactLabs.push({name=names[i], x=x, graph=graph.value});
    }
}

function startContactLab(index) {
    resetCourse();
    contactLabIndex = index;
    contactLabPhase = "";
    contactEvidenceSaved = false;
    contactLabAge = 0.0;
    contactLabTransferred = false;
    local lab = contactLabs[index];
    playerFeet = [lab.x + (index == 5 || index == 6 ? 0.5 : 0.0), 0.95,
                  index == 5 || index == 6 ? 2.77 : 3.52];
    local action = index == 3 ? "contact:ladder_mount" : "contact:ledge_grab";
    tick += 1;
    local started = runtime.tryBeginAnchor(lab.graph, world, "start", 1, action,
        playerFeet[0], playerFeet[1], playerFeet[2], 0.0, 1.0, 0.0, -1, 0.0, false, tick);
    if (!started.ok) throw started.status.summary;
    activeAction = action;
    actionPlayer.play(clips.hang);
    actionPlayer.setLoop(true);
    camera.setEye(lab.x + (index == 6 ? -3.5 : (index == 7 ? 6.0 : 3.5)), index == 4 ? 4.2 : 3.3,
                  index == 7 ? 2.5 : -0.5);
    camera.setTarget(lab.x, index == 4 ? 2.5 : 1.6, index == 7 ? 2.5 : 3.7);
    print("contact-lab: " + lab.name + " started\n");
}

function transitionContactLab() {
    if (contactLabIndex < 0 || contactLabIndex == 2 || contactLabPhase != "hanging" || contactLabTransferred) return;
    if (contactLabIndex == 4) {
        tick += 1;
        local climbed = runtime.climbUp(tick);
        if (!climbed.ok) throw climbed.status.summary;
        contactLabTransferred = true;
        activeAction = "contact:climb_up";
        actionPlayer.play(clips.mantle);
        actionPlayer.setLoop(false);
        return;
    }
    local ladder = contactLabIndex == 3;
    local special = contactLabIndex >= 5;
    local action = special ? "contact:" + contactLabs[contactLabIndex].name :
                   (ladder ? "contact:ladder_climb" : "contact:shimmy");
    tick += 1;
    local moved = runtime.transitionAnchor(contactLabs[contactLabIndex].graph, world,
        "next", special ? (contactLabIndex == 7 ? "jump" : "corner") :
                (ladder ? "climb" : "shimmy"), action, tick);
    if (!moved.ok) throw moved.status.summary;
    contactLabTransferred = true;
    contactEvidenceSaved = false;
    activeAction = action;
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
    contactLabIndex = -1;
    camera.setEye(10.5, 8.0, -4.5);
    camera.setTarget(0.0, 0.9, 8.0);
    if (runtime != null) {
        local inspected = runtime.inspect();
        if (inspected.ok && inspected.value.phase != "idle" && inspected.value.phase != "completed" &&
            inspected.value.phase != "cancelled" && inspected.value.phase != "failed") {
            tick += 1;
            local cancelled = runtime.cancel("player_request", tick);
            if (!cancelled.ok) throw cancelled.status.summary;
        }
    }
    contactBlend = emptyContactBlend();
    lastContactAdvance = null;
    playerFeet = [lane == 1 ? -2.8 : (lane == 2 ? 0.0 : 2.8), 0.0, 0.0];
    activeAction = "";
    status = "lane " + lane + " ready";
}

function beginClimbingAction() {
    if (contactLabIndex >= 0) { transitionContactLab(); return; }
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
        lastContactAdvance = null;
        status = advanced.status.summary;
        activeAction = "";
        return;
    }
    lastContactAdvance = advanced.value;
    if (contactLabIndex >= 0 && contactLabPhase != advanced.value.phase) {
        contactLabPhase = advanced.value.phase;
        print("contact-lab: " + contactLabs[contactLabIndex].name + " " + contactLabPhase +
              " leftFoot=" + advanced.value.hasLeftFootAnchor +
              " rightFoot=" + advanced.value.hasRightFootAnchor + "\n");
    }
    actionPlayer.update(FIXED_DT);
    playerFeet = [advanced.value.feet.x, advanced.value.feet.y, advanced.value.feet.z];
    if (contactLabIndex == 4 && advanced.value.phase == "completed") {
        if (abs(playerFeet[0] - contactLabs[4].x) > 0.03 || abs(playerFeet[1] - 2.5) > 0.03 ||
            abs(playerFeet[2] - 4.0) > 0.03)
            throw "climb-up did not reach the platform landing";
        print("contact-lab: climb_up verified landing=" + playerFeet[0] + "," +
              playerFeet[1] + "," + playerFeet[2] + "\n");
    }
    status = activeAction + " · " + advanced.value.phase;
    if (advanced.value.phase == "completed" || advanced.value.phase == "cancelled" ||
        advanced.value.phase == "failed") activeAction = "";
}

function applyCharacterPose() {
    // KayKit's forward axis is -Z; graph contact poses face the +Z wall.
    local yaw = contactLabIndex >= 0 ? 3.141592654 : 0.0;
    if (contactLabIndex >= 5 && contactLabTransferred && lastContactAdvance != null) {
        local t = lastContactAdvance.normalizedTime;
        local turn = contactLabIndex == 5 ? -1.570796327 :
                     (contactLabIndex == 6 ? 1.570796327 : 3.141592654);
        yaw += turn * t * t * (3.0 - 2.0 * t);
    }
    local cosine = cos(yaw);
    local sine = sin(yaw);
    local basePose = activeAction == "" ? matcher.getPose() : actionPlayer.getPose();
    contactPose.copyFrom(basePose);
    contactConstraints.clear();
    for (local i = 0; i < 4; ++i) {
        local contact = contactBlend[i];
        if (contact.weight <= 0.0 || contact.target == null) continue;
        local chain = contactRig[i];
        local dx = contact.target.x - playerFeet[0];
        local dz = contact.target.z - playerFeet[2];
        contactConstraints.addTwoBoneIK(chain[0], chain[1], chain[2],
            cosine * dx - sine * dz, contact.target.y - playerFeet[1],
            sine * dx + cosine * dz, contact.weight);
    }
    contactConstraints.apply(contactPose);
    if (contactLabIndex >= 0 && contactLabPhase == "hanging" && !contactEvidenceSaved) {
        contactEvidenceSaved = true;
        for (local i = 0; i < 4; ++i) {
            local bone = contactRig[i][2];
            local contact = contactBlend[i];
            if (contact.weight > 0.99 && contact.target != null) {
                local dx = contact.target.x - playerFeet[0];
                local dz = contact.target.z - playerFeet[2];
                local ex = contactPose.getWorldPositionX(bone) - (cosine * dx - sine * dz);
                local ey = contactPose.getWorldPositionY(bone) - (contact.target.y - playerFeet[1]);
                local ez = contactPose.getWorldPositionZ(bone) - (sine * dx + cosine * dz);
                if (sqrt(ex * ex + ey * ey + ez * ez) > 0.05)
                    throw "contact lab limb cannot reach its authored target: " + i;
            }
            print("contact-lab: limb=" + i + " weight=" + contact.weight +
                  " modelTip=" + contactPose.getWorldPositionX(bone) + "," +
                  contactPose.getWorldPositionY(bone) + "," + contactPose.getWorldPositionZ(bone) + "\n");
        }
    }
    local pose = contactPose;
    foreach (binding in characterSkins)
        binding.skin.applyToMesh(gfx, binding.part.getMesh(), pose);
    foreach (part in characterParts) {
        part.setPosition(playerFeet[0], playerFeet[1], playerFeet[2]);
        part.setYaw(yaw);
        part.setTint(0.90, 0.94, 1.0, 1.0);
    }
}

function fixedUpdate() {
    movingTime += FIXED_DT;
    local movingY = 0.52 + sin(movingTime * 1.35) * 0.24;
    movingBody.setPosition(2.8, movingY, 12.0);
    movingVisual.setPosition(2.8, movingY, 12.0);
    world.update(FIXED_DT);
    if (activeAction == "") {
        lastContactAdvance = null;
        if (contactLabIndex < 0) updateLocomotion(FIXED_DT);
        else {
            matcher.setDesiredVelocity(0.0, 0.0);
            matcher.update(FIXED_DT);
        }
    } else updateAction();
    local events = runtime.drainEvents();
    if (!events.ok) throw events.status.summary;
    foreach (event in events.value)
        if (debugMotion) print("climbing-event: " + event.kind + " action=" + event.actionId + "\n");
    updateContactPose(FIXED_DT);
    applyCharacterPose();
    if (contactLabIndex >= 0 && "contactLabAutoAdvance" in config && config.contactLabAutoAdvance) {
        contactLabAge += FIXED_DT;
        if (contactLabAge >= 2.0) transitionContactLab();
        if (contactLabAge >= 5.0) startContactLab((contactLabIndex + 1) % contactLabs.len());
    }
}

eve_init = function() {
    local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 18);
    hudFont = gfx.newFont(fontData, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789|:.-");
    gfx.setFont(hudFont);
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
    buildContactLabs();
    resetCourse();
    if ("contactLab" in config && config.contactLab >= 0) startContactLab(config.contactLab);
    applyCharacterPose();
    print("climbing-motion-matching: F2 contact lab | Space traverse | R course | W/A/S/D move | Shift run | Q/E lane\n");
    print("climbing-motion-matching: clips=10 motionFrames=" + motionDb.getFrameCount() +
          " whiteModelMeshes=" + mannequin.getMeshCount() + " license=CC0-1.0\n");
};

eve_update = function(dt) {
    if (edgePressed("F2") || edgePressed("f2")) startContactLab((contactLabIndex + 1) % contactLabs.len());
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
    gfx.drawSolidRect(18.0, 18.0, 470.0, 140.0, 0.02, 0.035, 0.06, 0.88);
    local color = activeAction == "" ? [0.18, 0.68, 0.92] : [0.96, 0.56, 0.16];
    gfx.drawSolidRect(30.0, 30.0, 20.0, 20.0, color[0], color[1], color[2], 1.0);
    gfx.drawSolidRect(62.0, 30.0, 390.0, 8.0, 0.18, 0.24, 0.32, 1.0);
    gfx.drawSolidRect(62.0, 30.0, 390.0 * (playerFeet[2] + 1.0) / 28.0, 8.0,
                      color[0], color[1], color[2], 1.0);
    gfx.drawSolidRect(62.0, 52.0, debugMotion ? 180.0 : 55.0, 8.0,
                      debugMotion ? 0.25 : 0.32, debugMotion ? 0.86 : 0.36, 0.48, 1.0);
    for (local i = 0; i < contactBlend.len(); ++i)
        gfx.drawSolidRect(30.0 + i * 110.0, 80.0, 100.0 * contactBlend[i].weight, 6.0,
                          i < 2 ? 0.25 : 0.9, 0.78, 0.55, 1.0);
    gfx.print(contactLabIndex < 0 ? "Parkour course" : "Contact: " + contactLabs[contactLabIndex].name,
              30.0, 103.0, 0.9, 0.94, 1.0, 1.0, 1.0);
    gfx.print("F2: next contact | Space: traverse | R: course", 30.0, 128.0,
              0.65, 0.75, 0.85, 1.0, 0.8);
    frame += 1;
    if (!screenshotSaved && frame > 120 && gfx.saveFramePng("climbing-motion-matching.png")) {
        screenshotSaved = true;
        print("climbing-motion-matching: saved climbing-motion-matching.png status=" + status + "\n");
    }
};
