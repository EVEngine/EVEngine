// Samurai blade arc: track katana root/tip -> fan ribbon -> textured slash glow.
// Root-table slots (<-) stay reachable from MCP eve_run_script for capture helpers.
local trailMod = loadfile("fx/trail.nut")();
trail <- null;
camera <- null;
body <- [];
sword <- [];  // {e, along} along = distance from pivot toward tip
time <- 0.0;
slashAge <- 10.0;
slashId <- 0;
slashKind <- 0;  // 0 horizontal, 1 diagonal, 2 rising
autoClock <- 0.9;
automatic <- true;
paused <- false;
lastSpace <- false;
orbit <- 0.55;
shotCount <- 0;
lastAppend <- "-";
closed <- false;
local PI = 3.14159265358979;

function clamp(v, a, b) { return v < a ? a : (v > b ? b : v); }
function lerp(a, b, t) { return a + (b - a) * t; }
function smooth(t) { t = clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }

function part(mesh, x, y, z, sx, sy, sz, color, lit = true) {
    local e = eve.Renderable3D();
    e.setMesh(mesh);
    e.setPosition(x.tofloat(), y.tofloat(), z.tofloat());
    e.setScale(sx.tofloat(), sy.tofloat(), sz.tofloat());
    e.setTint(color[0].tofloat(), color[1].tofloat(), color[2].tofloat(), 1.0);
    e.setReceiveLight(lit);
    e.setRoughness(0.62);
    e.setCastShadow(lit);
    return e;
}

function buildScene() {
    local cube = gfx.newMeshCube(1.0);
    local sphere = gfx.newMeshSphere(18, 12);
    local cylinder = gfx.newMeshCylinder(64, 1, true);

    // Dojo floor.
    part(cylinder, 0.0, -0.18, 0.0, 7.2, 0.22, 7.2, [0.10, 0.09, 0.08]);
    part(cylinder, 0.0, -0.02, 0.0, 5.8, 0.03, 5.8, [0.22, 0.16, 0.12]);
    for (local i = -4; i <= 4; ++i) {
        part(cube, i * 0.9, 0.01, 0.0, 0.04, 0.01, 5.4, [0.35, 0.24, 0.16], false);
        part(cube, 0.0, 0.012, i * 0.9, 5.4, 0.01, 0.04, [0.35, 0.24, 0.16], false);
    }
    // Soft lantern posts as depth occluders.
    for (local i = 0; i < 4; ++i) {
        local a = i * 0.5 * PI + 0.4;
        part(cube, cos(a) * 4.6, 0.9, sin(a) * 4.6, 0.22, 1.8, 0.22, [0.18, 0.14, 0.11]);
        part(sphere, cos(a) * 4.6, 1.95, sin(a) * 4.6, 0.28, 0.28, 0.28, [0.95, 0.72, 0.35], false);
    }

    // Stylized samurai (block figure) facing +Z.
    body.push(part(cube, -1.8, 1.05, 0.0, 0.52, 0.78, 0.34, [0.12, 0.14, 0.18]));
    body.push(part(sphere, -1.8, 1.68, 0.0, 0.24, 0.26, 0.24, [0.55, 0.45, 0.38]));
    body.push(part(cube, -1.8, 1.78, 0.08, 0.42, 0.12, 0.34, [0.16, 0.18, 0.22])); // kabuto
    body.push(part(cube, -2.05, 0.42, 0.05, 0.18, 0.72, 0.2, [0.10, 0.11, 0.14]));
    body.push(part(cube, -1.55, 0.42, -0.02, 0.18, 0.72, 0.2, [0.10, 0.11, 0.14]));
    body.push(part(cube, -2.18, 1.15, 0.0, 0.16, 0.55, 0.16, [0.14, 0.16, 0.2])); // left arm
    body.push(part(cube, -1.42, 1.2, 0.05, 0.16, 0.5, 0.16, [0.14, 0.16, 0.2]));  // sword arm

    // Katana parts: `along` is meters from pivot (hand) toward tip.
    sword.push({e = part(cube, 0, 0, 0, 0.07, 0.08, 0.07, [0.18, 0.14, 0.10]), along = 0.0});     // tsuka
    sword.push({e = part(cube, 0, 0, 0, 0.28, 0.04, 0.12, [0.72, 0.58, 0.28]), along = 0.18});    // tsuba
    sword.push({e = part(cube, 0, 0, 0, 0.055, 1.55, 0.02, [0.78, 0.84, 0.90]), along = 1.05});  // blade
    sword.push({e = part(cube, 0, 0, 0, 0.012, 1.52, 0.028, [0.95, 0.98, 1.0], false), along = 1.05}); // edge highlight

    camera = eve.Camera3D();
    camera.setTarget(-0.6, 1.25, 0.0);
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(42.0);
    camera.setAmbient(0.16, 0.14, 0.12);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.35, 0.85, 0.35, 0.55, 0.48, 0.40);
    gfx.setBackgroundColor(0.035, 0.040, 0.055, 1.0);
}

/**
 * Procedural slash pose. Returns pivot/root/tip plus Euler angles for the katana.
 * Engine modelFromTransform is Ry(yaw)*Rx(pitch)*Rz(roll); local +Y is the blade axis.
 * Time is injected slashAge; no wall clock.
 */
function bladeDir(yaw, pitch, roll) {
    local cy = cos(yaw), sy = sin(yaw);
    local cp = cos(pitch), sp = sin(pitch);
    local cr = cos(roll), sr = sin(roll);
    return {
        x = -cy * sr + sy * sp * cr,
        y = cp * cr,
        z = sy * sr + cy * sp * cr
    };
}

function slashPose(kind, age) {
    local duration = 0.42;
    local t = clamp(age / duration, 0.0, 1.0);
    local e = smooth(t);
    local active = age >= 0.0 && age <= duration;
    local pivot = {x = -1.35, y = 1.25, z = 0.15};
    local roll = 0.0, pitch = 0.0, yaw = 0.0;
    if (kind == 0) {
        // Horizontal cut: mostly roll in the view plane, slight yaw for depth.
        roll = lerp(0.85, -2.05, e);
        pitch = lerp(-0.08, 0.22, e);
        yaw = lerp(0.25, -0.35, e);
    } else if (kind == 1) {
        // Diagonal descending cut.
        roll = lerp(1.15, -1.55, e);
        pitch = lerp(-0.55, 0.70, e);
        yaw = lerp(0.15, -0.25, e);
    } else {
        // Rising cut from hip to shoulder.
        roll = lerp(-1.75, 0.95, e);
        pitch = lerp(0.55, -0.40, e);
        yaw = lerp(-0.20, 0.30, e);
    }
    local dir = bladeDir(yaw, pitch, roll);
    local rootAlong = 0.22;
    local tipAlong = 1.82;
    local root = {
        x = pivot.x + dir.x * rootAlong,
        y = pivot.y + dir.y * rootAlong,
        z = pivot.z + dir.z * rootAlong
    };
    local tip = {
        x = pivot.x + dir.x * tipAlong,
        y = pivot.y + dir.y * tipAlong,
        z = pivot.z + dir.z * tipAlong
    };
    return {
        active = active,
        duration = duration,
        t = t,
        pivot = pivot,
        root = root,
        tip = tip,
        yaw = yaw,
        pitch = pitch,
        roll = roll,
        dir = dir
    };
}

function applySword(pose) {
    foreach (p in sword) {
        local x = pose.pivot.x + pose.dir.x * p.along;
        local y = pose.pivot.y + pose.dir.y * p.along;
        local z = pose.pivot.z + pose.dir.z * p.along;
        p.e.setPosition(x, y, z);
        p.e.setRotation(pose.yaw, pose.pitch, pose.roll);
    }
}

function triggerSlash(kind = null) {
    if (closed) return;
    if (kind != null) slashKind = kind;
    ++slashId;
    slashAge = 0.0;
    ++shotCount;
    trail.clear();
    trail.breakTrail();
    lastAppend = "armed";
}

function runContracts() {
    local probe = trailMod.create(gfx, "fx/assets/slash-mask.tga", "fx/shaders/slash.frag.spv", {
        maxSamples = 8, lifetime = 0.4, minDistance = 0.01, teleportDistance = 1.5
    });
    if (probe.append({x = 0, y = 0, z = 0}, {x = 0, y = 1, z = 0}) != "started")
        throw "trail start contract";
    if (probe.append({x = 0.001, y = 0, z = 0}, {x = 0.001, y = 1, z = 0}) != "skipped")
        throw "trail min-distance contract";
    if (probe.append({x = 0.2, y = 0, z = 0}, {x = 0.2, y = 1, z = 0}) != "added")
        throw "trail add contract";
    if (probe.sampleCount() != 2) throw "trail sample count contract";
    probe.advance(0.5);
    if (probe.isVisible()) throw "trail lifetime contract";
    // Teleport starts a new segment without connecting a giant triangle.
    probe.clear();
    probe.append({x = 0, y = 0, z = 0}, {x = 0, y = 1, z = 0});
    if (probe.append({x = 5, y = 0, z = 0}, {x = 5, y = 1, z = 0}) != "started")
        throw "trail teleport contract";
    probe.close();
    print("SWORD_SLASH_TRAIL_CONTRACTS_PASS start skip add lifetime teleport\n");
}

eve_init <- function() {
    if ("dev" in eve) { eve.dev.console.setVisible(false); eve.dev.ai.setVisible(false); }
    if (!gfx.getRenderControl().setLightingMode("forwardPlus"))
        throw "Forward lighting unavailable";
    runContracts();
    trail = trailMod.create(gfx, "fx/assets/slash-mask.tga", "fx/shaders/slash.frag.spv", {
        maxSamples = 56,
        lifetime = 0.32,
        minDistance = 0.012,
        teleportDistance = 1.8,
        color = [1.0, 0.93, 0.70],
        edge = [0.45, 0.78, 1.0],
        opacity = 0.95
    });
    buildScene();

    ui.setTheme("dark");
    ui.setScale(0.9);
    ui.beginBuild();
    ui.beginWindow("SWORD SLASH TRAIL", "root");
    ui.text("SWORD SLASH TRAIL", "title");
    ui.text("ROOT/TIP TRACK  /  FAN RIBBON  /  MASK", "subtitle");
    ui.beginRow("kinds", 6.0);
    ui.button("Horizontal", "hcut");
    ui.button("Diagonal", "dcut");
    ui.button("Rising", "rcut");
    ui.end();
    ui.beginRow("transport", 6.0);
    ui.button("Slash [SPACE]", "slash");
    ui.button("Pause", "pause");
    ui.button("Clear", "clear");
    ui.end();
    ui.button("Auto: ON", "auto");
    ui.slider("Orbit", orbit, -3.14, 3.14, "orbit");
    ui.separator("s1");
    ui.text("", "status");
    ui.text("", "phase");
    ui.textWrapped(
        "Each swing samples the katana guard (root) and tip. Samples form a fan-shaped ribbon; the slash mask rides UV along the arc.",
        280.0, "help");
    ui.end();
    ui.mountBuildAs("slash");
    ui.select("slash");
    ui.setHostPos(10.0, 10.0, 0.0, 0.0);
    ui.setHostSize(320.0, 560.0);
    ui.setHostOverlay(true);
    print("SWORD_SLASH_TRAIL_READY track=root+tip ribbon=fan mask=slash-mask.tga\n");
};

eve_update <- function(dt) {
    ui.select("slash");
    local click = ui.consumeClick();
    while (click != "") {
        if (click == "slash/slash") triggerSlash();
        else if (click == "slash/hcut") triggerSlash(0);
        else if (click == "slash/dcut") triggerSlash(1);
        else if (click == "slash/rcut") triggerSlash(2);
        else if (click == "slash/pause") paused = !paused;
        else if (click == "slash/clear") { trail.clear(); slashAge = 10.0; }
        else if (click == "slash/auto") {
            automatic = !automatic;
            ui.setText("auto", automatic ? "Auto: ON" : "Auto: OFF");
        }
        click = ui.consumeClick();
    }
    local space = keyboard.isDown("SPACE");
    if (space && !lastSpace) triggerSlash();
    lastSpace = space;

    // Cap dt so slow hosts (software Vulkan) cannot skip the whole slash in one frame.
    local step = paused ? 0.0 : clamp(dt, 0.0, 1.0 / 30.0);
    time += step;
    slashAge += step;
    autoClock -= step;
    if (automatic && autoClock <= 0.0) {
        slashKind = (slashKind + 1) % 3;
        triggerSlash();
        autoClock = 2.8;
    }

    local pose = slashPose(slashKind, slashAge);
    applySword(pose);
    if (pose.active) {
        lastAppend = trail.append(pose.root, pose.tip);
    } else if (slashAge > pose.duration && slashAge < pose.duration + 0.05) {
        trail.breakTrail();
    }
    trail.advance(step);

    orbit = ui.getValue("orbit");
    camera.setEye(sin(orbit) * 11.5, 5.8, cos(orbit) * 11.5);
    local kindName = slashKind == 0 ? "horizontal" : (slashKind == 1 ? "diagonal" : "rising");
    ui.setText("status", format("%s / %s / slashes %d", kindName, lastAppend, shotCount));
    ui.setText("phase", format("%s  |  samples %d  |  age %.2f s",
        paused ? "PAUSED" : "LIVE", trail.sampleCount(), slashAge));
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    ui.select("slash");
    ui.beginFrameAndRender();
};

eve_before_restart <- function() {
    if (trail != null) trail.close();
    closed = true;
};
