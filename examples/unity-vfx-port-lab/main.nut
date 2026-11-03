// Phase 1 port showcase: loads bonfire / portal / fireworks from the shared
// particle-effects pack (see docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md).
persist bonfire = null
persist portal = null
persist fireworks = null
persist t = 0.0
persist readyLogged = false

local function loadEffect(path, x, y, intensity) {
    local fx = particles.newEffectFromFile(path);
    if (fx == null) {
        throw "load failed " + path + ": " + particles.getLastEffectError();
    }
    fx.setPosition(x, y);
    fx.setFloatParameter("intensity", intensity);
    fx.start();
    return fx;
}

eve_init <- function() {
    gfx.setBackgroundColor(0.012, 0.016, 0.028, 1.0);
    if (particles == null) particles = eve.Particles();

    // Local copies of examples/particle-effects/* (game VFS cannot leave the example root).
    if (bonfire == null) bonfire = loadEffect("bonfire.effect.json", 220.0, 420.0, 1.1);
    if (portal == null) portal = loadEffect("portal.effect.json", 550.0, 300.0, 1.15);
    if (fireworks == null) fireworks = loadEffect("fireworks.effect.json", 880.0, 480.0, 1.2);

    if (!readyLogged) {
        print("UNITY_VFX_PORT_READY bonfire=1 portal=1 fireworks=1\n");
        readyLogged = true;
    }
};

eve_update <- function(dt: float) {
    t += dt;
    local pulse = 1.05 + 0.18 * sin(t * 1.7);
    if (bonfire != null) bonfire.setFloatParameter("intensity", pulse);
    if (portal != null) portal.setFloatParameter("intensity", 1.1 + 0.25 * sin(t * 2.3));
    if (fireworks != null) fireworks.setFloatParameter("intensity", 1.0 + 0.15 * sin(t * 0.9));
    particles.update(dt);
};

eve_render <- function() {
    gfx.clear();
    // Ground strip under the bonfire.
    gfx.drawSolidRect(0.0, 500.0, 1100.0, 140.0, 0.02, 0.028, 0.04, 1.0);
    gfx.drawSolidRect(160.0, 500.0, 120.0, 8.0, 0.12, 0.08, 0.05, 1.0);
    // Portal frame hint.
    gfx.drawSolidRect(470.0, 230.0, 160.0, 4.0, 0.2, 0.35, 0.55, 0.35);
    gfx.drawSolidRect(470.0, 366.0, 160.0, 4.0, 0.2, 0.35, 0.55, 0.35);
    particles.render(gfx);
};
