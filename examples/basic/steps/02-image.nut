// 2. Replace the solid rectangle with a bundled image.
persist basicTexture = null

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
    // Load outside the frame loop. Keep the texture through script reloads.
    if (basicTexture == null) basicTexture = gfx.newTextureFromFile("assets/box.png");
};

eve_render = function() {
    gfx.clear();
    gfx.drawTexturedRect(basicTexture, 376.0, 276.0, 48.0, 48.0, 1.0, 1.0, 1.0, 1.0);
};
