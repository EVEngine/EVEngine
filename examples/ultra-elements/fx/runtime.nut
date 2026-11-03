// Cast pool + phase clock (Ultra Elements Casts port).
// Phases: charge -> release -> travel -> impact -> linger -> done
// Spells keep per-cast state in c.data. Params are borrowed from the live lab table.

MAX_CASTS <- 12;

function cloneParams(src) {
    local dst = {};
    foreach (k, v in src) dst[k] <- v;
    return dst;
}

function makeRecord(index) {
    return {
        index = index,
        alive = false,
        spell = null,
        form = "",
        shape = "projectile",
        phase = "done",
        t = 0.0,
        age = 0.0,
        seed = 0,
        fromX = 0.0, fromY = 1.2, fromZ = 0.0,
        toX = 3.4, toY = 1.1, toZ = 0.0,
        posX = 0.0, posY = 1.2, posZ = 0.0,
        velX = 0.0, velY = 0.0, velZ = 0.0,
        dirX = 1.0, dirY = 0.0, dirZ = 0.0,
        held = false,
        travelled = 0.0,
        target = -1,
        data = {},
        params = {},
        durations = { charge = 0.5, travel = 2.0, impact = 0.2, linger = 2.0 }
    };
}

function createRuntime(presenter) {
    local list = [];
    for (local i = 0; i < MAX_CASTS; ++i) list.push(makeRecord(i));
    local api = {
        presenter = presenter,
        list = list,
        last = null,
        castCount = 0
    };

    api.enter <- function(c, phase) {
        c.phase = phase;
        c.t = 0.0;
        this.presenter.onPhase(c, phase);
        if (phase == "done") {
            c.alive = false;
            this.presenter.releaseCast(c);
        }
    };

    api.cast <- function(spell, form, from, to, opts = null) {
        local held = false;
        local seed = null;
        local target = -1;
        if (opts != null) {
            if ("held" in opts) held = opts.held;
            if ("seed" in opts) seed = opts.seed;
            if ("target" in opts) target = opts.target;
        }
        local c = null;
        foreach (x in this.list) if (!x.alive) { c = x; break; }
        if (c == null) {
            c = this.list[0];
            foreach (x in this.list) if (x.age > c.age) c = x;
            this.enter(c, "done");
        }
        c.alive = true;
        c.spell = spell;
        c.form = form;
        c.shape = spell.shape;
        // Form-specific shape overrides for founding modules.
        if (spell.id == "fire" && form == "Inferno") c.shape = "area";
        if (spell.id == "water" && form == "Geyser") c.shape = "area";
        if (spell.id == "ice" && form == "Ice Wall") c.shape = "area";
        if (spell.id == "earth" && form == "Upheaval") c.shape = "area";
        if (spell.id == "wind" && form == "Cyclone") c.shape = "area";
        if (spell.id == "void" && form == "Rift") c.shape = "area";
        if (spell.id == "light" && form == "Judgment") c.shape = "area";
        if (spell.id == "acid" && form == "Corrosion Cloud") c.shape = "area";
        if (spell.id == "arcane" && form == "Aegis") c.shape = "self";
        if (spell.id == "lightning" && form == "Chain Arc") c.shape = "beam";
        c.t = 0.0;
        c.age = 0.0;
        c.held = held;
        c.travelled = 0.0;
        c.target = target;
        c.seed = seed != null ? seed : (this.castCount * 9973 + 17);
        c.fromX = from.x; c.fromY = from.y; c.fromZ = from.z;
        c.toX = to.x; c.toY = to.y; c.toZ = to.z;
        c.posX = from.x; c.posY = from.y; c.posZ = from.z;
        local dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
        local len = sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-5) { dx = 1.0; dy = 0.0; dz = 0.0; len = 1.0; }
        c.dirX = dx / len; c.dirY = dy / len; c.dirZ = dz / len;
        c.velX = 0.0; c.velY = 0.0; c.velZ = 0.0;
        c.params = cloneParams(spell.params);
        local T = spell.timing;
        c.durations.charge = T.charge;
        c.durations.travel = ("sustain" in T) ? T.sustain : 2.0;
        c.durations.impact = T.impact;
        c.durations.linger = T.linger;
        c.data = {};
        this.presenter.acquireCast(c);
        this.enter(c, "charge");
        this.last = c;
        this.castCount += 1;
        return c;
    };

    api.releaseHeld <- function() {
        foreach (c in this.list) c.held = false;
    };

    api.clear <- function() {
        foreach (c in this.list) if (c.alive) this.enter(c, "done");
    };

    api.aliveCount <- function() {
        local n = 0;
        foreach (c in this.list) if (c.alive) n += 1;
        return n;
    };

    api.update <- function(dt) {
        if (dt < 0.0) throw "ultra-elements runtime rejects negative dt";
        if (dt > 3600.0) throw "ultra-elements runtime rejects huge dt";
        foreach (c in this.list) {
            if (!c.alive) continue;
            c.t += dt;
            c.age += dt;
            local d = c.durations;
            if (c.phase == "charge") {
                c.posX = c.fromX; c.posY = c.fromY; c.posZ = c.fromZ;
                if (c.t >= d.charge) {
                    this.enter(c, "release");
                    local shape = c.shape;
                    if (shape == "projectile") {
                        local speed = ("speed" in c.params) ? c.params.speed : (("slashSpeed" in c.params) ? c.params.slashSpeed : 14.0);
                        c.velX = c.dirX * speed;
                        c.velY = c.dirY * speed;
                        c.velZ = c.dirZ * speed;
                        this.enter(c, "travel");
                    } else if (shape == "beam") {
                        this.enter(c, "travel");
                    } else {
                        if (shape != "self") {
                            c.posX = c.toX; c.posY = c.toY; c.posZ = c.toZ;
                        }
                        this.enter(c, "impact");
                    }
                }
            } else if (c.phase == "travel") {
                if (c.shape == "projectile") {
                    c.posX += c.velX * dt;
                    c.posY += c.velY * dt;
                    c.posZ += c.velZ * dt;
                    c.travelled += sqrt(c.velX * c.velX + c.velY * c.velY + c.velZ * c.velZ) * dt;
                    local rx = c.toX - c.posX, ry = c.toY - c.posY, rz = c.toZ - c.posZ;
                    local toward = rx * c.dirX + ry * c.dirY + rz * c.dirZ;
                    if (toward <= 0.0 || c.travelled > 40.0) {
                        c.posX = c.toX; c.posY = c.toY; c.posZ = c.toZ;
                        this.enter(c, "impact");
                    }
                } else {
                    // Beam / sustained travel.
                    if (!c.held && c.t >= d.travel) this.enter(c, "impact");
                }
            } else if (c.phase == "impact") {
                if (c.t >= d.impact) this.enter(c, "linger");
            } else if (c.phase == "linger") {
                if (c.t >= d.linger) this.enter(c, "done");
            }
            if (c.alive) this.presenter.onUpdate(c, dt);
        }
        this.presenter.tick(dt);
    };

    return api;
}

return { create = createRuntime, cloneParams = cloneParams };
