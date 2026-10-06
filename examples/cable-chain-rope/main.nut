// Steel cable / iron chain / hemp rope gallery.
// Mesh recipes: mesh.cable | mesh.chain | mesh.rope
// PBR recipes:  pbr.cable.steel | pbr.chain.iron | pbr.rope.hemp
// Run: make run/<platform>-debug GAME=examples/cable-chain-rope

persist lab = {
    items = [],
    active = 0,
    segments = 6,
    segLength = 1.0,
    seed = 1847,
    autoSpin = true,
    yaw = 0.0,
    camera = null,
    uiReady = false,
    rWasDown = false,
    tabWas = false,
    plusWas = false,
    minusWas = false,
}

local RECIPES = [
    {
        mesh = "mesh.cable",
        pbr = "pbr.cable.steel",
        label = "Steel Cable",
        radius = 0.08,
        thickness = 0.022,
        strands = 6,
        twists = 1,
        metallic = 0.92,
        roughness = 0.32,
        tint = { r = 0.85, g = 0.87, b = 0.90 },
    },
    {
        mesh = "mesh.chain",
        pbr = "pbr.chain.iron",
        label = "Iron Chain",
        radius = 0.12,
        thickness = 0.035,
        strands = 2,
        twists = 1,
        metallic = 0.78,
        roughness = 0.45,
        tint = { r = 0.72, g = 0.70, b = 0.66 },
    },
    {
        mesh = "mesh.rope",
        pbr = "pbr.rope.hemp",
        label = "Hemp Rope",
        radius = 0.10,
        thickness = 0.045,
        strands = 3,
        twists = 1,
        metallic = 0.02,
        roughness = 0.72,
        tint = { r = 0.78, g = 0.62, b = 0.38 },
    },
]

function buildOne(spec) {
    local paramsResult = procgen.newParams()
    if (!paramsResult.ok) {
        if (lab.uiReady) {
            ui.select("lab")
            ui.setText("status", "PARAMS FAIL: " + paramsResult.status.summary)
        }
        return null
    }
    local p = paramsResult.value
    p.setInt("segments", lab.segments)
    p.setFloat("segLength", lab.segLength)
    p.setFloat("radius", spec.radius)
    p.setFloat("thickness", spec.thickness)
    p.setInt("strands", spec.strands)
    p.setInt("twists", spec.twists)
    p.setFloat("uvRepeat", 2.0)
    p.setInt("lengthSegs", 20)
    p.setInt("radialSegs", 8)
    p.setInt("majorSegs", 20)
    p.setInt("minorSegs", 8)

    local meshResult = procgen.generateMesh(spec.mesh, p, gfx)
    if (!meshResult.ok) {
        if (lab.uiReady) {
            ui.select("lab")
            ui.setText("status", "MESH FAIL " + spec.mesh + ": " + meshResult.status.summary)
        }
        return null
    }

    local tpResult = procgen.newParams()
    if (!tpResult.ok) return null
    local tp = tpResult.value
    tp.setSeed(lab.seed)
    tp.setSize(256, 256)
    tp.setInt("seamless", 1)
    tp.setInt("strands", spec.strands)
    tp.setFloat("twist", 2.5)
    tp.setFloat("gap", 0.12)
    tp.setFloat("wear", 0.25)
    local pbrResult = procgen.generatePbrMaterial(spec.pbr, tp)
    if (!pbrResult.ok) {
        if (lab.uiReady) {
            ui.select("lab")
            ui.setText("status", "PBR FAIL " + spec.pbr + ": " + pbrResult.status.summary)
        }
        return null
    }
    local maps = pbrResult.value
    local albedo = gfx.newTexture(maps.getAlbedo(), true, true)
    local normal = gfx.newTexture(maps.getNormal(), true, true)
    local height = gfx.newTexture(maps.getHeight(), true, true)
    maps.destroy()

    local mat = gfx.newMaterial()
    mat.setAlbedoTexture(albedo)
    mat.setNormalTexture(normal)
    mat.setHeightTexture(height)
    mat.setMetallic(spec.metallic)
    mat.setRoughness(spec.roughness)
    mat.setParallax(0.018, 8.0, 20.0)

    local ent = eve.Renderable3D()
    ent.setMesh(meshResult.value)
    ent.setMaterial(mat)
    ent.setTexture(albedo)
    ent.setNormalTexture(normal)
    ent.setHeightTexture(height)
    ent.setTint(spec.tint.r, spec.tint.g, spec.tint.b, 1.0)
    ent.setMetallic(spec.metallic)
    ent.setRoughness(spec.roughness)
    ent.setCastShadow(true)
    ent.setReceiveShadow(true)
    local half = lab.segments * lab.segLength * 0.5
    ent.setPosition(-half, 0.0, 0.0)
    return { ent = ent, label = spec.label, mesh = spec.mesh, pbr = spec.pbr }
}

function rebuild() {
    foreach (item in lab.items) {
        if (item != null && item.ent != null) item.ent.setVisible(false)
    }
    lab.items = []
    foreach (spec in RECIPES) {
        lab.items.push(buildOne(spec))
    }
    for (local i = 0; i < lab.items.len(); i++) {
        local item = lab.items[i]
        if (item == null || item.ent == null) continue
        item.ent.setVisible(i == lab.active)
    }
    if (lab.uiReady) {
        local cur = lab.items[lab.active]
        local name = cur != null ? cur.label : "?"
        ui.select("lab")
        ui.setText("status", name + "  segs=" + lab.segments + "  seed=" + lab.seed)
        if (cur != null) ui.setText("recipe", cur.mesh + " + " + cur.pbr)
    }
    print("CABLE_CHAIN_ROPE_PASS active=" + lab.active + " segments=" + lab.segments)
}

function buildPanel() {
    ui.setTheme("dark")
    ui.setNavKeyboard(true)
    ui.beginBuild()
    ui.beginWindow("CABLE / CHAIN / ROPE", "root")
    ui.text("PROCEDURAL LINEAR FIXTURES", "eyebrow")
    ui.text("mesh.cable + pbr.cable.steel", "recipe")
    ui.slider("Segments", lab.segments.tofloat(), 1.0, 24.0, "segments")
    ui.slider("Unit length", lab.segLength, 0.4, 3.0, "segLength")
    ui.button("Next fixture", "next")
    ui.button("New seed", "randomize")
    ui.button("Rebuild", "rebuild")
    ui.button("Pause rotation", "spin")
    ui.text("Tab cycle · +/- segments · R reseed", "hint")
    ui.text("Ready.", "status")
    ui.end()
    ui.mountBuildAs("lab")
    ui.select("lab")
    ui.setHostOverlay(true)
    ui.setHostPos(900.0, 30.0, 340.0, 420.0)
    lab.uiReady = true
}

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.09, 0.10, 1.0)
    lab.camera = eve.Camera3D()
    lab.camera.setEye(0.0, 1.1, 3.4)
    lab.camera.setTarget(0.0, 0.0, 0.0)
    lab.camera.setFov(48.0)
    lab.camera.setAmbient(0.32, 0.33, 0.35)
    gfx.setDirectionalLight(-0.45, 0.9, 0.3, 1.85, 1.7, 1.5)
    if (!lab.uiReady) buildPanel()
    rebuild()
}

eve_update = function(dt) {
    if (lab.autoSpin) {
        lab.yaw += dt * 0.45
        local ex = math.polarX(3.4, lab.yaw)
        local ez = math.polarY(3.4, lab.yaw)
        lab.camera.setEye(ex, 1.1, ez)
        lab.camera.setTarget(0.0, 0.0, 0.0)
    }

    local changed = ui.consumeChange()
    while (changed != "") {
        ui.select("lab")
        if (changed == "lab/segments") {
            lab.segments = ui.getValue("segments").tointeger()
            rebuild()
        } else if (changed == "lab/segLength") {
            lab.segLength = ui.getValue("segLength")
            rebuild()
        }
        changed = ui.consumeChange()
    }

    local clicked = ui.consumeClick()
    while (clicked != "") {
        if (clicked == "lab/next") {
            lab.active = (lab.active + 1) % RECIPES.len()
            rebuild()
        } else if (clicked == "lab/rebuild") {
            rebuild()
        } else if (clicked == "lab/randomize") {
            lab.seed = (lab.seed * 1664525 + 1013904223) & 0x7fffffff
            rebuild()
        } else if (clicked == "lab/spin") {
            lab.autoSpin = !lab.autoSpin
        }
        clicked = ui.consumeClick()
    }

    local tabDown = keyboard.isDown("tab")
    local tabPressed = tabDown && !lab.tabWas
    lab.tabWas = tabDown
    if (tabPressed) {
        lab.active = (lab.active + 1) % RECIPES.len()
        rebuild()
    }

    local plus = keyboard.isDown("plus") || keyboard.isDown("equals")
    local wasPlus = lab.plusWas
    lab.plusWas = plus
    if (plus && !wasPlus) {
        lab.segments = lab.segments + 1
        if (lab.segments > 24) lab.segments = 24
        rebuild()
    }

    local minus = keyboard.isDown("minus")
    local wasMinus = lab.minusWas
    lab.minusWas = minus
    if (minus && !wasMinus && lab.segments > 1) {
        lab.segments = lab.segments - 1
        rebuild()
    }

    local rDown = keyboard.isDown("r") || keyboard.isDown("R")
    local rPressed = rDown && !lab.rWasDown
    lab.rWasDown = rDown
    if (rPressed) {
        lab.seed = (lab.seed * 1664525 + 1013904223) & 0x7fffffff
        rebuild()
    }
}

eve_render = function() {
    gfx.clear()
    gfx.render3D()
    ui.beginFrameAndRender()
}
