// High-fidelity Fire volume path — tornado-lab style fullscreen raymarch.
// Push layout matches shaders/fireball.frag (declareFloat order).
// Live spell.params / cast phases drive the volume so knobs stay editable while
// the default + hellfire/blue flame/soulfire presets track the upstream look.

function hexNyb(ch) {
    if (ch == "0") return 0; if (ch == "1") return 1; if (ch == "2") return 2; if (ch == "3") return 3;
    if (ch == "4") return 4; if (ch == "5") return 5; if (ch == "6") return 6; if (ch == "7") return 7;
    if (ch == "8") return 8; if (ch == "9") return 9;
    if (ch == "a" || ch == "A") return 10; if (ch == "b" || ch == "B") return 11;
    if (ch == "c" || ch == "C") return 12; if (ch == "d" || ch == "D") return 13;
    if (ch == "e" || ch == "E") return 14; if (ch == "f" || ch == "F") return 15;
    return 0;
}
function hexByte(h, i) {
    return (hexNyb(h.slice(i, i + 1)) * 16 + hexNyb(h.slice(i + 1, i + 2))).tofloat() / 255.0;
}
function hexRgb(hex) {
    if (typeof hex != "string" || hex.len() < 7) return [1.0, 1.0, 1.0];
    local h = hex.slice(1);
    return [hexByte(h, 0), hexByte(h, 2), hexByte(h, 4)];
}
function smooth01(t) {
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    return t * t * (3.0 - 2.0 * t);
}

FLOAT_NAMES <- [
    "time", "size", "temperature", "intensity", "soot", "smoke",
    "haze", "speed", "orbit", "steps",
    "px", "py", "pz", "dx", "dy", "dz",
    "tintR", "tintG", "tintB", "mode", "phaseT", "seed",
    "uiLeft", "viewW", "viewH", "height"
];

function create(gfx) {
    local shader = gfx.newShaderFromSpvFile("shaders/fireball.frag.spv");
    local white = gfx.newTexture(eve.Image().newImageDataFromFile("white.tga"), false, false);
    foreach (n in FLOAT_NAMES) shader.declareFloat(n);

    local api = {
        shader = shader,
        white = white,
        uiLeft = 350.0,
        viewW = 930.0,
        viewH = 900.0,
        steps = 96.0,
        time = 0.0,
        active = false
    };

    api.setScreen <- function(w, h, uiLeft) {
        this.viewW = w - uiLeft;
        this.viewH = h;
        this.uiLeft = uiLeft;
    };

    api.syncIdle <- function(params, orbit, time, form = "Fireball") {
        // Preview resting volume at caster/target home when nothing is casting.
        local size = ("size" in params) ? params.size.tofloat() : 1.0;
        local tint = hexRgb(("tint" in params) ? params.tint : "#ffffff");
        local isInferno = (form == "Inferno");
        this.shader.sendFloat("time", time);
        this.shader.sendFloat("size", size);
        this.shader.sendFloat("temperature", ("temperature" in params) ? params.temperature.tofloat() : 1900.0);
        this.shader.sendFloat("intensity", ("intensity" in params) ? params.intensity.tofloat() : 1.0);
        this.shader.sendFloat("soot", ("soot" in params) ? params.soot.tofloat() : 1.0);
        this.shader.sendFloat("smoke", ("smoke" in params) ? params.smoke.tofloat() : 1.0);
        this.shader.sendFloat("haze", ("haze" in params) ? params.haze.tofloat() : 1.0);
        this.shader.sendFloat("speed", ("speed" in params) ? params.speed.tofloat() : 18.0);
        this.shader.sendFloat("orbit", orbit);
        this.shader.sendFloat("steps", this.steps);
        if (isInferno) {
            this.shader.sendFloat("px", 3.6);
            this.shader.sendFloat("py", 0.0);
            this.shader.sendFloat("pz", 0.15);
            this.shader.sendFloat("mode", 2.0);
        } else {
            // Home: slightly ahead of focus toward target (matches upstream homes).
            this.shader.sendFloat("px", 1.2);
            this.shader.sendFloat("py", 1.35);
            this.shader.sendFloat("pz", 0.0);
            this.shader.sendFloat("mode", 0.0);
        }
        this.shader.sendFloat("dx", 1.0);
        this.shader.sendFloat("dy", 0.05);
        this.shader.sendFloat("dz", 0.0);
        this.shader.sendFloat("tintR", tint[0]);
        this.shader.sendFloat("tintG", tint[1]);
        this.shader.sendFloat("tintB", tint[2]);
        this.shader.sendFloat("phaseT", 0.0);
        this.shader.sendFloat("seed", 0.17);
        this.shader.sendFloat("uiLeft", this.uiLeft);
        this.shader.sendFloat("viewW", this.viewW);
        this.shader.sendFloat("viewH", this.viewH);
        this.shader.sendFloat("height", ("height" in params) ? params.height.tofloat() : 1.0);
        this.active = true;
    };

    api.syncCast <- function(c, orbit, time, form) {
        local p = c.params;
        local size = ("size" in p) ? p.size.tofloat() : 1.0;
        local tint = hexRgb(("tint" in p) ? p.tint : "#ffffff");
        local mode = 0.0;
        local phaseT = 0.0;
        local px = c.posX, py = c.posY, pz = c.posZ;
        local dx = c.dirX, dy = c.dirY, dz = c.dirZ;
        local intensity = ("intensity" in p) ? p.intensity.tofloat() : 1.0;
        local Rscale = size;

        if (form == "Inferno") {
            mode = 2.0;
            px = c.toX; py = 0.0; pz = c.toZ;
            if (c.phase == "charge") {
                // Column builds during charge: fade intensity in.
                local k = smooth01(c.t / c.durations.charge);
                intensity *= 0.15 + 0.85 * k;
                Rscale *= 0.55 + 0.45 * k;
            } else if (c.phase == "impact" || c.phase == "linger") {
                phaseT = c.t;
            }
        } else {
            // Fireball
            if (c.phase == "charge") {
                local k = smooth01(c.t / c.durations.charge);
                px = c.fromX + c.dirX * 0.6 * size;
                py = c.fromY + 0.12 * size;
                pz = c.fromZ + c.dirZ * 0.6 * size;
                dx = 0.0; dy = -1.0; dz = 0.0;
                Rscale = size * (0.15 + 0.85 * k);
                intensity *= 0.4 + 0.6 * k;
            } else if (c.phase == "travel" || c.phase == "release") {
                mode = 0.0;
                local sp = sqrt(c.velX * c.velX + c.velY * c.velY + c.velZ * c.velZ);
                if (sp > 1e-4) {
                    dx = c.velX / sp; dy = c.velY / sp; dz = c.velZ / sp;
                }
            } else if (c.phase == "impact" || c.phase == "linger") {
                mode = 1.0;
                phaseT = c.t;
                px = c.toX; py = c.toY; pz = c.toZ;
            }
        }

        this.shader.sendFloat("time", time);
        this.shader.sendFloat("size", Rscale);
        this.shader.sendFloat("temperature", ("temperature" in p) ? p.temperature.tofloat() : 1900.0);
        this.shader.sendFloat("intensity", intensity);
        this.shader.sendFloat("soot", ("soot" in p) ? p.soot.tofloat() : 1.0);
        this.shader.sendFloat("smoke", ("smoke" in p) ? p.smoke.tofloat() : 1.0);
        this.shader.sendFloat("haze", ("haze" in p) ? p.haze.tofloat() : 1.0);
        this.shader.sendFloat("speed", ("speed" in p) ? p.speed.tofloat() : 18.0);
        this.shader.sendFloat("orbit", orbit);
        this.shader.sendFloat("steps", this.steps);
        this.shader.sendFloat("px", px);
        this.shader.sendFloat("py", py);
        this.shader.sendFloat("pz", pz);
        this.shader.sendFloat("dx", dx);
        this.shader.sendFloat("dy", dy);
        this.shader.sendFloat("dz", dz);
        this.shader.sendFloat("tintR", tint[0]);
        this.shader.sendFloat("tintG", tint[1]);
        this.shader.sendFloat("tintB", tint[2]);
        this.shader.sendFloat("mode", mode);
        this.shader.sendFloat("phaseT", phaseT);
        this.shader.sendFloat("seed", (c.seed % 89).tofloat() * 0.017);
        this.shader.sendFloat("uiLeft", this.uiLeft);
        this.shader.sendFloat("viewW", this.viewW);
        this.shader.sendFloat("viewH", this.viewH);
        this.shader.sendFloat("height", ("height" in p) ? p.height.tofloat() : 1.0);
        this.active = true;
    };

    api.pickAndSync <- function(casts, spell, form, orbit, time) {
        local best = null;
        foreach (c in casts) {
            if (!c.alive) continue;
            if (c.spell == null || c.spell.id != "fire") continue;
            if (best == null || c.age < best.age) best = c;
        }
        if (best != null) this.syncCast(best, orbit, time, best.form);
        else this.syncIdle(spell.params, orbit, time, form);
    };

    api.render <- function(gfx) {
        if (!this.active) return;
        gfx.setShader(this.shader);
        gfx.drawTexturedRect(this.white, this.uiLeft, 0.0, this.viewW, this.viewH, 1.0, 1.0, 1.0, 1.0);
        gfx.setShader(null);
    };

    return api;
}

return { create = create };
