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
persist fireworksKick = 0.0

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

local function pad(x, y, w, h, r, g, b, a) {
    gfx.drawSolidRect(x, y, w, h, r, g, b, a);
}

eve_init <- function() {
    gfx.setBackgroundColor(0.012, 0.016, 0.028, 1.0);
    if (particles == null) particles = eve.Particles();

    // Local copies of examples/particle-effects/* (game VFS cannot leave the example root).
    if (bonfire == null) bonfire = loadEffect("bonfire.effect.json", 180.0, 220.0, 1.15);
    if (portal == null) portal = loadEffect("portal.effect.json", 550.0, 200.0, 1.2);
    if (fireworks == null) fireworks = loadEffect("fireworks.effect.json", 900.0, 250.0, 1.25);
    if (fireFlip == null) fireFlip = loadEffect("fire_flipbook.effect.json", 200.0, 470.0, 1.25);
    if (smokeFlip == null) smokeFlip = loadEffect("smoke_flipbook.effect.json", 520.0, 490.0, 1.2);
    if (ribbon == null) ribbon = loadEffect("ribbon_dissipate.effect.json", 820.0, 470.0, 1.3);

    if (!readyLogged) {
        print("UNITY_VFX_PORT_READY bonfire=1 portal=1 fireworks=1 fire_flipbook=1 smoke_flipbook=1 ribbon_dissipate=1\n");
        readyLogged = true;
    }
};

eve_update <- function(dt: float) {
    t += dt;
    local pulse = 1.1 + 0.2 * sin(t * 1.7);
    if (bonfire != null) bonfire.setFloatParameter("intensity", pulse);
    if (portal != null) portal.setFloatParameter("intensity", 1.15 + 0.28 * sin(t * 2.3));
    if (fireworks != null) {
        fireworks.setFloatParameter("intensity", 1.1 + 0.2 * sin(t * 0.9));
        // Timeline is finite-burst shaped; kick a fresh shell cycle so the
        // Phase-1 column stays readable between cues.
        fireworksKick += dt;
        if (fireworksKick >= 2.4) {
            fireworksKick = 0.0;
            fireworks.stop();
            fireworks.start();
        }
    }
    if (fireFlip != null) fireFlip.setFloatParameter("intensity", 1.15 + 0.22 * sin(t * 2.1));
    if (smokeFlip != null) smokeFlip.setFloatParameter("intensity", 1.1 + 0.15 * sin(t * 1.3));
    // Distance-emission ribbon needs a moving head (Ribbon Pack dissipate intent).
    if (ribbon != null) {
        local rx = 820.0 + 160.0 * sin(t * 1.55);
        local ry = 470.0 + 70.0 * cos(t * 1.2);
        ribbon.setPosition(rx, ry);
        ribbon.setFloatParameter("intensity", 1.15 + 0.25 * sin(t * 1.9));
    }
    particles.update(dt);
};

eve_render <- function() {
    gfx.clear();
    // Station pads (no font dependency): Phase-1 row then Phase-3 row.
    pad(0.0, 300.0, 1100.0, 3.0, 0.06, 0.08, 0.12, 0.7);
    pad(120.0, 300.0, 120.0, 6.0, 0.35, 0.18, 0.06, 0.85);   // bonfire
    pad(470.0, 130.0, 160.0, 3.0, 0.25, 0.45, 0.7, 0.45);    // portal
    pad(470.0, 266.0, 160.0, 3.0, 0.25, 0.45, 0.7, 0.45);
    pad(840.0, 300.0, 120.0, 6.0, 0.55, 0.25, 0.1, 0.7);     // fireworks
    pad(0.0, 560.0, 1100.0, 80.0, 0.02, 0.028, 0.04, 1.0);
    pad(140.0, 560.0, 120.0, 6.0, 0.7, 0.25, 0.05, 0.85);    // fire_flipbook
    pad(460.0, 560.0, 120.0, 6.0, 0.35, 0.4, 0.48, 0.75);    // smoke_flipbook
    pad(760.0, 560.0, 160.0, 6.0, 0.25, 0.55, 0.85, 0.75);   // ribbon
    particles.render(gfx);
};
