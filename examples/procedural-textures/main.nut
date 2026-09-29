// FLOOR LAB — parametric procedural floor textures.
// Combines tex.floor.wood + tex.floor.tile recipes into a live gallery:
// wood planks / herringbone / parquet and ceramic tile patterns via params.

persist floorLab = {
    camera = null,
    tiles = [],
    preview = null,
    albedo = null,
    normal = null,
    seed = 1847,
    size = 256,
    mode = 0, // 0 wood, 1 tile
    layoutIndex = 0,
    patternIndex = 0,
    toneIndex = 0,
    paletteIndex = 0,
    rows = 6.0,
    cols = 4.0,
    gap = 0.04,
    grain = 8.0,
    wear = 0.25,
    stain = 0.15,
    tilesX = 4.0,
    tilesY = 4.0,
    grout = 0.06,
    glaze = 0.35,
    motif = 0.55,
    speckles = 0.15,
    autoSpin = true,
    yaw = 0.0,
    uiReady = false,
    rWasDown = false,
    tabWasDown = false,
}

WOOD_LAYOUTS <- ["planks", "staggered", "herringbone", "chevron", "parquet", "basket"]
WOOD_TONES <- ["oak", "walnut", "pine", "cherry", "ebony", "ash"]
TILE_PATTERNS <- ["square", "checker", "diamond", "hex", "subway", "mosaic", "basket", "herringbone"]
TILE_PALETTES <- ["ceramic", "terracotta", "slate", "porcelain", "marble", "black", "mosaic", "subway"]

PRESETS <- [
    { mode = 0, layout = "planks", tone = "oak", rows = 6, cols = 4, gap = 0.04, grain = 8.0, wear = 0.2, stain = 0.1, label = "Oak planks" },
    { mode = 0, layout = "herringbone", tone = "walnut", rows = 8, cols = 8, gap = 0.035, grain = 10.0, wear = 0.3, stain = 0.25, label = "Walnut herringbone" },
    { mode = 0, layout = "parquet", tone = "cherry", rows = 8, cols = 8, gap = 0.045, grain = 7.0, wear = 0.22, stain = 0.18, label = "Cherry parquet" },
    { mode = 0, layout = "staggered", tone = "pine", rows = 5, cols = 3, gap = 0.05, grain = 6.5, wear = 0.35, stain = 0.05, label = "Pine staggered" },
    { mode = 1, pattern = "square", palette = "ceramic", tilesX = 4, tilesY = 4, grout = 0.06, glaze = 0.4, motif = 0.5, speckles = 0.12, label = "Ceramic square" },
    { mode = 1, pattern = "checker", palette = "black", tilesX = 6, tilesY = 6, grout = 0.05, glaze = 0.25, motif = 0.0, speckles = 0.08, label = "Checker marble" },
    { mode = 1, pattern = "diamond", palette = "porcelain", tilesX = 5, tilesY = 5, grout = 0.055, glaze = 0.45, motif = 0.7, speckles = 0.1, label = "Porcelain diamond" },
    { mode = 1, pattern = "hex", palette = "terracotta", tilesX = 6, tilesY = 6, grout = 0.07, glaze = 0.3, motif = 0.2, speckles = 0.2, label = "Terracotta hex" },
    { mode = 1, pattern = "subway", palette = "subway", tilesX = 5, tilesY = 8, grout = 0.045, glaze = 0.5, motif = 0.0, speckles = 0.05, label = "Subway tile" },
    { mode = 1, pattern = "mosaic", palette = "mosaic", tilesX = 7, tilesY = 7, grout = 0.08, glaze = 0.35, motif = 0.4, speckles = 0.25, label = "Stone mosaic" },
]

persist presetIndex = 0

function indexOf(list, value) {
    for (local i = 0; i < list.len(); i++) {
        if (list[i] == value) return i
    }
    return 0
}

function recipeId() {
    return floorLab.mode == 0 ? "tex.floor.wood" : "tex.floor.tile"
}

function statusLabel() {
    if (floorLab.mode == 0) {
        return "wood / " + WOOD_LAYOUTS[floorLab.layoutIndex] + " / " + WOOD_TONES[floorLab.toneIndex]
    }
    return "tile / " + TILE_PATTERNS[floorLab.patternIndex] + " / " + TILE_PALETTES[floorLab.paletteIndex]
}

function applyParams(p) {
    p.setSeed(floorLab.seed)
    p.setSize(floorLab.size, floorLab.size)
    p.setInt("seamless", 1)
    p.setInt("colors", 10)
    p.setInt("pixelSize", 1)
    if (floorLab.mode == 0) {
        p.setString("layout", WOOD_LAYOUTS[floorLab.layoutIndex])
        p.setString("tone", WOOD_TONES[floorLab.toneIndex])
        p.setInt("rows", floorLab.rows.tointeger())
        p.setInt("cols", floorLab.cols.tointeger())
        p.setFloat("gap", floorLab.gap)
        p.setFloat("grain", floorLab.grain)
        p.setFloat("warp", 1.4)
        p.setFloat("wear", floorLab.wear)
        p.setFloat("stain", floorLab.stain)
        p.setFloat("bevel", 0.08)
    } else {
        p.setString("pattern", TILE_PATTERNS[floorLab.patternIndex])
        p.setString("palette", TILE_PALETTES[floorLab.paletteIndex])
        p.setInt("tilesX", floorLab.tilesX.tointeger())
        p.setInt("tilesY", floorLab.tilesY.tointeger())
        p.setFloat("grout", floorLab.grout)
        p.setFloat("bevel", 0.10)
        p.setFloat("glaze", floorLab.glaze)
        p.setFloat("wear", floorLab.wear)
        p.setFloat("speckles", floorLab.speckles)
        p.setFloat("motif", floorLab.motif)
    }
}

function rebuildTextures() {
    local paramsResult = procgen.newParams()
    if (!paramsResult.ok) {
        if (floorLab.uiReady) {
            ui.select("lab")
            ui.setText("status", "PARAMS FAIL: " + paramsResult.status.summary)
        }
        return
    }
    local p = paramsResult.value
    applyParams(p)

    local texResult = procgen.generateTexture(recipeId(), p, gfx)
    if (!texResult.ok) {
        if (floorLab.uiReady) {
            ui.select("lab")
            ui.setText("status", "TEX FAIL: " + texResult.status.summary)
        }
        return
    }
    floorLab.albedo = texResult.value

    local normalResult = procgen.generateNormalImage(recipeId(), p)
    if (normalResult.ok) {
        floorLab.normal = gfx.newTexture(normalResult.value, true, true)
    } else {
        floorLab.normal = null
    }

    if (floorLab.preview != null) {
        floorLab.preview.setTexture(floorLab.albedo)
        if (floorLab.normal != null) floorLab.preview.setNormalTexture(floorLab.normal)
        floorLab.preview.setRoughness(floorLab.mode == 0 ? 0.55 : 0.35)
    }

    foreach (tile in floorLab.tiles) {
        if (tile == null) continue
        tile.setTexture(floorLab.albedo)
        if (floorLab.normal != null) tile.setNormalTexture(floorLab.normal)
    }

    if (floorLab.uiReady) {
        ui.select("lab")
        ui.setText("recipe", recipeId())
        ui.setText("status", statusLabel() + "  seed=" + floorLab.seed)
    }
}

function buildGallery() {
    floorLab.tiles = []
    local mesh = gfx.newMeshCube(1.0)
    local spacing = 2.35
    local startX = -spacing * 1.5
    for (local i = 0; i < 4; i++) {
        local ent = eve.Renderable3D()
        ent.setMesh(mesh)
        ent.setScale(2.0, 0.06, 2.0)
        ent.setPosition(startX + i * spacing, 0.0, 0.0)
        ent.setTint(1.0, 1.0, 1.0, 1.0)
        ent.setMetallic(0.02)
        ent.setRoughness(0.45)
        ent.setCastShadow(true)
        ent.setReceiveShadow(true)
        floorLab.tiles.append(ent)
    }

    floorLab.preview = eve.Renderable3D()
    floorLab.preview.setMesh(mesh)
    floorLab.preview.setScale(3.4, 0.08, 3.4)
    floorLab.preview.setPosition(0.0, 0.0, -3.6)
    floorLab.preview.setTint(1.0, 1.0, 1.0, 1.0)
    floorLab.preview.setMetallic(0.02)
    floorLab.preview.setRoughness(0.45)
    floorLab.preview.setCastShadow(true)
    floorLab.preview.setReceiveShadow(true)
}

function applyPreset(index) {
    if (index < 0 || index >= PRESETS.len()) return
    presetIndex = index
    local pr = PRESETS[index]
    floorLab.mode = pr.mode
    floorLab.wear = ("wear" in pr) ? pr.wear : floorLab.wear
    if (pr.mode == 0) {
        floorLab.layoutIndex = indexOf(WOOD_LAYOUTS, pr.layout)
        floorLab.toneIndex = indexOf(WOOD_TONES, pr.tone)
        floorLab.rows = pr.rows.tofloat()
        floorLab.cols = pr.cols.tofloat()
        floorLab.gap = pr.gap
        floorLab.grain = pr.grain
        floorLab.stain = pr.stain
    } else {
        floorLab.patternIndex = indexOf(TILE_PATTERNS, pr.pattern)
        floorLab.paletteIndex = indexOf(TILE_PALETTES, pr.palette)
        floorLab.tilesX = pr.tilesX.tofloat()
        floorLab.tilesY = pr.tilesY.tofloat()
        floorLab.grout = pr.grout
        floorLab.glaze = pr.glaze
        floorLab.motif = pr.motif
        floorLab.speckles = pr.speckles
    }
    if (floorLab.uiReady) syncSliders()
    rebuildTextures()
}

function syncSliders() {
    ui.select("lab")
    ui.setValue("rows", floorLab.rows)
    ui.setValue("cols", floorLab.cols)
    ui.setValue("gap", floorLab.gap)
    ui.setValue("grain", floorLab.grain)
    ui.setValue("wear", floorLab.wear)
    ui.setValue("stain", floorLab.stain)
    ui.setValue("tilesX", floorLab.tilesX)
    ui.setValue("tilesY", floorLab.tilesY)
    ui.setValue("grout", floorLab.grout)
    ui.setValue("glaze", floorLab.glaze)
    ui.setValue("motif", floorLab.motif)
    ui.setValue("speckles", floorLab.speckles)
    ui.setText("modeBtn", floorLab.mode == 0 ? "Mode: Wood floor" : "Mode: Ceramic tile")
    ui.setText("variantBtn", floorLab.mode == 0
        ? ("Layout: " + WOOD_LAYOUTS[floorLab.layoutIndex])
        : ("Pattern: " + TILE_PATTERNS[floorLab.patternIndex]))
    ui.setText("paletteBtn", floorLab.mode == 0
        ? ("Tone: " + WOOD_TONES[floorLab.toneIndex])
        : ("Palette: " + TILE_PALETTES[floorLab.paletteIndex]))
}

function buildPanel() {
    ui.setTheme("dark")
    ui.setNavKeyboard(true)
    ui.beginBuild()
    ui.beginWindow("FLOOR LAB / PROCEDURAL TEXTURES", "root")
    ui.text("Recipe", "eyebrow")
    ui.text(recipeId(), "recipe")
    ui.button("Mode: Wood floor", "modeBtn")
    ui.button("Layout: planks", "variantBtn")
    ui.button("Tone: oak", "paletteBtn")
    ui.text("Wood", "woodLabel")
    ui.slider("Rows", floorLab.rows, 1.0, 16.0, "rows")
    ui.slider("Columns", floorLab.cols, 1.0, 16.0, "cols")
    ui.slider("Groove", floorLab.gap, 0.0, 0.2, "gap")
    ui.slider("Grain", floorLab.grain, 1.0, 24.0, "grain")
    ui.slider("Wear", floorLab.wear, 0.0, 1.0, "wear")
    ui.slider("Stain", floorLab.stain, 0.0, 1.0, "stain")
    ui.text("Tile", "tileLabel")
    ui.slider("Tiles X", floorLab.tilesX, 1.0, 16.0, "tilesX")
    ui.slider("Tiles Y", floorLab.tilesY, 1.0, 16.0, "tilesY")
    ui.slider("Grout", floorLab.grout, 0.0, 0.25, "grout")
    ui.slider("Glaze", floorLab.glaze, 0.0, 1.0, "glaze")
    ui.slider("Motif", floorLab.motif, 0.0, 1.0, "motif")
    ui.slider("Speckles", floorLab.speckles, 0.0, 1.0, "speckles")
    ui.beginRow("actions", 8.0)
    ui.button("New seed", "randomize")
    ui.button("Rebuild", "rebuild")
    ui.end()
    ui.button("Next preset", "nextPreset")
    ui.button("Pause rotation", "spin")
    ui.text("Tab switches wood/tile · R new seed · presets cycle looks", "hint")
    ui.text("Generating…", "status")
    ui.end()
    ui.mountBuildAs("lab")
    ui.select("lab")
    ui.setHostOverlay(true)
    ui.setHostPos(860.0, 24.0, 300.0, 680.0)
    floorLab.uiReady = true
    syncSliders()
}

function randomizeSeed() {
    floorLab.seed = (floorLab.seed * 1664525 + 1013904223) & 0x7fffffff
    rebuildTextures()
}

function keyPressedR() {
    local down = keyboard.isDown("r") || keyboard.isDown("R")
    local pressed = down && !floorLab.rWasDown
    floorLab.rWasDown = down
    return pressed
}

function keyPressedTab() {
    local down = keyboard.isDown("tab") || keyboard.isDown("Tab")
    local pressed = down && !floorLab.tabWasDown
    floorLab.tabWasDown = down
    return pressed
}

eve_init = function() {
    gfx.setBackgroundColor(0.07, 0.075, 0.085, 1.0)
    floorLab.camera = eve.Camera3D()
    floorLab.camera.setEye(0.0, 5.8, 8.2)
    floorLab.camera.setTarget(0.0, 0.0, -1.2)
    floorLab.camera.setFov(40.0)
    floorLab.camera.setAmbient(0.28, 0.28, 0.30)
    gfx.setDirectionalLight(-0.45, 0.9, 0.25, 1.9, 1.75, 1.55)
    buildGallery()
    if (!floorLab.uiReady) buildPanel()
    applyPreset(0)
}

eve_update = function(dt) {
    if (floorLab.autoSpin) {
        floorLab.yaw += dt * 0.35
        if (floorLab.preview != null) floorLab.preview.setYaw(floorLab.yaw)
        local i = 0
        foreach (tile in floorLab.tiles) {
            if (tile != null) tile.setYaw(floorLab.yaw * 0.4 + i * 0.15)
            i += 1
        }
    }

    if (keyPressedR()) randomizeSeed()
    if (keyPressedTab()) {
        floorLab.mode = floorLab.mode == 0 ? 1 : 0
        if (floorLab.uiReady) syncSliders()
        rebuildTextures()
    }

    local clicked = ui.consumeClick()
    while (clicked != "") {
        if (clicked == "lab/randomize") randomizeSeed()
        else if (clicked == "lab/rebuild") rebuildTextures()
        else if (clicked == "lab/nextPreset") applyPreset((presetIndex + 1) % PRESETS.len())
        else if (clicked == "lab/modeBtn") {
            floorLab.mode = floorLab.mode == 0 ? 1 : 0
            syncSliders()
            rebuildTextures()
        }
        else if (clicked == "lab/variantBtn") {
            if (floorLab.mode == 0) {
                floorLab.layoutIndex = (floorLab.layoutIndex + 1) % WOOD_LAYOUTS.len()
            } else {
                floorLab.patternIndex = (floorLab.patternIndex + 1) % TILE_PATTERNS.len()
            }
            syncSliders()
            rebuildTextures()
        }
        else if (clicked == "lab/paletteBtn") {
            if (floorLab.mode == 0) {
                floorLab.toneIndex = (floorLab.toneIndex + 1) % WOOD_TONES.len()
            } else {
                floorLab.paletteIndex = (floorLab.paletteIndex + 1) % TILE_PALETTES.len()
            }
            syncSliders()
            rebuildTextures()
        }
        else if (clicked == "lab/spin") {
            floorLab.autoSpin = !floorLab.autoSpin
            ui.select("lab")
            ui.setText("spin", floorLab.autoSpin ? "Pause rotation" : "Resume rotation")
        }
        clicked = ui.consumeClick()
    }

    local changed = ui.consumeChange()
    local rebuild = false
    while (changed != "") {
        ui.select("lab")
        if (changed == "lab/rows") { floorLab.rows = ui.getValue("rows"); rebuild = true }
        else if (changed == "lab/cols") { floorLab.cols = ui.getValue("cols"); rebuild = true }
        else if (changed == "lab/gap") { floorLab.gap = ui.getValue("gap"); rebuild = true }
        else if (changed == "lab/grain") { floorLab.grain = ui.getValue("grain"); rebuild = true }
        else if (changed == "lab/wear") { floorLab.wear = ui.getValue("wear"); rebuild = true }
        else if (changed == "lab/stain") { floorLab.stain = ui.getValue("stain"); rebuild = true }
        else if (changed == "lab/tilesX") { floorLab.tilesX = ui.getValue("tilesX"); rebuild = true }
        else if (changed == "lab/tilesY") { floorLab.tilesY = ui.getValue("tilesY"); rebuild = true }
        else if (changed == "lab/grout") { floorLab.grout = ui.getValue("grout"); rebuild = true }
        else if (changed == "lab/glaze") { floorLab.glaze = ui.getValue("glaze"); rebuild = true }
        else if (changed == "lab/motif") { floorLab.motif = ui.getValue("motif"); rebuild = true }
        else if (changed == "lab/speckles") { floorLab.speckles = ui.getValue("speckles"); rebuild = true }
        changed = ui.consumeChange()
    }
    if (rebuild) rebuildTextures()
}

eve_render = function() {
    gfx.clear()
    gfx.render3D()
    if (floorLab.albedo != null) {
        gfx.drawTexturedRect(floorLab.albedo, 36.0, 36.0, 160.0, 160.0, 1.0, 1.0, 1.0, 1.0)
    }
    ui.beginFrameAndRender()
}
