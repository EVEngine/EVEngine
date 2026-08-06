// example game callbacks — overrides defaults from load.nut
// Frame: particles.update → clear → sprites → particles.render → ui → present
//
// Soft hot-reload: put one-time setup in eve_init; guard mutable state so
// re-dofile does not wipe C++ entity refs. Optional eve_reload / eve_asset_reload.

if (!("box" in getroottable()))
    box <- { x = 100.0, y = 100.0, vx = 60.0 };
if (!("fx" in getroottable()))
    fx <- null;

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.09, 0.14, 1.0);
    if (particles == null) particles = eve.Particles();
    if (fx == null) {
        fx = particles.newEmitterFromFile("particles/fire.json");
        if (fx == null) {
            fx = particles.newEmitter(256);
            fx.applyPreset("fire");
            fx.setPosition(config.width * 0.5, config.height * 0.75);
            fx.start();
        } else {
            fx.setPosition(config.width * 0.5, config.height * 0.75);
        }
    }
};

// Called after scripts soft-reload (not after asset-only changes).
eve_reload <- function() {
    // Example: tweak live state without recreating emitters.
};

// Called when a non-.nut watched file changes (after HotReload.tryReload).
eve_asset_reload <- function(path) {
};

eve_update = function(dt) {
    box.x = box.x + box.vx * dt;
    if (box.x < 0.0 || box.x + 200.0 > config.width)
        box.vx = -box.vx;
    if (particles) particles.update(dt);
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(box.x, box.y, 200.0, 160.0, 1.3, 0.8, 0.4, 1.0);
    if (particles) particles.render(gfx);
};
