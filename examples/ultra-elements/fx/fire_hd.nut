// Multi-spell HD volume path — tornado-lab style fullscreen raymarch.
// Push layout matches shaders/fireball.frag (declareFloat order).
// Modes: fireball/inferno + tornado, black-flame, judgment, portal, cyclone,
// ball-lightning, radiant-lance, supernova, aegis. Live params stay editable.

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

function spellSupportsHd(id) {
    return id == "fire" || id == "fire-tornado" || id == "black-flame"
        || id == "light" || id == "portal" || id == "wind" || id == "ball-lightning"
        || id == "supernova" || id == "arcane";
}

FLOAT_NAMES <- [
    "time", "size", "temperature", "intensity", "soot", "smoke",
    "haze", "speed", "orbit", "steps",
    "px", "py", "pz", "dx", "dy", "dz",
    "tintR", "tintG", "tintB", "mode", "phaseT", "seed",
    "uiLeft", "viewW", "viewH", "height",
    "spin", "absorb", "rimR", "rimG", "rimB", "pad"
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

    api.sendCommon <- function(time, size, temperature, intensity, soot, smoke, haze, speed,
                               orbit, px, py, pz, dx, dy, dz, tint, mode, phaseT, seed, height,
                               spin, absorb, rim) {
        this.shader.sendFloat("time", time);
        this.shader.sendFloat("size", size);
        this.shader.sendFloat("temperature", temperature);
        this.shader.sendFloat("intensity", intensity);
        this.shader.sendFloat("soot", soot);
        this.shader.sendFloat("smoke", smoke);
        this.shader.sendFloat("haze", haze);
        this.shader.sendFloat("speed", speed);
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
        this.shader.sendFloat("seed", seed);
        this.shader.sendFloat("uiLeft", this.uiLeft);
        this.shader.sendFloat("viewW", this.viewW);
        this.shader.sendFloat("viewH", this.viewH);
        this.shader.sendFloat("height", height);
        this.shader.sendFloat("spin", spin);
        this.shader.sendFloat("absorb", absorb);
        this.shader.sendFloat("rimR", rim[0]);
        this.shader.sendFloat("rimG", rim[1]);
        this.shader.sendFloat("rimB", rim[2]);
        this.shader.sendFloat("pad", 0.0);
        this.active = true;
    };

    api.modeFor <- function(spellId, form) {
        if (spellId == "fire") {
            if (form == "Inferno") return 2.0;
            return 0.0;
        }
        if (spellId == "fire-tornado") return 3.0;
        if (spellId == "black-flame") return 4.0;
        if (spellId == "light") {
            if (form == "Radiant Lance") return 9.0;
            return 5.0; // Judgment
        }
        if (spellId == "portal") return 6.0;
        if (spellId == "wind") {
            if (form == "Wind Slash") return -1.0; // mesh-only
            return 7.0; // Cyclone
        }
        if (spellId == "ball-lightning") return 8.0;
        if (spellId == "supernova") return 10.0;
        if (spellId == "arcane") {
            if (form == "Aegis") return 11.0;
            return -1.0;
        }
        return -1.0;
    };

    api.tintFor <- function(p, spellId, form) {
        if ("tint" in p && typeof p.tint == "string" && p.tint != "#ffffff") return hexRgb(p.tint);
        if ("core" in p && typeof p.core == "string") return hexRgb(p.core);
        if ("color" in p && typeof p.color == "string") return hexRgb(p.color);
        if (spellId == "light") return [1.0, 0.9, 0.55];
        if (spellId == "portal") return [0.7, 0.35, 1.0];
        if (spellId == "wind") return [0.78, 0.92, 0.95];
        if (spellId == "ball-lightning") return [0.55, 0.75, 1.0];
        if (spellId == "arcane") return [1.0, 0.35, 0.85];
        if (spellId == "supernova") return [1.0, 0.75, 0.4];
        return [1.0, 1.0, 1.0];
    };

    api.rimFor <- function(p) {
        if ("rim" in p && typeof p.rim == "string") return hexRgb(p.rim);
        if ("glow" in p && typeof p.glow == "string") return hexRgb(p.glow);
        return [1.0, 0.4, 0.15];
    };

    api.syncIdle <- function(spell, form, orbit, time) {
        local p = spell.params;
        local mode = this.modeFor(spell.id, form);
        if (mode < 0.0) { this.active = false; return; }
        local size = ("size" in p) ? p.size.tofloat() : 1.0;
        local tint = this.tintFor(p, spell.id, form);
        local rim = this.rimFor(p);
        local intensity = ("intensity" in p) ? p.intensity.tofloat() : (("glow" in p && typeof p.glow != "string") ? p.glow.tofloat() : 1.0);
        local height = ("height" in p) ? p.height.tofloat() : 1.0;
        local spin = ("spin" in p) ? p.spin.tofloat() : (("swirl" in p) ? p.swirl.tofloat() : 1.0);
        local absorb = ("absorb" in p) ? p.absorb.tofloat() : 1.0;
        local soot = ("soot" in p) ? p.soot.tofloat() : 1.0;
        local smoke = ("smoke" in p) ? p.smoke.tofloat() : (("rays" in p) ? p.rays.tofloat() : 1.0);
        local haze = ("haze" in p) ? p.haze.tofloat() : 1.0;
        local speed = ("speed" in p) ? p.speed.tofloat() : 18.0;
        local temp = ("temperature" in p) ? p.temperature.tofloat() : 1900.0;
        local px = 3.6, py = 1.15, pz = 0.15;
        local dx = 1.0, dy = 0.0, dz = 0.0;
        if (spell.id == "fire" && form != "Inferno") {
            px = 1.2; py = 1.35; pz = 0.0; mode = 0.0;
        } else if (spell.id == "fire") {
            py = 0.0; mode = 2.0;
        } else if (spell.id == "light" && form == "Radiant Lance") {
            px = 0.0; py = 1.25; pz = 0.0; dx = 1.0; dy = -0.02; dz = 0.04;
        } else if (spell.id == "light") {
            py = 0.0;
        } else if (spell.id == "arcane" && form == "Aegis") {
            px = 0.0; py = 1.2; pz = 0.0;
        } else if (spell.id == "portal") {
            py = 1.45;
        } else if (spell.id == "ball-lightning") {
            px = 1.8; py = 1.4; pz = 0.0;
        } else if (spell.id == "fire-tornado" || spell.id == "black-flame" || spell.id == "wind" || spell.id == "supernova") {
            py = (spell.id == "supernova") ? 1.3 : 0.0;
        }
        this.sendCommon(time, size, temp, intensity, soot, smoke, haze, speed, orbit,
            px, py, pz, dx, dy, dz, tint, mode, 0.0, 0.17, height, spin, absorb, rim);
    };

    api.syncCast <- function(c, orbit, time) {
        local spell = c.spell;
        local form = c.form;
        local mode = this.modeFor(spell.id, form);
        if (mode < 0.0) { this.active = false; return; }
        local p = c.params;
        local size = ("size" in p) ? p.size.tofloat() : 1.0;
        local tint = this.tintFor(p, spell.id, form);
        local rim = this.rimFor(p);
        local intensity = ("intensity" in p) ? p.intensity.tofloat() : (("glow" in p && typeof p.glow != "string") ? p.glow.tofloat() : 1.0);
        local height = ("height" in p) ? p.height.tofloat() : 1.0;
        local spin = ("spin" in p) ? p.spin.tofloat() : (("swirl" in p) ? p.swirl.tofloat() : 1.0);
        local absorb = ("absorb" in p) ? p.absorb.tofloat() : 1.0;
        local soot = ("soot" in p) ? p.soot.tofloat() : 1.0;
        local smoke = ("smoke" in p) ? p.smoke.tofloat() : (("rays" in p) ? p.rays.tofloat() : 1.0);
        local haze = ("haze" in p) ? p.haze.tofloat() : 1.0;
        local speed = ("speed" in p) ? p.speed.tofloat() : 18.0;
        local temp = ("temperature" in p) ? p.temperature.tofloat() : 1900.0;
        local phaseT = 0.0;
        local px = c.posX, py = c.posY, pz = c.posZ;
        local dx = c.dirX, dy = c.dirY, dz = c.dirZ;
        local Rscale = size;

        if (spell.id == "fire") {
            if (form == "Inferno") {
                mode = 2.0;
                px = c.toX; py = 0.0; pz = c.toZ;
                if (c.phase == "charge") {
                    local k = smooth01(c.t / c.durations.charge);
                    intensity *= 0.15 + 0.85 * k;
                    Rscale *= 0.55 + 0.45 * k;
                } else if (c.phase == "impact" || c.phase == "linger") phaseT = c.t;
            } else {
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
                    if (sp > 1e-4) { dx = c.velX / sp; dy = c.velY / sp; dz = c.velZ / sp; }
                } else if (c.phase == "impact" || c.phase == "linger") {
                    mode = 1.0; phaseT = c.t; px = c.toX; py = c.toY; pz = c.toZ;
                }
            }
        } else if (spell.id == "fire-tornado" || spell.id == "black-flame" || (spell.id == "wind" && form == "Cyclone")) {
            px = c.toX; py = 0.0; pz = c.toZ;
            if (c.phase == "charge") {
                local k = smooth01(c.t / c.durations.charge);
                intensity *= 0.2 + 0.8 * k;
                height *= 0.4 + 0.6 * k;
            }
        } else if (spell.id == "light") {
            if (form == "Radiant Lance") {
                px = c.fromX; py = c.fromY; pz = c.fromZ;
                dx = c.dirX; dy = c.dirY; dz = c.dirZ;
                if (c.phase == "charge") {
                    local k = smooth01(c.t / c.durations.charge);
                    intensity *= 0.25 + 0.75 * k;
                    Rscale *= 0.4 + 0.6 * k;
                }
            } else {
                px = c.toX - 0.35; py = 0.0; pz = c.toZ + 0.2;
                if (c.phase == "charge") {
                    local k = smooth01(c.t / c.durations.charge);
                    intensity *= 0.2 + 0.8 * k;
                    height *= 0.35 + 0.65 * k;
                }
            }
        } else if (spell.id == "portal") {
            px = c.toX; py = 1.45; pz = c.toZ;
            if (c.phase == "charge") {
                local k = smooth01(c.t / c.durations.charge);
                intensity *= 0.3 + 0.7 * k;
                Rscale *= 0.4 + 0.6 * k;
            }
        } else if (spell.id == "ball-lightning") {
            if (c.phase == "impact" || c.phase == "linger") {
                px = c.toX; py = c.toY; pz = c.toZ;
            } else {
                px = c.posX; py = c.posY; pz = c.posZ;
            }
            if (c.phase == "charge") {
                local k = smooth01(c.t / c.durations.charge);
                Rscale *= 0.2 + 0.8 * k;
            }
        } else if (spell.id == "supernova") {
            px = c.toX; py = 1.3; pz = c.toZ;
            phaseT = c.age;
            if (c.phase == "charge") {
                local k = smooth01(c.t / c.durations.charge);
                intensity *= 0.15 + 0.85 * k;
                Rscale *= 0.3 + 0.7 * k;
            }
        } else if (spell.id == "arcane" && form == "Aegis") {
            px = c.fromX; py = 1.2; pz = c.fromZ;
            if (c.phase == "charge") {
                local k = smooth01(c.t / c.durations.charge);
                Rscale *= 0.4 + 0.6 * k;
            }
        }

        this.sendCommon(time, Rscale, temp, intensity, soot, smoke, haze, speed, orbit,
            px, py, pz, dx, dy, dz, tint, mode, phaseT,
            (c.seed % 89).tofloat() * 0.017, height, spin, absorb, rim);
    };

    api.pickAndSync <- function(casts, spell, form, orbit, time) {
        local mode = this.modeFor(spell.id, form);
        if (mode < 0.0) { this.active = false; return; }
        local best = null;
        foreach (c in casts) {
            if (!c.alive || c.spell == null) continue;
            if (c.spell.id != spell.id) continue;
            if (c.form != form) continue;
            if (best == null || c.age < best.age) best = c;
        }
        if (best != null) this.syncCast(best, orbit, time);
        else this.syncIdle(spell, form, orbit, time);
    };

    api.render <- function(gfx) {
        if (!this.active) return;
        gfx.setShader(this.shader);
        gfx.drawTexturedRect(this.white, this.uiLeft, 0.0, this.viewW, this.viewH, 1.0, 1.0, 1.0, 1.0);
        gfx.setShader(null);
    };

    return api;
}

return { create = create, spellSupportsHd = spellSupportsHd };
