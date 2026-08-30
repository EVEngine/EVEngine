// Asset-free production autotile showcase: shore, wall and animated waterfall semantics.
persist shore = null;
persist walls = null;
persist falls = null;

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.06, 0.09, 1.0);
    if (map == null) map = eve.Map();

    if (shore == null) {
        shore = map.newLayer(14, 10, 32.0, 32.0);
        shore.setOrigin(32.0, 48.0);
        shore.defineAutotileFamily(1, "shore", 1337);
        for (local mask = 0; mask < 256; mask += 1)
            shore.setAutotileRule(1 + mask % 12, 1, mask, 1);
        shore.paintTerrainRect(1, 1, 9, 7, 1);
        shore.eraseTerrainRect(7, 1, 3, 2);
        shore.eraseTerrainRect(1, 6, 2, 2);
    }

    if (walls == null) {
        walls = map.newLayer(8, 10, 32.0, 32.0);
        walls.setOrigin(520.0, 48.0);
        walls.defineAutotileFamily(2, "wall", 7);
        walls.setAutotileRule(21, 2, 32, 1); // top
        walls.setAutotileRule(22, 2, 34, 1); // face/body
        walls.setAutotileRule(23, 2, 2, 1);  // foot
        walls.paintTerrainRect(1, 1, 1, 7, 2);
        walls.paintTerrainRect(4, 2, 1, 6, 2);
    }

    if (falls == null) {
        falls = map.newLayer(6, 10, 32.0, 32.0);
        falls.setOrigin(720.0, 48.0);
        falls.defineAutotileFamily(3, "waterfall", 11);
        falls.setAutotileRule(31, 3, 32, 1); // lip
        falls.setAutotileRule(32, 3, 34, 1); // repeating body
        falls.setAutotileRule(33, 3, 2, 1);  // splash/foot
        falls.addTileAnimationFrame(32, 32, 140);
        falls.addTileAnimationFrame(32, 34, 140);
        falls.addTileAnimationFrame(32, 35, 140);
        falls.paintTerrainRect(2, 1, 1, 7, 3);
    }
};

eve_update = function(dt) { if (map) map.update(dt); };
eve_render = function() { gfx.clear(); if (map) map.render(gfx); };
