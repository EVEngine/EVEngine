// ============================================================================
// Character Cape + Hair Physics
//
// pixiv three-vrm sample female (female-rig.glb) with:
//   - DynamicBone hair chains on J_Sec_Hair* secondary bones
//   - DynamicBone clothing chains on Tops hem/sleeve (+ bust) secondary bones
//   - Cloth3D cape pinned across the shoulder tops with body-shield collision
//   - Bidirectional hair↔cape proxies (hair bone spheres + cape-surface DynamicBone spheres)
//
// Controls:
//   Space       — toggle wind
//   R           — reset cape + secondary bones
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
persist clothChainCount = 0
persist clothModule = null
persist physics = null
persist bodyWorld = null
persist bodyColliders = []
persist hairProxyBones = [] // hair bones that push the cape away
persist hairProxyColliders = [] // kinematic spheres in bodyWorld
persist capeProxyColliderStart = -1 // DynamicBone world spheres that keep hair off the cape
persist capeProxyParticleIndices = []
persist cape = null
persist capeCols = 11
persist capeRows = 14
persist capeSpacing = 0.055
persist capePadRows = 5 // rows draped across the shoulder tops before hanging
persist capeAttach = []
persist windOn = false // start calm so the shoulder pad is readable; Space toggles wind
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
const NECK_BONE = "J_Bip_C_Neck"
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
    // VRM *Shoulder bones sit at the neck — use UpperArm tips for the shoulder line.
    local left = worldPos(L_UPPER_ARM)
    local right = worldPos(R_UPPER_ARM)
    local rightAxis = vnorm3([right[0] - left[0], right[1] - left[1], right[2] - left[2]])
    local upAxis = [0.0, 1.0, 0.0]
    local backAxis = vnorm3(vcross3(rightAxis, upAxis))
    // Prefer a back that points toward -Z in bind so the cape (behind) hits the shield.
    if (backAxis[2] > 0.0) {
        backAxis = [-backAxis[0], -backAxis[1], -backAxis[2]]
        rightAxis = [-rightAxis[0], -rightAxis[1], -rightAxis[2]]
    }
    return { right = rightAxis, back = backAxis, up = upAxis }
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
    // Keep the shield below the shoulder clasps so pinned top-row particles are not crushed.
    // UpperChest row blocks wrap-around into the shirt front under the neck.
    local rows = [
        { bone = UPPER_CHEST_BONE, back = 0.08, ys = -0.02, xs = [-0.08, 0.0, 0.08], r = 0.07 },
        { bone = CHEST_BONE, back = 0.10, ys = -0.04, xs = [-0.12, -0.06, 0.0, 0.06, 0.12], r = 0.085 },
        { bone = SPINE_BONE, back = 0.11, ys = 0.00, xs = [-0.12, -0.06, 0.0, 0.06, 0.12], r = 0.09 },
        { bone = HIPS_BONE, back = 0.10, ys = 0.02, xs = [-0.11, -0.055, 0.0, 0.055, 0.11], r = 0.09 }
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
        local p = worldPos(c.bone) // already mesh/cloth space (+offsetY)
        local x = c.x
        local y = c.y
        local back = c.back
        c.body.setPosition(
            p[0] + axes.right[0] * x + axes.back[0] * back,
            p[1] + y + axes.back[1] * back,
            p[2] + axes.right[2] * x + axes.back[2] * back)
    }
    syncHairProxyColliders()
}

function syncHairProxyColliders() {
    // Cape probes these kinematic spheres so free cloth cannot tunnel through hair.
    foreach (c in hairProxyColliders) {
        local p = worldPos(c.bone) // already mesh/cloth space
        c.body.setPosition(p[0], p[1], p[2])
    }
}

function syncCapeProxyColliders() {
    // Hair DynamicBone probes these world spheres so strands stay on the body side of
    // the cape. Cape particles are mesh/cloth space (+offsetY); DynamicBone is pose
    // space — strip offsetY. Bias centers slightly toward the body so outside-sphere
    // resolution pushes hair forward (not through to the cape exterior). Pad rows get
    // an extra upward lift so hair rests on the leather plate instead of stabbing it.
    if (dynamic == null || cape == null || capeProxyColliderStart < 0) return
    local axes = bodyAxes()
    local bias = -0.03 // toward body (opposite of back)
    local pad = capePadRows
    if (pad < 1) pad = 1
    for (local i = 0; i < capeProxyParticleIndices.len(); ++i) {
        local idx = capeProxyParticleIndices[i]
        local row = idx / capeCols
        local upLift = row < pad ? 0.05 : 0.0
        dynamic.setColliderCenter(
            capeProxyColliderStart + i,
            cape.getParticleX(idx) + axes.back[0] * bias,
            cape.getParticleY(idx) - offsetY + axes.back[1] * bias + upLift,
            cape.getParticleZ(idx) + axes.back[2] * bias)
    }
}

function buildHairCapeCollision() {
    hairProxyColliders = []
    foreach (boneName in hairProxyBones) {
        // Soft hair volume so the free cape sheet rests behind the curtain.
        local radius = 0.12
        hairProxyColliders.append({
            body = addBackSphere(radius), kind = "hair",
            bone = boneName, x = 0.0, y = 0.0, back = 0.0, radius = radius
        })
    }

    capeProxyParticleIndices = []
    capeProxyColliderStart = dynamic.getColliderCount()
    local pad = capePadRows
    if (pad < 1) pad = 1
    local axes = bodyAxes()
    local bias = -0.03 // toward body
    // Full shoulder pad + hanging sheet samples.
    for (local row = 0; row < capeRows; row += 1) {
        for (local col = 0; col < capeCols; col += 1) {
            if (row >= pad && ((row + col) % 2 == 1)) continue
            local idx = row * capeCols + col
            capeProxyParticleIndices.append(idx)
            local radius = row < pad ? 0.15 : 0.11
            local upLift = row < pad ? 0.05 : 0.0
            dynamic.addColliderSphere(
                cape.getParticleX(idx) + axes.back[0] * bias,
                cape.getParticleY(idx) - offsetY + axes.back[1] * bias + upLift,
                cape.getParticleZ(idx) + axes.back[2] * bias,
                radius)
        }
    }
    // Upper-back torso shields (pose space) so long hair cannot swing through the body
    // into the cape from the inside.
    if (UPPER_CHEST_BONE in bones) {
        dynamic.addBoneColliderSphere(bones[UPPER_CHEST_BONE], 0.0, 0.02, -0.06, 0.16)
        dynamic.addBoneColliderSphere(bones[UPPER_CHEST_BONE], 0.0, -0.06, -0.10, 0.14)
    }
    if (CHEST_BONE in bones) {
        dynamic.addBoneColliderSphere(bones[CHEST_BONE], 0.0, 0.0, -0.08, 0.15)
    }
    // Mild stiffness + fat particles so cape/torso colliders win without limp strands.
    for (local c = 0; c < hairChainCount; ++c) {
        for (local p = 1; p < 24; ++p) {
            dynamic.setChainParticleParameters(c, p, 0.09, 0.24, 0.75, 1.0, 0.06)
        }
    }
    print("HAIR_CAPE_COLLISION hairProxies=" + hairProxyColliders.len() +
          " capeProxies=" + capeProxyParticleIndices.len() + "\n")
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

function rememberHairProxyBone(name) {
    if (!(name in bones)) return
    foreach (existing in hairProxyBones) {
        if (existing == name) return
    }
    if (hairProxyBones.len() >= 48) return
    hairProxyBones.append(name)
}

function discoverHairChains() {
    local groups = {}
    hairProxyBones = []
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
        if (names.len() < 2) continue
        local root = names[0]
        local tip = names[names.len() - 1]
        local chain = anim.setupHairChain(dynamic, root, tip, 0.12, 0.18, 0.8, 0.08, true)
        if (chain >= 0) {
            // Proxy every bone after the root so the cape cannot sink into long hair.
            for (local bi = 1; bi < names.len(); ++bi) {
                rememberHairProxyBone(names[bi])
            }
            count += 1
            print("HAIR_CHAIN " + root + " -> " + tip + " idx=" + chain + "\n")
        } else {
            print("HAIR_CHAIN_FAIL " + root + " -> " + tip + "\n")
        }
    }
    return count
}

function setupClothChain(root, tip, stiffness, damping, inertia, endLength) {
    // Clothing uses the same spring-chain solver as hair, with stiffer defaults.
    local chain = anim.setupHairChain(dynamic, root, tip, stiffness, damping, inertia, endLength, false)
    if (chain >= 0) {
        clothChainCount += 1
        print("CLOTH_CHAIN " + root + " -> " + tip + " idx=" + chain + "\n")
    } else {
        print("CLOTH_CHAIN_FAIL " + root + " -> " + tip + "\n")
    }
    return chain
}

function discoverClothingChains() {
    // Authoring from the original VRM VRMC_springBone Tops groups: hem + sleeve soft bones.
    local tops = [
        "J_Sec_L_TopsUpperLegBack", "J_Sec_L_TopsUpperLegFront", "J_Sec_L_TopsUpperLegSide",
        "J_Sec_R_TopsUpperLegBack", "J_Sec_R_TopsUpperLegFront", "J_Sec_R_TopsUpperLegSide",
        "J_Sec_L_TopsUpperArmInside", "J_Sec_L_TopsUpperArmOutside",
        "J_Sec_R_TopsUpperArmInside", "J_Sec_R_TopsUpperArmOutside"
    ]
    clothChainCount = 0
    foreach (root in tops) {
        local tip = root + "_end"
        if (!(root in bones) || !(tip in bones)) {
            print("CLOTH_CHAIN_SKIP missing " + root + " / " + tip + "\n")
            continue
        }
        // Stiffer than hair so the shirt keeps its silhouette while still reacting to wind.
        setupClothChain(root, tip, 0.32, 0.24, 0.45, 0.0)
    }
    // Soft bust secondary bones (same spring family as Tops in the source VRM).
    foreach (side in ["L", "R"]) {
        local root = "J_Sec_" + side + "_Bust1"
        local tip = "J_Sec_" + side + "_Bust2"
        if ((root in bones) && (tip in bones))
            setupClothChain(root, tip, 0.40, 0.28, 0.35, 0.0)
    }
    // Keep soft clothing from collapsing into the torso / hips while swinging.
    if (HEAD_BONE in bones)
        dynamic.addBoneColliderSphere(bones[HEAD_BONE], 0.0, 0.02, 0.0, 0.11)
    if (CHEST_BONE in bones)
        dynamic.addBoneColliderSphere(bones[CHEST_BONE], 0.0, 0.0, 0.0, 0.14)
    if (HIPS_BONE in bones)
        dynamic.addBoneColliderSphere(bones[HIPS_BONE], 0.0, 0.0, 0.0, 0.13)
    return clothChainCount
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
    clothChainCount = discoverClothingChains()
    print("HAIR_CHAINS=" + hairChainCount + " CLOTH_CHAINS=" + clothChainCount +
          " HEAD_COLLIDERS=" + colliders + "\n")
    if (hairChainCount <= 0) throw "No hair chains were set up"
    if (clothChainCount <= 0) throw "No clothing cloth chains were set up"
}

function capeTopAnchor(t) {
    // Outer tips sit on the visible deltoid tops (past the hair curtain); the middle
    // bows behind the neck as a collar. Mesh parts use +offsetY — Cloth3D must match.
    local left = worldPos(L_UPPER_ARM) // mesh/cloth space already (+offsetY)
    local right = worldPos(R_UPPER_ARM)
    local neck = worldPos(NECK_BONE)
    local axes = bodyAxes()
    local lift = 0.03 // sit on the deltoid crest (do not add offsetY again)
    local out = 0.08 // on the deltoid top, not past the upper-arm silhouette
    // Keep the plate behind the hair curtain so strands stay between body and cape.
    local backSet = 0.16
    local ls = [
        left[0] + axes.right[0] * (-out) + axes.back[0] * backSet,
        left[1] + lift,
        left[2] + axes.right[2] * (-out) + axes.back[2] * backSet
    ]
    local rs = [
        right[0] + axes.right[0] * out + axes.back[0] * backSet,
        right[1] + lift,
        right[2] + axes.right[2] * out + axes.back[2] * backSet
    ]
    // Deeper collar recess — a hair channel down the spine so the pad is U-shaped.
    local collar = [
        neck[0] + axes.back[0] * (0.10 + backSet + 0.08),
        ((ls[1] + rs[1]) * 0.5) - 0.02,
        neck[2] + axes.back[2] * (0.10 + backSet + 0.08)
    ]
    if (t < 0.5) {
        local u = t * 2.0
        return [
            ls[0] + (collar[0] - ls[0]) * u,
            ls[1] + (collar[1] - ls[1]) * u,
            ls[2] + (collar[2] - ls[2]) * u
        ]
    }
    local u = (t - 0.5) * 2.0
    return [
        collar[0] + (rs[0] - collar[0]) * u,
        collar[1] + (rs[1] - collar[1]) * u,
        collar[2] + (rs[2] - collar[2]) * u
    ]
}

function capeShoulderPoint(t, depth) {
    // depth in [0,1]: wrap over the deltoid — front (lower) → peak on top → back.
    // A flat pad is edge-on from the side and reads as "floating behind"; the arch
    // gives the side view a clear cloth section sitting on the shoulder.
    local c = capeTopAnchor(t)
    local axes = bodyAxes()
    local edge = fabs(t - 0.5) * 2.0
    local center = 1.0 - edge // 1 at spine, 0 at outer tips
    local padDepth = 0.06 + 0.16 * edge
    // Slight forward bias so the pad sits on the shoulder top, not behind the scapula.
    local along = (depth - 0.3) * padDepth
    // Extra back recess near the spine so long hair has a clear channel through the pad.
    along += center * center * 0.10 * depth
    // Flat plateau across the deltoid crest, then drop toward the front/back faces.
    local yOff = 0.02 * edge
    if (depth < 0.25) yOff -= (0.25 - depth) * 0.10
    else if (depth > 0.65) yOff -= (depth - 0.65) * 0.06
    return [
        c[0] + axes.back[0] * along,
        c[1] + yOff,
        c[2] + axes.back[2] * along
    ]
}

function layoutCape() {
    local axes = bodyAxes()
    local pad = capePadRows
    if (pad < 1) pad = 1
    if (pad > capeRows) pad = capeRows
    for (local row = 0; row < capeRows; ++row) {
        for (local col = 0; col < capeCols; ++col) {
            local t = capeCols <= 1 ? 0.5 : col.tofloat() / (capeCols - 1).tofloat()
            local x = 0.0
            local y = 0.0
            local z = 0.0
            if (row < pad) {
                local depth = pad <= 1 ? 1.0 : row.tofloat() / (pad - 1).tofloat()
                local p = capeShoulderPoint(t, depth)
                x = p[0]; y = p[1]; z = p[2]
            } else {
                local backEdge = capeShoulderPoint(t, 1.0)
                local hang = (row - pad + 1).tofloat() * capeSpacing
                x = backEdge[0] + axes.back[0] * hang * 1.05
                y = backEdge[1] - hang
                z = backEdge[2] + axes.back[2] * hang * 1.05
            }
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
    // Probe radius: large enough for hair volume, small enough to keep deltoid tips seated.
    cape.setParticleSize(0.055)
    cape.setParticleMass(0.03)
    cape.setSelfCollision(true)
    cape.setFoldStiffness(0.55)
    cape.setMaxFoldAngle(140.0)
    cape.setAerodynamics(1.225, 1.05, 0.18)
    cape.setCollisionMaterial(0.35, 0.0)
    // Bright cool tint so the cloth does not read as brown hair under modest light.
    cape.setColor(0.25, 0.70, 0.95, 1.0)
    cape.setBounds(-2.5, -0.2, -2.5, 5.0, 3.0, 5.0)
    cape.setCollideWorld(bodyWorld)
    syncBodyColliders()
    layoutCape()

    capeAttach = []
    local pad = capePadRows
    if (pad < 1) pad = 1
    if (pad > capeRows) pad = capeRows
    // U-shaped pin: outer deltoid tips stay pinned; the spine channel stays free so
    // long hair can fall through and push that cloth back via hair proxies.
    for (local row = 0; row < pad; ++row) {
        local depth = pad <= 1 ? 1.0 : row.tofloat() / (pad - 1).tofloat()
        for (local col = 0; col < capeCols; ++col) {
            local idx = row * capeCols + col
            local t = capeCols <= 1 ? 0.5 : col.tofloat() / (capeCols - 1).tofloat()
            local edge = fabs(t - 0.5) * 2.0
            local p = capeShoulderPoint(t, depth)
            if (edge < 0.38) {
                // Free spine channel — starts in the recess, then collides with hair.
                cape.setParticleInverseMass(idx, 1.0 / 0.03)
                cape.setParticlePosition(idx, p[0], p[1], p[2])
                continue
            }
            cape.setParticleInverseMass(idx, 0.0)
            cape.attachParticle(idx, p[0], p[1], p[2], 0.0)
            capeAttach.append({ index = idx, t = t, depth = depth })
        }
    }
    print("CAPE_ATTACH=" + capeAttach.len() + " padRows=" + pad +
          " particles=" + cape.getParticleCount() +
          " bodyColliders=" + bodyColliders.len() + "\n")
    buildHairCapeCollision()
    syncHairProxyColliders()
    syncCapeProxyColliders()
}

function syncCapeAttachments() {
    foreach (item in capeAttach) {
        local p = capeShoulderPoint(item.t, item.depth)
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
    camera.setAmbient(0.48, 0.50, 0.54)
    camera.setActive(true)
    gfx.setDirectionalLight(-0.25, -1.0, -0.35, 1.45, 1.40, 1.35)
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
    print("CAPE_HAIR_READY hair=" + hairChainCount + " cloth=" + clothChainCount +
          " capeAttach=" + capeAttach.len() + " bodyColliders=" + bodyColliders.len() +
          " meshes=" + parts.len() + "\n")
}

eve_reload <- function() {
    ready = false
    cape = null
    dynamic = null
    bodyWorld = null
    bodyColliders = []
    hairProxyBones = []
    hairProxyColliders = []
    capeProxyColliderStart = -1
    capeProxyParticleIndices = []
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
        // Same-frame resolve: publish new cape spheres, then a short DynamicBone pass so
        // hair reacts to this frame's cloth instead of lagging one tick behind.
        syncCapeProxyColliders()
        dynamic.setExternalForce(0.0, 0.0, 0.0)
        dynamic.update(pose, 1.0 / 120.0)
        pose.computeWorld(skeleton)
        syncHairProxyColliders()
    }
    applySkins()
}

eve_render <- function() {
    gfx.clear()
    gfx.render3D()
    if (cape != null) cape.draw(gfx)
}
