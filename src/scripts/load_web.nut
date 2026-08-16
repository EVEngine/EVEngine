// WebGPU (browser/WASM) root script — trimmed module set.
// The Emscripten build compiles out the modules whose third-party deps (Poco,
// OpenAL, assimp/medialoader, freetype) were dropped, so this root only wires
// the modules that exist in the WASM runtime.

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
    title = "EVEngine WebGPU"
    debug = false
    hotReload = false
};

if (file_exists("config.nut")) {
    dofile("config.nut");
}

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

config.width = win.getWidth();
config.height = win.getHeight();

event <- eve.Event();
timer <- eve.Timer();
system <- eve.System();
math <- eve.Math();
spatial <- eve.Spatial();
editor <- eve.Editor();
ui <- eve.UI();
scene <- eve.Scene();
camera <- eve.Camera();
physics <- eve.Physics();
keyboard <- eve.Keyboard();
mouse <- eve.Mouse();
touch <- eve.Touch();
gpgpu <- eve.Gpgpu();
thread <- eve.Thread();
fs <- eve.Filesystem();

eve_init <- function() {};
eve_update <- function(dt) {};
eve_render <- function() {
    gfx.clear();
};
eve_quit <- function() {};

if (file_exists("main.nut")) {
    dofile("main.nut");
} else if ("demoScript" in eve && eve.demoScript != null && eve.demoScript != "") {
    try {
        compilestring(eve.demoScript)();
    } catch (e) {
        print("Embedded demo failed to load: " + e + "\n");
    }
}

try {
    eve_init();
} catch (e) {
    print("eve_init failed: " + e + "\n");
}

// One frame of the game loop, driven from C++ via emscripten_set_main_loop
// (requestAnimationFrame). Returns false to stop the loop (e.g. on quit).
eve_frame <- function() {
    event.pump();
    while (true) {
        local name = event.poll();
        if (name == "") break;
        if (name == "quit") return false;
    }

    config.width = win.getWidth();
    config.height = win.getHeight();

    local dt = timer.step();
    try {
        eve_update(dt);
    } catch (e) {
        print("eve_update error: " + e + "\n");
    }
    try {
        eve_render();
    } catch (e) {
        print("eve_render error: " + e + "\n");
    }
    try {
        ui.dispatchEvents();
    } catch (e) {
        print("ui.dispatchEvents error: " + e + "\n");
    }
    try {
        gfx.present();
    } catch (e) {
        print("present error: " + e + "\n");
    }
    return true;
};
