persist factory = null
persist paused = false
persist useGpu = false
persist gpu = null
persist posShader = null
persist simBuffer = null
persist posBuffer = null
persist fixedAcc = 0.0
persist hudCrafts = 0
persist hudSunk = 0
persist hudItems = 0
persist kPosKernel = null

const CELL = 24.0
const ORIGIN_X = 48.0
const ORIGIN_Y = 120.0
const GRID_W = 28
const GRID_H = 12
const FIXED_DT = 0.0333333

function ignoreResult(result, reason) {
    if (result == null) return
    if (("ok" in result) && result.ok) return
    eve.result.ignore(result, reason)
}

function buildLine() {
    factory.clear()
    local recipe = {
        id = "ingot",
        inputType = 1,
        inputCount = 1,
        outputType = 2,
        outputCount = 1,
        durationSec = 0.6
    }
    ignoreResult(factory.registerRecipe(recipe), "recipe")
    ignoreResult(factory.placeSource(1, 5, 1, 3.0, 0), "source")
    for (local x = 2; x <= 8; x += 1)
        ignoreResult(factory.placeBelt(x, 5, 0, 0), "belt-in")
    ignoreResult(factory.placeMachine(9, 4, 2, 2, "ingot", 0), "machine")
    for (local x = 11; x <= 20; x += 1)
        ignoreResult(factory.placeBelt(x, 5, 0, 0), "belt-out")
    ignoreResult(factory.placeSink(21, 5, 0), "sink")
    factory.setBeltSpeed(5.0)
    factory.setBufferCapacity(6)
}

function setupGpu() {
    useGpu = false
    posShader = null
    simBuffer = null
    posBuffer = null
    if (!has_module("gpgpu")) return
    try {
        gpu = eve.Gpgpu()
        if (!gpu.isAvailable()) return
        local src = fs.readText("item_pos.comp")
        posShader = gpu.newShader(src)
        useGpu = true
        print("factory: gpgpu position kernel ready\n")
    } catch (e) {
        useGpu = false
        print("factory: gpgpu unavailable: " + e + "\n")
    }
}

function ensureGpuBuffers(itemCount) {
    if (!useGpu || itemCount <= 0) return false
    local simBytes = itemCount * 6 * 4
    local posBytes = itemCount * 3 * 4
    if (simBuffer == null || simBuffer.getSize() < simBytes)
        simBuffer = gpu.newBuffer(simBytes, "storage")
    if (posBuffer == null || posBuffer.getSize() < posBytes)
        posBuffer = gpu.newBuffer(posBytes, "storage")
    return true
}

function drawGrid() {
    for (local y = 0; y < factory.getHeight(); y += 1) {
        for (local x = 0; x < factory.getWidth(); x += 1) {
            local kind = factory.cellKind(x, y, 0)
            if (kind == 0) continue
            local px = ORIGIN_X + x * CELL
            local py = ORIGIN_Y + y * CELL
            if (kind == 1)
                gfx.drawSolidRect(px + 1.0, py + 1.0, CELL - 2.0, CELL - 2.0, 0.22, 0.24, 0.28, 1.0)
            else if (kind == 2)
                gfx.drawSolidRect(px + 1.0, py + 1.0, CELL - 2.0, CELL - 2.0, 0.18, 0.42, 0.72, 1.0)
            else if (kind == 3)
                gfx.drawSolidRect(px + 2.0, py + 2.0, CELL - 4.0, CELL - 4.0, 0.72, 0.42, 0.16, 1.0)
            else if (kind == 4)
                gfx.drawSolidRect(px + 2.0, py + 2.0, CELL - 4.0, CELL - 4.0, 0.45, 0.18, 0.22, 1.0)
        }
    }
}

function drawItemsCpu() {
    local n = factory.getItemCount()
    for (local i = 0; i < n; i += 1) {
        local item = factory.getItem(i)
        local r = 0.95
        local g = 0.55
        local b = 0.18
        if (item.type == 2) {
            r = 0.25
            g = 0.82
            b = 0.78
        }
        gfx.drawSolidRect(item.x - 5.0, item.y - 5.0, 10.0, 10.0, r, g, b, 1.0)
    }
}

function drawItemsGpu() {
    local n = factory.getItemCount()
    if (n <= 0) return
    local packed = factory.packItemSimFloats()
    if (!packed.ok) {
        eve.result.ignore(packed, "pack sim")
        drawItemsCpu()
        return
    }
    if (!ensureGpuBuffers(n)) {
        drawItemsCpu()
        return
    }
    local floats = packed.value
    local i = 0
    local total = n * 6
    while (i < total) {
        local v = floats[i]
        simBuffer.writeFloat32(i, v)
        i += 1
    }
    posShader.bindBuffer(0, simBuffer)
    posShader.bindBuffer(1, posBuffer)
    posShader.setFloat(0, n * 1.0)
    posShader.setFloat(1, CELL)
    posShader.setFloat(2, ORIGIN_X)
    posShader.setFloat(3, ORIGIN_Y)
    local groups = (n + 63) / 64
    gpu.dispatch(posShader, groups, 1, 1)
    local worldCount = n * 3
    local world = posBuffer.readFloat32s(worldCount, 0)
    local wi = 0
    while (wi < n) {
        local ox = world[wi * 3]
        local oy = world[wi * 3 + 1]
        local kind = world[wi * 3 + 2]
        local r = 0.95
        local g = 0.55
        local b = 0.18
        if (kind == 2.0) {
            r = 0.25
            g = 0.82
            b = 0.78
        }
        gfx.drawSolidRect(ox - 5.0, oy - 5.0, 10.0, 10.0, r, g, b, 1.0)
        wi += 1
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.07, 0.08, 0.10, 1.0)
    if (factory == null) factory = eve.Logistics()
    local cfg = factory.configure(GRID_W, GRID_H, 1, CELL, ORIGIN_X, ORIGIN_Y)
    if (!cfg.ok) throw cfg.status.summary
    buildLine()
    setupGpu()
}

eve_reload <- function() {
    setupGpu()
}

eve_update = function(dt) {
    if (dt <= 0.0) return
    if (key_just_pressed("space")) paused = !paused
    if (key_just_pressed("r") || key_just_pressed("R")) buildLine()
    if (paused) return
    fixedAcc += dt
    if (fixedAcc > 0.25) fixedAcc = 0.25
    while (fixedAcc >= FIXED_DT) {
        local step = factory.advance(FIXED_DT)
        if (!step.ok) throw step.status.summary
        fixedAcc -= FIXED_DT
    }
    hudCrafts = factory.getCraftsTotal()
    hudSunk = factory.getSunkTotal()
    hudItems = factory.getItemCount()
}

eve_render = function() {
    gfx.clear()
    drawGrid()
    if (useGpu) drawItemsGpu()
    else drawItemsCpu()
    local machine = factory.getMachine(0)
    local craftBar = 0.0
    if (machine.id >= 0 && machine.crafting) craftBar = machine.craftProgress
    gfx.drawSolidRect(48.0, 24.0, 240.0, 12.0, 0.12, 0.14, 0.16, 1.0)
    gfx.drawSolidRect(48.0, 24.0, 240.0 * craftBar, 12.0, 0.30, 0.70, 0.95, 1.0)
}
