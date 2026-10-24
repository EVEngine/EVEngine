// Versioned effect owns the four particle layers; script drives pressure only.
persist jet = null
persist jetTime = 0.0

eve_init = function() {
    gfx.setBackgroundColor(0.012, 0.018, 0.028, 1.0);
    if (jet == null) {
        jet = particles.newEffectFromFile("jet.effect.json");
        if (jet == null) throw "Flame jet load failed: " + particles.getLastEffectError();
        jet.setPosition(200.0, 355.0);
        jet.start();
    }
};
eve_update = function(dt) {
    jetTime += dt;
    local pressure = 0.85 + 0.22 * sin(jetTime * 3.2) + 0.10 * sin(jetTime * 17.0);
    jet.setFloatParameter("intensity", pressure);
    particles.update(dt);
};
eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(0.0, 490.0, 1100.0, 160.0, 0.022, 0.03, 0.043, 1.0);
    gfx.drawSolidRect(0.0, 490.0, 1100.0, 2.0, 0.075, 0.085, 0.10, 1.0);
    // Steel feed pipe, mounting foot, heat-darkened nozzle and incandescent rim.
    gfx.drawSolidRect(0.0, 334.0, 166.0, 42.0, 0.12, 0.15, 0.19, 1.0);
    gfx.drawSolidRect(0.0, 338.0, 166.0, 5.0, 0.25, 0.29, 0.33, 1.0);
    gfx.drawSolidRect(92.0, 376.0, 18.0, 114.0, 0.10, 0.12, 0.15, 1.0);
    gfx.drawSolidRect(65.0, 480.0, 75.0, 10.0, 0.16, 0.19, 0.23, 1.0);
    gfx.drawSolidRect(156.0, 324.0, 33.0, 62.0, 0.23, 0.25, 0.28, 1.0);
    gfx.drawSolidRect(189.0, 337.0, 12.0, 36.0, 0.45, 0.16, 0.045, 1.0);
    particles.render(gfx);
};
