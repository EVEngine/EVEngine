// Ultra Elements → EVEngine parametric spell VFX lab.
// Catalog params/ranges/presets mirror Humpalumps/ultra-elements; presentation is native.

local catalogMod = loadfile("fx/catalog.nut")();
local runtimeMod = loadfile("fx/runtime.nut")();
local presenterMod = loadfile("fx/presenter.nut")();
local fireHdMod = loadfile("fx/fire_hd.nut")();

persist camera = null
persist particlesMod = null
persist presenter = null
persist runtime = null
persist fireHd = null
persist entries = null
persist elements = null
persist entryIndex = 0
persist elementFilter = "all"
persist autoCast = true
persist autoClock = 0.0
persist paused = false
persist orbit = 0.55
persist lastSpace = false
persist castCount = 0
persist statusLine = "ready"
persist uiBuilt = false
persist paramKeys = null
persist dummy = null
persist casterOrb = null
persist keyEdge = null
persist labTime = 0.0

function isFireHdActive() {
    local e = currentEntry();
    return e != null && e.spell.id == "fire";
}

FOCUS <- { x = 0.0, y = 1.25, z = 0.0 };
TARGET <- { x = 3.6, y = 1.15, z = 0.15 };

function buildEntries(cat) {
    local list = [];
    foreach (spell in cat.spells) {
        local forms = spell.forms;
        foreach (form in forms) {
            list.push({ spell = spell, form = form, element = spell.element, name = form });
        }
    }
    return list;
}

function filteredEntries() {
    if (elementFilter == "all") return entries;
    local out = [];
    foreach (e in entries) if (e.element == elementFilter) out.push(e);
    return out;
}

function currentEntry() {
    local list = filteredEntries();
    if (list.len() == 0) return entries[0];
    if (entryIndex < 0) entryIndex = 0;
    if (entryIndex >= list.len()) entryIndex = list.len() - 1;
    return list[entryIndex];
}

function rebuildParamUI() {
    local e = currentEntry();
    local spell = e.spell;
    paramKeys = [];
    ui.select("ultra");
    // Rebuild window content for live knobs.
    ui.beginBuild();
    ui.beginWindow("ULTRA ELEMENTS", "root");
    ui.text("ULTRA ELEMENTS", "title");
    ui.text("PARAMETRIC SPELL VFX LAB", "subtitle");
    local hdNote = (e.spell.id == "fire") ? "  [HD VOLUME ~ original]" : "";
    ui.text(e.element + "  /  " + e.name + hdNote, "spell");
    ui.beginRow("nav", 5.0);
    ui.button("Prev [Q]", "prev"); ui.button("Cast [Space]", "cast"); ui.button("Next [E]", "next");
    ui.end();
    ui.beginRow("transport", 5.0);
    ui.button(autoCast ? "Auto: ON" : "Auto: OFF", "auto");
    ui.button(paused ? "Paused" : "Pause", "pause");
    ui.button("Clear", "clear");
    ui.end();
    ui.beginRow("elements1", 4.0);
    ui.button("All", "el_all"); ui.button("Lightning", "el_lightning"); ui.button("Fire", "el_fire"); ui.button("Water", "el_water");
    ui.end();
    ui.beginRow("elements2", 4.0);
    ui.button("Ice", "el_ice"); ui.button("Earth", "el_earth"); ui.button("Wind", "el_wind"); ui.button("Void", "el_void");
    ui.end();
    ui.beginRow("elements3", 4.0);
    ui.button("Light", "el_light"); ui.button("Acid", "el_acid"); ui.button("Arcane", "el_arcane"); ui.button("Hybrid", "el_hybrid");
    ui.end();
    if (spell.presets.len() > 0) {
        ui.beginRow("presets", 4.0);
        for (local i = 0; i < spell.presets.len() && i < 4; ++i) {
            ui.button(spell.presets[i].name, "preset_" + i);
        }
        ui.end();
        ui.button("Reset", "reset");
    } else {
        ui.button("Reset params", "reset");
    }
    ui.separator("s0");
    ui.text("Live parameters", "paramsHdr");
    foreach (k, range in spell.ranges) {
        local cur = (k in spell.params) ? spell.params[k].tofloat() : range[0];
        ui.slider(k, cur, range[0], range[1], "p_" + k);
        paramKeys.push(k);
    }
    ui.slider("Orbit", orbit, -3.14, 3.14, "orbit");
    ui.separator("s1");
    ui.text("", "status");
    ui.textWrapped("Sliders write spell.params live. Space casts. 1-0 / H filter elements. Q/E step spells.", 300.0, "help");
    ui.end();
    ui.mountBuildAs("ultra");
    ui.select("ultra");
    ui.setHostPos(10.0, 10.0, 0.0, 0.0);
    ui.setHostSize(340.0, 860.0);
    ui.setHostOverlay(true);
    uiBuilt = true;
}

function syncParamsFromUI() {
    if (!uiBuilt || paramKeys == null) return;
    local spell = currentEntry().spell;
    ui.select("ultra");
    foreach (k in paramKeys) {
        spell.params[k] = ui.getValue("p_" + k);
    }
    orbit = ui.getValue("orbit");
}

function applyPreset(index) {
    local spell = currentEntry().spell;
    if (index < 0 || index >= spell.presets.len()) return;
    local values = spell.presets[index].values;
    foreach (k, v in values) spell.params[k] <- v;
    rebuildParamUI();
    statusLine = "preset=" + spell.presets[index].name;
}

function resetParams() {
    // Reload defaults from a fresh catalog clone of this spell id.
    local fresh = loadfile("fx/catalog.nut")();
    local spell = currentEntry().spell;
    foreach (s in fresh.spells) {
        if (s.id == spell.id) {
            foreach (k, v in s.params) spell.params[k] <- v;
            break;
        }
    }
    rebuildParamUI();
    statusLine = "params reset";
}

function castCurrent(held = false) {
    syncParamsFromUI();
    local e = currentEntry();
    local c = runtime.cast(e.spell, e.form, FOCUS, TARGET, { held = held, seed = castCount * 131 + 7 });
    castCount += 1;
    statusLine = "cast " + e.name + " #" + castCount + " shape=" + c.shape;
    print("ULTRA_ELEMENTS_CAST id=" + e.spell.id + " form=" + e.form + " element=" + e.element + " params=" + e.spell.params.len() + "\n");
}

function stepSpell(delta) {
    local list = filteredEntries();
    if (list.len() == 0) return;
    entryIndex = (entryIndex + delta) % list.len();
    if (entryIndex < 0) entryIndex += list.len();
    rebuildParamUI();
    autoClock = 0.15;
}

function setElement(id) {
    elementFilter = id;
    entryIndex = 0;
    rebuildParamUI();
    statusLine = "filter=" + id;
}

function prop(mesh, x, y, z, sx, sy, sz, r, g, b, lit = true) {
    local e = eve.Renderable3D();
    e.setMesh(mesh);
    e.setPosition(x, y, z);
    e.setScale(sx, sy, sz);
    e.setTint(r, g, b, 1.0);
    e.setReceiveLight(lit);
    e.setRoughness(0.7);
    e.setCastShadow(lit);
    return e;
}

function buildArena() {
    local cube = gfx.newMeshCube(1.0);
    local sphere = gfx.newMeshSphere(20, 12);
    local cylinder = gfx.newMeshCylinder(48, 1, true);
    prop(cylinder, 1.5, -0.18, 0.0, 8.5, 0.28, 8.5, 0.05, 0.07, 0.10);
    prop(cylinder, 1.5, -0.02, 0.0, 7.4, 0.03, 7.4, 0.08, 0.11, 0.15);
    for (local i = -4; i <= 6; ++i) {
        prop(cube, i.tofloat(), 0.01, 0.0, 0.015, 0.015, 9.0, 0.14, 0.20, 0.26, false);
        prop(cube, 1.5, 0.012, i.tofloat() - 1.0, 9.0, 0.015, 0.015, 0.14, 0.20, 0.26, false);
    }
    // Caster pedestal + focus orb.
    prop(cylinder, FOCUS.x, 0.35, FOCUS.z, 0.55, 0.7, 0.55, 0.12, 0.16, 0.22);
    casterOrb = prop(sphere, FOCUS.x, FOCUS.y, FOCUS.z, 0.18, 0.18, 0.18, 0.75, 0.85, 1.0, false);
    // Training dummy.
    dummy = prop(cylinder, TARGET.x, 1.0, TARGET.z, 0.35, 2.0, 0.35, 0.55, 0.2, 0.75);
    prop(sphere, TARGET.x, 2.15, TARGET.z, 0.28, 0.28, 0.28, 0.9, 0.9, 0.92);
    prop(cube, TARGET.x, 2.22, TARGET.z + 0.22, 0.22, 0.05, 0.05, 0.95, 0.95, 1.0, false);
    // Pillars for depth read.
    for (local i = 0; i < 5; ++i) {
        local a = i * 0.9 + 0.4;
        prop(cube, cos(a) * 6.2 + 1.2, 1.1, sin(a) * 5.5, 0.4, 2.2, 0.4, 0.12, 0.18, 0.24);
    }
    camera = eve.Camera3D();
    camera.setTarget(1.4, 1.2, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(43.0);
    camera.setAmbient(0.10, 0.13, 0.18);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 0.55, 0.62, 0.75);
    gfx.setBackgroundColor(0.015, 0.022, 0.038, 1.0);
}

eve_init <- function() {
    if ("dev" in eve) { eve.dev.console.setVisible(false); eve.dev.ai.setVisible(false); }
    if (!gfx.getRenderControl().setLightingMode("forwardPlus")) throw "Forward lighting unavailable";
    local cat = catalogMod;
    elements = cat.elements;
    entries = buildEntries(cat);
    if (entries.len() != 39) throw "expected 39 spell entries, got " + entries.len();

    particlesMod = eve.Particles();
    // Root slot from config.modules (includes weather).
    local weatherMod = weather;
    keyEdge = {};
    presenter = presenterMod.create(gfx, particlesMod, weatherMod);
    presenter.setScreen(config.width.tofloat(), config.height.tofloat());
    runtime = runtimeMod.create(presenter);
    fireHd = fireHdMod.create(gfx);
    fireHd.setScreen(config.width.tofloat(), config.height.tofloat(), 350.0);

    buildArena();
    ui.setTheme("dark");
    ui.setScale(0.82);
    rebuildParamUI();

    print("ULTRA_ELEMENTS_READY spells=" + entries.len() + " elements=" + elements.len() + " bolts=" + presenter.bolts.len() + " fireHd=1\n");
};

eve_update <- function(dt) {
    if (camera == null) return;
    ui.select("ultra");
    local click = ui.consumeClick();
    while (click != "") {
        if (click == "ultra/cast") castCurrent();
        else if (click == "ultra/prev") stepSpell(-1);
        else if (click == "ultra/next") stepSpell(1);
        else if (click == "ultra/auto") { autoCast = !autoCast; rebuildParamUI(); }
        else if (click == "ultra/pause") { paused = !paused; rebuildParamUI(); }
        else if (click == "ultra/clear") { runtime.clear(); statusLine = "cleared"; }
        else if (click == "ultra/reset") resetParams();
        else if (click.find("ultra/preset_") == 0) {
            local idx = click.slice("ultra/preset_".len()).tointeger();
            applyPreset(idx);
        }
        else if (click.find("ultra/el_") == 0) {
            local id = click.slice("ultra/el_".len());
            setElement(id);
        }
        click = ui.consumeClick();
    }

    // Keyboard: Space cast, Q/E step, 1-0 / H element, F auto, R reset.
    local space = keyboard.isDown("SPACE");
    if (space && !lastSpace) castCurrent(true);
    if (!space && lastSpace) runtime.releaseHeld();
    lastSpace = space;
    local keys = ["Q", "E", "F", "R", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "H"];
    foreach (name in keys) {
        local down = keyboard.isDown(name);
        local was = (name in keyEdge) ? keyEdge[name] : false;
        keyEdge[name] <- down;
        if (!(down && !was)) continue;
        if (name == "Q") stepSpell(-1);
        else if (name == "E") stepSpell(1);
        else if (name == "F") { autoCast = !autoCast; rebuildParamUI(); }
        else if (name == "R") resetParams();
        else if (name == "1") setElement("lightning");
        else if (name == "2") setElement("fire");
        else if (name == "3") setElement("water");
        else if (name == "4") setElement("ice");
        else if (name == "5") setElement("earth");
        else if (name == "6") setElement("wind");
        else if (name == "7") setElement("void");
        else if (name == "8") setElement("light");
        else if (name == "9") setElement("acid");
        else if (name == "0") setElement("arcane");
        else if (name == "H") setElement("hybrid");
    }

    syncParamsFromUI();
    local hd = isFireHdActive();
    presenter.hdFireSkip = hd;
    if (!paused) {
        labTime += dt;
        if (autoCast) {
            autoClock -= dt;
            if (autoClock <= 0.0) {
                castCurrent();
                stepSpell(1);
                autoClock = 2.4;
            }
        }
        runtime.update(dt);
        presenter.updateBolts(dt);
        presenter.updateParticles(dt);
    } else {
        // Pause freezes phase clocks, but still rebuild the mesh pool so
        // MCP cast-while-paused / mid-charge captures stay readable.
        presenter.tick(0.0);
    }
    // Always redraw alive casts (HD fire path clears mesh props).
    if (!hd) presenter.drawAll(runtime.list);
    else presenter.drawAll([]);

    local e = currentEntry();
    if (hd && fireHd != null) {
        fireHd.pickAndSync(runtime.list, e.spell, e.name, orbit, labTime);
    }

    local eyeX = 1.4 + cos(orbit) * 12.5;
    local eyeZ = 0.0 + sin(orbit) * 12.5;
    camera.setEye(eyeX, 7.0, eyeZ);
    camera.setTarget(1.4, 1.15, 0.0);

    local tag = hd ? " HD~orig" : "";
    ui.setText("status", statusLine + " | alive=" + runtime.aliveCount() + " | " + e.name + tag);
    if (casterOrb != null) {
        local pulse = 0.16 + 0.03 * sin(labTime * 3.0);
        casterOrb.setScale(pulse, pulse, pulse);
    }
};

eve_render <- function() {
    gfx.clear();
    if (isFireHdActive() && fireHd != null) {
        fireHd.render(gfx);
    } else {
        gfx.render3D();
        if (presenter != null) presenter.renderParticles();
    }
    ui.select("ultra");
    ui.beginFrameAndRender();
};
