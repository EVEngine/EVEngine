// EVEngine presentation layer for Ultra Elements spells.
// Uses pooled Renderable3D props + weather.Lightning + particle bursts.
// Live params (c.params) drive scale, intensity, colour and density every frame.

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

function mix3(a, b, t) {
    return [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
}

function clamp01(v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
function smooth(t) { t = clamp01(t); return t * t * (3.0 - 2.0 * t); }
function lerp(a, b, t) { return a + (b - a) * t; }
function fmax(a, b) { return a > b ? a : b; }
function fmin(a, b) { return a < b ? a : b; }

function tempRgb(kelvin) {
    // Cheap blackbody-ish ramp for fire knobs.
    local t = clamp01((kelvin - 900.0) / 5600.0);
    if (t < 0.35) return mix3([1.0, 0.25, 0.04], [1.0, 0.55, 0.12], t / 0.35);
    if (t < 0.7) return mix3([1.0, 0.55, 0.12], [1.0, 0.92, 0.55], (t - 0.35) / 0.35);
    return mix3([1.0, 0.92, 0.55], [0.55, 0.75, 1.0], (t - 0.7) / 0.3);
}

function elementHue(id) {
    if (id == "lightning") return [0.50, 0.70, 1.0];
    if (id == "fire") return [1.0, 0.48, 0.10];
    if (id == "water") return [0.18, 0.66, 1.0];
    if (id == "ice") return [0.62, 0.94, 1.0];
    if (id == "earth") return [0.75, 0.54, 0.29];
    if (id == "wind") return [0.78, 0.96, 0.89];
    if (id == "void") return [0.54, 0.24, 1.0];
    if (id == "light") return [1.0, 0.88, 0.54];
    if (id == "acid") return [0.54, 1.0, 0.24];
    if (id == "arcane") return [1.0, 0.31, 0.85];
    return [1.0, 1.0, 1.0];
}

function hideProp(e) {
    e.setScale(0.001, 0.001, 0.001);
    e.setPosition(0.0, -40.0, 0.0);
    e.setVisible(false);
}

function showProp(e, x, y, z, sx, sy, sz, r, g, b, a = 1.0) {
    e.setVisible(true);
    e.setPosition(x, y, z);
    e.setScale(sx, sy, sz);
    e.setTint(r, g, b, a);
}

function checked(result, ctx) {
    if (!result.ok) throw ctx + ": " + result.status.summary;
    return ("value" in result) ? result.value : null;
}

function createPresenter(gfx, particlesMod, weatherMod) {
    // When true, Fire / Fireball / Inferno are drawn by fx/fire_hd.nut instead of mesh props.
    local cube = gfx.newMeshCube(1.0);
    local sphere = gfx.newMeshSphere(18, 12);
    local cylinder = gfx.newMeshCylinder(24, 1, true);

    local orbs = []; local segs = []; local spikes = []; local rings = []; local columns = [];
    for (local i = 0; i < 64; ++i) {
        local e = eve.Renderable3D(); e.setMesh(sphere); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.35); hideProp(e); orbs.push(e);
    }
    for (local i = 0; i < 48; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cube); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.4); hideProp(e); segs.push(e);
    }
    for (local i = 0; i < 32; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cylinder); e.setReceiveLight(true); e.setCastShadow(false);
        e.setRoughness(0.85); hideProp(e); spikes.push(e);
    }
    for (local i = 0; i < 12; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cylinder); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.5); hideProp(e); rings.push(e);
    }
    for (local i = 0; i < 8; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cylinder); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.45); hideProp(e); columns.push(e);
    }

    local bolts = [];
    if (weatherMod != null) {
        for (local i = 0; i < 6; ++i) {
            local s = eve.LightningSettings();
            s.width = 0.14; s.displacement = 0.22; s.branches = 4; s.duration = 0.55; s.seed = 20 + i * 17; s.variants = 5;
            local fx = checked(weatherMod.prepareLightning(gfx, s), "prepareLightning");
            checked(fx.setColor(0.7, 0.85, 1.0), "bolt color");
            bolts.push(fx);
        }
    }

    local burst = null;
    if (particlesMod != null) {
        burst = particlesMod.newEffectFromFile("particles/burst.effect.json");
        if (burst == null) print("ULTRA_ELEMENTS_WARN particles: " + particlesMod.getLastEffectError() + "\n");
        else { burst.setVisible(false); }
    }

    local api = {
        gfx = gfx,
        particles = particlesMod,
        weather = weatherMod,
        orbs = orbs, segs = segs, spikes = spikes, rings = rings, columns = columns,
        bolts = bolts, burst = burst,
        orbCursor = 0, segCursor = 0, spikeCursor = 0, ringCursor = 0, colCursor = 0, boltCursor = 0,
        active = {},
        time = 0.0,
        screenW = 1280.0, screenH = 800.0,
        // When true, Fireball/Inferno are drawn by fx/fire_hd volumetric path.
        hdFireSkip = false
    };

    api.setScreen <- function(w, h) { this.screenW = w; this.screenH = h; };

    api.resetCursors <- function() {
        this.orbCursor = 0; this.segCursor = 0; this.spikeCursor = 0;
        this.ringCursor = 0; this.colCursor = 0;
    };

    api.takeOrb <- function() {
        if (this.orbCursor >= this.orbs.len()) return null;
        return this.orbs[this.orbCursor++];
    };
    api.takeSeg <- function() {
        if (this.segCursor >= this.segs.len()) return null;
        return this.segs[this.segCursor++];
    };
    api.takeSpike <- function() {
        if (this.spikeCursor >= this.spikes.len()) return null;
        return this.spikes[this.spikeCursor++];
    };
    api.takeRing <- function() {
        if (this.ringCursor >= this.rings.len()) return null;
        return this.rings[this.ringCursor++];
    };
    api.takeCol <- function() {
        if (this.colCursor >= this.columns.len()) return null;
        return this.columns[this.colCursor++];
    };

    api.worldToScreen <- function(x, y, z) {
        // Fixed camera approx matching main.nut orbit defaults (eye ~ look-at arena).
        local eyeX = 0.2, eyeY = 7.2, eyeZ = 14.5;
        local tx = 1.2, ty = 1.1, tz = 0.0;
        local fx = tx - eyeX, fy = ty - eyeY, fz = tz - eyeZ;
        local fl = sqrt(fx * fx + fy * fy + fz * fz);
        fx /= fl; fy /= fl; fz /= fl;
        local rx = fy * 0.0 - fz * 1.0; // cross(f, up≈Y) then normalize-ish
        local rz = fx * 1.0 - fy * 0.0;
        local rl = sqrt(rx * rx + rz * rz); if (rl < 1e-5) { rx = 1.0; rz = 0.0; rl = 1.0; }
        rx /= rl; local ry = 0.0; rz /= rl;
        local ux = ry * fz - rz * fy, uy = rz * fx - rx * fz, uz = rx * fy - ry * fx;
        local dx = x - eyeX, dy = y - eyeY, dz = z - eyeZ;
        local camZ = dx * fx + dy * fy + dz * fz;
        if (camZ < 0.2) camZ = 0.2;
        local camX = dx * rx + dy * ry + dz * rz;
        local camY = dx * ux + dy * uy + dz * uz;
        local fov = 0.75; // ~43 deg
        local sx = (camX / (camZ * fov)) * 0.5 + 0.5;
        local sy = 1.0 - ((camY / (camZ * fov)) * 0.5 + 0.5);
        return { x = sx * this.screenW, y = sy * this.screenH };
    };

    api.pulseBurst <- function(x, y, z, intensity, rgb) {
        if (this.burst == null) return;
        local s = this.worldToScreen(x, y, z);
        this.burst.setPosition(s.x, s.y);
        this.burst.setFloatParameter("intensity", intensity);
        this.burst.setVisible(true);
        this.burst.reset();
        this.burst.start();
        // Best-effort tint via first emitter if present.
        local e0 = this.burst.getEmitterByName("core");
        if (e0 != null) {
            e0.setColorStart(rgb[0], rgb[1], rgb[2], 1.0);
            e0.setColorEnd(rgb[0] * 0.2, rgb[1] * 0.15, rgb[2] * 0.1, 0.0);
        }
    };

    api.triggerBolt <- function(ax, ay, az, bx, by, bz, width, jag, branches, rgb) {
        if (this.bolts.len() == 0) return;
        local fx = this.bolts[this.boltCursor % this.bolts.len()];
        this.boltCursor += 1;
        checked(fx.setEndpoints(ax, ay, az, bx, by, bz), "bolt endpoints");
        checked(fx.setColor(rgb[0], rgb[1], rgb[2]), "bolt color");
        checked(fx.trigger(), "bolt trigger");
    };

    api.acquireCast <- function(c) {
        this.active[c.index] <- true;
    };

    api.releaseCast <- function(c) {
        if (c.index in this.active) this.active[c.index] = false;
        c.data = {};
    };

    api.spellRgb <- function(c) {
        local p = c.params;
        local hue = elementHue(c.spell.element);
        if ("core" in p && typeof p.core == "string") return hexRgb(p.core);
        if ("color" in p && typeof p.color == "string") return hexRgb(p.color);
        if ("tint" in p && typeof p.tint == "string" && p.tint != "#ffffff") {
            local t = hexRgb(p.tint);
            return [hue[0] * t[0], hue[1] * t[1], hue[2] * t[2]];
        }
        if ("temperature" in p) {
            local tb = tempRgb(p.temperature);
            if ("tint" in p && typeof p.tint == "string") {
                local t = hexRgb(p.tint);
                return [tb[0] * t[0], tb[1] * t[1], tb[2] * t[2]];
            }
            return tb;
        }
        if ("glow" in p && typeof p.glow == "string") return hexRgb(p.glow);
        return hue;
    };

    api.drawCharge <- function(c, rgb, scale) {
        local e = this.takeOrb();
        if (e == null) return;
        local u = clamp01(c.t / c.durations.charge);
        local s = (0.08 + 0.22 * u) * scale;
        showProp(e, c.fromX, c.fromY, c.fromZ, s, s, s, rgb[0], rgb[1], rgb[2], 0.55 + 0.45 * u);
        local ring = this.takeRing();
        if (ring != null) {
            local rs = (0.35 + 0.55 * u) * scale;
            showProp(ring, c.fromX, c.fromY - 0.05, c.fromZ, rs, 0.04, rs, rgb[0], rgb[1], rgb[2], 0.7);
        }
    };

    api.drawProjectile <- function(c, rgb, size) {
        local e = this.takeOrb();
        if (e == null) return;
        local s = 0.18 * size;
        showProp(e, c.posX, c.posY, c.posZ, s * 1.1, s, s * 1.1, rgb[0], rgb[1], rgb[2], 1.0);
        local core = this.takeOrb();
        if (core != null) showProp(core, c.posX, c.posY, c.posZ, s * 0.45, s * 0.45, s * 0.45, 1.0, 1.0, 1.0, 1.0);
        // Trail beads.
        for (local i = 1; i <= 8; ++i) {
            local t = i.tofloat() / 8.0;
            local seg = this.takeSeg();
            if (seg == null) break;
            local x = c.posX - c.dirX * t * 0.9 * size;
            local y = c.posY - c.dirY * t * 0.9 * size;
            local z = c.posZ - c.dirZ * t * 0.9 * size;
            local thick = s * (1.0 - t * 0.7);
            showProp(seg, x, y, z, thick * 1.4, thick * 0.6, thick, rgb[0], rgb[1] * (1.0 - 0.3 * t), rgb[2], 1.0 - t * 0.7);
        }
    };

    api.drawBeam <- function(c, rgb, width) {
        local steps = 14;
        for (local i = 0; i < steps; ++i) {
            local u = (i.tofloat() + 0.5) / steps.tofloat();
            local seg = this.takeSeg();
            if (seg == null) break;
            local x = lerp(c.fromX, c.toX, u);
            local y = lerp(c.fromY, c.toY, u) + sin(u * 6.283 + this.time * 8.0) * 0.05 * width;
            local z = lerp(c.fromZ, c.toZ, u);
            local thick = 0.06 * width * (0.7 + 0.3 * sin(u * 12.0 + this.time * 10.0));
            showProp(seg, x, y, z, thick * 1.5, thick, thick, rgb[0], rgb[1], rgb[2], 0.95);
        }
        local tip = this.takeOrb();
        if (tip != null) showProp(tip, c.toX, c.toY, c.toZ, 0.16 * width, 0.16 * width, 0.16 * width, rgb[0], rgb[1], rgb[2], 1.0);
    };

    api.drawStrike <- function(c, rgb) {
        local p = c.params;
        local jag = ("jag" in p) ? p.jag : 0.1;
        local branches = ("branches" in p) ? p.branches.tointeger() : 4;
        local width = 0.1 + 0.08 * (("intensity" in p) ? p.intensity : 1.0);
        if (c.phase == "impact" || (c.phase == "linger" && c.t < 0.35)) {
            this.triggerBolt(c.fromX, c.fromY + 6.5, c.fromZ, c.toX, c.toY, c.toZ, width, jag, branches, rgb);
            // Ground Lichtenberg stubs.
            for (local i = 0; i < 5; ++i) {
                local a = c.seed * 0.01 + i * 1.1;
                local seg = this.takeSeg();
                if (seg == null) break;
                showProp(seg, c.toX + cos(a) * 0.4, 0.05, c.toZ + sin(a) * 0.4,
                    0.55, 0.03, 0.04, rgb[0], rgb[1], rgb[2], 0.8);
            }
        } else if (c.phase == "charge") {
            this.drawCharge(c, rgb, 1.0);
        }
    };

    api.drawAreaColumn <- function(c, rgb, height, radius, swirl) {
        local col = this.takeCol();
        if (col != null) {
            showProp(col, c.toX, height * 0.5, c.toZ, radius, height, radius, rgb[0], rgb[1], rgb[2], 0.55);
        }
        local ring = this.takeRing();
        if (ring != null) {
            local rs = radius * (1.2 + 0.35 * sin(this.time * 4.0 * swirl));
            showProp(ring, c.toX, 0.05, c.toZ, rs, 0.05, rs, rgb[0], rgb[1], rgb[2], 0.8);
        }
        for (local i = 0; i < 10; ++i) {
            local a = this.time * 2.5 * swirl + i * 0.63;
            local e = this.takeOrb();
            if (e == null) break;
            local h = (i.tofloat() / 10.0) * height;
            local rad = radius * (0.55 + 0.35 * sin(a));
            showProp(e, c.toX + cos(a) * rad, h, c.toZ + sin(a) * rad, 0.1, 0.1, 0.1, rgb[0], rgb[1], rgb[2], 0.9);
        }
    };

    api.drawImpactBurst <- function(c, rgb, size) {
        local n = 16;
        for (local i = 0; i < n; ++i) {
            local a = i * 0.4 + c.seed * 0.001;
            local e = this.takeOrb();
            if (e == null) break;
            local u = smooth(clamp01(c.t / fmax(c.durations.impact, 0.05)));
            local rad = (0.3 + 1.1 * u) * size;
            showProp(e, c.toX + cos(a) * rad, c.toY + 0.15 + (i % 4) * 0.12 * (1.0 - u),
                c.toZ + sin(a) * rad * 0.85, 0.08 * size * (1.1 - 0.5 * u), 0.08 * size, 0.08 * size,
                rgb[0], rgb[1], rgb[2], 1.0 - u * 0.5);
        }
        local core = this.takeOrb();
        if (core != null) {
            local s = (0.35 + 0.55 * (1.0 - clamp01(c.t * 2.0))) * size;
            showProp(core, c.toX, c.toY, c.toZ, s, s, s, rgb[0], rgb[1], rgb[2], 1.0);
        }
    };

    api.drawSpikes <- function(c, rgb, count, height, radius) {
        local n = count.tointeger();
        if (n < 1) n = 1; if (n > 16) n = 16;
        for (local i = 0; i < n; ++i) {
            local a = i * 6.2831853 / n.tofloat() + c.seed * 0.01;
            local sp = this.takeSpike();
            if (sp == null) break;
            local grow = smooth(clamp01(c.t / 0.35));
            if (c.phase == "linger") grow = 1.0 - smooth(clamp01(c.t / fmax(c.durations.linger, 0.1))) * 0.35;
            local h = height * grow;
            showProp(sp, c.toX + cos(a) * radius, h * 0.5, c.toZ + sin(a) * radius,
                0.18, h, 0.18, rgb[0] * 0.85, rgb[1] * 0.8, rgb[2] * 0.75, 1.0);
        }
    };

    api.drawWave <- function(c, rgb, height, width, speed) {
        local u = clamp01(c.age * 0.35 * speed);
        local x = lerp(c.fromX - width * 0.2, c.toX + width * 0.15, u);
        for (local i = 0; i < 12; ++i) {
            local side = (i.tofloat() / 11.0 - 0.5) * width;
            local seg = this.takeSeg();
            if (seg == null) break;
            local h = height * (0.55 + 0.45 * sin(i * 0.7 + this.time * 3.0));
            showProp(seg, x, h * 0.5, c.toZ + side * 0.15, 0.55, h, 0.22, rgb[0], rgb[1], rgb[2], 0.75);
        }
        local foam = this.takeRing();
        if (foam != null) showProp(foam, x, 0.08, c.toZ, width * 0.35, 0.06, width * 0.18, 0.9, 0.95, 1.0, 0.7);
    };

    api.drawPortal <- function(c, rgb, size) {
        local ring = this.takeRing();
        if (ring != null) {
            local s = 1.2 * size * (1.0 + 0.08 * sin(this.time * 5.0));
            showProp(ring, c.toX, 1.2, c.toZ, s, 0.08, s, rgb[0], rgb[1], rgb[2], 0.95);
        }
        local core = this.takeOrb();
        if (core != null) showProp(core, c.toX, 1.2, c.toZ, 0.55 * size, 0.55 * size, 0.55 * size, 0.05, 0.02, 0.08, 0.95);
        for (local i = 0; i < 10; ++i) {
            local a = this.time * 3.0 + i * 0.7;
            local e = this.takeOrb();
            if (e == null) break;
            local rad = 0.7 * size;
            showProp(e, c.toX + cos(a) * rad, 1.2 + sin(a * 1.7) * 0.25, c.toZ + sin(a) * rad,
                0.07, 0.07, 0.07, rgb[0], rgb[1], rgb[2], 0.9);
        }
    };

    api.drawSwarm <- function(c, rgb, count, radius, speed) {
        local n = (18.0 * count).tointeger();
        if (n < 6) n = 6; if (n > 36) n = 36;
        for (local i = 0; i < n; ++i) {
            local a = this.time * 2.2 * speed + i * 0.9 + c.seed * 0.01;
            local e = this.takeOrb();
            if (e == null) break;
            local rad = radius * (0.6 + 0.4 * sin(a * 1.3 + i));
            local y = c.toY + sin(a * 2.1 + i) * 0.55 * radius;
            showProp(e, c.toX + cos(a) * rad, y, c.toZ + sin(a) * rad, 0.07, 0.07, 0.07, rgb[0], rgb[1], rgb[2], 0.95);
        }
    };

    api.onPhase <- function(c, phase) {
        local rgb = this.spellRgb(c);
        local intensity = ("intensity" in c.params) ? c.params.intensity : (("glow" in c.params && typeof c.params.glow != "string") ? c.params.glow : 1.0);
        if (typeof intensity != "float" && typeof intensity != "integer") intensity = 1.0;
        if (phase == "impact") {
            local size = ("size" in c.params) ? c.params.size : (("blast" in c.params) ? c.params.blast : 1.0);
            this.pulseBurst(c.toX, c.toY, c.toZ, intensity * size, rgb);
            if (c.spell.element == "lightning" || c.spell.id == "frozen-lightning" || c.spell.id == "thunderstorm") {
                local jag = ("jag" in c.params) ? c.params.jag : 0.12;
                local branches = ("branches" in c.params) ? c.params.branches.tointeger() : 5;
                this.triggerBolt(c.toX, c.toY + 7.0, c.toZ, c.toX, 0.1, c.toZ, 0.12 + 0.06 * intensity, jag, branches, rgb);
            }
        }
    };

    api.onUpdate <- function(c, dt) {
        // Visuals are rebuilt each tick from a shared pool; nothing stored per cast except data hooks.
        if (!("accent" in c.data)) c.data.accent <- 0.0;
        c.data.accent += dt;
    };

    api.tick <- function(dt) {
        this.time += dt;
        this.resetCursors();
        foreach (e in this.orbs) hideProp(e);
        foreach (e in this.segs) hideProp(e);
        foreach (e in this.spikes) hideProp(e);
        foreach (e in this.rings) hideProp(e);
        foreach (e in this.columns) hideProp(e);

        // Draw all alive casts after hide — borrow props in cast order.
        // Caller must iterate casts; we expose drawCast.
    };

    api.drawCast <- function(c) {
        if (!c.alive) return;
        local p = c.params;
        local rgb = this.spellRgb(c);
        local size = ("size" in p) ? p.size : 1.0;
        local intensity = 1.0;
        if ("intensity" in p && (typeof p.intensity == "float" || typeof p.intensity == "integer")) intensity = p.intensity;
        else if ("glow" in p && (typeof p.glow == "float" || typeof p.glow == "integer")) intensity = p.glow;
        local strength = ("strength" in p) ? p.strength : intensity;

        if (c.phase == "charge") {
            this.drawCharge(c, rgb, size * (0.8 + 0.4 * intensity));
            return;
        }

        local id = c.spell.id;
        local form = c.form;
        local shape = c.shape;

        if (id == "lightning" || shape == "strike") {
            this.drawStrike(c, rgb);
            if (form == "Chain Arc" && (c.phase == "travel" || c.phase == "impact")) {
                this.drawBeam(c, rgb, 0.8 + 0.5 * intensity);
                this.triggerBolt(c.fromX, c.fromY, c.fromZ, c.toX, c.toY, c.toZ, 0.12, ("jag" in p) ? p.jag : 0.1, ("branches" in p) ? p.branches.tointeger() : 4, rgb);
            }
            return;
        }

        if (id == "ball-lightning") {
            if (c.phase == "travel" || c.phase == "linger") {
                this.drawProjectile(c, rgb, size);
                if ((c.age * 3.0).tointeger() % 2 == 0) {
                    local a = c.seed * 0.02 + c.age;
                    this.triggerBolt(c.posX, c.posY, c.posZ, c.posX + cos(a) * p.reach * 0.2, 0.05, c.posZ + sin(a) * p.reach * 0.2,
                        0.08, 0.15, 3, rgb);
                }
            } else if (c.phase == "impact") this.drawImpactBurst(c, rgb, size * p.blast);
            return;
        }

        if (id == "thunderstorm") {
            local h = ("height" in p) ? p.height : 9.0;
            local r = ("radius" in p) ? p.radius * 0.12 : 1.0;
            this.drawAreaColumn(c, rgb, h * 0.35, r, ("spin" in p) ? p.spin : 1.0);
            if (c.phase == "impact" || c.phase == "linger") {
                if ((c.age * p.strikes * 2.0).tointeger() != ((c.age - 0.016) * p.strikes * 2.0).tointeger()) {
                    local a = c.seed + c.age * 7.0;
                    local tx = c.toX + cos(a) * r * 2.5, tz = c.toZ + sin(a) * r * 2.5;
                    this.triggerBolt(tx, h * 0.5, tz, tx, 0.05, tz, 0.14, p.jag, p.branches.tointeger(), rgb);
                }
            }
            return;
        }

        if (id == "fire" && this.hdFireSkip) return;
        if (id == "fire" && form == "Inferno") {
            local h = 2.8 * (("height" in p) ? p.height : 1.0) * size;
            this.drawAreaColumn(c, rgb, h, 0.55 * size, 1.4);
            if (c.phase == "impact") this.drawImpactBurst(c, rgb, size);
            return;
        }
        if (id == "fire" || id == "phoenix" || id == "solar-flare") {
            if (c.phase == "travel") this.drawProjectile(c, rgb, size * (id == "phoenix" ? 1.35 : 1.0));
            else if (c.phase == "impact" || c.phase == "linger") this.drawImpactBurst(c, rgb, size);
            if (id == "phoenix" && c.phase == "travel") {
                // Wing ribbons.
                for (local i = 0; i < 6; ++i) {
                    local side = (i % 2 == 0) ? 1.0 : -1.0;
                    local seg = this.takeSeg();
                    if (seg == null) break;
                    local flap = sin(this.time * 10.0 * p.flap + i) * 0.35 * p.swing;
                    showProp(seg, c.posX + side * (0.25 + i * 0.08), c.posY + flap, c.posZ,
                        0.35, 0.05, 0.12, rgb[0], rgb[1], rgb[2], 0.85);
                }
            }
            return;
        }

        if (id == "ember-swarm" || id == "plague-swarm") {
            local rad = ("radius" in p) ? p.radius : 1.1;
            local count = ("count" in p) ? p.count : 1.0;
            this.drawSwarm(c, rgb, count, rad * size, ("speed" in p) ? p.speed : 1.0);
            return;
        }

        if (id == "water") {
            if (form == "Geyser") {
                local h = ("height" in p) ? p.height : 4.5;
                this.drawAreaColumn(c, rgb, h * size, 0.35 * size, 0.8);
            } else {
                if (c.phase == "travel" || c.phase == "impact") this.drawBeam(c, rgb, size * (0.7 + 0.5 * p.whip));
            }
            return;
        }
        if (id == "tsunami") {
            this.drawWave(c, rgb, p.height * 0.22, p.width * 0.12, p.speed);
            return;
        }

        if (id == "ice") {
            if (form == "Ice Wall") {
                for (local i = 0; i < 7; ++i) {
                    local sp = this.takeSpike();
                    if (sp == null) break;
                    local side = (i - 3).tofloat() * 0.35 * size;
                    local h = (1.2 + (i % 3) * 0.45) * size * smooth(clamp01(c.t + 0.2));
                    showProp(sp, c.toX + side * c.dirZ, h * 0.5, c.toZ - side * c.dirX, 0.2, h, 0.2, rgb[0], rgb[1], rgb[2], 0.95);
                }
            } else {
                if (c.phase == "travel") this.drawProjectile(c, rgb, size);
                else this.drawSpikes(c, rgb, 8.0, 1.6 * size, 0.55 * size);
            }
            return;
        }
        if (id == "absolute-zero") {
            local dome = this.takeOrb();
            if (dome != null) {
                local s = 1.8 * size * smooth(clamp01(c.age * 0.6));
                showProp(dome, c.toX, 1.0, c.toZ, s, s * 0.85, s, rgb[0], rgb[1], rgb[2], 0.35);
            }
            this.drawSpikes(c, rgb, 10.0, 1.2 * size, 1.1 * size);
            return;
        }
        if (id == "blizzard") {
            this.drawSwarm(c, rgb, 1.2, (("radius" in p) ? p.radius : 2.5) * 0.35, 1.3);
            local col = this.takeCol();
            if (col != null) showProp(col, c.toX, 1.6, c.toZ, 1.4, 3.2, 1.4, rgb[0], rgb[1], rgb[2], 0.25);
            return;
        }

        if (id == "earth") {
            if (form == "Upheaval") this.drawSpikes(c, rgb, ("spikes" in p) ? p.spikes : 7.0, ("height" in p) ? p.height : 2.4, 0.7 * size);
            else {
                if (c.phase == "travel") this.drawProjectile(c, rgb, ("meteorSize" in p) ? p.meteorSize : size);
                else this.drawImpactBurst(c, rgb, size * 1.3);
            }
            return;
        }
        if (id == "fissure") {
            local reach = ("reach" in p) ? p.reach : 9.0;
            local segsN = 10;
            for (local i = 0; i < segsN; ++i) {
                local u = i.tofloat() / (segsN - 1).tofloat();
                local appear = smooth(clamp01(c.age * 1.5 - u * 0.8));
                if (appear <= 0.02) continue;
                local seg = this.takeSeg();
                if (seg == null) break;
                local x = lerp(c.fromX, c.fromX + c.dirX * reach, u);
                local z = lerp(c.fromZ, c.fromZ + c.dirZ * reach, u);
                showProp(seg, x, 0.04, z, 0.55, 0.08 * p.width * 2.0, p.width, 1.0, 0.25, 0.05, appear);
            }
            return;
        }
        if (id == "lava-geyser") {
            this.drawAreaColumn(c, rgb, p.height * 0.45, p.width * 2.5, 1.2);
            if (c.phase == "impact") this.drawImpactBurst(c, rgb, size);
            return;
        }

        if (id == "wind" || id == "dust-devils" || id == "fire-tornado") {
            local h = 3.2 * size * (("height" in p) ? p.height : 1.0);
            local spin = ("spin" in p) ? p.spin : (("strength" in p) ? p.strength : 1.0);
            if (form == "Wind Slash") {
                if (c.phase == "travel") this.drawBeam(c, rgb, ("slashSize" in p) ? p.slashSize : size);
                else this.drawImpactBurst(c, rgb, size * 0.7);
            } else {
                this.drawAreaColumn(c, rgb, h, 0.7 * size * strength, spin);
                if (id == "dust-devils") {
                    for (local k = 0; k < 3; ++k) {
                        local a = this.time * 0.7 + k * 2.1;
                        local ox = c.toX + cos(a) * 1.4 * (("spread" in p) ? p.spread : 1.0);
                        local oz = c.toZ + sin(a) * 1.4 * (("spread" in p) ? p.spread : 1.0);
                        local col = this.takeCol();
                        if (col != null) showProp(col, ox, h * 0.35, oz, 0.35, h * 0.7, 0.35, rgb[0], rgb[1], rgb[2], 0.4);
                    }
                }
            }
            return;
        }

        if (id == "void" || id == "black-flame") {
            if (form == "Rift" || id == "black-flame") {
                this.drawAreaColumn(c, rgb, 2.4 * size * (("height" in p) ? p.height : 1.0), 0.55 * size, 1.5);
                local core = this.takeOrb();
                if (core != null) showProp(core, c.toX, 1.0, c.toZ, 0.35 * size, 0.55 * size, 0.35 * size, 0.02, 0.0, 0.05, 0.95);
            } else {
                if (c.phase == "travel") this.drawProjectile(c, rgb, size);
                else this.drawPortal(c, rgb, size);
            }
            return;
        }
        if (id == "portal") { this.drawPortal(c, rgb, size); return; }

        if (id == "light") {
            if (form == "Radiant Lance" || shape == "beam") this.drawBeam(c, rgb, ("lance" in p) ? p.lance * 2.0 : 1.0);
            else {
                local ring = this.takeRing();
                if (ring != null) {
                    local s = (("sigil" in p) ? p.sigil : 1.5) * (0.8 + 0.4 * intensity);
                    showProp(ring, c.toX, 0.08, c.toZ, s, 0.05, s, rgb[0], rgb[1], rgb[2], 0.9);
                }
                this.drawAreaColumn(c, rgb, 4.0 * intensity, 0.35, 0.4);
            }
            return;
        }
        if (id == "aurora-veil") {
            for (local i = 0; i < 8; ++i) {
                local seg = this.takeSeg();
                if (seg == null) break;
                local x = c.toX + (i - 3.5) * 0.55;
                local h = 2.5 + sin(this.time * 1.5 + i) * 0.8;
                showProp(seg, x, h * 0.5, c.toZ, 0.12, h, 0.35, rgb[0], rgb[1], rgb[2], 0.55);
            }
            return;
        }
        if (id == "supernova") {
            if (c.phase == "charge" || c.age < c.durations.charge + 0.4) {
                local s = 1.2 * size * (1.0 - clamp01(c.age * 0.4));
                local e = this.takeOrb();
                if (e != null) showProp(e, c.toX, 1.3, c.toZ, fmax(s, 0.15), fmax(s, 0.15), fmax(s, 0.15), rgb[0], rgb[1], rgb[2], 1.0);
            } else this.drawImpactBurst(c, rgb, size * (("blast" in p) ? p.blast : 1.0) * 1.6);
            return;
        }

        if (id == "acid") {
            if (form == "Corrosion Cloud") this.drawAreaColumn(c, rgb, 2.2, 1.1 * size, 0.7);
            else {
                if (c.phase == "travel") this.drawProjectile(c, rgb, size);
                else this.drawImpactBurst(c, rgb, size);
            }
            return;
        }

        if (id == "arcane") {
            if (form == "Aegis") {
                local ring = this.takeRing();
                if (ring != null) showProp(ring, c.fromX, 1.1, c.fromZ, p.shield, 0.08, p.shield, rgb[0], rgb[1], rgb[2], 0.85);
                for (local i = 0; i < 6; ++i) {
                    local a = this.time * 2.0 + i * 1.05;
                    local e = this.takeOrb();
                    if (e == null) break;
                    showProp(e, c.fromX + cos(a) * p.shield * 0.55, 1.1 + sin(a * 2.0) * 0.2, c.fromZ + sin(a) * p.shield * 0.55,
                        0.08, 0.08, 0.08, rgb[0], rgb[1], rgb[2], 1.0);
                }
            } else {
                local n = ("missiles" in p) ? p.missiles.tointeger() : 8;
                if (n > 12) n = 12;
                for (local i = 0; i < n; ++i) {
                    local u = clamp01(c.age * 0.7 - i * 0.05);
                    local e = this.takeOrb();
                    if (e == null) break;
                    local x = lerp(c.fromX, c.toX, u) + sin(i + this.time * p.weave) * 0.25 * p.spread;
                    local y = lerp(c.fromY, c.toY, u) + cos(i * 1.3) * 0.2;
                    local z = lerp(c.fromZ, c.toZ, u) + cos(i + this.time * p.weave) * 0.25 * p.spread;
                    showProp(e, x, y, z, 0.1, 0.1, 0.1, rgb[0], rgb[1], rgb[2], 1.0);
                }
            }
            return;
        }
        if (id == "starfall") {
            local n = ("stars" in p) ? p.stars.tointeger() : 12;
            if (n > 20) n = 20;
            for (local i = 0; i < n; ++i) {
                local delay = i.tofloat() / n.tofloat() * p.duration;
                local u = clamp01((c.age - delay) * (p.speed * 0.05));
                if (u <= 0.0) continue;
                local a = c.seed * 0.01 + i * 1.7;
                local tx = c.toX + cos(a) * p.radius * 0.25;
                local tz = c.toZ + sin(a) * p.radius * 0.25;
                local e = this.takeOrb();
                if (e == null) break;
                local y = lerp(p.height, 0.3, u);
                showProp(e, lerp(tx - 1.5, tx, u), y, lerp(tz - 1.0, tz, u), 0.1 * p.size, 0.1 * p.size, 0.1 * p.size, rgb[0], rgb[1], rgb[2], 1.0);
                local trail = this.takeSeg();
                if (trail != null) showProp(trail, lerp(tx - 1.5, tx, u), y + 0.3, lerp(tz - 1.0, tz, u), 0.05, 0.45 * p.trail * 0.1, 0.05, rgb[0], rgb[1], rgb[2], 0.7);
            }
            return;
        }

        if (id == "frozen-lightning") {
            this.drawStrike(c, rgb);
            if (c.phase == "linger") this.drawSpikes(c, rgb, 8.0, 1.3 * size, 0.7 * size);
            return;
        }

        // Generic fallback by shape.
        if (shape == "projectile") {
            if (c.phase == "travel") this.drawProjectile(c, rgb, size);
            else if (c.phase == "impact" || c.phase == "linger") this.drawImpactBurst(c, rgb, size);
        } else if (shape == "beam") {
            this.drawBeam(c, rgb, size);
        } else if (shape == "area" || shape == "disk" || shape == "capsule") {
            this.drawAreaColumn(c, rgb, 2.5 * size, 0.7 * size, 1.0);
            if (c.phase == "impact") this.drawImpactBurst(c, rgb, size);
        } else if (shape == "self") {
            local ring = this.takeRing();
            if (ring != null) showProp(ring, c.fromX, 1.0, c.fromZ, 1.2 * size, 0.06, 1.2 * size, rgb[0], rgb[1], rgb[2], 0.85);
        }
    };

    api.drawAll <- function(casts) {
        foreach (c in casts) this.drawCast(c);
    };

    api.updateBolts <- function(dt) {
        foreach (b in this.bolts) checked(b.update(dt), "bolt update");
    };

    api.updateParticles <- function(dt) {
        if (this.particles != null) this.particles.update(dt);
    };

    api.renderParticles <- function() {
        if (this.particles != null) this.particles.render(this.gfx);
    };

    return api;
}

return { create = createPresenter, hexRgb = hexRgb, elementHue = elementHue };
