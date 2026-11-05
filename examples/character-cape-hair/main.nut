// ============================================================================
// Character Cape + Hair Physics
//
// pixiv three-vrm sample female (female-rig.glb) with:
//   - DynamicBone hair chains on J_Sec_Hair* secondary bones
//   - Cloth3D cape pinned to shoulder / upper-chest bones
//
// Controls:
//   Space       — toggle wind
//   R           — reset cape + hair
//   Left drag   — grab cape
//   Right drag  — orbit camera
//   Mouse wheel — zoom
//
// Run: make run/linux-debug GAME=examples/character-cape-hair
// ============================================================================

persist ready = false
persist model = null
persist skeleton = null
persist pose = null
persist bones = {}
persist bind = []
persist parts = []
persist skins = []
persist dynamic = null
persist hairChainCount = 0
persist clothModule = null
persist physics = null
persist bodyWorld = null
persist bodyColliders = []
persist cape = null
persist capeCols = 11
persist capeRows = 14
persist capeSpacing = 0.055
persist capeAttach = []
persist windOn = true
persist elapsed = 0.0
persist offsetY = 0.0
persist camera = null
persist orbitYaw = 0.35
persist orbitPitch = 0.18
persist orbitDist = 2.6
persist orbitDragging = false
persist prevMouseX = 0.0
persist prevMouseY = 0.0
persist grabbing = false
persist prevKeys = {}
persist prevMouseLeft = false

const HEAD_BONE = "J_Bip_C_Head"
const HIPS_BONE = "J_Bip_C_Hips"
const SPINE_BONE = "J_Bip_C_Spine"
const CHEST_BONE = "J_Bip_C_Chest"
const UPPER_CHEST_BONE = "J_Bip_C_UpperChest"
const L_SHOULDER = "J_Bip_L_Shoulder"
const R_SHOULDER = "J_Bip_R_Shoulder"
const L_UPPER_ARM = "J_Bip_L_UpperArm"
const R_UPPER_ARM = "J_Bip_R_UpperArm"
const L_UPPER_LEG = "J_Bip_L_UpperLeg"
const R_UPPER_LEG = "J_Bip_R_UpperLeg"
const L_LOWER_LEG = "J_Bip_L_LowerLeg"
const R_LOWER_LEG = "J_Bip_R_LowerLeg"

function edgePressed(name) {
    local down = keyboard.isDown(name)
    local key = "k_" + name
    local was = (key in prevKeys) ? prevKeys[key] : false
    prevKeys[key] <- down
    return down && !was
}

function quatMul(a, b) {
    return [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]
    ]
}

function rotateBone(name, x, y, z) {
    if (!(name in bones)) return
    local i = bones[name]
    local b = bind[i]
    local q = quatMul([sin(x * 0.5), 0.0, 0.0, cos(x * 0.5)],
        quatMul([0.0, sin(y * 0.5), 0.0, cos(y * 0.5)],
            [0.0, 0.0, sin(z * 0.5), cos(z * 0.5)]))
    q = quatMul(b.q, q)
    pose.setLocalRotation(i, q[0], q[1], q[2], q[3])
}

function worldPos(name) {
    local i = bones[name]
    return [
        pose.getWorldPositionX(i),
        pose.getWorldPositionY(i) + offsetY,
        pose.getWorldPositionZ(i)
    ]
}

function midWorld(a, b) {
    local pa = worldPos(a)
    local pb = worldPos(b)
    return [
        (pa[0] + pb[0]) * 0.5,
        (pa[1] + pb[1]) * 0.5,
        (pa[2] + pb[2]) * 0.5
    ]
}

function vlen3(v) {
    return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])
}

function vnorm3(v) {
    local l = vlen3(v)
    if (l < 1e-6) return [0.0, 0.0, -1.0]
    return [v[0] / l, v[1] / l, v[2] / l]
}

function vcross3(a, b) {
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0]
    ]
}

function bodyAxes() {
    // Shoulder line defines character right; back faces opposite the camera-facing torso.
    local left = worldPos(L_SHOULDER)
    local right = worldPos(R_SHOULDER)
    local rightAxis = vnorm3([right[0] - left[0], right[1] - left[1], right[2] - left[2]])
    local upAxis = [0.0, 1.0, 0.0]
    local backAxis = vnorm3(vcross3(rightAxis, upAxis))
    // Prefer a back that points toward -Z in bind so the cape (behind) hits the shield.
    if (backAxis[2] > 0.0) {
        backAxis = [-backAxis[0], -backAxis[1], -backAxis[2]]
        rightAxis = [-rightAxis[0], -rightAxis[1], -rightAxis[2]]
    }
    return { right = rightAxis, back = backAxis }
}

function addBackSphere(radius) {
    local body = bodyWorld.newBody("kinematic", 0.0, 0.0, 0.0)
    body.newSphereShape(radius, 1.0, 0.4, 0.0)
    return body
}

function buildBodyColliders() {
    if (physics == null) physics = eve.Physics()
    bodyWorld = physics.newWorld3D(0.0, -9.8, 0.0, true)
    bodyColliders = []
    // Back-facing shield: a lattice of spheres on the outside of clothing so the cape
    // cannot wrap around a single center sphere into the shirt from the front.
    local rows = [
        { bone = UPPER_CHEST_BONE, back = 0.10, ys = 0.00, xs = [-0.14, -0.07, 0.0, 0.07, 0.14], r = 0.09 },
        { bone = CHEST_BONE, back = 0.11, ys = 0.00, xs = [-0.15, -0.075, 0.0, 0.075, 0.15], r = 0.10 },
        { bone = SPINE_BONE, back = 0.12, ys = 0.00, xs = [-0.13, -0.065, 0.0, 0.065, 0.13], r = 0.10 },
        { bone = HIPS_BONE, back = 0.11, ys = 0.02, xs = [-0.12, -0.06, 0.0, 0.06, 0.12], r = 0.10 }
    ]
    foreach (row in rows) {
        foreach (x in row.xs) {
            bodyColliders.append({
                body = addBackSphere(row.r), kind = "back",
                bone = row.bone, x = x, y = row.ys, back = row.back, radius = row.r
            })
        }
    }
    // Thigh outsides — keep the lower cape from slicing through the legs.
    foreach (spec in [
        { bone = L_UPPER_LEG, x = -0.04, y = -0.05, back = 0.07, r = 0.09 },
        { bone = R_UPPER_LEG, x = 0.04, y = -0.05, back = 0.07, r = 0.09 },
        { bone = L_LOWER_LEG, x = -0.02, y = -0.02, back = 0.05, r = 0.075 },
        { bone = R_LOWER_LEG, x = 0.02, y = -0.02, back = 0.05, r = 0.075 }
    ]) {
        bodyColliders.append({
            body = addBackSphere(spec.r), kind = "back",
            bone = spec.bone, x = spec.x, y = spec.y, back = spec.back, radius = spec.r
        })
    }
    print("BODY_COLLIDERS=" + bodyColliders.len() + "\n")
}

function syncBodyColliders() {
    local axes = bodyAxes()
    foreach (c in bodyColliders) {
        local p = worldPos(c.bone)
        local x = c.x
        local y = c.y
        local back = c.back
        c.body.setPosition(
            p[0] + axes.right[0] * x + axes.back[0] * back,
            p[1] + y + axes.back[1] * back,
            p[2] + axes.right[2] * x + axes.back[2] * back)
    }
}

function hairSegIndex(name) {
    // J_Sec_Hair{seg}_{id} → seg (prefix length 10).
    if (name.len() <= 10) return -1
    local i = 10
    local n = 0
    local saw = false
    while (i < name.len()) {
        local c = name.slice(i, i + 1)
        if (c < "0" || c > "9") break
        n = n * 10 + c.tointeger()
        saw = true
        i += 1
    }
    return saw ? n : -1
}

function hairChainId(name) {
    local last = -1
    for (local i = 0; i < name.len(); ++i) {
        if (name.slice(i, i + 1) == "_") last = i
    }
    if (last < 0 || last + 1 >= name.len()) return ""
    return name.slice(last + 1)
}

function discoverHairChains() {
    local groups = {}
    for (local i = 0; i < skeleton.getBoneCount(); ++i) {
        local name = skeleton.getBoneName(i)
        if (name.len() < 12) continue
        if (name.slice(0, 10) != "J_Sec_Hair") continue
        if (name.len() >= 4 && name.slice(name.len() - 4) == "_end") continue
        if (hairSegIndex(name) < 0) continue
        local id = hairChainId(name)
        if (id == "") continue
        if (!(id in groups)) groups[id] <- []
        groups[id].append(name)
    }
    local count = 0
    foreach (id, names in groups) {
        // Sort by Hair{N} segment number so root is Hair1 and tip is the last.
        for (local a = 0; a < names.len(); ++a) {
            for (local b = a + 1; b < names.len(); ++b) {
                if (hairSegIndex(names[a]) > hairSegIndex(names[b])) {
                    local tmp = names[a]
                    names[a] = names[b]
                    names[b] = tmp
                }
            }
        }
        local root = names[0]
        local tip = names[names.len() - 1]
        local chain = anim.setupHairChain(dynamic, root, tip, 0.12, 0.18, 0.8, 0.08, true)
        if (chain >= 0) {
            count += 1
            print("HAIR_CHAIN " + root + " -> " + tip + " idx=" + chain + "\n")
        } else {
            print("HAIR_CHAIN_FAIL " + root + " -> " + tip + "\n")
        }
    }
    return count
}

function loadCharacter() {
    model = model3d.newModelDataFromFile("assets/pixiv/female-rig.glb")
    skeleton = anim.newSkeletonFromModel(model)
    pose = anim.newPose(skeleton.getBoneCount())
    bones = {}
    bind = []
    for (local i = 0; i < skeleton.getBoneCount(); ++i) {
        bones[skeleton.getBoneName(i)] <- i
        bind.append({
            p = [skeleton.getBindPositionX(i), skeleton.getBindPositionY(i), skeleton.getBindPositionZ(i)],
            q = [skeleton.getBindRotationX(i), skeleton.getBindRotationY(i),
                 skeleton.getBindRotationZ(i), skeleton.getBindRotationW(i)],
            s = [skeleton.getBindScaleX(i), skeleton.getBindScaleY(i), skeleton.getBindScaleZ(i)]
        })
    }
    foreach (required in [HEAD_BONE, HIPS_BONE, SPINE_BONE, CHEST_BONE, UPPER_CHEST_BONE,
                          L_SHOULDER, R_SHOULDER, L_UPPER_ARM, R_UPPER_ARM,
                          L_UPPER_LEG, R_UPPER_LEG, L_LOWER_LEG, R_LOWER_LEG]) {
        if (!(required in bones)) throw "Missing required bone: " + required
    }

    parts = []
    skins = []
    for (local i = 0; i < model.getMeshCount(); ++i) {
        if (!model.hasBones(i)) throw "Unskinned primitive in character mesh " + i
        local part = model3d.createRenderable(gfx, model, i)
        local skin = anim.newSkinFromModel(model, i, skeleton)
        if (!skin.bindGpuMesh(gfx, part.getMesh())) throw "GPU skin binding failed"
        parts.append(part)
        skins.append(skin)
    }

    dynamic = anim.newDynamicBoneSolver(skeleton)
    dynamic.setGlobalGravity(0.0, -9.81, 0.0)
    hairChainCount = discoverHairChains()
    local colliders = anim.setupHairHeadCollider(dynamic, HEAD_BONE, 0.11)
    print("HAIR_CHAINS=" + hairChainCount + " HEAD_COLLIDERS=" + colliders + "\n")
    if (hairChainCount <= 0) throw "No hair chains were set up"
}

function capeTopAnchor(t) {
    // t in [0,1] across a line behind the upper back (not on the shoulder bones),
    // so pinned particles stay outside clothing and the back-shield colliders.
    local left = worldPos(L_SHOULDER)
    local right = worldPos(R_SHOULDER)
    local chest = worldPos(UPPER_CHEST_BONE)
    local axes = bodyAxes()
    local across = 0.16
    local x = chest[0] + axes.right[0] * (-across + 2.0 * across * t)
    local y = chest[1] - 0.03
    local z = chest[2] + axes.right[2] * (-across + 2.0 * across * t)
    local back = 0.26
    return [
        x + axes.back[0] * back,
        y + axes.back[1] * back,
        z + axes.back[2] * back
    ]
}

function layoutCape() {
    // Native grid is XZ; remap into a vertical XY panel hanging from the shoulders.
    local left = capeTopAnchor(0.0)
    local right = capeTopAnchor(1.0)
    local width = right[0] - left[0]
    if (width < 0.2) width = 0.35
    local topY = (left[1] + right[1]) * 0.5
    local backZ = (left[2] + right[2]) * 0.5
    for (local row = 0; row < capeRows; ++row) {
        for (local col = 0; col < capeCols; ++col) {
            local t = capeCols <= 1 ? 0.5 : col.tofloat() / (capeCols - 1).tofloat()
            local anchor = capeTopAnchor(t)
            local x = anchor[0]
            local y = topY - row.tofloat() * capeSpacing
            local z = backZ - row.tofloat() * capeSpacing * 0.28
            cape.setParticlePosition(row * capeCols + col, x, y, z)
        }
    }
}

function buildCape() {
    if (clothModule == null) clothModule = eve.Cloth()
    if (bodyWorld == null) buildBodyColliders()
    cape = clothModule.newCloth3D(capeCols, capeRows, capeSpacing, -0.3, 1.4, -0.2)
    cape.setGravity(0.0, -9.8, 0.0)
    cape.setStiffness(0.92)
    cape.setIterations(8)
    cape.setDamping(0.02)
    // Particle radius doubles as the body-probe margin against kinematic spheres.
    cape.setParticleSize(0.065)
    cape.setParticleMass(0.03)
    cape.setSelfCollision(true)
    cape.setFoldStiffness(0.55)
    cape.setMaxFoldAngle(140.0)
    cape.setAerodynamics(1.225, 1.05, 0.18)
    cape.setCollisionMaterial(0.35, 0.0)
    cape.setColor(0.22, 0.28, 0.55, 1.0)
    cape.setBounds(-2.5, -0.2, -2.5, 5.0, 3.0, 5.0)
    cape.setCollideWorld(bodyWorld)
    syncBodyColliders()
    layoutCape()

    capeAttach = []
    for (local col = 0; col < capeCols; ++col) {
        local idx = col // top row, already pinned by ClothModel::grid
        local t = capeCols <= 1 ? 0.5 : col.tofloat() / (capeCols - 1).tofloat()
        local p = capeTopAnchor(t)
        cape.attachParticle(idx, p[0], p[1], p[2], 0.0)
        capeAttach.append({ index = idx, t = t })
    }
    print("CAPE_ATTACH=" + capeAttach.len() + " particles=" + cape.getParticleCount() +
          " bodyColliders=" + bodyColliders.len() + "\n")
}

function syncCapeAttachments() {
    foreach (item in capeAttach) {
        local p = capeTopAnchor(item.t)
        cape.setParticlePosition(item.index, p[0], p[1], p[2])
        cape.updateAttachment(item.index, p[0], p[1], p[2])
    }
}

function applyIdlePose(seconds) {
    skeleton.applyBindPose(pose)
    local sway = sin(seconds * 1.6)
    local twist = sin(seconds * 0.9)
    local bob = sin(seconds * 3.1)
    rotateBone(HIPS_BONE, 0.0, 0.12 * twist, 0.04 * sway)
    rotateBone(SPINE_BONE, 0.03 * bob, 0.08 * twist, 0.05 * sway)
    rotateBone(CHEST_BONE, 0.02 * bob, 0.10 * twist, 0.0)
    rotateBone(UPPER_CHEST_BONE, 0.015 * bob, 0.06 * twist, 0.0)
    rotateBone(HEAD_BONE, 0.05 * sin(seconds * 1.1), 0.18 * twist, 0.04 * sway)
    // Stronger A-pose so the cape clears the upper arms.
    rotateBone(L_UPPER_ARM, 0.0, 0.0, -0.95)
    rotateBone(R_UPPER_ARM, 0.0, 0.0, 0.95)
}

function updateFootOffset() {
    local lf = bones["J_Bip_L_Foot"]
    local rf = bones["J_Bip_R_Foot"]
    local footY = pose.getWorldPositionY(lf)
    local ry = pose.getWorldPositionY(rf)
    if (ry < footY) footY = ry
    offsetY = 0.02 - footY
}

function applySkins() {
    for (local i = 0; i < skins.len(); ++i) {
        if (!skins[i].updateGpuMesh(parts[i].getMesh(), pose)) throw "Skin palette update failed"
        parts[i].setPosition(0.0, offsetY, 0.0)
    }
}

function updateCamera() {
    local targetY = 1.15
    local cx = sin(orbitYaw) * cos(orbitPitch) * orbitDist
    local cy = targetY + sin(orbitPitch) * orbitDist
    local cz = cos(orbitYaw) * cos(orbitPitch) * orbitDist
    camera.setEye(cx, cy, cz)
    camera.setTarget(0.0, targetY, 0.0)
}

function windForce(seconds) {
    if (!windOn) return [0.0, 0.0, 0.0]
    local amp = 0.55 + 0.25 * sin(seconds * 0.7)
    return [math.polarY(amp, seconds * 1.15), 0.08, 0.35 + 0.15 * sin(seconds * 0.9)]
}

function grabPoint() {
    local planeY = 1.05
    camera.screenToRay(mouse.getX(), mouse.getY(), gfx.getWidth().tofloat(), gfx.getHeight().tofloat())
    local oy = camera.getScreenRayOriginY()
    local dy = camera.getScreenRayDirY()
    if (dy >= -0.0001 && dy <= 0.0001) return null
    local t = (planeY - oy) / dy
    if (t <= 0.0) return null
    return [
        camera.getScreenRayOriginX() + camera.getScreenRayDirX() * t,
        planeY,
        camera.getScreenRayOriginZ() + camera.getScreenRayDirZ() * t
    ]
}

function resetSimulation() {
    if (dynamic != null) dynamic.reset()
    if (cape != null) {
        syncBodyColliders()
        cape.reset()
        layoutCape()
        syncCapeAttachments()
        grabbing = false
    }
    print("RESET cape+hair\n")
}

eve_init <- function() {
    ready = false
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0)
    camera = eve.Camera3D()
    camera.setUp(0.0, 1.0, 0.0)
    camera.setFov(38.0)
    camera.setAmbient(0.32, 0.34, 0.38)
    camera.setActive(true)
    gfx.setDirectionalLight(-0.35, -1.0, -0.45, 1.2, 1.12, 1.05)
    updateCamera()

    loadCharacter()
    // Seed pose / skins once so cape anchors see a valid bind world.
    applyIdlePose(0.0)
    dynamic.update(pose, 1.0 / 60.0)
    pose.computeWorld(skeleton)
    updateFootOffset()
    applySkins()
    buildBodyColliders()
    buildCape()
    syncCapeAttachments()

    ready = true
    print("CAPE_HAIR_READY hair=" + hairChainCount + " capeAttach=" + capeAttach.len() +
          " bodyColliders=" + bodyColliders.len() + " meshes=" + parts.len() + "\n")
}

eve_reload <- function() {
    ready = false
    cape = null
    dynamic = null
    bodyWorld = null
    bodyColliders = []
    parts = []
    skins = []
    loadCharacter()
    applyIdlePose(elapsed)
    dynamic.update(pose, 1.0 / 60.0)
    pose.computeWorld(skeleton)
    updateFootOffset()
    applySkins()
    buildBodyColliders()
    buildCape()
    syncCapeAttachments()
    ready = true
}

eve_update <- function(dt) {
    if (!ready) return
    elapsed += dt

    if (edgePressed("space") || edgePressed(" ")) {
        windOn = !windOn
        print("WIND " + (windOn ? "ON" : "off") + "\n")
    }
    if (edgePressed("R") || edgePressed("r")) resetSimulation()

    // Orbit camera with right mouse.
    local rightDown = mouse.isDown(2)
    if (rightDown) {
        local mx = mouse.getX().tofloat()
        local my = mouse.getY().tofloat()
        if (orbitDragging) {
            orbitYaw += (mx - prevMouseX) * 0.005
            orbitPitch += (my - prevMouseY) * 0.005
            if (orbitPitch > 1.2) orbitPitch = 1.2
            if (orbitPitch < -0.2) orbitPitch = -0.2
        }
        prevMouseX = mx
        prevMouseY = my
        orbitDragging = true
    } else {
        orbitDragging = false
    }
    local wheel = mouse.getWheelY()
    if (wheel != 0) {
        orbitDist -= wheel.tofloat() * 0.15
        if (orbitDist < 1.4) orbitDist = 1.4
        if (orbitDist > 5.5) orbitDist = 5.5
    }
    updateCamera()

    applyIdlePose(elapsed)
    local wind = windForce(elapsed)
    dynamic.setExternalForce(wind[0], wind[1], wind[2])
    dynamic.update(pose, dt)
    pose.computeWorld(skeleton)
    updateFootOffset()
    applySkins()
    syncBodyColliders()

    if (cape != null) {
        syncCapeAttachments()
        if (windOn) {
            cape.setWindVelocity(wind[0] * 2.2, 0.4 + wind[1], wind[2] * 2.0)
        } else {
            cape.setWindVelocity(0.0, 0.0, 0.0)
        }

        local leftDown = mouse.isDown(1)
        if (leftDown && !prevMouseLeft) {
            local pt = grabPoint()
            if (pt != null) {
                local idx = cape.grabAt(pt[0], pt[1], pt[2], 0.35)
                grabbing = idx >= 0
            }
        }
        if (leftDown && grabbing) {
            local pt = grabPoint()
            if (pt != null) cape.moveGrab(pt[0], pt[1], pt[2])
        } else if (grabbing) {
            cape.releaseGrab()
            grabbing = false
        }
        prevMouseLeft = leftDown
        // Kinematic body poses are written directly; cloth probes them during update.
        if (bodyWorld != null) bodyWorld.update(dt)
        cape.update(dt)
    }
}

eve_render <- function() {
    gfx.clear()
    gfx.render3D()
    if (cape != null) cape.draw(gfx)
}
