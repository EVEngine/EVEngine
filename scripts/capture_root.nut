// Auto-capture root script for EVEngine examples.
//
// Mirrors the engine's standard src/scripts/load.nut startup sequence (bind
// modules, create window, load config.nut + main.nut, call eve_init) and then
// runs the frame loop. After `capture_frame` frames it saves the actual
// presented frame via gfx.saveFramePng(capture_path) and exits — the PNG is
// written by the engine from the swapchain readback, so it is exactly what the
// engine rendered.
//
// The game directory may contain an optional `capture_settings.nut`:
//     capture_path  <- "C:/path/out.png";   // absolute or game-relative
//     capture_frame <- 180;                  // frame index to capture at
//     capture_tries <- 15;                   // max extra frames to retry
// Without it the defaults below apply (capture.png after 180 frames).

function file_exists(path) {
    try {
        file(path, "r").close();
        return true;
    } catch (e) {
        return false;
    }
}

function has_module(slot) {
    return slot in getroottable() && getroottable()[slot] != null;
}

config <- {
    width = 800
    height = 600
    title = "EVEngine"
    debug = false
    hotReload = false
};

if (file_exists("config.nut"))
    dofile("config.nut");

// Bind the modules this build actually contains.
foreach (m in eve.moduleList) {
    if (!(m.cls in eve)) continue;
    try {
        getroottable()[m.slot] <- eve[m.cls]();
    } catch (e) {
        print("module " + m.cls + " failed to initialize: " + e + "\n");
    }
}

if (!has_module("win") || !has_module("gfx")) {
    print("engine build is missing the window or graphics module\n");
    return;
}

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

// Node-style async (Promise / nextTick / setTimeout), same as load.nut.
if ("asyncScript" in eve && eve.asyncScript != null && eve.asyncScript != "") {
    try {
        compilestring(eve.asyncScript)();
    } catch (e) {
        print("async runtime failed to load: " + e + "\n");
    }
} else if (file_exists("async.nut")) {
    try {
        dofile("async.nut");
    } catch (e) {
        print("async.nut failed to load: " + e + "\n");
    }
}

eve_init <- function() {};
eve_update <- function(dt) {
    if (has_module("scene")) scene.updateTransformsAll();
};
eve_render <- function() {
    gfx.clear();
};
eve_quit <- function() {};

// Capture configuration (overridable by capture_settings.nut in the game dir).
capture_path <- "capture.png";
capture_frame <- 180;
capture_tries <- 15;
if (file_exists("capture_settings.nut")) {
    try {
        dofile("capture_settings.nut");
    } catch (e) {
        print("capture_settings.nut failed: " + e + "\n");
    }
}

if (file_exists("main.nut")) {
    try {
        dofile("main.nut");
    } catch (e) {
        print("main.nut failed: " + e + "\n");
        return;
    }
}

try {
    eve_init();
} catch (e) {
    print("eve_init failed: " + e + "\n");
}

startup_frames <- 45;
frame <- 0;
left_to_try <- capture_tries + 1;
captured <- false;

while (true) {
    event.pump();
    local running = true;
    while (true) {
        local name = event.poll();
        if (name == "") break;
        local data = event.getLastData();
        if ("async_dispatch_event" in getroottable())
            async_dispatch_event(name, data);
        if (name == "quit") {
            if (startup_frames <= 0)
                running = false;
        }
    }
    if (startup_frames > 0)
        startup_frames -= 1;
    if (!running)
        break;

    local dt = has_module("timer") ? timer.step() : 0.016;
    try {
        if ("async_pump" in getroottable())
            async_pump();
        eve_update(dt);
        if ("async_pump" in getroottable())
            async_pump();
        eve_render();
    } catch (e) {
        print("frame error: " + e + "\n");
    }

    try {
        gfx.present();
        if (has_module("ui"))
            ui.dispatchEvents();
    } catch (e) {
        print("present error: " + e + "\n");
    }

    frame++;
    if (frame >= capture_frame && !captured && left_to_try > 0) {
        left_to_try -= 1;
        if (gfx.saveFramePng(capture_path)) {
            print("CAPTURED " + capture_path + "\n");
            captured = true;
            break;
        }
        // First call only enables readback; the next presented frame lands in
        // the buffer, so keep retrying on following frames.
    }
}

try {
    eve_quit();
} catch (e) {
}
win.close();
if (!captured)
    print("CAPTURE_FAILED\n");
