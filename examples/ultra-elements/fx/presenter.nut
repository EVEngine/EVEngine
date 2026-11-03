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
    for (local i = 0; i < 128; ++i) {
        local e = eve.Renderable3D(); e.setMesh(sphere); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.35); hideProp(e); orbs.push(e);
    }
    for (local i = 0; i < 96; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cube); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.4); hideProp(e); segs.push(e);
    }
    for (local i = 0; i < 48; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cylinder); e.setReceiveLight(true); e.setCastShadow(false);
        e.setRoughness(0.85); hideProp(e); spikes.push(e);
    }
    for (local i = 0; i < 28; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cylinder); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.5); hideProp(e); rings.push(e);
    }
    for (local i = 0; i < 24; ++i) {
        local e = eve.Renderable3D(); e.setMesh(cylinder); e.setReceiveLight(false); e.setCastShadow(false);
        e.setRoughness(0.45); hideProp(e); columns.push(e);
    }

    local bolts = [];
    if (weatherMod != null) {
        for (local i = 0; i < 8; ++i) {
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

    // Tapered funnel (cyclone / dust devil / lava) — helix only, never a solid cake spine.
    api.drawFunnel <- function(cx, cz, height, baseR, topR, rgb, swirl, alpha) {
        for (local i = 0; i < 32; ++i) {
            local u = i.tofloat() / 31.0;
            local y = u * height;
            local r = lerp(baseR, topR, u);
            local a = this.time * 3.2 * swirl + u * 10.0;
            local e = this.takeOrb();
            if (e != null) showProp(e, cx + cos(a) * r, y, cz + sin(a) * r,
                0.11 + 0.06 * (1.0 - u), 0.11 + 0.06 * (1.0 - u), 0.11 + 0.06 * (1.0 - u),
                rgb[0], rgb[1], rgb[2], 0.8 + 0.15 * alpha);
            if (i % 2 == 0) {
                local a2 = a + 2.1;
                local e2 = this.takeOrb();
                if (e2 != null) showProp(e2, cx + cos(a2) * r * 0.72, y, cz + sin(a2) * r * 0.72,
                    0.08, 0.08, 0.08, rgb[0], rgb[1], rgb[2], 0.8);
            }
            if (i % 3 == 0) {
                local seg = this.takeSeg();
                if (seg != null) showProp(seg, cx + cos(a + 0.4) * r * 0.9, y, cz + sin(a + 0.4) * r * 0.9,
                    0.4, 0.07, 0.09, rgb[0], rgb[1], rgb[2], 0.75);
            }
        }
        local ring = this.takeRing();
        if (ring != null) showProp(ring, cx, 0.06, cz, baseR * 1.35, 0.05, baseR * 1.35, rgb[0], rgb[1], rgb[2], 0.8);
        local ring2 = this.takeRing();
        if (ring2 != null) showProp(ring2, cx, height * 0.92, cz, topR * 1.6, 0.04, topR * 1.6, rgb[0], rgb[1], rgb[2], 0.5);
    };

    // Thick continuous beam with tip splash (water / radiant lance).
    api.drawThickBeam <- function(c, rgb, width, wobble) {
        local steps = 22;
        for (local i = 0; i < steps; ++i) {
            local u = (i.tofloat() + 0.5) / steps.tofloat();
            local seg = this.takeSeg();
            if (seg == null) break;
            local x = lerp(c.fromX, c.toX, u);
            local y = lerp(c.fromY, c.toY, u) + sin(u * 9.0 + this.time * 7.0) * 0.04 * wobble;
            local z = lerp(c.fromZ, c.toZ, u);
            local thick = 0.11 * width * (0.75 + 0.35 * sin(u * 14.0 + this.time * 9.0));
            showProp(seg, x, y, z, thick * 2.2, thick * 1.1, thick * 1.1, rgb[0], rgb[1], rgb[2], 0.98);
        }
        local tip = this.takeOrb();
        if (tip != null) showProp(tip, c.toX, c.toY, c.toZ, 0.28 * width, 0.28 * width, 0.28 * width, 1.0, 1.0, 1.0, 1.0);
        for (local i = 0; i < 8; ++i) {
            local a = this.time * 6.0 + i * 0.8;
            local e = this.takeOrb();
            if (e == null) break;
            showProp(e, c.toX + cos(a) * 0.35 * width, c.toY + sin(a * 1.4) * 0.2, c.toZ + sin(a) * 0.35 * width,
                0.07, 0.07, 0.07, rgb[0], rgb[1], rgb[2], 0.85);
        }
    };

    // Vertical portal / rift disc facing roughly +Z (ring of segs).
    api.drawVerticalRing <- function(x, y, z, radius, rgb, spin, darkCore) {
        local n = 16;
        for (local i = 0; i < n; ++i) {
            local a = i.tofloat() / n.tofloat() * 6.2831853 + spin;
            local seg = this.takeSeg();
            if (seg == null) break;
            showProp(seg, x + cos(a) * radius, y + sin(a) * radius, z, 0.14, 0.14, 0.08, rgb[0], rgb[1], rgb[2], 0.95);
        }
        local rim = this.takeRing();
        if (rim != null) showProp(rim, x, y, z, radius * 1.05, 0.1, radius * 1.05, rgb[0], rgb[1], rgb[2], 0.75);
        if (darkCore) {
            local core = this.takeOrb();
            if (core != null) showProp(core, x, y, z, radius * 0.72, radius * 0.72, radius * 0.35, 0.02, 0.01, 0.04, 0.95);
        } else {
            local core = this.takeOrb();
            if (core != null) showProp(core, x, y, z, radius * 0.35, radius * 0.35, radius * 0.2, rgb[0], rgb[1], rgb[2], 0.9);
        }
    };

    // Crescent slash (wind slash).
    api.drawCrescent <- function(c, rgb, size) {
        local n = 14;
        for (local i = 0; i < n; ++i) {
            local u = i.tofloat() / (n - 1).tofloat();
            local a = -1.1 + u * 2.2;
            local seg = this.takeSeg();
            if (seg == null) break;
            local px = c.posX + cos(a) * 0.9 * size * c.dirZ + sin(a) * 0.15;
            local py = c.posY + sin(a) * 0.85 * size;
            local pz = c.posZ - cos(a) * 0.9 * size * c.dirX;
            showProp(seg, px, py, pz, 0.55 * size, 0.07, 0.12, rgb[0], rgb[1], rgb[2], 0.9);
        }
        local tip = this.takeOrb();
        if (tip != null) showProp(tip, c.posX, c.posY, c.posZ, 0.2 * size, 0.2 * size, 0.2 * size, 1.0, 1.0, 1.0, 1.0);
    };

    api.onPhase <- function(c, phase) {
        local rgb = this.spellRgb(c);
        local intensity = ("intensity" in c.params) ? c.params.intensity : (("glow" in c.params && typeof c.params.glow != "string") ? c.params.glow : 1.0);
        if (typeof intensity != "float" && typeof intensity != "integer") intensity = 1.0;
        if (phase == "impact") {
            local id = c.spell.id;
            // Skip generic white burst for spells whose silhouette is the whole read.
            local skipBurst = (id == "black-flame" || id == "aegis" || id == "portal" || id == "thunderstorm"
                || id == "judgment" || (id == "light" && c.form == "Judgment"));
            if (!skipBurst) {
                local size = ("size" in c.params) ? c.params.size : (("blast" in c.params) ? c.params.blast : 1.0);
                local bx = (id == "aegis") ? c.fromX : c.toX;
                local by = (id == "aegis") ? c.fromY : c.toY;
                local bz = (id == "aegis") ? c.fromZ : c.toZ;
                this.pulseBurst(bx, by, bz, intensity * size, rgb);
            }
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
            local px = (c.phase == "impact" || c.phase == "linger") ? c.toX : c.posX;
            local py = (c.phase == "impact" || c.phase == "linger") ? c.toY : c.posY;
            local pz = (c.phase == "impact" || c.phase == "linger") ? c.toZ : c.posZ;
            if (c.phase != "charge") {
                local core = this.takeOrb();
                if (core != null) showProp(core, px, py, pz, 0.48 * size, 0.48 * size, 0.48 * size, 1.0, 1.0, 1.0, 1.0);
                local shell = this.takeOrb();
                if (shell != null) showProp(shell, px, py, pz, 0.85 * size, 0.85 * size, 0.85 * size, rgb[0], rgb[1], rgb[2], 0.5);
                local boltsN = ("bolts" in p) ? fmin(p.bolts, 10.0).tointeger() : 7;
                for (local i = 0; i < boltsN; ++i) {
                    local a = c.seed * 0.02 + this.time * 4.0 + i * 0.9;
                    local reach = (("reach" in p) ? p.reach : 5.5) * 0.28;
                    local bx = px + cos(a) * reach, bz = pz + sin(a) * reach;
                    this.triggerBolt(px, py, pz, bx, 0.05, bz, 0.11, 0.18, 4, rgb);
                    // Persistent arc stubs (readable while paused — bolts are edge-triggered).
                    for (local k = 0; k < 4; ++k) {
                        local t = (k + 0.5) / 4.0;
                        local seg = this.takeSeg();
                        if (seg == null) break;
                        showProp(seg, lerp(px, bx, t), lerp(py, 0.12, t) + sin(t * 8.0 + a) * 0.12,
                            lerp(pz, bz, t), 0.1, 0.06, 0.06, rgb[0], rgb[1], rgb[2], 0.95);
                    }
                    local spark = this.takeOrb();
                    if (spark != null) showProp(spark, bx, 0.15 + fabs(sin(a)) * 0.45, bz, 0.1, 0.1, 0.1, 1.0, 1.0, 1.0, 1.0);
                }
                if (c.phase == "impact" || c.phase == "linger") this.drawImpactBurst(c, rgb, size * (("blast" in p) ? p.blast : 1.0));
            }
            return;
        }

        if (id == "thunderstorm") {
            local h = ("height" in p) ? p.height : 9.0;
            local r = (("radius" in p) ? p.radius : 9.0) * 0.18;
            // Cloud deck.
            for (local i = 0; i < 5; ++i) {
                local a = i * 1.25 + this.time * 0.2;
                local col = this.takeCol();
                if (col == null) break;
                showProp(col, c.toX + cos(a) * r * 0.55, h * 0.42, c.toZ + sin(a) * r * 0.55,
                    r * 0.55, 0.55, r * 0.55, 0.35, 0.4, 0.48, 0.55);
            }
            // Rain sheets.
            local rain = ("rain" in p) ? p.rain : 1.0;
            for (local i = 0; i < 18; ++i) {
                local a = c.seed * 0.01 + i * 0.7;
                local seg = this.takeSeg();
                if (seg == null) break;
                local rx = c.toX + cos(a) * r * (0.3 + (i % 5) * 0.12);
                local rz = c.toZ + sin(a) * r * (0.3 + (i % 4) * 0.14);
                showProp(seg, rx, h * 0.22, rz, 0.04, h * 0.35 * rain, 0.04, 0.55, 0.7, 0.95, 0.45);
            }
            // Persistent strikes (works while paused).
            local strikes = ("strikes" in p) ? p.strikes : 1.0;
            local nBolt = fmax(2.0, fmin(4.0, strikes * 3.0)).tointeger();
            for (local i = 0; i < nBolt; ++i) {
                local a = c.seed + i * 2.3 + (this.time * 2.0).tointeger().tofloat() * 0.1;
                local tx = c.toX + cos(a) * r * 0.85, tz = c.toZ + sin(a) * r * 0.85;
                this.triggerBolt(tx, h * 0.55, tz, tx, 0.05, tz, 0.14, ("jag" in p) ? p.jag : 0.12,
                    ("branches" in p) ? p.branches.tointeger() : 7, rgb);
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
        if (id == "phoenix") {
            local px = (c.phase == "impact" || c.phase == "linger") ? c.toX : c.posX;
            local py = (c.phase == "impact" || c.phase == "linger") ? c.toY : c.posY;
            local pz = (c.phase == "impact" || c.phase == "linger") ? c.toZ : c.posZ;
            if (c.phase != "charge") {
                local body = this.takeOrb();
                if (body != null) showProp(body, px, py, pz, 0.32 * size, 0.26 * size, 0.4 * size, rgb[0], rgb[1], rgb[2], 1.0);
                local glow = this.takeOrb();
                if (glow != null) showProp(glow, px, py, pz, 0.55 * size, 0.45 * size, 0.6 * size, 1.0, 0.55, 0.12, 0.45);
                local head = this.takeOrb();
                if (head != null) showProp(head, px + c.dirX * 0.32 * size, py + 0.1, pz + c.dirZ * 0.32 * size,
                    0.16 * size, 0.16 * size, 0.16 * size, 1.0, 0.95, 0.7, 1.0);
                local feathers = ("feathers" in p) ? p.feathers : 1.0;
                local flap = ("flap" in p) ? p.flap : 1.0;
                local swing = ("swing" in p) ? p.swing : 1.0;
                for (local i = 0; i < 16; ++i) {
                    local side = (i % 2 == 0) ? 1.0 : -1.0;
                    local seg = this.takeSeg();
                    if (seg == null) break;
                    local wing = sin(this.time * 10.0 * flap + i) * 0.55 * swing;
                    showProp(seg, px + side * (0.2 + i * 0.06) * feathers,
                        py + wing - i * 0.015, pz - c.dirZ * i * 0.025,
                        0.5 * size, 0.06, 0.16 * size, 1.0, 0.45 + 0.35 * (i % 3) / 3.0, 0.08, 0.95);
                    local ember = this.takeOrb();
                    if (ember != null) showProp(ember, px + side * (0.35 + i * 0.05) * feathers,
                        py + wing * 0.8, pz, 0.08, 0.08, 0.08, 1.0, 0.7, 0.2, 0.9);
                }
                local tail = ("tail" in p) ? p.tail : 1.0;
                for (local i = 1; i <= 12; ++i) {
                    local t = i.tofloat() / 12.0;
                    local e = this.takeOrb();
                    if (e == null) break;
                    showProp(e, px - c.dirX * t * 1.6 * tail * size, py - 0.04 * i,
                        pz - c.dirZ * t * 1.6 * tail * size,
                        0.14 * size * (1.0 - t * 0.55), 0.14 * size * (1.0 - t * 0.55), 0.14 * size * (1.0 - t * 0.55),
                        1.0, 0.4 + 0.3 * (1.0 - t), 0.05, 0.95 - t * 0.45);
                }
            }
            if (c.phase == "impact" || c.phase == "linger") this.drawImpactBurst(c, rgb, size * 1.35);
            return;
        }
        if (id == "fire" || id == "solar-flare") {
            if (c.phase == "travel") this.drawProjectile(c, rgb, size * (id == "solar-flare" ? 1.25 : 1.0));
            else if (c.phase == "impact" || c.phase == "linger") this.drawImpactBurst(c, rgb, size);
            if (id == "solar-flare" && c.phase == "travel") {
                for (local i = 0; i < 8; ++i) {
                    local a = this.time * 5.0 + i * 0.8;
                    local e = this.takeOrb();
                    if (e == null) break;
                    showProp(e, c.posX + cos(a) * 0.35 * size, c.posY + sin(a * 1.3) * 0.2, c.posZ + sin(a) * 0.35 * size,
                        0.08, 0.08, 0.08, 1.0, 0.9, 0.5, 0.9);
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
                this.drawAreaColumn(c, rgb, h * size, 0.4 * size, 0.9);
                for (local i = 0; i < 12; ++i) {
                    local a = this.time * 4.0 + i * 0.55;
                    local e = this.takeOrb();
                    if (e == null) break;
                    showProp(e, c.toX + cos(a) * 0.55 * size, h * size * (0.4 + 0.5 * (i % 5) / 5.0),
                        c.toZ + sin(a) * 0.55 * size, 0.1, 0.1, 0.1, 0.85, 0.95, 1.0, 0.85);
                }
            } else {
                // Tidal Lance — thick watery beam.
                if (c.phase == "travel" || c.phase == "impact" || c.phase == "linger") {
                    local whip = ("whip" in p) ? p.whip : 1.0;
                    this.drawThickBeam(c, rgb, size * (1.1 + 0.6 * whip), whip);
                }
            }
            return;
        }
        if (id == "tsunami") {
            local h = (("height" in p) ? p.height : 6.0) * 0.4;
            local w = (("width" in p) ? p.width : 13.0) * 0.2;
            local u = clamp01(c.age * 0.4 * (("speed" in p) ? p.speed : 1.0));
            local frontX = lerp(c.fromX - w * 0.15, c.toX + w * 0.2, u);
            // Curved face: stacked thinner slabs leaning forward.
            for (local i = 0; i < 7; ++i) {
                local col = this.takeCol();
                if (col == null) break;
                local layer = i.tofloat() / 6.0;
                local yh = h * (0.35 + 0.65 * layer);
                local lean = layer * 0.55;
                showProp(col, frontX - lean, yh * 0.5, c.toZ, w * (1.1 - layer * 0.25), yh, 0.35 + layer * 0.25,
                    rgb[0], rgb[1], rgb[2], 0.45 + 0.08 * i);
            }
            this.drawWave(c, rgb, h * 0.7, w * 0.85, ("speed" in p) ? p.speed : 1.0);
            for (local i = 0; i < 14; ++i) {
                local e = this.takeOrb();
                if (e == null) break;
                showProp(e, frontX + (i - 7) * 0.16, h * 0.95 + sin(i + this.time * 5.0) * 0.2, c.toZ + sin(i * 0.7) * 0.15,
                    0.16, 0.16, 0.16, 0.92, 0.97, 1.0, 0.9);
            }
            return;
        }

        if (id == "ice") {
            if (form == "Ice Wall") {
                for (local i = 0; i < 9; ++i) {
                    local sp = this.takeSpike();
                    if (sp == null) break;
                    local side = (i - 4).tofloat() * 0.38 * size;
                    local h = (1.4 + (i % 3) * 0.55) * size * smooth(clamp01(c.t + 0.25));
                    showProp(sp, c.toX + side * c.dirZ, h * 0.5, c.toZ - side * c.dirX, 0.22, h, 0.22, rgb[0], rgb[1], rgb[2], 0.95);
                }
            } else {
                // Glacial Spike — oversized crystal projectile + impact spikes.
                if (c.phase == "travel") {
                    local spike = this.takeSpike();
                    if (spike != null) showProp(spike, c.posX, c.posY, c.posZ, 0.28 * size, 1.1 * size, 0.28 * size, rgb[0], rgb[1], rgb[2], 1.0);
                    local core = this.takeOrb();
                    if (core != null) showProp(core, c.posX, c.posY, c.posZ, 0.22 * size, 0.22 * size, 0.22 * size, 0.85, 0.95, 1.0, 1.0);
                    for (local i = 1; i <= 6; ++i) {
                        local t = i.tofloat() / 6.0;
                        local e = this.takeOrb();
                        if (e == null) break;
                        showProp(e, c.posX - c.dirX * t * 0.9, c.posY - c.dirY * t * 0.9, c.posZ - c.dirZ * t * 0.9,
                            0.1 * (1.0 - t), 0.1 * (1.0 - t), 0.1 * (1.0 - t), rgb[0], rgb[1], rgb[2], 0.8);
                    }
                } else {
                    this.drawSpikes(c, rgb, ("spikes" in p) ? p.spikes : 10.0, 2.0 * size, 0.7 * size);
                    this.drawImpactBurst(c, rgb, size * 0.9);
                }
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
                // Meteor — glowing rock with heat trail + crater.
                local ms = ("meteorSize" in p) ? p.meteorSize : size;
                if (c.phase == "travel") {
                    local rock = this.takeOrb();
                    if (rock != null) showProp(rock, c.posX, c.posY, c.posZ, 0.45 * ms, 0.45 * ms, 0.45 * ms, 0.55, 0.35, 0.2, 1.0);
                    local glow = this.takeOrb();
                    if (glow != null) showProp(glow, c.posX, c.posY, c.posZ, 0.65 * ms, 0.65 * ms, 0.65 * ms, 1.0, 0.45, 0.08, 0.55);
                    for (local i = 1; i <= 10; ++i) {
                        local t = i.tofloat() / 10.0;
                        local e = this.takeOrb();
                        if (e == null) break;
                        showProp(e, c.posX - c.dirX * t * 1.6, c.posY - c.dirY * t * 1.6 + t * 0.2, c.posZ - c.dirZ * t * 1.6,
                            0.2 * ms * (1.0 - t), 0.2 * ms * (1.0 - t), 0.2 * ms * (1.0 - t), 1.0, 0.4 + 0.3 * t, 0.05, 0.85 - t * 0.5);
                    }
                } else {
                    this.drawImpactBurst(c, rgb, size * 1.5);
                    this.drawSpikes(c, rgb, 6.0, 1.2 * size, 0.9 * size);
                    local crater = this.takeRing();
                    if (crater != null) showProp(crater, c.toX, 0.06, c.toZ, 1.4 * size, 0.08, 1.4 * size, 0.35, 0.22, 0.12, 0.85);
                }
            }
            return;
        }
        if (id == "fissure") {
            local reach = ("reach" in p) ? p.reach : 9.0;
            local width = ("width" in p) ? p.width : 0.42;
            local segsN = 14;
            for (local i = 0; i < segsN; ++i) {
                local u = i.tofloat() / (segsN - 1).tofloat();
                local appear = smooth(clamp01(c.age * 1.8 - u * 0.65));
                if (appear <= 0.02) continue;
                local x = lerp(c.fromX, c.fromX + c.dirX * reach, u);
                local z = lerp(c.fromZ, c.fromZ + c.dirZ * reach, u);
                local seg = this.takeSeg();
                if (seg != null) showProp(seg, x, 0.05, z, 0.7, 0.1, width * 2.2, 1.0, 0.28, 0.05, appear);
                // Magma glow orbs along cut.
                local e = this.takeOrb();
                if (e != null) showProp(e, x, 0.12, z, 0.12, 0.12, 0.12, 1.0, 0.45, 0.08, appear);
                // Lip slabs.
                if (i % 2 == 0) {
                    local lip = this.takeSpike();
                    if (lip != null) {
                        local side = (i % 4 == 0) ? 1.0 : -1.0;
                        local lh = (("lipHeight" in p) ? p.lipHeight : 0.4) * 2.5 * appear;
                        showProp(lip, x + c.dirZ * side * width * 2.5, lh * 0.5, z - c.dirX * side * width * 2.5,
                            0.2, lh, 0.35, 0.45, 0.3, 0.2, appear);
                    }
                }
            }
            return;
        }
        if (id == "lava-geyser") {
            local h = (("height" in p) ? p.height : 6.0) * 0.55;
            local w = (("width" in p) ? p.width : 0.4) * 3.0;
            this.drawFunnel(c.toX, c.toZ, h, w * 1.1, w * 0.35, [1.0, 0.35, 0.05], 1.4, 0.7);
            local rim = this.takeRing();
            if (rim != null) showProp(rim, c.toX, 0.08, c.toZ, w * 2.2, 0.1, w * 2.2, 1.0, 0.25, 0.05, 0.9);
            for (local i = 0; i < 14; ++i) {
                local a = this.time * 3.0 + i * 0.5;
                local e = this.takeOrb();
                if (e == null) break;
                showProp(e, c.toX + cos(a) * w * 0.8, h * (0.3 + 0.6 * (i % 5) / 5.0), c.toZ + sin(a) * w * 0.8,
                    0.12, 0.12, 0.12, 1.0, 0.55, 0.1, 0.9);
            }
            return;
        }

        if (id == "fire-tornado") {
            local h = 4.2 * size * (("height" in p) ? p.height : 1.0);
            local spin = ("spin" in p) ? p.spin : 1.0;
            // Helix-only funnel (no solid spine) so it reads as fire swirl, not a yellow cake.
            local ring = this.takeRing();
            if (ring != null) showProp(ring, c.toX, 0.06, c.toZ, 1.1 * size, 0.08, 1.1 * size, 1.0, 0.4, 0.05, 0.95);
            for (local i = 0; i < 36; ++i) {
                local u = i.tofloat() / 35.0;
                local a = this.time * 7.0 * spin + u * 16.0;
                local rad = lerp(0.95, 0.16, u) * size;
                local e = this.takeOrb();
                if (e == null) break;
                showProp(e, c.toX + cos(a) * rad, u * h, c.toZ + sin(a) * rad,
                    0.14 + 0.06 * (1.0 - u), 0.14 + 0.06 * (1.0 - u), 0.14 + 0.06 * (1.0 - u),
                    1.0, 0.45 + 0.4 * (1.0 - u), 0.06, 1.0);
                if (i % 2 == 0) {
                    local a2 = a + 2.4;
                    local e2 = this.takeOrb();
                    if (e2 != null) showProp(e2, c.toX + cos(a2) * rad * 0.7, u * h, c.toZ + sin(a2) * rad * 0.7,
                        0.09, 0.09, 0.09, 1.0, 0.75, 0.25, 0.9);
                }
                if (i % 3 == 0) {
                    local seg = this.takeSeg();
                    if (seg != null) showProp(seg, c.toX + cos(a + 0.3) * rad * 0.85, u * h, c.toZ + sin(a + 0.3) * rad * 0.85,
                        0.4, 0.07, 0.09, 1.0, 0.5, 0.08, 0.85);
                }
            }
            return;
        }

        if (id == "wind" || id == "dust-devils") {
            local h = 3.4 * size * (("height" in p) ? p.height : 1.0);
            local spin = ("spin" in p) ? p.spin : strength;
            if (form == "Wind Slash") {
                if (c.phase == "travel") this.drawCrescent(c, rgb, (("slashSize" in p) ? p.slashSize : size) * 1.3);
                else this.drawImpactBurst(c, [0.85, 0.95, 0.9], size * 0.9);
            } else if (id == "dust-devils") {
                local dustRgb = [0.72, 0.58, 0.38];
                for (local k = 0; k < 3; ++k) {
                    local a = this.time * 0.8 + k * 2.1;
                    local spread = ("spread" in p) ? p.spread : 1.0;
                    local ox = c.toX + cos(a) * 1.5 * spread;
                    local oz = c.toZ + sin(a) * 1.5 * spread;
                    this.drawFunnel(ox, oz, h * 0.75, 0.55 * size, 0.18 * size, dustRgb, 1.3 + k * 0.2, 0.55);
                }
            } else {
                this.drawFunnel(c.toX, c.toZ, h, 0.9 * size * strength, 0.25 * size, [0.75, 0.88, 0.85], spin, 0.5);
            }
            return;
        }

        if (id == "black-flame") {
            local h = 3.0 * size * (("height" in p) ? p.height : 1.0);
            local rim = ("rim" in p && typeof p.rim == "string") ? hexRgb(p.rim) : [1.0, 0.35, 0.12];
            // Charcoal absorbing column + hot rim helix (no white burst — skipBurst handles impact).
            for (local i = 0; i < 5; ++i) {
                local u = i.tofloat() / 4.0;
                local col = this.takeCol();
                if (col == null) break;
                local r = lerp(0.75, 0.3, u) * size;
                showProp(col, c.toX, h * 0.5, c.toZ, r * (1.0 - u * 0.15), h * 0.95, r * (1.0 - u * 0.15),
                    0.05 + u * 0.02, 0.04, 0.055, 0.98);
            }
            for (local i = 0; i < 22; ++i) {
                local u = i.tofloat() / 21.0;
                local a = this.time * 4.2 + u * 12.0;
                local e = this.takeOrb();
                if (e == null) break;
                local rad = lerp(0.65, 0.2, u) * size;
                showProp(e, c.toX + cos(a) * rad, u * h, c.toZ + sin(a) * rad,
                    0.11, 0.11, 0.11, rim[0], rim[1], rim[2], 1.0);
                if (i % 3 == 0) {
                    local seg = this.takeSeg();
                    if (seg != null) showProp(seg, c.toX + cos(a + 0.5) * rad * 0.9, u * h, c.toZ + sin(a + 0.5) * rad * 0.9,
                        0.28, 0.07, 0.07, rim[0], rim[1], rim[2], 0.85);
                }
            }
            local core = this.takeOrb();
            if (core != null) showProp(core, c.toX, h * 0.4, c.toZ, 0.4 * size, 0.55 * size, 0.4 * size, 0.01, 0.0, 0.015, 1.0);
            local baseRing = this.takeRing();
            if (baseRing != null) showProp(baseRing, c.toX, 0.06, c.toZ, 1.05 * size, 0.08, 1.05 * size, rim[0], rim[1], rim[2], 0.95);
            return;
        }

        if (id == "void") {
            if (form == "Rift") {
                this.drawVerticalRing(c.toX, 1.4, c.toZ, 1.0 * size, rgb, this.time * 1.5, true);
                for (local i = 0; i < 8; ++i) {
                    local a = this.time * 2.0 + i;
                    local seg = this.takeSeg();
                    if (seg == null) break;
                    showProp(seg, c.toX + cos(a) * 0.2, 0.4 + i * 0.28, c.toZ + sin(a) * 0.15,
                        0.08, 0.35, 0.08, rgb[0], rgb[1], rgb[2], 0.7);
                }
            } else {
                // Singularity — dark core + accretion disk.
                if (c.phase == "travel") {
                    local core = this.takeOrb();
                    if (core != null) showProp(core, c.posX, c.posY, c.posZ, 0.28 * size, 0.28 * size, 0.28 * size, 0.02, 0.0, 0.05, 1.0);
                    local glow = this.takeOrb();
                    if (glow != null) showProp(glow, c.posX, c.posY, c.posZ, 0.5 * size, 0.5 * size, 0.5 * size, rgb[0], rgb[1], rgb[2], 0.45);
                    for (local i = 0; i < 10; ++i) {
                        local a = this.time * 4.0 + i * 0.63;
                        local e = this.takeOrb();
                        if (e == null) break;
                        showProp(e, c.posX + cos(a) * 0.55 * size, c.posY + sin(a * 0.7) * 0.12, c.posZ + sin(a) * 0.55 * size,
                            0.07, 0.07, 0.07, 1.0, 0.7, 0.35, 0.9);
                    }
                } else {
                    this.drawVerticalRing(c.toX, 1.2, c.toZ, 1.15 * size, rgb, this.time * 2.2, true);
                    this.drawImpactBurst(c, rgb, size * 0.8);
                }
            }
            return;
        }
        if (id == "portal") {
            this.drawVerticalRing(c.toX, 1.45, c.toZ, 1.25 * size, rgb, this.time * 2.4, true);
            // Outer runes + swirl into the void.
            for (local i = 0; i < 18; ++i) {
                local a = this.time * 3.5 + i * 0.35;
                local e = this.takeOrb();
                if (e == null) break;
                local rad = 1.15 * size * (0.55 + 0.45 * ((i % 3).tofloat() / 2.0));
                showProp(e, c.toX + cos(a) * rad, 1.45 + sin(a * 1.7) * 0.4 * size, c.toZ + sin(a) * 0.2,
                    0.08, 0.08, 0.08, rgb[0], rgb[1], rgb[2], 0.95);
            }
            for (local i = 0; i < 8; ++i) {
                local a = i * 0.785 + this.time * 0.6;
                local seg = this.takeSeg();
                if (seg == null) break;
                showProp(seg, c.toX + cos(a) * 1.35 * size, 1.45, c.toZ + sin(a) * 0.15,
                    0.35, 0.08, 0.08, 0.7, 0.35, 1.0, 0.85);
            }
            return;
        }

        if (id == "light") {
            if (form == "Radiant Lance" || (shape == "beam" && form != "Judgment")) {
                local lance = ("lance" in p) ? p.lance : 0.6;
                this.drawThickBeam(c, rgb, 1.4 + lance * 2.2, 0.6);
            } else {
                // Judgment — gold sky-pillar + ground sigil. Drawn from charge onward so
                // long charges / paused MCP captures still read as Judgment.
                local jx = c.toX - 0.35, jz = c.toZ + 0.2;
                local build = (c.phase == "charge") ? smooth(clamp01(c.t / fmax(0.15, c.durations.charge))) : 1.0;
                local sig = (("sigil" in p) ? p.sigil : 1.5) * (1.15 + 0.35 * intensity) * (0.55 + 0.45 * build);
                local pillarH = 7.2 * build;
                local ring = this.takeRing();
                if (ring != null) showProp(ring, jx, 0.08, jz, sig, 0.1, sig, 1.0, 0.88, 0.35, 1.0);
                local ring2 = this.takeRing();
                if (ring2 != null) showProp(ring2, jx, 0.16, jz, sig * 0.58, 0.08, sig * 0.58, 1.0, 0.95, 0.65, 1.0);
                // Hex spokes on ground.
                local spokes = ("rays" in p) ? fmax(6.0, p.rays * 10.0).tointeger() : 8;
                if (spokes > 12) spokes = 12;
                for (local i = 0; i < spokes; ++i) {
                    local a = i.tofloat() / spokes.tofloat() * 6.2831853 + (("tilt" in p) ? p.tilt : 0.0) * 0.01;
                    local seg = this.takeSeg();
                    if (seg == null) break;
                    showProp(seg, jx + cos(a) * sig * 0.45, 0.1, jz + sin(a) * sig * 0.45,
                        sig * 0.55, 0.06, 0.12, 1.0, 0.9, 0.4, 0.95 * build);
                }
                // Opaque gold core column + bright outer sheath.
                local core = this.takeCol();
                if (core != null) showProp(core, jx, pillarH * 0.5, jz, 0.38 * size, pillarH, 0.38 * size, 1.0, 0.92, 0.45, 1.0);
                local sheath = this.takeCol();
                if (sheath != null) showProp(sheath, jx, pillarH * 0.5, jz, 0.62 * size, pillarH * 0.95, 0.62 * size, 1.0, 0.78, 0.25, 0.55);
                // Falling ray segments (readable even if columns fail).
                for (local i = 0; i < 14; ++i) {
                    local u = i.tofloat() / 13.0;
                    local a = this.time * 1.2 + i * 0.55;
                    local seg = this.takeSeg();
                    if (seg == null) break;
                    showProp(seg, jx + cos(a) * 0.12, pillarH * (1.0 - u) + 0.2, jz + sin(a) * 0.12,
                        0.18, 0.65, 0.18, 1.0, 0.95, 0.7, 1.0);
                }
                // Impact crown + motes.
                local crown = this.takeOrb();
                if (crown != null) showProp(crown, jx, 0.35, jz, 0.55 * size * build, 0.35 * size * build, 0.55 * size * build, 1.0, 0.95, 0.7, 1.0);
                local motes = ("motes" in p) ? p.motes : 1.0;
                local nMote = fmax(8.0, motes * 12.0).tointeger();
                if (nMote > 18) nMote = 18;
                for (local i = 0; i < nMote; ++i) {
                    local a = this.time * 1.8 + i * 0.7;
                    local e = this.takeOrb();
                    if (e == null) break;
                    showProp(e, jx + cos(a) * sig * 0.5, 0.2 + (i % 5) * 0.45 * build, jz + sin(a) * sig * 0.5,
                        0.11, 0.11, 0.11, 1.0, 0.95, 0.55, 1.0);
                }
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
            local blast = (("blast" in p) ? p.blast : 1.0) * size;
            local u = smooth(clamp01(c.age * 0.55));
            local core = this.takeOrb();
            if (core != null) showProp(core, c.toX, 1.3, c.toZ, 0.7 * size * (1.25 - u * 0.35), 0.7 * size * (1.25 - u * 0.35), 0.7 * size * (1.25 - u * 0.35), 1.0, 0.95, 0.75, 1.0);
            local shell = this.takeOrb();
            if (shell != null) {
                local s = (0.8 + 2.8 * u) * blast;
                showProp(shell, c.toX, 1.3, c.toZ, s, s, s, rgb[0], rgb[1], rgb[2], 0.4 * (1.0 - u * 0.45));
            }
            local outer = this.takeOrb();
            if (outer != null) {
                local s2 = (1.2 + 3.4 * u) * blast;
                showProp(outer, c.toX, 1.3, c.toZ, s2, s2 * 0.85, s2, 1.0, 0.7, 0.35, 0.22);
            }
            local rays = ("rays" in p) ? p.rays : 1.0;
            for (local i = 0; i < 16; ++i) {
                local a = i * 0.393 + this.time * 0.5;
                local seg = this.takeSeg();
                if (seg == null) break;
                local rad = (0.6 + u * 2.2) * blast;
                showProp(seg, c.toX + cos(a) * rad, 1.3 + sin(a * 2.0) * 0.15, c.toZ + sin(a) * rad,
                    0.75 * rays, 0.1, 0.1, 1.0, 0.85, 0.45, 0.95);
            }
            this.drawImpactBurst(c, rgb, blast * 1.4);
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
                local shield = ("shield" in p) ? p.shield : 1.45;
                // Clear magenta dome around caster (self), not the training dummy.
                local dome = this.takeOrb();
                if (dome != null) showProp(dome, c.fromX, 1.2, c.fromZ, shield * 0.95, shield * 0.8, shield * 0.95, rgb[0], rgb[1], rgb[2], 0.38);
                local inner = this.takeOrb();
                if (inner != null) showProp(inner, c.fromX, 1.2, c.fromZ, shield * 0.55, shield * 0.5, shield * 0.55, 1.0, 0.7, 1.0, 0.25);
                local ring = this.takeRing();
                if (ring != null) showProp(ring, c.fromX, 0.12, c.fromZ, shield * 1.05, 0.1, shield * 1.05, rgb[0], rgb[1], rgb[2], 1.0);
                local ring2 = this.takeRing();
                if (ring2 != null) showProp(ring2, c.fromX, 1.85, c.fromZ, shield * 0.7, 0.08, shield * 0.7, rgb[0], rgb[1], rgb[2], 0.95);
                // Hex lattice on the dome surface.
                for (local i = 0; i < 18; ++i) {
                    local a = this.time * 1.6 + i * 0.35;
                    local e = this.takeOrb();
                    if (e == null) break;
                    local y = 0.35 + fabs(sin(a * 0.85 + i)) * 1.85;
                    showProp(e, c.fromX + cos(a) * shield * 0.82, y, c.fromZ + sin(a) * shield * 0.82,
                        0.1, 0.1, 0.1, rgb[0], rgb[1], rgb[2], 1.0);
                    if (i % 2 == 0) {
                        local seg = this.takeSeg();
                        if (seg != null) showProp(seg, c.fromX + cos(a) * shield * 0.78, y, c.fromZ + sin(a) * shield * 0.78,
                            0.28, 0.07, 0.07, rgb[0], rgb[1], rgb[2], 0.9);
                    }
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
