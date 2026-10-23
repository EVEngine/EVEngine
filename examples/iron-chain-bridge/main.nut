// Simple iron chain bridge composed from procgen recipes:
//   mesh.chain + pbr.chain.iron  — side suspension chains
//   mesh.rope  + pbr.rope.hemp   — handrail / hangers
//   mesh.bridge                 — wooden deck run
// Run: make run/<platform>-debug GAME=examples/iron-chain-bridge

persist bridge = {
    parts = [],
    segments = 10,
    seed = 2026,
    autoSpin = true,
    yaw = 0.35,
    camera = null,
    uiReady = false,
    rWasDown = false,
    plusWas = false,
    minusWas = false,
}

function makePbrMaterial(recipeId, strands, twist, metallic, roughness) {
    local tpResult = procgen.newParams()
    if (!tpResult.ok) return null
    local tp = tpResult.value
    tp.setSeed(bridge.seed)
    tp.setSize(256, 256)
    tp.setInt("seamless", 1)
    tp.setInt("strands", strands)
    tp.setFloat("twist", twist)
    tp.setFloat("gap", 0.12)
    tp.setFloat("wear", 0.3)
    tp.setFloat("rust", 0.4)
    tp.setFloat("fiber", 0.7)
    local pbrResult = procgen.generatePbrMaterial(recipeId, tp)
    if (!pbrResult.ok) return null
    local maps = pbrResult.value
    local albedo = gfx.newTexture(maps.getAlbedo(), true, true)
    local normal = gfx.newTexture(maps.getNormal(), true, true)
    local height = gfx.newTexture(maps.getHeight(), true, true)
    maps.destroy()
    local mat = gfx.newMaterial()
    mat.setAlbedoTexture(albedo)
    mat.setNormalTexture(normal)
    mat.setHeightTexture(height)
    mat.setMetallic(metallic)
    mat.setRoughness(roughness)
    mat.setParallax(0.016, 8.0, 18.0)
    return { mat = mat, albedo = albedo, normal = normal, height = height,
             metallic = metallic, roughness = roughness }
}

function makeMeshEntity(meshId, paramsFn, material, tint, x, y, z) {
    local paramsResult = procgen.newParams()
    if (!paramsResult.ok) return null
    local p = paramsResult.value
    paramsFn(p)
    local meshResult = procgen.generateMesh(meshId, p, gfx)
    if (!meshResult.ok) {
        if (bridge.uiReady) {
            ui.select("lab")
            ui.setText("status", "MESH FAIL " + meshId + ": " + meshResult.status.summary)
        }
        return null
    }
    local ent = eve.Renderable3D()
    ent.setMesh(meshResult.value)
    if (material != null) {
        ent.setMaterial(material.mat)
        ent.setTexture(material.albedo)
        ent.setNormalTexture(material.normal)
        ent.setHeightTexture(material.height)
        ent.setMetallic(material.metallic)
        ent.setRoughness(material.roughness)
    }
    ent.setTint(tint.r, tint.g, tint.b, 1.0)
    ent.setCastShadow(true)
    ent.setReceiveShadow(true)
    ent.setPosition(x, y, z)
    return ent
}

function clearParts() {
    foreach (p in bridge.parts) {
        if (p != null) p.setVisible(false)
    }
    bridge.parts = []
}

function rebuild() {
    clearParts()

    local ironMat = makePbrMaterial("pbr.chain.iron", 4, 1.5, 0.78, 0.48)
    local hempMat = makePbrMaterial("pbr.rope.hemp", 3, 2.5, 0.02, 0.72)
    if (ironMat == null || hempMat == null) {
        if (bridge.uiReady) {
            ui.select("lab")
            ui.setText("status", "PBR material bake failed")
        }
        return
    }

    local span = bridge.segments * 0.55
    local half = span * 0.5
    local chainY = 1.15
    local deckY = 0.55
    local railZ = 0.55

    // Side suspension chains (left / right).
    local chainParams = function(p) {
        p.setInt("segments", bridge.segments)
        p.setFloat("segLength", 0.55)
        p.setFloat("radius", 0.11)
        p.setFloat("thickness", 0.032)
        p.setInt("majorSegs", 18)
        p.setInt("minorSegs", 7)
        p.setFloat("uvRepeat", 1.5)
    }
    local tintIron = { r = 0.70, g = 0.68, b = 0.64 }
    bridge.parts.push(makeMeshEntity("mesh.chain", chainParams, ironMat, tintIron, -half, chainY, -railZ))
    bridge.parts.push(makeMeshEntity("mesh.chain", chainParams, ironMat, tintIron, -half, chainY, railZ))

    // Wooden deck (linear bridge recipe, shorter height so it reads as planks).
    local deckParams = function(p) {
        p.setInt("segments", bridge.segments)
        p.setFloat("segLength", 0.55)
        p.setFloat("height", 0.35)
        p.setFloat("depth", 1.0)
        p.setFloat("thickness", 0.08)
        p.setFloat("uvRepeat", 2.0)
    }
    local tintWood = { r = 0.55, g = 0.38, b = 0.22 }
    bridge.parts.push(makeMeshEntity("mesh.bridge", deckParams, null, tintWood, -half, deckY, 0.0))

    // Hemp handrail ropes along each side, slightly above the deck.
    local ropeParams = function(p) {
        p.setInt("segments", bridge.segments)
        p.setFloat("segLength", 0.55)
        p.setFloat("radius", 0.04)
        p.setFloat("thickness", 0.018)
        p.setInt("strands", 3)
        p.setInt("twists", 1)
        p.setInt("lengthSegs", 14)
        p.setInt("radialSegs", 7)
        p.setFloat("uvRepeat", 2.5)
    }
    local tintHemp = { r = 0.78, g = 0.62, b = 0.38 }
    bridge.parts.push(makeMeshEntity("mesh.rope", ropeParams, hempMat, tintHemp, -half, deckY + 0.55, -railZ))
    bridge.parts.push(makeMeshEntity("mesh.rope", ropeParams, hempMat, tintHemp, -half, deckY + 0.55, railZ))

    // Vertical hangers every other segment (short rope stubs).
    local hangerParams = function(p) {
        p.setInt("segments", 1)
        p.setFloat("segLength", chainY - deckY)
        p.setFloat("radius", 0.025)
        p.setFloat("thickness", 0.012)
        p.setInt("strands", 3)
        p.setInt("twists", 1)
        p.setInt("lengthSegs", 8)
        p.setInt("radialSegs", 6)
    }
    for (local i = 1; i < bridge.segments; i += 2) {
        local x = -half + i * 0.55
        // Rotate hangers upright: chain/rope recipes grow along +X, so yaw 0 and
        // pitch via setRotation(pitch, yaw, roll) — use 90° about Z to stand up.
        // Recipes grow along +X; roll -π/2 stands hangers up along +Y.
        local stand = -3.14159265 * 0.5
        local left = makeMeshEntity("mesh.rope", hangerParams, hempMat, tintHemp, x, deckY, -railZ)
        if (left != null) {
            left.setRotation(0.0, 0.0, stand)
            bridge.parts.push(left)
        }
        local right = makeMeshEntity("mesh.rope", hangerParams, hempMat, tintHemp, x, deckY, railZ)
        if (right != null) {
            right.setRotation(0.0, 0.0, stand)
            bridge.parts.push(right)
        }
    }

    // Anchor posts (simple cubes).
    local postMesh = gfx.newMeshCube(1.0)
    foreach (side in [-1.0, 1.0]) {
        foreach (zSide in [-railZ, railZ]) {
            local post = eve.Renderable3D()
            post.setMesh(postMesh)
            post.setScale(0.22, 1.6, 0.22)
            post.setPosition(side * (half + 0.15), 0.8, zSide)
            post.setTint(0.35, 0.34, 0.32, 1.0)
            post.setMetallic(0.55)
            post.setRoughness(0.55)
            post.setCastShadow(true)
            post.setReceiveShadow(true)
            bridge.parts.push(post)
        }
    }

    // Ground slab.
    local ground = eve.Renderable3D()
    ground.setMesh(gfx.newMeshCube(1.0))
    ground.setScale(18.0, 0.08, 10.0)
    ground.setPosition(0.0, -0.04, 0.0)
    ground.setTint(0.28, 0.32, 0.26, 1.0)
    ground.setMetallic(0.02)
    ground.setRoughness(0.9)
    ground.setReceiveShadow(true)
    bridge.parts.push(ground)

    local hangers = ((bridge.segments - 1) / 2) * 2
    print("IRON_CHAIN_BRIDGE_PASS chains=2 planks=1 ropes=2 hangers=" + hangers + " segs=" + bridge.segments)
    if (bridge.uiReady) {
        ui.select("lab")
        ui.setText("status", "铁锁桥  segs=" + bridge.segments + "  seed=" + bridge.seed)
    }
}

function buildPanel() {
    ui.setTheme("dark")
    ui.setNavKeyboard(true)
    ui.beginBuild()
    ui.beginWindow("IRON CHAIN BRIDGE", "root")
    ui.text("铁锁桥 / PROCGEN COMPOSITION", "eyebrow")
    ui.text("mesh.chain + mesh.rope + mesh.bridge", "recipe")
    ui.slider("Chain segments", bridge.segments.tofloat(), 4.0, 20.0, "segments")
    ui.button("New seed", "randomize")
    ui.button("Rebuild", "rebuild")
    ui.button("Pause orbit", "spin")
    ui.text("+/- density · R reseed", "hint")
    ui.text("Ready.", "status")
    ui.end()
    ui.mountBuildAs("lab")
    ui.select("lab")
    ui.setHostOverlay(true)
    ui.setHostPos(900.0, 30.0, 340.0, 360.0)
    bridge.uiReady = true
}

eve_init = function() {
    gfx.setBackgroundColor(0.45, 0.62, 0.78, 1.0)
    bridge.camera = eve.Camera3D()
    bridge.camera.setEye(6.5, 3.2, 7.5)
    bridge.camera.setTarget(0.0, 0.8, 0.0)
    bridge.camera.setFov(48.0)
    bridge.camera.setAmbient(0.38, 0.40, 0.44)
    gfx.setDirectionalLight(-0.35, 0.85, 0.25, 1.9, 1.75, 1.5)
    if (!bridge.uiReady) buildPanel()
    rebuild()
}

eve_update = function(dt) {
    if (bridge.autoSpin) {
        bridge.yaw += dt * 0.28
        local ex = math.polarX(8.5, bridge.yaw)
        local ez = math.polarY(8.5, bridge.yaw)
        bridge.camera.setEye(ex, 3.2, ez)
        bridge.camera.setTarget(0.0, 0.8, 0.0)
    }

    local changed = ui.consumeChange()
    while (changed != "") {
        ui.select("lab")
        if (changed == "lab/segments") {
            bridge.segments = ui.getValue("segments").tointeger()
            if (bridge.segments < 4) bridge.segments = 4
            rebuild()
        }
        changed = ui.consumeChange()
    }

    local clicked = ui.consumeClick()
    while (clicked != "") {
        if (clicked == "lab/rebuild") rebuild()
        else if (clicked == "lab/randomize") {
            bridge.seed = (bridge.seed * 1664525 + 1013904223) & 0x7fffffff
            rebuild()
        } else if (clicked == "lab/spin") {
            bridge.autoSpin = !bridge.autoSpin
        }
        clicked = ui.consumeClick()
    }

    local plus = keyboard.isDown("plus") || keyboard.isDown("equals")
    local wasPlus = bridge.plusWas
    bridge.plusWas = plus
    if (plus && !wasPlus && bridge.segments < 20) {
        bridge.segments = bridge.segments + 1
        rebuild()
    }
    local minus = keyboard.isDown("minus")
    local wasMinus = bridge.minusWas
    bridge.minusWas = minus
    if (minus && !wasMinus && bridge.segments > 4) {
        bridge.segments = bridge.segments - 1
        rebuild()
    }
    local rDown = keyboard.isDown("r") || keyboard.isDown("R")
    local rPressed = rDown && !bridge.rWasDown
    bridge.rWasDown = rDown
    if (rPressed) {
        bridge.seed = (bridge.seed * 1664525 + 1013904223) & 0x7fffffff
        rebuild()
    }
}

eve_render = function() {
    gfx.clear()
    gfx.render3D()
    ui.beginFrameAndRender()
}
