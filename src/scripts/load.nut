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
ui <- eve.UI();
particles <- eve.Particles();

demo <- { x = 40.0, vx = 80.0 };
fx <- null;

eve_init <- function() {
    // Named ECS hosts: hud + menu; select before props/clicks.
    ui.beginBuild();
    ui.beginWindow("HUD", "root");
    ui.text("HP 100", "hp");
    ui.button("Pause", "pause");
    ui.end();
    ui.mountBuildAs("hud");

    ui.beginBuild();
    ui.beginWindow("Menu", "root");
    ui.button("Resume", "resume");
    ui.end();
    ui.mountBuildAs("menu");
    ui.setHostVisible(false); // menu starts hidden (selected=menu)

    ui.select("hud");

    fx = particles.newEmitter(200);
    fx.applyPreset("spark");
    fx.setPosition(config.width * 0.5, config.height * 0.5);
    fx.start();
};
eve_update <- function(dt) {
    demo.x += demo.vx * dt;
    if (demo.x < 0.0 || demo.x + 120.0 > config.width)
        demo.vx = -demo.vx;

    particles.update(dt);

    local id = ui.consumeClick();
    while (id != "") {
        if (id == "hud/pause") {
            ui.select("hud");
            ui.setHostVisible(false);
            ui.select("menu");
            ui.setHostVisible(true);
        } else if (id == "menu/resume") {
            ui.select("menu");
            ui.setHostVisible(false);
            ui.select("hud");
            ui.setHostVisible(true);
        }
        id = ui.consumeClick();
    }
};
eve_render <- function() {
    gfx.clear();
    gfx.drawSolidRect(demo.x, 40.0, 120.0, 80.0, 1.0, 0.4, 0.2, 1.0);
    particles.render(gfx);
    ui.beginFrameAndRender();
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
    ui.dispatchEvents();
}

eve_quit();
win.close();
