// Dual demo: VehicleController (ground car) + FlyingController (sky craft).
// Tab switches which controller receives input; both keep simulating.
dofile("vehicle_controller.nut");
dofile("flying_controller.nut");

const PI = 3.141592653589793;
const STEP = 0.016666666666667;

persist mode = "car"
persist ready = false

local physicsOwner = null;
local world2 = null;
local world3 = null;
local carCtrl = null;
local flyCtrl = null;
local camera = null;
local cubeMesh = null;
local carParts = [];
local planeParts = [];
local scenery = [];
local materials = [];
local fontOwner = null;
local hudFont = null;
local previousKeys = {};
local accumulator = 0.0;
local camX = 0.0;
local camY = 8.0;
local camZ = -18.0;

function clampf(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }

function pressed(key) {
    local down = keyboard.isDown(key);
    local old = key in previousKeys ? previousKeys[key] : false;
    previousKeys[key] <- down;
    return down && !old;
}

function material(r, g, b, metal = 0.0, unlit = false) {
    local m = gfx.newMaterial();
    m.setTint(r, g, b, 1.0);
    m.setMetallic(metal);
    m.setRoughness(0.62);
    if (unlit) { m.setShadingModel("unlit"); m.setReceiveLight(false); }
    materials.append(m);
    return m;
}

function box(x, y, z, sx, sy, sz, m, yaw = 0.0) {
    local e = eve.Renderable3D();
    e.setMesh(cubeMesh);
    e.setMaterial(m);
    e.setPosition(x.tofloat(), y.tofloat(), z.tofloat());
    e.setScale(sx.tofloat(), sy.tofloat(), sz.tofloat());
    e.setRotation(yaw.tofloat(), 0.0, 0.0);
    scenery.append(e);
    return e;
}

function buildScenery() {
    local ground = material(0.12, 0.28, 0.16);
    local asphalt = material(0.16, 0.18, 0.20);
    local stripe = material(0.92, 0.88, 0.35, 0.0, true);
    local tower = material(0.35, 0.42, 0.48, 0.2);
    local cloud = material(0.78, 0.84, 0.92, 0.0, true);
    box(0.0, -0.35, 0.0, 220.0, 0.7, 220.0, ground);
    // Oval track apron.
    for (local i = 0; i < 48; ++i) {
        local a = i * 2.0 * PI / 48.0;
        local x = cos(a) * 34.0;
        local z = sin(a) * 22.0;
        box(x, 0.02, z, 5.5, 0.05, 3.2, asphalt, -a);
        if (i % 2 == 0) box(x, 0.04, z, 0.35, 0.04, 2.4, stripe, -a);
    }
    // Runway strip for the plane.
    box(0.0, 0.03, -48.0, 80.0, 0.06, 8.0, asphalt);
    for (local x = -36.0; x <= 36.0; x += 6.0)
        box(x, 0.05, -48.0, 2.2, 0.03, 0.35, stripe);
    // Control tower and distant hills.
    box(-42.0, 6.0, -8.0, 3.0, 12.0, 3.0, tower);
    box(-42.0, 12.5, -8.0, 4.2, 1.0, 4.2, material(0.55, 0.75, 0.9, 0.0, true));
    for (local i = 0; i < 10; ++i) {
        local a = i * 0.62;
        box(cos(a) * 90.0, 4.0 + (i % 3), sin(a) * 90.0, 18.0, 8.0 + i, 14.0, material(0.18, 0.32, 0.22));
    }
    for (local i = 0; i < 8; ++i)
        box(-20.0 + i * 8.0, 28.0 + (i % 3) * 2.0, -70.0 + (i % 2) * 12.0, 10.0, 3.0, 6.0, cloud);
}

function buildCarVisual() {
    local body = material(0.15, 0.55, 0.95, 0.35);
    local dark = material(0.05, 0.06, 0.08, 0.4);
    local glass = material(0.55, 0.75, 0.9, 0.1);
    carParts = [
        box(0.0, 0.0, 0.0, 2.2, 0.55, 4.2, body),
        box(0.0, 0.55, -0.2, 1.8, 0.45, 2.0, glass),
        box(0.0, 0.05, 0.0, 2.35, 0.12, 4.4, dark)
    ];
}

function buildPlaneVisual() {
    local body = material(0.85, 0.88, 0.92, 0.25);
    local accent = material(0.9, 0.25, 0.18, 0.2);
    local dark = material(0.12, 0.14, 0.16, 0.3);
    planeParts = [
        box(0.0, 0.0, 0.0, 4.8, 0.55, 1.1, body),
        box(0.2, 0.05, 0.0, 1.4, 0.12, 6.5, accent),
        box(-1.8, 0.35, 0.0, 0.9, 0.08, 2.4, accent),
        box(-2.0, 0.7, 0.0, 0.7, 1.1, 0.12, dark)
    ];
}

function syncCarVisual(p) {
    // Vehicle heading is degrees in XY; pose.yaw is already render radians.
    foreach (e in carParts) {
        e.setPosition(p.x, 0.45, p.z);
        e.setRotation(p.yaw, 0.0, 0.0);
    }
}

function syncPlaneVisual(p) {
    foreach (e in planeParts) {
        e.setPosition(p.x, p.y, p.z);
        e.setRotation(p.yaw, p.pitch, p.roll);
    }
}

function followCamera(target, lookAhead, height, distance, dt) {
    // headingRad: vehicle forward in XZ (neon-racing convention). Plane uses yaw as heading.
    local h = "headingRad" in target ? target.headingRad : target.yaw;
    local desiredX = target.x - cos(h) * distance;
    local desiredZ = target.z - sin(h) * distance;
    local desiredY = target.y + height;
    local k = clampf(dt * 4.5, 0.0, 1.0);
    camX += (desiredX - camX) * k;
    camY += (desiredY - camY) * k;
    camZ += (desiredZ - camZ) * k;
    camera.setEye(camX, camY, camZ);
    camera.setTarget(target.x + cos(h) * lookAhead, target.y + 0.8, target.z + sin(h) * lookAhead);
}

function toggleMode() {
    mode = mode == "car" ? "plane" : "car";
    if (mode == "car") {
        flyCtrl.cruiseHold();
        print("MODE=car VehicleController active\n");
    } else {
        carCtrl.park();
        print("MODE=plane FlyingController active\n");
    }
}

function resetActive() {
    if (mode == "car") {
        carCtrl.reset(0.0, 0.0, 0.0);
        print("VehicleController reset\n");
    } else {
        flyCtrl.reset(0.0, 22.0, -48.0);
        print("FlyingController reset\n");
    }
}

eve_init <- function() {
    gfx.setBackgroundColor(0.42, 0.62, 0.88, 1.0);
    cubeMesh = gfx.newMeshCube(1.0);
    physicsOwner = eve.Physics();
    world2 = physicsOwner.newWorld(0.0, 0.0, true);
    world2.setMeter(1.0);
    world3 = physicsOwner.newWorld3D(0.0, -9.8, 0.0, true);

    // Soft ground collider so the plane has something to bounce on.
    local ground = world3.newBody("static", 0.0, -0.5, 0.0);
    ground.newBoxShape(240.0, 1.0, 240.0, 1.0, 0.6, 0.1);

    buildScenery();
    buildCarVisual();
    buildPlaneVisual();

    carCtrl = VehicleController();
    carCtrl.setup(world2);
    carCtrl.spawn(0.0, 0.0, 0.0);

    flyCtrl = FlyingController();
    flyCtrl.setup(world3);
    flyCtrl.spawn(0.0, 22.0, -48.0);
    flyCtrl.cruiseHold();

    camera = eve.Camera3D();
    camera.setUp(0.0, 1.0, 0.0);
    camera.setFov(55.0);
    camera.setClipPlanes(0.2, 500.0);
    camera.setAmbient(0.48, 0.55, 0.68);
    camera.setActive(true);
    gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.6, 1.5, 1.35);

    fontOwner = eve.Font();
    local data = fontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf", 22);
    if (data != null) {
        hudFont = gfx.newFont(data, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:.-+()");
        gfx.setFont(hudFont);
    }

    local carPose = carCtrl.pose();
    camX = carPose.x; camY = 6.0; camZ = carPose.z - 14.0;
    ready = true;
    print("FLIGHT_VEHICLE_CONTROLLERS_READY mode=car\n");
    print("Tab switch | R reset | Car: WASD+Space | Plane: W/S throttle, arrows pitch/roll, Q/E yaw\n");
};

eve_update <- function(dt) {
    if (!ready) return;
    if (pressed("tab") || pressed("TAB")) toggleMode();
    if (pressed("r") || pressed("R")) resetActive();

    accumulator += clampf(dt, 0.0, 0.25);
    while (accumulator >= STEP) {
        if (mode == "car") {
            carCtrl.sampleKeyboard();
            flyCtrl.cruiseHold();
        } else {
            flyCtrl.sampleKeyboard();
            carCtrl.park();
        }
        carCtrl.step(STEP);
        flyCtrl.step(STEP);
        accumulator -= STEP;
    }

    local carPose = carCtrl.pose();
    local planePose = flyCtrl.pose();
    syncCarVisual(carPose);
    syncPlaneVisual(planePose);
    followCamera(mode == "car" ? carPose : planePose,
                 mode == "car" ? 6.0 : 10.0,
                 mode == "car" ? 5.5 : 4.0,
                 mode == "car" ? 12.0 : 16.0,
                 dt);
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    if (hudFont == null) return;
    local carPose = carCtrl.pose();
    local planePose = flyCtrl.pose();
    local line1 = mode == "car" ? "MODE  CAR  (VehicleController)" : "MODE  PLANE  (FlyingController)";
    local line2 = format("Car  spd %.1f  pos %.0f,%.0f", carPose.speed, carPose.x, carPose.z);
    local line3 = format("Plane spd %.1f  alt %.1f  thr %.0f%%", planePose.speed, planePose.altitude, flyCtrl.throttle * 100.0);
    gfx.drawText(hudFont, line1, 18.0, 18.0, 1.0, 1.0, 1.0, 1.0, 1.0);
    gfx.drawText(hudFont, line2, 18.0, 46.0, 0.75, 0.9, 1.0, 1.0, 0.9);
    gfx.drawText(hudFont, line3, 18.0, 74.0, 0.9, 0.85, 0.7, 1.0, 0.9);
    gfx.drawText(hudFont, "Tab switch  R reset", 18.0, 108.0, 0.7, 0.75, 0.8, 1.0, 0.8);
};
