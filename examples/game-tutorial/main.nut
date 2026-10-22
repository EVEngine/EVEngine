import { new_state, target_at, update_state, encode_save, decode_save } from "./scripts/rules.nut"

persist tutorial_state = new_state()
persist hero_texture = null
local image = eve.Image()
local save_ready = false
local notice = "Arrows: move; R: restart; S: save; L: load"

function build_view() {
    local pixels = image.newEmptyImageData(8, 8, "RGBA8")
    for (local y = 0; y < 8; ++y)
        for (local x = 0; x < 8; ++x)
            pixels.setPixel(x, y, 0.2, (x + y) % 2 == 0 ? 0.7 : 0.9, 1.0, 1.0)
    if (hero_texture == null) hero_texture = gfx.newTexture(pixels, false, false)
    else gfx.updateTextureFromImageData(hero_texture, pixels)
    ui.beginBuild()
    ui.beginWindow("Collect Squares", "root")
    ui.text("Score: " + tutorial_state.score, "score")
    ui.text(notice, "status")
    ui.button("Restart", "restart")
    ui.button("Save", "save")
    ui.button("Load", "load")
    ui.end()
    ui.mountBuildAs("tutorial")
    ui.setHostPos(940.0, 20.0, 1.0, 0.0)
    ui.setHostSize(500.0, 300.0)
    ui.setHostMovable(false)
    ui.setHostResizable(false)
}

function restart_game() {
    tutorial_state = new_state()
    notice = "Restarted"
}

function save_game() {
    if (!save_ready) { notice = "Save directory unavailable"; return }
    local text = encode_save(tutorial_state)
    decode_save(text) // validate our candidate before committing
    local previous = fs.readText("tutorial-slot.txt")
    if (previous != "") {
        local valid = false
        try { decode_save(previous); valid = true } catch (error) { valid = false }
        if (valid && fs.writeTextAtomic("tutorial-slot.backup.txt", previous) == 0) {
            notice = "Backup write failed; save cancelled"
            return
        }
    }
    notice = fs.writeTextAtomic("tutorial-slot.txt", text) != 0 ? "Saved" : "Save write failed"
}

function load_game() {
    if (!save_ready) { notice = "Save directory unavailable"; return }
    foreach (path in ["tutorial-slot.txt", "tutorial-slot.backup.txt"]) {
        try {
            local text = fs.readText(path)
            if (text == "") continue
            local candidate = decode_save(text)
            tutorial_state = candidate // commit only after complete validation
            notice = "Loaded " + path
            return
        } catch (error) { notice = "Load rejected: " + error }
    }
    notice = "No valid save; current game retained"
}

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0)
    save_ready = fs.setIdentity("evescript-game-tutorial", false) && fs.setupWriteDirectory()
    build_view()
}

eve_update = function(dt) {
    local dx = 0.0, dy = 0.0
    if (keyboard.isDown("Left")) dx -= 1.0
    if (keyboard.isDown("Right")) dx += 1.0
    if (keyboard.isDown("Up")) dy -= 1.0
    if (keyboard.isDown("Down")) dy += 1.0
    update_state(tutorial_state, dx, dy, dt, config.width, config.height)
    if (key_just_pressed("R")) restart_game()
    if (key_just_pressed("S")) save_game()
    if (key_just_pressed("L")) load_game()
    local click = ui.consumeClick()
    while (click != "") {
        if (click == "tutorial/restart") restart_game()
        else if (click == "tutorial/save") save_game()
        else if (click == "tutorial/load") load_game()
        click = ui.consumeClick()
    }
    ui.setText("score", "Score: " + tutorial_state.score)
    ui.setText("status", notice)
}

eve_render = function() {
    gfx.clear()
    local target = target_at(tutorial_state.score)
    gfx.drawTexturedRect(hero_texture, tutorial_state.x, tutorial_state.y, 32.0, 32.0, 1.0, 1.0, 1.0, 1.0)
    gfx.drawSolidRect(target.x, target.y, 24.0, 24.0, 1.0, 0.8, 0.2, 1.0)
    ui.beginFrameAndRender()
}

eve_reload <- function() {
    if (!("schema" in tutorial_state) || tutorial_state.schema != "tutorial.collect" || tutorial_state.version != 1)
        throw "tutorial state migration required"
    save_ready = fs.setIdentity("evescript-game-tutorial", false) && fs.setupWriteDirectory()
    build_view()
}

eve_quit = function() { hero_texture = null }
