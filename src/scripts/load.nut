function file_exists(path) {
    try {
        file(path, "r").close();
        return true;
    } catch (e) {
        return false;
    }
}

function path_endswith(str, suffix) {
    if (str == null || suffix == null) return false;
    if (str.len() < suffix.len()) return false;
    return str.slice(str.len() - suffix.len()) == suffix;
}

function normalize_path(path) {
    if (path == null) return "";
    local s = path;
    while (s.len() >= 2 && s.slice(0, 2) == "./")
        s = s.slice(2);
    return s;
}

config <- {
    width = 800
    height = 600
    title = "EVEngine"
    debug = false
    hotReload = false
};

if (file_exists("config.nut")) {
    dofile("config.nut");
}
if (!("hotReload" in config))
    config.hotReload <- true;

win <- eve.Window();
gfx <- eve.Graphics();
win.setGraphics(gfx);

local s = eve.WindowSettings();
s.width = config.width;
s.height = config.height;
s.centered = true;
if (!win.setWindowSettings(s)) {
    print("setWindowSettings failed\n");
    return;
}

// Keep script layout in sync with the real window (mobile may ignore 800x600).
config.width = win.getWidth();
config.height = win.getHeight();

event <- eve.Event();
timer <- eve.Timer();
system <- eve.System();
math <- eve.Math();
spatial <- eve.Spatial();
editor <- eve.Editor();
tf <- eve.TF();
ui <- eve.UI();
scene <- eve.Scene();
camera <- eve.Camera();
particles <- eve.Particles();
map <- eve.Map();
procgen <- eve.Procgen();
avatar <- eve.Avatar();
dialogue <- eve.Dialogue();
anim <- eve.Animation();
stylize <- eve.Stylize();
spritestack <- eve.SpriteStack();
gpgpu <- eve.Gpgpu();
daynight <- eve.DayNight();
weather <- eve.Weather();
physics <- eve.Physics();
keyboard <- eve.Keyboard();
mouse <- eve.Mouse();
touch <- eve.Touch();
sound <- eve.Sound();
audio <- eve.Audio();
model3d <- eve.Model3D();
font <- eve.Font();
thread <- eve.Thread();
fs <- eve.Filesystem();
hot <- eve.HotReload();
i18n <- eve.I18n();

// Node-style async (Promise / nextTick / setTimeout). Embedded via eve.asyncScript.
if ("asyncScript" in eve && eve.asyncScript != null && eve.asyncScript != "") {
    try {
        compilestring(eve.asyncScript)();
    } catch (e) {
        // Report FIRST: with "break on error" the debugger pauses at the
        // throwing script line / catch site before stdout is flushed.
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("async runtime failed to load: " + e + "\n");
    }
} else if (file_exists("async.nut")) {
    try {
        dofile("async.nut");
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("async.nut failed to load: " + e + "\n");
    }
}

eve_init <- function() {};
eve_update <- function(dt) {};
eve_render <- function() {
    gfx.clear();
};
eve_quit <- function() {};

// Soft hot-reload bookkeeping (disk main.nut only; embedded demo is build-time).
watched_scripts <- [];

function track_script(path) {
    local p = normalize_path(path);
    if (p == "" || !path_endswith(p, ".nut")) return;
    foreach (existing in watched_scripts) {
        if (existing == p) return;
    }
    watched_scripts.append(p);
}

function soft_reload_scripts() {
    foreach (p in watched_scripts) {
        if (!file_exists(p)) continue;
        try {
            dofile(p);
            print("hot-reload script: " + p + "\n");
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("hot-reload script failed: " + p + ": " + e + "\n");
        }
    }
    if ("eve_reload" in getroottable()) {
        try {
            eve_reload();
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("eve_reload failed: " + e + "\n");
        }
    }
}

function poll_hot_reload() {
    if (!config.hotReload) return;
    local needScripts = false;
    local assets = [];
    while (true) {
        local kind = fs.pollWatch();
        if (kind == "") break;
        if (kind != "modified" && kind != "added" && kind != "movedTo") continue;
        local p = normalize_path(fs.getLastWatchPath());
        if (p == "") continue;
        if (path_endswith(p, ".nut")) {
            track_script(p);
            needScripts = true;
        } else {
            assets.append(p);
        }
    }
    if (needScripts)
        soft_reload_scripts();
    foreach (p in assets) {
        try {
            hot.tryReload(p);
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("hot-reload asset failed: " + p + ": " + e + "\n");
        }
        if ("eve_asset_reload" in getroottable()) {
            try {
                eve_asset_reload(p);
            } catch (e) {
                if ("dev" in eve) eve.dev.reportError("" + e);
                print("eve_asset_reload failed: " + p + ": " + e + "\n");
            }
        }
    }
}

if (file_exists("main.nut")) {
    dofile("main.nut");
    track_script("main.nut");
} else if ("demoScript" in eve && eve.demoScript != null && eve.demoScript != "") {
    // Prefer try/catch over `in` — class slot checks differ across SSQ builds.
    try {
        compilestring(eve.demoScript)();
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("Embedded demo failed to load: " + e + "\n");
    }
}

if (config.hotReload) {
    try {
        // Ensure VFS source is set when Run did not mount (e.g. custom root).
        try { fs.setSource("."); } catch (e) {}
        local n = hot.watchTree(".");
        // Explicit file watch as a second registration (same OS dir, basename filter).
        if (file_exists("main.nut"))
            fs.watch("main.nut");
        print("hot-reload: watching " + n + " path(s)\n");
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("hot-reload watchTree failed: " + e + "\n");
    }
}

try {
    eve_init();
} catch (e) {
    if ("dev" in eve) eve.dev.reportError("" + e);
    print("eve_init failed: " + e + "\n");
}

// DevTools helpers (only present when `eve run --debug`).
function has_dev() {
    return ("dev" in eve);
}

function dev_poll() {
    if (has_dev())
        eve.dev.poll();
}

function dev_should_update() {
    if (!has_dev()) return true;
    return eve.dev.shouldRunUpdate();
}

function dev_notify_frame_done() {
    if (has_dev())
        eve.dev.notifyFrameDone();
}

function handle_dev_key(key, scancode) {
    if (!has_dev()) return;
    // Pause key (keyboard Pause/Break) toggles frame-level pause.
    if (key == "Pause" || scancode == "Pause") {
        eve.dev.togglePause();
        print(eve.dev.isPaused() ? "dev: paused\n" : "dev: resumed\n");
        return;
    }
    // F5 = continue to next breakpoint (continueRun; `resume` is a Squirrel keyword).
    if (key == "F5") {
        if (eve.dev.isPaused()) {
            eve.dev.continueRun();
            print("dev: continue\n");
        }
        return;
    }
    // F10 = step over (next statement, skip call bodies).
    if (key == "F10" && eve.dev.isPaused()) {
        eve.dev.stepOver();
        print("dev: step over\n");
        return;
    }
    // F11 = step into (next statement, enter calls).
    if (key == "F11" && eve.dev.isPaused()) {
        eve.dev.stepInto();
        print("dev: step into\n");
        return;
    }
    // F8 = step one game frame (secondary; statement stepping is primary).
    if (key == "F8" && eve.dev.isPaused()) {
        eve.dev.stepFrame();
        print("dev: step frame\n");
        return;
    }
    // F6 / F7 = save / load script-state snapshot.
    if (key == "F6") {
        local r = eve.dev.saveSnapshot("eve_snapshot.json");
        print("dev: saveSnapshot -> " + r + "\n");
        return;
    }
    if (key == "F7") {
        local r = eve.dev.loadSnapshot("eve_snapshot.json");
        print("dev: loadSnapshot -> " + r + "\n");
        return;
    }
    // F9 = toggle DevTools AI / MCP panel.
    if (key == "F9") {
        eve.dev.ai.toggleVisible();
        print("dev: AI panel " + (eve.dev.ai.isVisible() ? "shown" : "hidden") + "\n");
        return;
    }
    // F4 = toggle DevTools runtime console / log / REPL panel.
    if (key == "F4") {
        eve.dev.console.toggleVisible();
        print("dev: console " + (eve.dev.console.isVisible() ? "shown" : "hidden") + "\n");
    }
}

function dev_draw_ai() {
    if (has_dev())
        eve.dev.ai.draw();
}

function dev_draw_console() {
    if (has_dev())
        eve.dev.console.draw();
}

// On Android, SDL may queue a spurious "quit" while setOrientation recreates
// the surface. Ignore quit for a few frames so a slow eve_init (demo) does not
// instantly exit. Real back-button quits still work after startup settles.
local startupFrames = 45;
local running = true;
while (running) {
    event.pump();
    while (true) {
        local name = event.poll();
        if (name == "") break;
        local data = event.getLastData();
        if ("async_dispatch_event" in getroottable())
            async_dispatch_event(name, data);
        if (name == "keypressed") {
            // getLastData() is the key name (first string arg of the Message).
            handle_dev_key(data, data);
        }
        if (name == "quit") {
            if (startupFrames <= 0)
                running = false;
        }
    }
    if (startupFrames > 0)
        startupFrames -= 1;

    // Rotation / foldable: keep gameplay bounds aligned with the graphics viewport.
    config.width = win.getWidth();
    config.height = win.getHeight();

    poll_hot_reload();
    dev_poll();

    local dt = timer.step();
    try {
        // Timers + Promise microtasks before game logic (Node-like macrotask boundary).
        if ("async_pump" in getroottable())
            async_pump();
        if (dev_should_update()) {
            eve_update(dt);
            // Flush reactions scheduled during eve_update.
            if ("async_pump" in getroottable())
                async_pump();
            dev_notify_frame_done();
        }
        eve_render();
        // ImGui AI/MCP panel (requires ui.beginFrameAndRender in eve_render).
        dev_draw_ai();
        dev_draw_console();
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("frame error: " + e + "\n");
    }
    // Always present: eve_render may have opened a 3D pass (gfx.render3D)
    // before throwing. Skipping present leaves swapchainPassOpen and every
    // later frame fails with "begin3DFrame: swapchain pass already open".
    try {
        gfx.present();
        ui.dispatchEvents();
    } catch (e) {
        print("present error: " + e + "\n");
    }
}

eve_quit();
win.close();
