// 5. Add sparks that follow the falling box.
persist basicTexture = null
persist basicWorld = null
persist basicGround = null
persist basicBody = null
persist basicSparks = null

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.16, 1.0);
    if (basicTexture == null) basicTexture = gfx.newTextureFromFile("assets/box.png");
    if (basicWorld == null) {
        physics.setMeter(30.0);
        // Positive Y points down. Physics owns the box position from here on.
        basicWorld = physics.newWorld(0.0, 980.0, true);
        basicGround = basicWorld.newBody("static", 400.0, 570.0);
        basicGround.newRectangleFixture(800.0, 40.0, 0.0, 0.6, 0.0);
        basicBody = basicWorld.newBody("dynamic", 400.0, 80.0);
        basicBody.newRectangleFixture(48.0, 48.0, 1.0, 0.3, 0.2);
    }
    if (basicSparks == null) basicSparks = particles.newEmitterFromFile("particles/spark.json");
};
eve_update = function(dt) {
    basicWorld.update(dt);
    basicSparks.setPosition(basicBody.getX(), basicBody.getY() - 24.0);
    particles.update(dt);
};
eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(0.0, 550.0, 800.0, 40.0, 0.25, 0.30, 0.38, 1.0);
    // Body coordinates are the center; image coordinates are the top-left.
    gfx.drawTexturedRect(basicTexture, basicBody.getX() - 24.0, basicBody.getY() - 24.0,
        48.0, 48.0, 1.0, 1.0, 1.0, 1.0);
    particles.render(gfx);
};
