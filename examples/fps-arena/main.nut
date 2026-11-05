// FPS Arena — Phase A vertical slice (plan: docs/dev/2026-11-06-fps-vertical-slice-plan.md).
// Composes: CameraController firstperson + relative mouse + World3D.moveCapsule +
// weapon Fire events → rayCast → CombatRuntime.applyDamage. Weapon never owns HP.
//
// Run: make run/<platform>-debug GAME=examples/fps-arena
// Smoke: MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh fps-arena

persist physicsMod = null
persist world = null
persist weaponMod = null
persist rifle = null
persist combat = null
persist cam = null
persist ctrl = null
persist font = null
persist visuals = []
persist targets = []
persist playerFeet = [0.0, 0.0, 0.0]
persist playerVelY = 0.0
persist grounded = true
persist lookYaw = 180.0
persist lookPitch = 0.0
persist mouseCaptured = false
persist prevKeys = {}
persist tick = 0
persist hudMessage = "LMB fire | RMB ADS | R reload | Esc capture"
persist hitFlash = 0.0
persist tracerLife = 0.0
persist tracerOx = 0.0
persist tracerOy = 0.0
persist tracerOz = 0.0
persist tracerHx = 0.0
persist tracerHy = 0.0
persist tracerHz = 0.0
persist autoVerify = 0.5
persist verified = false
persist playerUuid = "11121314-1516-1718-991a-1b1c1d1e1f20"
persist eyeHeight = 1.65

const MOVE_SPEED = 5.5
const LOOK_SENS = 0.12
const HIP_FOV = 70.0
const CAP_RADIUS = 0.30
const CAP_HEIGHT = 1.80
const JUMP_SPEED = 5.5
const GRAVITY = 16.0
const RIFLE_RANGE = 80.0
const RIFLE_DAMAGE = 18.0

function requireOk(result, label) {
    if (result == null) throw label + ": null Result";
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}

function edgePressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in prevKeys) ? prevKeys[name] : false;
    prevKeys[name] <- down;
    return down && !was;
}

function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

function makeBox(x, y, z, sx, sy, sz, r, g, b) {
    local part = eve.Renderable3D();
    part.setMesh(gfx.newMeshCube(1.0));
    part.setPosition(x, y, z);
    part.setScale(sx, sy, sz);
    part.setTint(r, g, b, 1.0);
    part.setRoughness(0.72);
    part.setCastShadow(true);
    part.setReceiveShadow(true);
    visuals.push(part);
    return part;
}

function makeStaticBox(x, y, z, sx, sy, sz, r, g, b) {
    local body = world.newBody("static", x, y, z);
    body.newBoxShape(sx, sy, sz, 1.0, 0.6, 0.0);
    makeBox(x, y, z, sx, sy, sz, r, g, b);
    return body;
}

function yawPitchForward(yawDeg, pitchDeg) {
    local yaw = yawDeg * 0.017453292519943295;
    local pitch = pitchDeg * 0.017453292519943295;
    local cp = cos(pitch);
    return [cp * sin(yaw), sin(pitch), cp * cos(yaw)];
}

function setMouseCaptured(captured) {
    mouseCaptured = captured;
    mouse.setVisible(!captured);
    local ok = mouse.setRelativeMode(captured);
    if (!ok) {
        hudMessage = "relative mouse rejected by SDL";
        mouseCaptured = false;
        mouse.setVisible(true);
        return;
    }
    mouse.getMovementX();
    mouse.getMovementY();
}

function findTargetByBody(bodyId) {
    foreach (t in targets) {
        if (t.bodyId == bodyId) return t;
    }
    return null;
}

function refreshTargetViews() {
    foreach (t in targets) {
        t.view = requireOk(combat.state(t.id), "state " + t.id);
        local alive = t.view.health > 0.0;
        local flash = t.flash > 0.0 ? 1.0 : 0.0;
        local tintR = alive ? (0.85 + 0.15 * flash) : 0.22;
        local tintG = alive ? (0.28 + 0.40 * flash) : 0.22;
        local tintB = alive ? (0.22 + 0.20 * flash) : 0.24;
        t.visual.setTint(tintR, tintG, tintB, 1.0);
        if (!alive) t.visual.setScale(0.9, 0.35, 0.9);
    }
}

function spawnTarget(index, x, z, hp) {
    local id = "02020304-0506-0708-890a-0b0c0d0e" + (10000 + index).tostring().slice(1);
    local owner = "fighter:target-" + index;
    local y = 0.9;
    local body = world.newBody("static", x, y, z);
    body.newBoxShape(0.7, 1.8, 0.7, 1.0, 0.5, 0.0);
    local visual = makeBox(x, y, z, 0.7, 1.8, 0.7, 0.85, 0.32, 0.22);
    requireOk(combat.registerFighter(id, owner, x, z, hp, 40.0, 1.0, 8.0),
              "register target " + index);
    local view = requireOk(combat.state(id), "state target " + index);
    targets.push({
        id = id,
        owner = owner,
        bodyId = body.getId(),
        visual = visual,
        view = view,
        flash = 0.0,
        x = x,
        y = y,
        z = z
    });
}

function buildArena() {
    makeStaticBox(0.0, -0.25, 0.0, 24.0, 0.5, 24.0, 0.18, 0.22, 0.28);
    makeStaticBox(-8.0, 1.5, 0.0, 0.4, 3.0, 12.0, 0.22, 0.26, 0.32);
    makeStaticBox(8.0, 1.5, 0.0, 0.4, 3.0, 12.0, 0.22, 0.26, 0.32);
    makeStaticBox(0.0, 1.5, -10.0, 16.0, 3.0, 0.4, 0.22, 0.26, 0.32);
    makeStaticBox(0.0, 1.5, 10.0, 16.0, 3.0, 0.4, 0.22, 0.26, 0.32);
    makeStaticBox(-3.0, 0.75, 4.0, 2.0, 1.5, 1.0, 0.30, 0.34, 0.40);
    makeStaticBox(3.5, 0.5, -2.0, 1.5, 1.0, 1.5, 0.30, 0.34, 0.40);
    spawnTarget(1, 0.0, -6.0, 100.0);
    spawnTarget(2, 4.5, -7.5, 80.0);
    spawnTarget(3, -4.0, -5.5, 80.0);
}

function setupWeapon() {
    weaponMod = eve.Weapon();
    local registered = weaponMod.registerWeaponsFromJson(@"[
      {""id"":""arena.rifle"",""logic"":""hitscan"",""damage"":18,""range"":80,
       ""cooldown"":0.11,""spread"":1.2,""fireMode"":""auto"",
       ""recoilPitch"":1.4,""recoilYaw"":0.35,""recoilRecover"":8.0,
       ""zoomFov"":42.0,
       ""ammo"":{""mag"":30,""reserve"":90,""reload"":1.6}}
    ]");
    if (registered < 1) throw "registerWeaponsFromJson registered=" + registered;
    rifle = weaponMod.newWeapon("arena.rifle");
    if (rifle == null) throw "newWeapon(arena.rifle) returned null";
    rifle.setOwnerId(1);
}

function setupCombat() {
    combat = requireOk(eve.Combat().newRuntime(), "Create combat runtime");
    requireOk(combat.registerFighter(playerUuid, "fighter:player", 0.0, 0.0, 100.0, 40.0, 5.0, 20.0),
              "register player");
}

function setupCamera() {
    cam = eve.Camera3D();
    cam.setAmbient(0.28, 0.30, 0.34);
    cam.setFov(HIP_FOV);
    cam.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.25, 1.15, 1.05);
    ctrl = eve.CameraController();
    ctrl.setCamera(cam);
    ctrl.setMode("firstperson");
    ctrl.setFov(HIP_FOV);
    lookYaw = 180.0;
    lookPitch = 0.0;
    ctrl.setYaw(lookYaw);
    ctrl.setPitch(lookPitch);
    ctrl.setSmooth(0.0);
    ctrl.setPositionSmooth(0.0);
    ctrl.setTargetSmooth(0.0);
    ctrl.setTarget(playerFeet[0], playerFeet[1] + eyeHeight, playerFeet[2]);
    ctrl.snap();
}

function eyePos() {
    return [playerFeet[0], playerFeet[1] + eyeHeight, playerFeet[2]];
}

function settleHitscan() {
    local eye = eyePos();
    local fwd = yawPitchForward(lookYaw, lookPitch);
    local range = RIFLE_RANGE;
    local ex = eye[0] + fwd[0] * range;
    local ey = eye[1] + fwd[1] * range;
    local ez = eye[2] + fwd[2] * range;
    world.setQueryIgnoredBodyId(-1);
    local bodyId = world.rayCast(eye[0], eye[1], eye[2], ex, ey, ez);
    local hx = ex;
    local hy = ey;
    local hz = ez;
    if (world.hasRayHit()) {
        hx = world.getRayHitX();
        hy = world.getRayHitY();
        hz = world.getRayHitZ();
        bodyId = world.getRayHitBodyId();
    }
    tracerOx = eye[0]; tracerOy = eye[1]; tracerOz = eye[2];
    tracerHx = hx; tracerHy = hy; tracerHz = hz;
    tracerLife = 0.08;
    local target = findTargetByBody(bodyId);
    if (target == null || target.view.health <= 0.0) {
        hudMessage = "shot miss";
        return false;
    }
    local outcome = requireOk(
        combat.applyDamage(playerUuid, target.id, "Damage.Physical.Slash",
            RIFLE_DAMAGE, 6.0, fwd[0] * 2.0, 0.0, fwd[2] * 2.0, false, tick),
        "applyDamage");
    local applied = outcome.settlement.applied;
    target.view = requireOk(combat.state(target.id), "refresh target");
    target.flash = 0.25;
    hitFlash = 0.12;
    hudMessage = "hit " + applied.tointeger().tostring() + " hp=" +
                 target.view.health.tointeger().tostring();
    print("FPS_ARENA_HIT body=" + bodyId.tostring() + " applied=" + applied.tostring() +
          " hp=" + target.view.health.tostring() + "\n");
    if (target.view.health <= 0.0)
        print("FPS_ARENA_KILL target=" + target.id + "\n");
    return applied > 0.0;
}

function consumeWeaponEvents() {
    local n = weaponMod.getEventCount();
    for (local i = 0; i < n; i += 1) {
        local type = weaponMod.getEventType(i);
        if (type == "fire") {
            settleHitscan();
            local rp = weaponMod.getEventRecoilPitch(i);
            local ry = weaponMod.getEventRecoilYaw(i);
            lookYaw += ry;
            lookPitch = clampf(lookPitch + rp, -89.0, 89.0);
            ctrl.setYaw(lookYaw);
            ctrl.setPitch(lookPitch);
        } else if (type == "reload_start") {
            hudMessage = "reloading…";
        } else if (type == "reload_end") {
            hudMessage = "reload done";
        } else if (type == "empty") {
            hudMessage = "empty — press R";
        }
    }
    weaponMod.clearEvents();
}

function tryFire() {
    if (rifle == null || !rifle.canFire()) return;
    local eye = eyePos();
    local fwd = yawPitchForward(lookYaw, lookPitch);
    local tx = eye[0] + fwd[0] * RIFLE_RANGE;
    local ty = eye[1] + fwd[1] * RIFLE_RANGE;
    local tz = eye[2] + fwd[2] * RIFLE_RANGE;
    rifle.setAim(lookYaw, lookPitch);
    rifle.fireAt(tx, ty, tz, 1);
}

function updateMovement(dt) {
    local yaw = lookYaw * 0.017453292519943295;
    local fx = sin(yaw);
    local fz = cos(yaw);
    local rx = cos(yaw);
    local rz = -sin(yaw);
    local ix = 0.0;
    local iz = 0.0;
    if (keyboard.isDown("w") || keyboard.isDown("W")) { ix += fx; iz += fz; }
    if (keyboard.isDown("s") || keyboard.isDown("S")) { ix -= fx; iz -= fz; }
    if (keyboard.isDown("a") || keyboard.isDown("A")) { ix -= rx; iz -= rz; }
    if (keyboard.isDown("d") || keyboard.isDown("D")) { ix += rx; iz += rz; }
    local len = sqrt(ix * ix + iz * iz);
    if (len > 0.0001) {
        ix = ix / len * MOVE_SPEED * dt;
        iz = iz / len * MOVE_SPEED * dt;
    } else {
        ix = 0.0;
        iz = 0.0;
    }
    if (grounded && (edgePressed("Space") || edgePressed(" "))) {
        playerVelY = JUMP_SPEED;
        grounded = false;
    }
    playerVelY -= GRAVITY * dt;
    if (playerVelY < -22.0) playerVelY = -22.0;
    local dy = playerVelY * dt;
    local x = playerFeet[0];
    local y = playerFeet[1];
    local z = playerFeet[2];
    world.setMoverSlopeLimit(0.55);
    world.moveCapsule(x, y + CAP_RADIUS, z, x, y + CAP_HEIGHT - CAP_RADIUS, z,
                      CAP_RADIUS, ix, dy, iz);
    playerFeet[0] = x + world.getMoverDeltaX();
    playerFeet[1] = y + world.getMoverDeltaY();
    playerFeet[2] = z + world.getMoverDeltaZ();
    grounded = world.isMoverGrounded();
    if (grounded && playerVelY < 0.0) playerVelY = 0.0;
}

function updateLook() {
    if (edgePressed("Escape")) setMouseCaptured(!mouseCaptured);
    local dx = mouse.getMovementX();
    local dy = mouse.getMovementY();
    if (!mouseCaptured) return;
    lookYaw += dx * LOOK_SENS;
    lookPitch = clampf(lookPitch - dy * LOOK_SENS, -89.0, 89.0);
    ctrl.setYaw(lookYaw);
    ctrl.setPitch(lookPitch);
}

function updateAiming() {
    local ads = mouse.isDown(3) || mouse.isDown(2) || keyboard.isDown("q") || keyboard.isDown("Q");
    rifle.setAiming(ads);
    local fov = ads ? rifle.getZoomFov() : HIP_FOV;
    if (fov <= 1.0) fov = HIP_FOV;
    ctrl.setFov(fov);
    cam.setFov(fov);
}

function runAutoVerify() {
    if (verified) return;
    // Aim at the center target and fire once so headless smoke exercises the damage path.
    lookYaw = 180.0;
    lookPitch = -4.0;
    ctrl.setYaw(lookYaw);
    ctrl.setPitch(lookPitch);
    ctrl.setTarget(playerFeet[0], playerFeet[1] + eyeHeight, playerFeet[2]);
    ctrl.snap();
    tryFire();
    weaponMod.update(0.0);
    consumeWeaponEvents();
    verified = true;
    print("FPS_ARENA_PASS auto-verify fired\n");
}

eve_init <- function() {
    gfx.setBackgroundColor(0.05, 0.07, 0.10, 1.0);
    if (physicsMod == null) physicsMod = eve.Physics();
    world = physicsMod.newWorld3D(0.0, -9.8, 0.0, true);
    setupCombat();
    buildArena();
    setupWeapon();
    playerFeet = [0.0, 0.0, 6.0];
    playerVelY = 0.0;
    grounded = true;
    setupCamera();
    local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 22);
    font = gfx.newFont(fontData,
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-:/+.%[]()_>=!|,");
    setMouseCaptured(true);
    print("FPS_ARENA_READY targets=" + targets.len() + "\n");
};

eve_update <- function(dt) {
    if (dt > 0.05) dt = 0.05;
    tick += 1;
    updateLook();
    updateMovement(dt);
    ctrl.setTarget(playerFeet[0], playerFeet[1] + eyeHeight, playerFeet[2]);
    updateAiming();
    if (edgePressed("r") || edgePressed("R")) rifle.startReload();
    if (mouse.isDown(1) || keyboard.isDown("f") || keyboard.isDown("F")) tryFire();
    weaponMod.update(dt);
    consumeWeaponEvents();
    world.update(dt);
    ctrl.update(dt);
    refreshTargetViews();
    if (hitFlash > 0.0) hitFlash -= dt;
    if (tracerLife > 0.0) tracerLife -= dt;
    foreach (t in targets) {
        if (t.flash > 0.0) t.flash -= dt;
    }
    if (autoVerify >= 0.0) {
        autoVerify -= dt;
        if (autoVerify < 0.0) runAutoVerify();
    }
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    local cx = config.width.tofloat() * 0.5;
    local cy = config.height.tofloat() * 0.5;
    local ads = false;
    if (rifle != null) ads = rifle.isAiming();
    local arm = ads ? 7.0 : 11.0;
    local gap = ads ? 2.0 : 3.0;
    gfx.drawSolidRect(cx - arm, cy - 1.0, arm - gap, 2.0, 1.0, 1.0, 1.0, 0.9);
    gfx.drawSolidRect(cx + gap, cy - 1.0, arm - gap, 2.0, 1.0, 1.0, 1.0, 0.9);
    gfx.drawSolidRect(cx - 1.0, cy - arm, 2.0, arm - gap, 1.0, 1.0, 1.0, 0.9);
    gfx.drawSolidRect(cx - 1.0, cy + gap, 2.0, arm - gap, 1.0, 1.0, 1.0, 0.9);
    if (hitFlash > 0.0)
        gfx.drawSolidRect(cx - 2.0, cy - 2.0, 4.0, 4.0, 1.0, 0.35, 0.2, 1.0);
    else
        gfx.drawSolidRect(cx - 1.5, cy - 1.5, 3.0, 3.0, 1.0, 0.2, 0.2, 0.95);
    if (font != null && rifle != null && combat != null) {
        local mag = rifle.getMagAmmo();
        local reserve = rifle.getReserveAmmo();
        local playerView = requireOk(combat.state(playerUuid), "player hud state");
        local hp = playerView.health.tointeger();
        local line1 = "HP " + hp.tostring() + "   AMMO " + mag.tostring() + "/" + reserve.tostring();
        if (rifle.isReloading()) line1 += "  REL";
        if (rifle.isAiming()) line1 += "  ADS";
        local bottom = config.height.tofloat();
        gfx.drawText(font, line1, 24.0, bottom - 64.0, 0.92, 0.94, 0.98, 1.0, 0.85);
        gfx.drawText(font, hudMessage, 24.0, bottom - 36.0, 0.75, 0.82, 0.90, 1.0, 0.75);
        local alive = 0;
        foreach (t in targets) {
            if (t.view.health > 0.0) alive += 1;
        }
        gfx.drawText(font, "TARGETS " + alive.tostring() + "/" + targets.len().tostring(),
                     24.0, 24.0, 0.85, 0.55, 0.40, 1.0, 0.85);
    }
};
