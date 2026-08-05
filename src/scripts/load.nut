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

event <- eve.Event();
timer <- eve.Timer();

demo <- { x = 40.0, vx = 80.0 };

eve_init <- function() {};
eve_update <- function(dt) {
    demo.x += demo.vx * dt;
    if (demo.x < 0.0 || demo.x + 120.0 > config.width)
        demo.vx = -demo.vx;
};
eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(demo.x, 40.0, 120.0, 80.0, 1.0, 0.4, 0.2, 1.0);
};
eve_quit <- function() {};

if (file_exists("main.nut")) {
    dofile("main.nut");
}

eve_init();

local running = true;
while (running) {
    event.pump();
    while (true) {
        local name = event.poll();
        if (name == "") break;
        if (name == "quit") running = false;
    }

    local dt = timer.step();
    eve_update(dt);
    eve_render();
    gfx.present();
}

eve_quit();
win.close();
