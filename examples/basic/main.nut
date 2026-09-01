// example game callbacks — overrides defaults from load.nut
// Frame: physics → map/particles.update → clear → map → box → particles → debug → present
//
// Soft hot-reload: put one-time setup in eve_init; guard mutable state so
// re-dofile does not wipe C++ entity refs. Optional eve_reload / eve_asset_reload.

persist fx = null
persist world = null
persist ground = null
persist boxBody = null
persist boxW = 48.0
persist boxH = 48.0

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.09, 0.14, 1.0);

    if (world == null) {
        particles = eve.Particles();
        physics = eve.Physics();
        physics.setMeter(30.0);

        // Gravity in pixels/s² (+Y is down, matching screen coords).
        world = physics.newWorld(0.0, 980.0, true);

        ground = world.newBody("static", config.width * 0.5, config.height - 20.0);
        ground.newRectangleFixture(config.width * 1.0, 40.0, 0.0, 0.6, 0.0);

        boxBody = world.newBody("dynamic", config.width * 0.5, 80.0);
        boxBody.newRectangleFixture(boxW, boxH, 1.0, 0.3, 0.2);
    }

    if (fx == null) {
        fx = particles.newEmitterFromFile("particles/fire.json");
    }
};

// Called after scripts soft-reload (not after asset-only changes).
eve_reload <- function() {
    // Example: tweak live state without recreating emitters / layers.
};

// Called when a non-.nut watched file changes (after HotReload.tryReload).
eve_asset_reload <- function(path) {
};

eve_update = function(dt) {
    if (world) world.update(dt);
    if (fx) fx.setPosition(boxBody.getX(), boxBody.getY());
    if (particles) particles.update(dt);
};

eve_render = function() {
    gfx.clear();
    if (particles) particles.render(gfx);
    if (config.debug && world) world.drawDebug(gfx);
};
