// 1. Initialize once; draw a rectangle each frame. Coordinates are pixels.
eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
};
eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(376.0, 276.0, 48.0, 48.0, 0.3, 0.75, 1.0, 1.0);
};
