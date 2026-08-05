// example game callbacks — overrides defaults from load.nut

box <- { x = 100.0, y = 100.0, vx = 60.0 };

eve_init = function() {
    gfx.setBackgroundColor(0.12, 0.14, 0.22, 1.0);
};

eve_update = function(dt) {
    box.x = box.x + box.vx * dt;
    if (box.x < 0.0 || box.x + 100.0 > config.width)
        box.vx = -box.vx;
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(box.x, box.y, 100.0, 60.0, 0.2, 0.8, 0.4, 1.0);
};
