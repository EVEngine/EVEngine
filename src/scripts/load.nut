function file_exists(path) {
    try {
        file(path, "r").close();
        return true;
    } catch (e) {
        return false;
    }
}

config <- {
    width = 800
    height = 600
    title = "EVEngine"
    debug = false
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

// Keep script layout in sync with the real window (mobile may ignore 800x600).
config.width = win.getWidth();
config.height = win.getHeight();

event <- eve.Event();
timer <- eve.Timer();
ui <- eve.UI();
particles <- eve.Particles();
keyboard <- eve.Keyboard();
mouse <- eve.Mouse();
touch <- eve.Touch();
sound <- eve.Sound();
audio <- eve.Audio();

eve_init <- function() {};
eve_update <- function(dt) {};
eve_render <- function() {
    gfx.clear();
};
eve_quit <- function() {};

if (file_exists("main.nut")) {
    dofile("main.nut");
} else if ("demoScript" in eve && eve.demoScript != null && eve.demoScript != "") {
    // Prefer try/catch over `in` — class slot checks differ across SSQ builds.
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

    local dt = timer.step();
    try {
        eve_update(dt);
        eve_render();
        gfx.present();
        ui.dispatchEvents();
    } catch (e) {
        print("frame error: " + e + "\n");
    }
}

eve_quit();
win.close();
