// example game callbacks — overrides defaults from load.nut
// Frame: particles.update → clear → sprites → particles.render → ui → present

box <- { x = 100.0, y = 100.0, vx = 60.0 };
fx <- null;

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.09, 0.14, 1.0);
    if (particles == null) particles = eve.Particles();
    fx = particles.newEmitterFromFile("particles/fire.json");
    if (fx == null) {
        fx = particles.newEmitter(256);
        fx.applyPreset("fire");
        fx.setPosition(config.width * 0.5, config.height * 0.75);
        fx.start();
    } else {
        fx.setPosition(config.width * 0.5, config.height * 0.75);
    }
};

eve_update = function(dt) {
    box.x = box.x + box.vx * dt;
    if (box.x < 0.0 || box.x + 100.0 > config.width)
        box.vx = -box.vx;
    if (particles) particles.update(dt);
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(box.x, box.y, 100.0, 60.0, 0.2, 0.8, 0.4, 1.0);
    if (particles) particles.render(gfx);
};
