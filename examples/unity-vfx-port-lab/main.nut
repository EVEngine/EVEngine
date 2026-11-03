// Phase 1–3 port showcase: shared particle-effects pack recipes
// (see docs/dev/superpowers/specs/2026-11-03-unity-vfx-porting-plan.md).
persist bonfire = null
persist portal = null
persist fireworks = null
persist fireFlip = null
persist smokeFlip = null
persist ribbon = null
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
    if (bonfire == null) bonfire = loadEffect("bonfire.effect.json", 180.0, 220.0, 1.1);
    if (portal == null) portal = loadEffect("portal.effect.json", 550.0, 200.0, 1.15);
    if (fireworks == null) fireworks = loadEffect("fireworks.effect.json", 900.0, 260.0, 1.2);
    if (fireFlip == null) fireFlip = loadEffect("fire_flipbook.effect.json", 200.0, 480.0, 1.15);
    if (smokeFlip == null) smokeFlip = loadEffect("smoke_flipbook.effect.json", 520.0, 500.0, 1.1);
    if (ribbon == null) ribbon = loadEffect("ribbon_dissipate.effect.json", 780.0, 460.0, 1.2);

    if (!readyLogged) {
        print("UNITY_VFX_PORT_READY bonfire=1 portal=1 fireworks=1 fire_flipbook=1 smoke_flipbook=1 ribbon_dissipate=1\n");
        readyLogged = true;
    }
};

eve_update <- function(dt: float) {
    t += dt;
    local pulse = 1.05 + 0.18 * sin(t * 1.7);
    if (bonfire != null) bonfire.setFloatParameter("intensity", pulse);
    if (portal != null) portal.setFloatParameter("intensity", 1.1 + 0.25 * sin(t * 2.3));
    if (fireworks != null) fireworks.setFloatParameter("intensity", 1.0 + 0.15 * sin(t * 0.9));
    if (fireFlip != null) fireFlip.setFloatParameter("intensity", 1.05 + 0.2 * sin(t * 2.1));
    if (smokeFlip != null) smokeFlip.setFloatParameter("intensity", 1.0 + 0.12 * sin(t * 1.3));
    // Distance-emission ribbon needs a moving head (Ribbon Pack dissipate intent).
    if (ribbon != null) {
        local rx = 780.0 + 140.0 * sin(t * 1.4);
        local ry = 460.0 + 55.0 * cos(t * 1.1);
        ribbon.setPosition(rx, ry);
        ribbon.setFloatParameter("intensity", 1.05 + 0.2 * sin(t * 1.8));
    }
    particles.update(dt);
};

eve_render <- function() {
    gfx.clear();
    // Ground strip under Phase-1 / Phase-3 rows.
    gfx.drawSolidRect(0.0, 300.0, 1100.0, 4.0, 0.05, 0.07, 0.1, 0.55);
    gfx.drawSolidRect(0.0, 560.0, 1100.0, 80.0, 0.02, 0.028, 0.04, 1.0);
    gfx.drawSolidRect(130.0, 300.0, 100.0, 6.0, 0.12, 0.08, 0.05, 1.0);
    // Portal frame hint.
    gfx.drawSolidRect(470.0, 130.0, 160.0, 4.0, 0.2, 0.35, 0.55, 0.35);
    gfx.drawSolidRect(470.0, 266.0, 160.0, 4.0, 0.2, 0.35, 0.55, 0.35);
    particles.render(gfx);
};
