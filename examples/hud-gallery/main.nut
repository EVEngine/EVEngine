// Presentation reads hp/cooldown; the UI never owns gameplay values.
persist hudState = { hp = 0.78, cooldown = 0.0 }
local displayedHp = hudState.hp
persist iconTexture = null
persist iconId = 0
local scale = 1.0
local safeMargin = 24.0
local resolution = 0
local hudWindow = eve.Window()
local previousKeys = {}
local previousMouse = false

function pressed(key) {
    local down = keyboard.isDown(key)
    local before = key in previousKeys ? previousKeys[key] : false
    previousKeys[key] <- down
    return down && !before
}

local notice = "Click the scene to damage; Q or skill button to cast; R to heal"

function configureHost(name, ax, ay, x, y, width, height, displayOnly) {
    ui.select(name)
    ui.setHostOverlay(true)
    ui.setHostOverlayAlpha(0.0)
    ui.setHostInputTransparent(displayOnly)
    ui.setHostHudCanvas(1920.0, 1080.0, scale)
    ui.setHostSafeArea(safeMargin, safeMargin, safeMargin, safeMargin)
    ui.setHostAnchor(ax, ay)
    ui.setHostPos(x, y, ax, ay)
    ui.setHostSize(width, height)
}

function buildHud() {
    ui.beginBuild()
    ui.beginWindow("Resources", "root")
    ui.text("VANGUARD / LEVEL 12", "title")
    ui.resourceBar("hp", 360.0, 28.0, hudState.hp)
    ui.text("Click the scene to take damage", "help")
    ui.end()
    if (!ui.mountBuildAs("resources")) throw "Could not mount resources"
    configureHost("resources", 0.0, 0.0, 16.0, 16.0, 390.0, 120.0, true)
    ui.setImageTextureId("hp/value", iconId)
    ui.setImageTint("hp/value", 0.28, 0.92, 0.68, 1.0)
    ui.setImageTint("hp/trail", 1.0, 0.58, 0.20, 1.0)

    ui.beginBuild()
    ui.beginWindow("Skills", "root")
    ui.beginRow("slots", 12.0)
    ui.skillSlot("pulse", 88.0, iconId, hudState.cooldown / 4.0, "Q")
    ui.skillSlot("dash", 88.0, iconId, 0.0, "E")
    ui.end()
    ui.text("", "cooldown-label")
    ui.end()
    if (!ui.mountBuildAs("skills")) throw "Could not mount skills"
    configureHost("skills", 0.5, 1.0, 0.0, -16.0, 188.0, 130.0, false)

    ui.beginBuild()
    ui.beginWindow("Quest", "root")
    ui.text("OBJECTIVE", "title")
    ui.text("Secure the relay station", "objective")
    ui.text("F1: scale | F2: safe area | V: resolution", "controls")
    ui.end()
    if (!ui.mountBuildAs("quest")) throw "Could not mount quest"
    configureHost("quest", 1.0, 0.0, -16.0, 16.0, 410.0, 120.0, true)

    ui.beginBuild()
    ui.beginWindow("Notifications", "root")
    ui.text(notice, "notice")
    ui.end()
    if (!ui.mountBuildAs("notifications")) throw "Could not mount notification"
    configureHost("notifications", 0.5, 0.0, 0.0, 150.0, 680.0, 60.0, true)
}

function cast() {
    if (hudState.cooldown > 0.0) return
    hudState.cooldown = 4.0
    notice = "PULSE / cooldown started"
}

function configureAll() {
    foreach (name in ["resources", "skills", "quest", "notifications"]) {
        ui.select(name)
        ui.setHostHudCanvas(1920.0, 1080.0, scale)
        ui.setHostSafeArea(safeMargin, safeMargin, safeMargin, safeMargin)
    }
}

eve_init <- function() {
    local image = eve.Image()
    local pixels = image.newEmptyImageData(32, 32, "RGBA8")
    for (local y = 0; y < 32; ++y)
        for (local x = 0; x < 32; ++x)
            pixels.setPixel(x, y, 0.2 + x / 64.0, 0.4 + y / 64.0, 0.9, 1.0)
    if (iconTexture == null) iconTexture = gfx.newTexture(pixels, false, false)
    else gfx.updateTextureFromImageData(iconTexture, pixels)
    if (iconTexture == null) throw "Could not create HUD texture"
    if (iconId != 0) ui.unregisterTexture(iconId)
    iconId = ui.registerTexture(iconTexture)
    if (iconId == 0) throw "Could not register HUD texture"
    gfx.setBackgroundColor(0.025, 0.04, 0.075, 1.0)
    buildHud()
}

eve_update <- function(dt) {
    local click = ui.consumeClick()
    while (click != "") {
        if (click == "skills/pulse/activate") cast()
        if (click == "skills/dash/activate") notice = "DASH / input received"
        click = ui.consumeClick()
    }
    if (!ui.wantCaptureKeyboard()) {
        if (pressed("Q")) cast()
        if (pressed("R")) hudState.hp = 1.0
        if (pressed("F1")) { scale = scale > 1.0 ? 1.0 : 1.25; configureAll() }
        if (pressed("F2")) { safeMargin = safeMargin > 24.0 ? 24.0 : 90.0; configureAll() }
        if (pressed("V")) {
            resolution = (resolution + 1) % 3
            if (resolution == 0) hudWindow.setSize(1280, 720)
            else if (resolution == 1) hudWindow.setSize(1280, 800)
            else hudWindow.setSize(1680, 720)
            // Window recreation shuts down the UI backend; registrations belong to that backend lifetime.
            eve_init()
        }
    }
    local mouseDown = mouse.isDown(1)
    if (!ui.wantCaptureMouse() && mouseDown && !previousMouse) {
        hudState.hp = hudState.hp > 0.15 ? hudState.hp - 0.15 : 0.0
        notice = "HIT / decorative HUD allows scene input"
    }
    previousMouse = mouseDown
    hudState.cooldown = hudState.cooldown > dt ? hudState.cooldown - dt : 0.0
    ui.select("resources")
    ui.setValue("hp/value", hudState.hp)
    if (displayedHp != hudState.hp) {
        ui.animateItemValue("hp/trail", hudState.hp, hudState.hp < displayedHp ? 450.0 : 0.0,
                            "outQuad", hudState.hp < displayedHp ? 350.0 : 0.0)
        displayedHp = hudState.hp
    }
    ui.setText("hp/label", "HP " + (hudState.hp * 100.0).tointeger() + " / 100")
    ui.select("skills")
    ui.setValue("pulse/cooldown", hudState.cooldown / 4.0)
    ui.setEnabled("pulse/activate", hudState.cooldown <= 0.0)
    ui.setText("cooldown-label", hudState.cooldown > 0.0 ? "PULSE / recharging" : "PULSE / ready")
    ui.select("notifications")
    ui.setText("notice", notice)
}

eve_render <- function() {
    gfx.clear()
    // Sparse scene geometry makes the unblocked play area visible.
    gfx.drawSolidRect(0.0, 460.0, 1600.0, 3.0, 0.12, 0.22, 0.30, 1.0)
    gfx.drawSolidRect(590.0, 280.0, 100.0, 100.0, 0.12, 0.28, 0.34, 1.0)
    ui.beginFrameAndRender()
}

eve_reload <- function() { eve_init() }
eve_quit = function() {
    if (iconId != 0) ui.unregisterTexture(iconId)
    iconId = 0
    iconTexture = null
}
