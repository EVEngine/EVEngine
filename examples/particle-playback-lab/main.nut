persist trailFx = null
persist burstFx = null
persist playbackTime = 0.0

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.035, 0.065, 1.0);
    if (particles == null) particles = eve.Particles();

    if (trailFx == null) {
        trailFx = particles.newEmitter(900);
        trailFx.setRandomSeed(20260826);
        trailFx.setAutoRandomSeed(false);
        trailFx.setEmissionRate(0.0);
        trailFx.setEmissionRateOverDistance(0.32);
        trailFx.setParticleLifetime(0.65, 1.15);
        trailFx.setParticleSize(22.0, 22.0);
        trailFx.setSizes(1.0, 0.45);
        trailFx.setSizeVariation(0.35);
        trailFx.setSpeed(4.0, 18.0);
        trailFx.setSpread(6.2831853);
        trailFx.setGravity(0.0, 16.0);
        trailFx.setDamping(0.25);
        trailFx.setBlendMode("alpha");
        trailFx.setColorStart(0.35, 0.9, 1.0, 1.0);
        trailFx.setColorEnd(0.45, 0.15, 1.0, 0.35);
        trailFx.setFixedTimeStep(0.008333333, 8);
        trailFx.start();
    }

    if (burstFx == null) {
        burstFx = particles.newEffectFromFile("impact.effect.json");
        if (burstFx == null) {
            print("particle effect load failed: " + particles.getLastEffectError() + "\n");
            return;
        }
        burstFx.setPosition(480.0, 320.0);
        burstFx.setFloatParameter("intensity", 1.15);
        burstFx.start();
    }
};

eve_update = function(dt) {
    playbackTime += dt;
    local x = 480.0 + cos(playbackTime * 1.35) * 330.0;
    local y = 320.0 + sin(playbackTime * 2.05) * 190.0;
    trailFx.setPosition(x, y);
    particles.update(dt);
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(44.0, 42.0, 872.0, 556.0, 0.04, 0.065, 0.12, 1.0);
    gfx.drawSolidRect(46.0, 44.0, 868.0, 2.0, 0.1, 0.45, 0.65, 0.9);
    gfx.drawSolidRect(46.0, 594.0, 868.0, 2.0, 0.35, 0.12, 0.55, 0.9);
    particles.render(gfx);
};
