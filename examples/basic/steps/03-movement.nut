// 3. Hold arrow keys. dt is seconds; speed is pixels per second.
persist basicTexture = null

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
    // Load outside the frame loop. Keep the texture through script reloads.
    if (basicTexture == null) basicTexture = gfx.newTextureFromFile("assets/box.png");
};
persist basicX = 376.0
persist basicY = 276.0

eve_update = function(dt) {
    local speed = 220.0;
    if (keyboard.isDown("left")) basicX -= speed * dt;
    if (keyboard.isDown("right")) basicX += speed * dt;
    if (keyboard.isDown("up")) basicY -= speed * dt;
    if (keyboard.isDown("down")) basicY += speed * dt;
    if (basicX < 0.0) basicX = 0.0;
    if (basicX > config.width - 48.0) basicX = config.width - 48.0;
    if (basicY < 0.0) basicY = 0.0;
    if (basicY > config.height - 48.0) basicY = config.height - 48.0;
};
eve_render = function() {
    gfx.clear();
    gfx.drawTexturedRect(basicTexture, basicX, basicY, 48.0, 48.0, 1.0, 1.0, 1.0, 1.0);
};
