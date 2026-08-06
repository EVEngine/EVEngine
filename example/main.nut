// example game callbacks — overrides defaults from load.nut
// Frame: physics → map/particles.update → clear → map → box → particles → debug → present
//
// Soft hot-reload: put one-time setup in eve_init; guard mutable state so
// re-dofile does not wipe C++ entity refs. Optional eve_reload / eve_asset_reload.

if (!("fx" in getroottable()))
    fx <- null;
if (!("tileLayer" in getroottable()))
    tileLayer <- null;
if (!("world" in getroottable()))
    world <- null;
if (!("ground" in getroottable()))
    ground <- null;
if (!("boxBody" in getroottable()))
    boxBody <- null;
if (!("boxW" in getroottable()))
    boxW <- 48.0;
if (!("boxH" in getroottable()))
    boxH <- 48.0;

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.09, 0.14, 1.0);
    if (particles == null) particles = eve.Particles();
    if (map == null) map = eve.Map();
    if (physics == null) physics = eve.Physics();
    physics.setMeter(30.0);

    if (tileLayer == null) {
        tileLayer = map.newLayerFromFile("maps/demo.json");
        if (tileLayer == null) {
            // Fallback: procedural checkerboard without atlas (solid debug colors).
            tileLayer = map.newLayer(10, 7, 32.0, 32.0);
            tileLayer.setOrigin(40.0, 80.0);
            tileLayer.setLayer(0);
            for (local y = 0; y < 7; y += 1) {
                for (local x = 0; x < 10; x += 1) {
                    local gid = ((x + y) % 2 == 0) ? 1 : 2;
                    if (x == 0 || y == 0 || x == 9 || y == 6) gid = 3;
                    tileLayer.setTile(x, y, gid);
                }
            }
        }
    }

    if (world == null) {
        // Gravity in pixels/s² (+Y is down, matching screen coords).
        world = physics.newWorld(0.0, 980.0, true);

        ground = world.newBody("static", config.width * 0.5, config.height - 20.0);
        ground.newRectangleFixture(config.width * 1.0, 40.0, 0.0, 0.6, 0.0);

        boxBody = world.newBody("dynamic", config.width * 0.5, 80.0);
        boxBody.newRectangleFixture(boxW, boxH, 1.0, 0.3, 0.2);
    }

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
    // Example: tweak live state without recreating emitters / layers.
};

// Called when a non-.nut watched file changes (after HotReload.tryReload).
eve_asset_reload <- function(path) {
};

eve_update = function(dt) {
    if (world) world.update(dt);
    if (map) map.update(dt);
    if (particles) particles.update(dt);
};

eve_render = function() {
    gfx.clear();
    if (map) map.render(gfx);
    if (boxBody) {
        local x = boxBody.getX() - boxW * 0.5;
        local y = boxBody.getY() - boxH * 0.5;
        gfx.drawSolidRect(x, y, boxW, boxH, 1.3, 0.8, 0.4, 1.0);
    }
    if (ground) {
        gfx.drawSolidRect(0.0, config.height - 40.0, config.width * 1.0, 40.0, 0.25, 0.35, 0.3, 1.0);
    }
    if (particles) particles.render(gfx);
    if (config.debug && world)
        world.drawDebug(gfx);
};
