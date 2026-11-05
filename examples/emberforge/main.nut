// Emberforge — craft a blade, quench it on the anvil, equip, then test the cut.

persist inv = null
persist rpg = null
persist forge = null
persist bag = null
persist gear = null
persist hero = null
persist bootstrapped = false
persist paused = false
persist simTick = 0
persist fixedAcc = 0.0
persist hudFont = null
persist toast = ""
persist toastLife = 0.0
persist sparks = []
persist floaters = []
persist heat = 0.0
persist hammerPhase = 0.0
persist hammerActive = false
persist ambientT = 0.0
persist dummyHit = 0.0
persist dummyHp = 100.0
persist lastKeyCraft = false
persist lastKeyUpgrade = false
persist lastKeyEquip = false
persist lastKeyRestock = false
persist lastKeyPause = false
persist lastKeyStrike = false

const FIXED_DT = 0.05
const MAX_ENHANCE = 5
const W = 960.0
const H = 640.0

function pushToast(text) {
    toast = text
    toastLife = 2.6
    print("[emberforge] " + text + "\n")
}

function restock() {
    bag.addItem("ore.iron", 12)
    bag.addItem("coal", 6)
    bag.addItem("emberstone", 10)
    pushToast("Restocked ore, coal and emberstones")
}

function ensureContent() {
    inv.registerItemsFromJson(@"[
      {""id"":""ore.iron"",""displayName"":""Iron Ore"",""maxStack"":99,""weight"":0.4,""tags"":[""material""]},
      {""id"":""coal"",""displayName"":""Coal"",""maxStack"":99,""weight"":0.2,""tags"":[""material""]},
      {""id"":""emberstone"",""displayName"":""Emberstone"",""maxStack"":99,""weight"":0.1,""tags"":[""material"",""catalyst""]},
      {""id"":""sword.ember"",""displayName"":""Ember Blade"",""maxStack"":1,""weight"":3.5,
       ""equipSlot"":""weapon"",""tags"":[""weapon"",""blade""]}
    ]")
    rpg.registerItemStatsFromJson("sword.ember", @"[
      {""attribute"":""attack"",""op"":""add"",""value"":12.0},
      {""attribute"":""crit"",""op"":""add"",""value"":4.0}
    ]")
}

function ensureSetup() {
    if (bootstrapped) return
    gfx.setBackgroundColor(0.05, 0.04, 0.05, 1.0)
    if (hudFont == null) {
        local fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 18)
        hudFont = gfx.newFont(fontData,
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%_+#")
    }

    inv = eve.Inventory()
    rpg = eve.RPG()
    ensureContent()

    bag = inv.newBag(16)
    bag.setId("forge.bag")
    gear = inv.newEquipmentSet()
    gear.setId("forge.gear")
    gear.defineSlot("weapon")
    gear.addSlotAllowedTag("weapon", "weapon")

    hero = rpg.newActor()
    hero.setBaseAttribute("attack", 8.0)
    hero.setBaseAttribute("crit", 0.0)

    forge = eve.RPGForge()
    local bound = forge.bind(bag, gear, hero)
    if (!bound.ok) throw bound.status.summary

    restock()
    bootstrapped = true
    pushToast("Emberforge lit — C craft, U enhance, E equip, F strike")
}

function bladeSlot() {
    return bag.findItem("sword.ember")
}

function bladeEnhance() {
    local slot = bladeSlot()
    if (slot < 0) return -1
    local text = bag.getSlotProp(slot, "enhance", "0")
    return text.tointeger()
}

function spawnSparks(cx, cy, count) {
    for (local i = 0; i < count; i += 1) {
        sparks.push({
            x = cx + (i % 7) * 4.0 - 12.0,
            y = cy + (i % 3) * 2.0,
            vx = ((i * 37) % 17) - 8.0,
            vy = -55.0 - ((i * 19) % 40),
            life = 0.5 + (i % 5) * 0.08,
            r = 1.0,
            g = 0.42 + (i % 4) * 0.12,
            b = 0.08
        })
    }
}

function spawnFloater(x, y, text, r, g, b) {
    floaters.push({ x = x, y = y, text = text, life = 1.1, r = r, g = g, b = b })
}

function kickHammer() {
    hammerActive = true
    hammerPhase = 0.0
}

function tryCraft() {
    if (bag.countItem("ore.iron") < 3 || bag.countItem("coal") < 1) {
        pushToast("Need 3 ore and 1 coal")
        return
    }
    local recipe = @"{
      ""id"":""forge.ember_blade"",
      ""process"":""craft"",
      ""ingredients"":{""ore.iron"":3,""coal"":1},
      ""outputs"":[{""item"":""sword.ember"",""quantity"":1}],
      ""duration"":1.2
    }"
    local begun = forge.craftFromJson(recipe, 1, 10)
    if (!begun.ok) {
        pushToast("Craft refused: " + begun.status.summary)
        return
    }
    heat = 1.0
    kickHammer()
    spawnSparks(480.0, 360.0, 14)
    pushToast("Forging Ember Blade…")
}

function tryUpgrade() {
    if (!gear.isSlotEmpty("weapon")) {
        pushToast("Unequip the blade before enhancing")
        return
    }
    local slot = bladeSlot()
    if (slot < 0) {
        pushToast("No blade in the bag — craft one first")
        return
    }
    local level = bladeEnhance()
    if (level >= MAX_ENHANCE) {
        pushToast("Blade already at +" + level)
        return
    }
    local cost = level + 1
    if (bag.countItem("emberstone") < cost) {
        pushToast("Need " + cost + " emberstones for +" + (level + 1))
        return
    }
    local nextLevel = level + 1
    local duration = 0.7 + level * 0.15
    local recipe = "{\"id\":\"enhance.ember." + level +
        "\",\"process\":\"upgrade\",\"itemId\":\"sword.ember\",\"fromLevel\":" + level +
        ",\"toLevel\":" + nextLevel + ",\"ingredients\":{\"emberstone\":" + cost +
        "},\"duration\":" + duration + "}"
    local begun = forge.upgradeFromJson(recipe, slot, bag.getSlotInstanceId(slot), 20)
    if (!begun.ok) {
        pushToast("Enhance refused: " + begun.status.summary)
        return
    }
    heat = 1.0
    kickHammer()
    spawnSparks(480.0, 340.0, 18)
    pushToast("Quenching toward +" + (level + 1) + "…")
}

function tryEquip() {
    if (!gear.isSlotEmpty("weapon")) {
        if (!gear.unequipToBag("weapon", bag)) {
            pushToast("Cannot unequip — bag full?")
            return
        }
        forge.syncEquipment()
        pushToast("Blade returned to the bag")
        return
    }
    local slot = bladeSlot()
    if (slot < 0) {
        pushToast("No blade to equip")
        return
    }
    if (!gear.equipFromBag("weapon", bag, slot)) {
        pushToast("Equip failed")
        return
    }
    forge.syncEquipment()
    local atk = hero.getFinalAttribute("attack")
    pushToast("Equipped — attack " + atk)
}

function tryStrike() {
    if (gear.isSlotEmpty("weapon")) {
        pushToast("Equip the blade before striking")
        return
    }
    local atk = hero.getFinalAttribute("attack")
    local crit = hero.getFinalAttribute("crit")
    local isCrit = (simTick % 7) < (crit.tointeger() / 8)
    local dmg = isCrit ? (atk * 1.6) : atk
    dummyHit = 1.0
    dummyHp -= dmg
    if (dummyHp < 0.0) dummyHp = 0.0
    heat = heat < 0.55 ? 0.55 : heat
    spawnSparks(760.0, 330.0, isCrit ? 22 : 12)
    spawnFloater(730.0, 280.0, (isCrit ? "CRIT " : "") + dmg.tointeger().tostring(),
        isCrit ? 1.0 : 0.95, isCrit ? 0.55 : 0.78, isCrit ? 0.2 : 0.45)
    if (dummyHp <= 0.0) {
        pushToast("Training post shattered — R restocks materials, F again resets the post")
        dummyHp = 100.0
    }
}

function pollKeys() {
    local craft = keyboard.isDown("c") || keyboard.isDown("C")
    local upgrade = keyboard.isDown("u") || keyboard.isDown("U")
    local equip = keyboard.isDown("e") || keyboard.isDown("E")
    local restockKey = keyboard.isDown("r") || keyboard.isDown("R")
    local pauseKey = keyboard.isDown("space")
    local strike = keyboard.isDown("f") || keyboard.isDown("F")

    if (craft && !lastKeyCraft) tryCraft()
    if (upgrade && !lastKeyUpgrade) tryUpgrade()
    if (equip && !lastKeyEquip) tryEquip()
    if (restockKey && !lastKeyRestock) restock()
    if (pauseKey && !lastKeyPause) {
        paused = !paused
        pushToast(paused ? "Forge paused" : "Forge resumed")
    }
    if (strike && !lastKeyStrike) tryStrike()

    lastKeyCraft = craft
    lastKeyUpgrade = upgrade
    lastKeyEquip = equip
    lastKeyRestock = restockKey
    lastKeyPause = pauseKey
    lastKeyStrike = strike
}

function activeProgress() {
    local n = forge.taskCount()
    for (local i = 0; i < n; i += 1) {
        local state = forge.taskStateAt(i)
        if (state == "running" || state == "queued" || state == "ready_to_settle") {
            local dur = forge.taskDurationAt(i)
            local prog = forge.taskProgressAt(i)
            if (dur <= 0.0) return 0.0
            local bar = prog / dur
            if (bar > 1.0) bar = 1.0
            return bar
        }
    }
    return -1.0
}

function stepForge(dt) {
    if (paused) return
    fixedAcc += dt
    while (fixedAcc >= FIXED_DT) {
        fixedAcc -= FIXED_DT
        simTick += 1
        local advanced = forge.advance(simTick, FIXED_DT)
        if (!advanced.ok) throw advanced.status.summary
        local settled = forge.settleReady()
        if (!settled.ok) throw settled.status.summary
        if (settled.value > 0) {
            heat = 1.0
            kickHammer()
            spawnSparks(480.0, 350.0, 24)
            local level = bladeEnhance()
            if (level >= 0)
                pushToast("Anvil done — blade +" + level)
            else
                pushToast("Anvil done — blade ready")
            forge.syncEquipment()
        }
    }
}

function updateFx(dt) {
    ambientT += dt
    local nextSparks = []
    foreach (s in sparks) {
        s.life -= dt
        if (s.life <= 0.0) continue
        s.x += s.vx * dt
        s.y += s.vy * dt
        s.vy += 110.0 * dt
        nextSparks.push(s)
    }
    sparks = nextSparks

    local nextFloat = []
    foreach (f in floaters) {
        f.life -= dt
        if (f.life <= 0.0) continue
        f.y -= 42.0 * dt
        nextFloat.push(f)
    }
    floaters = nextFloat

    if (heat > 0.0) heat = heat - dt * 0.48
    if (heat < 0.0) heat = 0.0
    if (toastLife > 0.0) toastLife -= dt
    if (dummyHit > 0.0) dummyHit -= dt * 2.2
    if (dummyHit < 0.0) dummyHit = 0.0

    if (hammerActive) {
        hammerPhase += dt * 3.4
        if (hammerPhase >= 1.0) {
            hammerPhase = 0.0
            if (activeProgress() < 0.0) hammerActive = false
        }
    } else if (activeProgress() >= 0.0) {
        kickHammer()
    }
}

function idleHeat() {
    // Soft hearth breath so the loft never looks cold between jobs.
    local saw = (ambientT * 1.1) % 2.0
    if (saw > 1.0) saw = 2.0 - saw
    return 0.28 + saw * 0.22 + heat * 0.55
}

function drawBackdrop() {
    gfx.drawSolidRect(0.0, 0.0, W, H, 0.08, 0.05, 0.045, 1.0)
    // Warm upper wash.
    gfx.drawSolidRect(0.0, 0.0, W, 220.0, 0.18, 0.07, 0.03, 0.22)
    // Ceiling soot bands.
    for (local i = 0; i < 6; i += 1) {
        local y = 40.0 + i * 18.0
        gfx.drawSolidRect(0.0, y, W, 10.0, 0.05, 0.03, 0.03, 0.22 + i * 0.02)
    }
    // Floor ash.
    gfx.drawSolidRect(0.0, 430.0, W, 210.0, 0.12, 0.07, 0.05, 1.0)
    for (local i = 0; i < 10; i += 1) {
        local y = 440.0 + i * 18.0
        gfx.drawSolidRect(0.0, y, W, 12.0, 0.22 + heat * 0.2, 0.09, 0.04, 0.08 + i * 0.015)
    }
    // Hearth glow behind the anvil — always alive, hotter after jobs.
    local glow = idleHeat()
    gfx.drawSolidRect(300.0, 220.0, 360.0, 220.0, 0.75, 0.22, 0.04, glow * 0.35)
    gfx.drawSolidRect(360.0, 260.0, 240.0, 150.0, 0.95, 0.38, 0.08, glow * 0.7)
    gfx.drawSolidRect(410.0, 300.0, 140.0, 90.0, 1.0, 0.55, 0.12, glow)
    // Ambient drifting embers.
    for (local i = 0; i < 18; i += 1) {
        local t = ambientT * (0.35 + (i % 4) * 0.08) + i * 1.7
        local x = 80.0 + ((i * 73) % 800) + (t * 18.0) % 40.0
        local y = 470.0 - ((t * 28.0 + i * 17.0) % 260.0)
        local a = 0.25 + ((i + simTick) % 5) * 0.05
        gfx.drawSolidRect(x, y, 4.0, 4.0, 1.0, 0.58, 0.16, a)
    }
}

function drawForgeFurniture() {
    // Back wall plate + twin chimneys.
    gfx.drawSolidRect(300.0, 170.0, 360.0, 34.0, 0.2, 0.14, 0.12, 1.0)
    gfx.drawSolidRect(340.0, 110.0, 90.0, 70.0, 0.16, 0.11, 0.1, 1.0)
    gfx.drawSolidRect(530.0, 110.0, 90.0, 70.0, 0.16, 0.11, 0.1, 1.0)
    gfx.drawSolidRect(360.0, 90.0, 50.0, 24.0, 0.12, 0.09, 0.08, 1.0)
    gfx.drawSolidRect(550.0, 90.0, 50.0, 24.0, 0.12, 0.09, 0.08, 1.0)
    // Bellows silhouette.
    gfx.drawSolidRect(230.0, 350.0, 90.0, 48.0, 0.36, 0.18, 0.1, 1.0)
    gfx.drawSolidRect(245.0, 320.0, 60.0, 34.0, 0.42, 0.2, 0.11, 1.0)
    gfx.drawSolidRect(200.0, 370.0, 40.0, 16.0, 0.28, 0.14, 0.08, 1.0)
}

function drawAnvilAndBlade() {
    // Anvil body.
    gfx.drawSolidRect(395.0, 336.0, 170.0, 34.0, 0.32, 0.32, 0.36, 1.0)
    gfx.drawSolidRect(425.0, 368.0, 110.0, 54.0, 0.24, 0.24, 0.28, 1.0)
    gfx.drawSolidRect(440.0, 420.0, 80.0, 20.0, 0.18, 0.18, 0.2, 1.0)
    gfx.drawSolidRect(382.0, 342.0, 22.0, 18.0, 0.3, 0.3, 0.34, 1.0)

    local level = bladeEnhance()
    local equipped = !gear.isSlotEmpty("weapon")
    local hasBlade = level >= 0 || equipped
    if (hasBlade) {
        local enhance = level < 0 ? 0 : level
        local bladeLen = 70.0 + enhance * 10.0
        local glowR = 0.55 + enhance * 0.08
        local glowG = 0.18 + heat * 0.25
        // Hot blade on the anvil (bag) or raised when equipped.
        local bx = 440.0
        local by = equipped ? 300.0 : 328.0
        gfx.drawSolidRect(bx, by, bladeLen, 8.0, glowR, 0.55 + heat * 0.3, glowG, 1.0)
        gfx.drawSolidRect(bx + bladeLen - 6.0, by - 2.0, 14.0, 12.0, 0.85, 0.75, 0.55, 1.0)
        gfx.drawSolidRect(bx - 14.0, by - 4.0, 16.0, 16.0, 0.25, 0.18, 0.12, 1.0)
        if (enhance > 0) {
            for (local i = 0; i < enhance; i += 1) {
                gfx.drawSolidRect(bx + 12.0 + i * 12.0, by - 10.0, 6.0, 4.0, 1.0, 0.7, 0.25, 0.85)
            }
        }
    }

    local bar = activeProgress()
    if (bar < 0.0) bar = 0.0
    gfx.drawSolidRect(410.0, 318.0, 140.0, 10.0, 0.12, 0.10, 0.10, 1.0)
    gfx.drawSolidRect(410.0, 318.0, 140.0 * bar, 10.0, 0.95, 0.45 + heat * 0.3, 0.12, 1.0)
}

function drawHammer() {
    local swing = 0.0
    if (hammerActive) {
        // Ease: raise then strike.
        if (hammerPhase < 0.45)
            swing = hammerPhase / 0.45
        else
            swing = 1.0 - (hammerPhase - 0.45) / 0.55
    }
    local hx = 560.0 + swing * 10.0
    local hy = 250.0 - swing * 70.0
    gfx.drawSolidRect(hx, hy, 14.0, 90.0 - swing * 20.0, 0.28, 0.18, 0.12, 1.0)
    gfx.drawSolidRect(hx - 18.0, hy - 8.0, 50.0, 22.0, 0.35, 0.35, 0.38, 1.0)
}

function drawDummy() {
    local shake = dummyHit * 8.0
    local dx = 740.0 + shake * (((simTick % 2) == 0) ? 1.0 : -1.0)
    local dy = 270.0
    gfx.drawSolidRect(dx + 14.0, dy + 130.0, 48.0, 80.0, 0.28, 0.16, 0.09, 1.0)
    gfx.drawSolidRect(dx - 4.0, dy, 84.0, 132.0, 0.52 + dummyHit * 0.35, 0.32, 0.18, 1.0)
    gfx.drawSolidRect(dx + 10.0, dy + 10.0, 64.0, 18.0, 0.62, 0.4, 0.22, 1.0)
    gfx.drawSolidRect(dx + 24.0, dy + 36.0, 36.0, 22.0, 0.14, 0.1, 0.08, 1.0)
    // HP bar.
    local ratio = dummyHp / 100.0
    if (ratio < 0.0) ratio = 0.0
    gfx.drawSolidRect(dx - 4.0, dy - 20.0, 84.0, 10.0, 0.14, 0.1, 0.1, 1.0)
    gfx.drawSolidRect(dx - 4.0, dy - 20.0, 84.0 * ratio, 10.0, 0.85, 0.3, 0.16, 1.0)
}

function drawSparks() {
    foreach (s in sparks) {
        local a = s.life / 0.65
        if (a > 1.0) a = 1.0
        gfx.drawSolidRect(s.x, s.y, 3.0, 3.0, s.r, s.g, s.b, a)
    }
}

function drawFloaters() {
    if (hudFont == null) return
    foreach (f in floaters) {
        local a = f.life
        if (a > 1.0) a = 1.0
        gfx.drawText(hudFont, f.text, f.x, f.y, f.r, f.g, f.b, a, 1.0)
    }
}

function drawHud() {
    if (hudFont == null) return
    local atk = hero.getFinalAttribute("attack")
    local crit = hero.getFinalAttribute("crit")
    local enhance = bladeEnhance()
    local enhanceText = enhance < 0 ? "—" : ("+" + enhance)
    local equipped = gear.isSlotEmpty("weapon") ? "bag" : "equipped"
    local lines = [
        "EMBERFORGE",
        "ore " + bag.countItem("ore.iron") + "   coal " + bag.countItem("coal") +
            "   ember " + bag.countItem("emberstone"),
        "blade " + enhanceText + " (" + equipped + ")   attack " + atk + "   crit " + crit,
        "C craft   U enhance   E equip   F strike   R restock   Space pause",
    ]
    if (paused) lines.push("PAUSED")
    if (toastLife > 0.0) lines.push(toast)
    local y = 28.0
    foreach (line in lines) {
        local scale = (y < 40.0) ? 1.15 : 1.0
        gfx.drawText(hudFont, line, 36.0, y, 0.96, 0.82, 0.62, 1.0, scale)
        y += (y < 50.0) ? 30.0 : 26.0
    }
}

eve_init <- function() {
    ensureSetup()
}

eve_update <- function(dt) {
    ensureSetup()
    pollKeys()
    stepForge(dt)
    updateFx(dt)
}

eve_render <- function() {
    ensureSetup()
    gfx.clear()
    drawBackdrop()
    drawForgeFurniture()
    drawAnvilAndBlade()
    drawHammer()
    drawDummy()
    drawSparks()
    drawFloaters()
    drawHud()
}
