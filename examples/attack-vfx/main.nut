// AttackVfx showcase — recipe orchestration + script presentation FX.
// Cycles WATER whip → EARTH cones → FIRE burst → AIR slash (video-shaped arena).
// MeshVfx/trail/distortion/prefab layers are CPU orchestration today; visible
// impact comes from Renderable3D presentation props + particles + decal + audio.

persist avCamera = null
persist avTiles = null
persist avMannequin = null
persist avStaff = null
persist avDummy = null
persist avDummyCap = null
persist avDummyEyeL = null
persist avDummyEyeR = null
persist avDecal = null
persist avAction = null
persist avParticles = null
persist avFont = null
persist avHandle = null
persist avSkinIndex = 0
persist avPhase = "idle"   // idle | anticipate | release
persist avTimer = 0.0
persist avCycle = 0
persist avLastEvents = ""
persist avStarted = 0
persist avLayerHits = 0
persist avPassPrinted = false
persist avTime = 0.0
persist avFxSegs = null
persist avFxCones = null
persist avFxOrbs = null
persist avChargeRing = null

skins <- [
    {
        id = "skin:water", label = "WATER", title = "Water - Whip",
        tint = [0.35, 0.78, 1.0], meshUri = "skill:weaponSlash",
        particle = "water_burst.particle.json",
        decalR = 0.25, decalG = 0.55, decalB = 0.95
    },
    {
        id = "skin:earth", label = "EARTH", title = "Earth - Impact Cones",
        tint = [0.72, 0.42, 0.22], meshUri = "skill:impactFlash",
        particle = "earth_burst.particle.json",
        decalR = 0.55, decalG = 0.35, decalB = 0.18
    },
    {
        id = "skin:fire", label = "FIRE", title = "Fire - Projectile Burst",
        tint = [1.0, 0.42, 0.08], meshUri = "skill:burningBody",
        particle = "fire_burst.particle.json",
        decalR = 0.95, decalG = 0.28, decalB = 0.06
    },
    {
        id = "skin:air", label = "AIR", title = "Air - Impact Distortions",
        tint = [0.55, 0.95, 0.82], meshUri = "skill:weaponSlash",
        particle = "air_burst.particle.json",
        decalR = 0.45, decalG = 0.85, decalB = 0.75
    }
];

SRC_X <- 0.0;
SRC_Z <- 0.0;
TGT_X <- 3.4;
TGT_Z <- 0.15;
HAND_Y <- 1.35;

function avRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function hideProp(prop) {
    if (prop == null) return;
    prop.setScale(0.001, 0.001, 0.001);
    prop.setPosition(0.0, -20.0, 0.0);
}

function hideAllFx() {
    if (avFxSegs != null)
        for (local i = 0; i < avFxSegs.len(); ++i) hideProp(avFxSegs[i]);
    if (avFxCones != null)
        for (local i = 0; i < avFxCones.len(); ++i) hideProp(avFxCones[i]);
    if (avFxOrbs != null)
        for (local i = 0; i < avFxOrbs.len(); ++i) hideProp(avFxOrbs[i]);
    hideProp(avChargeRing);
}

function recipeJson(skin) {
    // Screen-space particle placement near the impact (1100x700).
    local px = 720.0;
    local py = 360.0;
    return "{" +
        "\"schema\":\"eve.stylize.attack-vfx\",\"schemaVersion\":1," +
        "\"id\":\"attackvfx:showcase\"," +
        "\"skin\":{" +
            "\"id\":\"" + skin.id + "\"," +
            "\"styleHints\":{\"meshStyle\":\"slash\"}," +
            "\"shakeProfile\":{\"posAmp\":0.1,\"rotAmp\":0.45,\"duration\":0.14}," +
            "\"distortionProfile\":{\"strength\":0.42}" +
        "}," +
        "\"phases\":[" +
            "{" +
                "\"kind\":\"anticipate\",\"durationSeconds\":0.42," +
                "\"layers\":[" +
                    "{\"role\":\"meshVfx\",\"uri\":\"skill:chargeAura\"}," +
                    "{\"role\":\"prefab\",\"uri\":\"prefab://charge-ring\"}," +
                    "{\"role\":\"particles\",\"uri\":\"" + skin.particle +
                        "\",\"floatParams\":{\"x\":" + (px - 180.0) +
                        ",\"y\":" + (py + 40.0) + ",\"intensity\":0.7}}" +
                "]" +
            "}," +
            "{" +
                "\"kind\":\"release\",\"startCue\":\"impact\",\"durationSeconds\":0.78," +
                "\"layers\":[" +
                    "{\"role\":\"meshVfx\",\"uri\":\"" + skin.meshUri + "\"}," +
                    "{\"role\":\"trail\",\"uri\":\"trail://blade\",\"floatParams\":{\"lifetime\":0.28}}," +
                    "{\"role\":\"camera\",\"floatParams\":{\"posAmp\":0.14,\"duration\":0.12}}," +
                    "{\"role\":\"distortion\",\"floatParams\":{\"strength\":0.48}}," +
                    "{\"role\":\"decal\",\"uri\":\"decal://scorch\",\"floatParams\":{" +
                        "\"x\":" + TGT_X + ",\"y\":0.06,\"z\":" + TGT_Z +
                        ",\"size\":1.9,\"lifetime\":1.4,\"depth\":0.4," +
                        "\"r\":" + skin.decalR + ",\"g\":" + skin.decalG +
                        ",\"b\":" + skin.decalB + ",\"a\":0.8}}," +
                    "{\"role\":\"audio\",\"uri\":\"hit.wav\",\"floatParams\":{\"volume\":0.75}}," +
                    "{\"role\":\"prefab\",\"uri\":\"prefab://impact-burst\"}," +
                    "{\"role\":\"particles\",\"uri\":\"" + skin.particle +
                        "\",\"floatParams\":{\"x\":" + px + ",\"y\":" + py +
                        ",\"intensity\":1.35}}" +
                "]" +
            "}" +
        "]" +
    "}";
}

function summarizeEvents(frame) {
    if (!frame || !frame.ok || !frame.hasValue) return "";
    local text = "";
    local events = frame.value.events;
    local started = 0;
    local skipped = 0;
    local count = events.len();
    for (local i = 0; i < count; ++i) {
        local ev = events[i];
        if (ev.kind == "layerStarted") {
            started += 1;
            text += (text == "" ? "" : " ") + ev.role;
        } else if (ev.kind == "layerSkipped") {
            skipped += 1;
            text += (text == "" ? "" : " ") + ev.role + "?";
        }
    }
    avLayerHits += started;
    return "started=" + started + " skipped=" + skipped + " [" + text + "]";
}

function playCycle() {
    local skin = skins[avSkinIndex];
    avRequire(avAction.registerRecipeJson(recipeJson(skin)), "register recipe");
    local played = avRequire(avAction.play("attackvfx:showcase", 0, 2), "play");
    avHandle = played.value;
    avPhase = "anticipate";
    avTimer = 0.0;
    avStarted += 1;
    avLastEvents = "play " + skin.label + " slot=" + avHandle.slot;
    hideAllFx();
    print("ATTACK_VFX_PLAY skin=" + skin.label + " cycle=" + avCycle + "\n");
}

function makePart(mesh, x, y, z, sx, sy, sz, r, g, b, roughness) {
    local p = eve.Renderable3D();
    p.setMesh(mesh);
    p.setPosition(x, y, z);
    p.setScale(sx, sy, sz);
    p.setTint(r, g, b, 1.0);
    p.setRoughness(roughness);
    p.setCastShadow(true);
    p.setReceiveShadow(true);
    return p;
}

function buildMannequin(cube, cyl, sphere) {
    // Featureless white humanoid in a forward-lean attack pose.
    avMannequin = [];
    avMannequin.push(makePart(cyl, SRC_X, 0.55, SRC_Z, 0.22, 0.7, 0.22, 0.92, 0.92, 0.94, 0.55)); // torso
    avMannequin.push(makePart(sphere, SRC_X, 1.22, SRC_Z + 0.02, 0.18, 0.18, 0.18, 0.93, 0.93, 0.95, 0.45)); // head
    avMannequin.push(makePart(cyl, SRC_X - 0.16, 0.18, SRC_Z + 0.08, 0.08, 0.36, 0.08, 0.9, 0.9, 0.92, 0.6)); // L leg
    avMannequin.push(makePart(cyl, SRC_X + 0.14, 0.22, SRC_Z - 0.12, 0.08, 0.4, 0.08, 0.9, 0.9, 0.92, 0.6)); // R leg
    avMannequin.push(makePart(cyl, SRC_X - 0.28, 1.05, SRC_Z + 0.05, 0.07, 0.34, 0.07, 0.91, 0.91, 0.93, 0.55)); // L arm
    avMannequin.push(makePart(cyl, SRC_X + 0.32, 1.15, SRC_Z + 0.18, 0.07, 0.38, 0.07, 0.91, 0.91, 0.93, 0.55)); // R arm
    local larm = avMannequin[4];
    larm.setRotation(0.0, 0.0, 0.85);
    local rarm = avMannequin[5];
    rarm.setRotation(0.35, -0.2, -1.1);
    avStaff = makePart(cyl, SRC_X + 0.45, HAND_Y, SRC_Z + 0.35, 0.035, 1.15, 0.035, 0.55, 0.28, 0.12, 0.35);
    avStaff.setRotation(0.2, 0.15, 1.35);
}

function buildDummy(cyl, cube, sphere) {
    avDummy = makePart(cyl, TGT_X, 0.95, TGT_Z, 0.55, 1.9, 0.55, 0.62, 0.32, 0.95, 0.55);
    avDummyCap = makePart(cyl, TGT_X, 1.92, TGT_Z, 0.52, 0.08, 0.52, 0.95, 0.95, 0.97, 0.4);
    local base = makePart(cyl, TGT_X, 0.06, TGT_Z, 0.7, 0.12, 0.7, 0.5, 0.28, 0.82, 0.7);
    avDummyEyeL = makePart(sphere, TGT_X - 0.14, 1.45, TGT_Z + 0.48, 0.1, 0.12, 0.06, 0.98, 0.98, 1.0, 0.3);
    avDummyEyeR = makePart(sphere, TGT_X + 0.14, 1.45, TGT_Z + 0.48, 0.1, 0.12, 0.06, 0.98, 0.98, 1.0, 0.3);
    local pupilL = makePart(sphere, TGT_X - 0.14, 1.45, TGT_Z + 0.52, 0.04, 0.05, 0.04, 0.08, 0.08, 0.1, 0.2);
    local pupilR = makePart(sphere, TGT_X + 0.14, 1.45, TGT_Z + 0.52, 0.04, 0.05, 0.04, 0.08, 0.08, 0.1, 0.2);
    // Keep base/pupils reachable via dummy list for lifetime; store on arrays.
    avDummy = [avDummy, avDummyCap, base, avDummyEyeL, avDummyEyeR, pupilL, pupilR];
}

function buildFxPools(cube, cyl, sphere) {
    avFxSegs = [];
    for (local i = 0; i < 18; ++i) {
        local s = makePart(cyl, 0.0, -20.0, 0.0, 0.001, 0.001, 0.001, 1.0, 1.0, 1.0, 0.25);
        s.setCastShadow(false);
        avFxSegs.push(s);
    }
    avFxCones = [];
    for (local i = 0; i < 28; ++i) {
        local c = makePart(cyl, 0.0, -20.0, 0.0, 0.001, 0.001, 0.001, 0.6, 0.35, 0.2, 0.75);
        c.setCastShadow(false);
        avFxCones.push(c);
    }
    avFxOrbs = [];
    for (local i = 0; i < 14; ++i) {
        local o = makePart(sphere, 0.0, -20.0, 0.0, 0.001, 0.001, 0.001, 1.0, 0.5, 0.1, 0.2);
        o.setCastShadow(false);
        avFxOrbs.push(o);
    }
    avChargeRing = makePart(cyl, SRC_X, HAND_Y, SRC_Z, 0.001, 0.001, 0.001, 0.6, 0.85, 1.0, 0.2);
    avChargeRing.setCastShadow(false);
}

function buildArena() {
    avCamera = eve.Camera3D();
    avCamera.setEye(5.8, 4.6, 7.2);
    avCamera.setTarget(1.6, 0.85, 0.0);
    avCamera.setUp(0.0, 1.0, 0.0);
    avCamera.setFov(46.0);
    avCamera.setAmbient(0.18, 0.19, 0.22);
    avCamera.setActive(true);

    gfx.setDirectionalLight(-0.35, -1.0, -0.25, 1.35, 1.25, 1.15);
    gfx.setBackgroundColor(0.02, 0.025, 0.03, 1.0);

    local cube = gfx.newMeshCube(1.0);
    local cyl = gfx.newMeshCylinder(24, 1, true);
    local sphere = gfx.newMeshSphere(16, 12);

    // Dark checkered floor (video arena).
    avTiles = [];
    local tile = 0.95;
    local half = 7;
    for (local z = -half; z <= half; ++z) {
        for (local x = -half; x <= half + 2; ++x) {
            local shade = ((x + z) % 2 == 0) ? 0.22 : 0.11;
            local t = eve.Renderable3D();
            t.setMesh(cube);
            t.setPosition(x * tile + 1.2, -0.06, z * tile);
            t.setScale(tile * 0.98, 0.1, tile * 0.98);
            t.setTint(shade, shade * 1.02, shade * 1.05, 1.0);
            t.setRoughness(0.88);
            t.setMetallic(0.08);
            t.setCastShadow(false);
            t.setReceiveShadow(true);
            avTiles.push(t);
        }
    }

    buildMannequin(cube, cyl, sphere);
    buildDummy(cyl, cube, sphere);
    buildFxPools(cube, cyl, sphere);
    hideAllFx();

    avDecal = eve.Decal();
    avDecal.setEnabled(gfx, true);

    avParticles = eve.Particles();

    local data = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 26);
    avFont = gfx.newFont(data,
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-:/");
}

function lerp(a, b, t) {
    return a + (b - a) * t;
}

function clamp01(t) {
    if (t < 0.0) return 0.0;
    if (t > 1.0) return 1.0;
    return t;
}

function smooth(t) {
    t = clamp01(t);
    return t * t * (3.0 - 2.0 * t);
}

function setSeg(i, x, y, z, sx, sy, sz, yaw, pitch, r, g, b, aScale) {
    if (i < 0 || i >= avFxSegs.len()) return;
    local s = avFxSegs[i];
    s.setPosition(x, y, z);
    s.setScale(sx * aScale, sy * aScale, sz * aScale);
    s.setRotation(yaw, pitch, 0.0);
    s.setTint(r, g, b, 1.0);
}

function setCone(i, x, y, z, rBase, h, r, g, b, visible) {
    if (i < 0 || i >= avFxCones.len()) return;
    local c = avFxCones[i];
    if (!visible) { hideProp(c); return; }
    c.setPosition(x, y + h * 0.5, z);
    c.setScale(rBase, h, rBase);
    c.setRotation(0.0, 0.0, 0.0);
    c.setTint(r, g, b, 1.0);
}

function setOrb(i, x, y, z, radius, r, g, b) {
    if (i < 0 || i >= avFxOrbs.len()) return;
    local o = avFxOrbs[i];
    o.setPosition(x, y, z);
    o.setScale(radius, radius, radius);
    o.setTint(r, g, b, 1.0);
}

function updateChargeAura(skin, t) {
    local pulse = 0.35 + 0.65 * (0.5 + 0.5 * sin(avTime * 14.0));
    local rad = 0.25 + 0.2 * pulse * t;
    avChargeRing.setPosition(SRC_X + 0.35, HAND_Y, SRC_Z + 0.25);
    avChargeRing.setScale(rad, 0.04, rad);
    avChargeRing.setTint(skin.tint[0], skin.tint[1], skin.tint[2], 1.0);
    // Small orbiting orbs.
    for (local i = 0; i < 6; ++i) {
        local ang = avTime * 5.0 + i * 1.047;
        local ox = SRC_X + 0.35 + cos(ang) * rad;
        local oz = SRC_Z + 0.25 + sin(ang) * rad;
        setOrb(i, ox, HAND_Y + 0.05 * sin(ang * 2.0), oz, 0.05 + 0.03 * pulse,
               skin.tint[0], skin.tint[1], skin.tint[2]);
    }
}

function updateWaterWhip(skin, t) {
    // Arc whip from hand toward target with bulbous head.
    local n = 14;
    for (local i = 0; i < n; ++i) {
        local u = (i.tofloat() + 0.5) / n.tofloat();
        local travel = clamp01(t * 1.35 - u * 0.55);
        if (travel <= 0.01) { hideProp(avFxSegs[i]); continue; }
        local p = smooth(travel);
        local x = lerp(SRC_X + 0.4, TGT_X - 0.2, u);
        local z = lerp(SRC_Z + 0.3, TGT_Z, u) + sin(u * 3.14) * 0.35;
        local y = lerp(HAND_Y, 0.85, u) + sin(u * 3.14) * 1.1 * (1.0 - p * 0.3);
        local thick = lerp(0.04, 0.22, u * u) * (0.55 + 0.45 * p);
        local len = 0.28;
        local yaw = atan2(TGT_Z - SRC_Z, TGT_X - SRC_X);
        local pitch = -0.35 + u * 0.7;
        local r = lerp(0.45, 0.85, u);
        local g = lerp(0.85, 0.55, u);
        local b = 1.0;
        setSeg(i, x, y, z, thick, len, thick, yaw, pitch, r, g, b, p);
    }
    // Head splash orbs near tip.
    local tip = smooth(clamp01(t * 1.2));
    local hx = lerp(SRC_X + 0.4, TGT_X, tip);
    local hz = lerp(SRC_Z + 0.3, TGT_Z, tip);
    local hy = lerp(HAND_Y, 0.9, tip) + sin(tip * 3.14) * 0.9;
    setOrb(0, hx, hy, hz, 0.18 + 0.22 * tip, 0.55, 0.8, 1.0);
    setOrb(1, hx + 0.12, hy - 0.08, hz, 0.1, 0.75, 0.9, 1.0);
    setOrb(2, hx - 0.1, hy + 0.05, hz + 0.08, 0.08, 0.9, 0.95, 1.0);
    // Droplet spray after impact.
    if (t > 0.55) {
        local spray = clamp01((t - 0.55) / 0.35);
        for (local i = 3; i < 10; ++i) {
            local a = i * 0.9 + avTime;
            setOrb(i, TGT_X + cos(a) * 0.55 * spray, 0.7 + (i % 3) * 0.15,
                   TGT_Z + sin(a) * 0.4 * spray, 0.04 + 0.03 * (1.0 - spray),
                   0.4, 0.75, 1.0);
        }
    }
}

function updateEarthCones(skin, t) {
    hideProp(avChargeRing);
    local pathN = 10;
    local coneIdx = 0;
    for (local i = 0; i < pathN; ++i) {
        local u = (i.tofloat() + 0.5) / pathN.tofloat();
        local appear = clamp01(t * 1.5 - u * 0.7);
        if (appear <= 0.02) continue;
        local x = lerp(SRC_X + 0.5, TGT_X + 0.3, u);
        local z = lerp(SRC_Z, TGT_Z, u) + ((i % 2 == 0) ? -0.18 : 0.18) * (0.4 + u);
        local h = (0.25 + u * 0.85) * appear;
        local rBase = (0.12 + u * 0.22) * (1.15 - 0.3 * appear);
        local shade = 0.85 - u * 0.25;
        setCone(coneIdx, x, 0.0, z, rBase, h,
                skin.tint[0] * shade, skin.tint[1] * shade, skin.tint[2] * shade, true);
        coneIdx += 1;
        // Secondary shard.
        if (coneIdx < avFxCones.len()) {
            setCone(coneIdx, x + 0.14, 0.0, z - 0.1, rBase * 0.55, h * 0.7,
                    skin.tint[0] * 0.8, skin.tint[1] * 0.75, skin.tint[2] * 0.7, appear > 0.3);
            coneIdx += 1;
        }
    }
    // Impact cluster around dummy.
    if (t > 0.45) {
        local blast = smooth(clamp01((t - 0.45) / 0.35));
        for (local k = 0; k < 8 && coneIdx < avFxCones.len(); ++k) {
            local a = k * 0.785 + 0.2;
            local rad = 0.35 + (k % 3) * 0.18;
            setCone(coneIdx, TGT_X + cos(a) * rad * blast, 0.0,
                    TGT_Z + sin(a) * rad * blast,
                    0.16 + (k % 2) * 0.08, 0.55 + (k % 3) * 0.25 * blast,
                    0.65, 0.38, 0.2, true);
            coneIdx += 1;
        }
        // Dust orbs.
        for (local i = 0; i < 6; ++i) {
            local a = i * 1.1 + avTime * 0.5;
            setOrb(i, TGT_X + cos(a) * 0.7 * blast, 0.4 + i * 0.08,
                   TGT_Z + sin(a) * 0.5 * blast, 0.08 * (1.0 - blast * 0.4),
                   0.75, 0.7, 0.65);
        }
    }
    while (coneIdx < avFxCones.len()) { hideProp(avFxCones[coneIdx]); coneIdx += 1; }
}

function updateFireBurst(skin, t) {
    // Traveling fire whip / projectile then impact sphere.
    local travel = smooth(clamp01(t / 0.35));
    local px = lerp(SRC_X + 0.45, TGT_X, travel);
    local pz = lerp(SRC_Z + 0.25, TGT_Z, travel);
    local py = lerp(HAND_Y, 1.1, travel) + sin(travel * 3.14) * 0.45;
    // Trail segments.
    for (local i = 0; i < 10; ++i) {
        local u = i.tofloat() / 9.0;
        local tt = clamp01(travel - (1.0 - u) * 0.35);
        if (tt <= 0.01) { hideProp(avFxSegs[i]); continue; }
        local x = lerp(SRC_X + 0.45, px, u);
        local z = lerp(SRC_Z + 0.25, pz, u);
        local y = lerp(HAND_Y, py, u) + sin(u * 3.14) * 0.25;
        local thick = lerp(0.03, 0.12, u) * tt;
        setSeg(i, x, y, z, thick, 0.22, thick, 0.0, 0.4,
               1.0, lerp(0.9, 0.35, u), lerp(0.5, 0.05, u), tt);
    }
    // Impact burst.
    if (t > 0.32) {
        local blast = smooth(clamp01((t - 0.32) / 0.4));
        setOrb(0, TGT_X, 1.15, TGT_Z, 0.35 + 0.75 * blast, 1.0, 0.55, 0.1);
        setOrb(1, TGT_X, 1.2, TGT_Z, 0.2 + 0.35 * blast, 1.0, 0.9, 0.45);
        // Magic ring (flat cylinder).
        avChargeRing.setPosition(TGT_X, 1.25, TGT_Z);
        avChargeRing.setScale(0.55 + 0.35 * blast, 0.05, 0.55 + 0.35 * blast);
        avChargeRing.setTint(1.0, 0.95, 0.75, 1.0);
        // Ember cones around impact.
        for (local i = 0; i < 10; ++i) {
            local a = i * 0.628 + avTime * 2.0;
            local rad = 0.4 + 0.5 * blast;
            setCone(i, TGT_X + cos(a) * rad, 0.15,
                    TGT_Z + sin(a) * rad * 0.7,
                    0.08 + 0.06 * (i % 3), 0.35 + 0.4 * blast * (1.0 - (i % 4) * 0.1),
                    1.0, 0.35 + (i % 3) * 0.1, 0.05, blast > 0.1);
        }
        for (local i = 2; i < 8; ++i) {
            local a = i * 0.9;
            setOrb(i, TGT_X + cos(a) * 0.9 * blast, 0.8 + sin(a + avTime) * 0.3,
                   TGT_Z + sin(a) * 0.7 * blast, 0.08 + 0.05 * (1.0 - blast),
                   1.0, 0.4, 0.05);
        }
    } else {
        setOrb(0, px, py, pz, 0.14 + 0.08 * travel, 1.0, 0.65, 0.15);
        setOrb(1, px - 0.1, py, pz, 0.08, 1.0, 0.85, 0.35);
    }
}

function updateAirSlash(skin, t) {
    // Wide curved mint/cyan ribbon with bright head + residual strands.
    local n = 16;
    local sweep = smooth(clamp01(t / 0.45));
    for (local i = 0; i < n; ++i) {
        local u = i.tofloat() / (n - 1).tofloat();
        local appear = clamp01(sweep * 1.4 - u * 0.55);
        if (appear <= 0.01) { hideProp(avFxSegs[i]); continue; }
        local ang = -0.6 + u * 2.4;
        local radius = 1.4 + u * 1.6;
        local cx = lerp(SRC_X, TGT_X - 0.4, 0.55);
        local cz = SRC_Z;
        local x = cx + cos(ang) * radius * 0.55;
        local z = cz + sin(ang) * radius * 0.85;
        local y = 0.7 + sin(u * 3.14) * 1.35;
        local thick = lerp(0.03, 0.16, u * u) * (0.5 + 0.5 * appear);
        local len = 0.32 + u * 0.15;
        local r = lerp(0.55, 0.85, u);
        local g = lerp(0.95, 0.7, u);
        local b = lerp(0.85, 1.0, u);
        // Purple core near head.
        if (u > 0.7) { r = lerp(r, 0.7, (u - 0.7) / 0.3); g = lerp(g, 0.45, (u - 0.7) / 0.3); b = lerp(b, 0.95, (u - 0.7) / 0.3); }
        setSeg(i, x, y, z, thick, len, thick * 0.7, ang, 0.25, r, g, b, appear);
    }
    // Bright slash flash at head.
    if (t > 0.25) {
        local flash = clamp01((t - 0.25) / 0.2);
        local fade = 1.0 - clamp01((t - 0.45) / 0.4);
        local a = flash * fade;
        setOrb(0, TGT_X - 0.2, 1.5, TGT_Z + 0.3, 0.25 * a, 1.0, 1.0, 1.0);
        setOrb(1, TGT_X, 1.3, TGT_Z, 0.35 * a, 0.65, 0.55, 0.95);
        // Distortion stand-in: expanding translucent ring.
        avChargeRing.setPosition(TGT_X, 0.9, TGT_Z);
        avChargeRing.setScale(0.4 + 1.2 * flash, 0.04, 0.4 + 1.2 * flash);
        avChargeRing.setTint(0.7, 0.95, 0.9, 1.0);
        for (local i = 2; i < 8; ++i) {
            local a2 = i * 0.9 + avTime * 3.0;
            setOrb(i, TGT_X + cos(a2) * 0.8 * flash, 1.0 + sin(a2) * 0.3,
                   TGT_Z + sin(a2) * 0.6 * flash, 0.05 * fade,
                   0.55, 0.95, 0.85);
        }
    }
}

function updatePresentation(dt) {
    local skin = skins[avSkinIndex];
    if (avPhase == "idle") {
        hideAllFx();
        // Soft idle bob on staff.
        if (avStaff != null) {
            avStaff.setPosition(SRC_X + 0.45, HAND_Y + 0.03 * sin(avTime * 2.0), SRC_Z + 0.35);
        }
        return;
    }
    if (avPhase == "anticipate") {
        local t = clamp01(avTimer / 0.42);
        updateChargeAura(skin, t);
        // Warm-up strand for water/air.
        if (skin.label == "WATER" || skin.label == "AIR") {
            for (local i = 0; i < 4; ++i) {
                local u = i.tofloat() / 4.0;
                setSeg(i, SRC_X + 0.4 + u * 0.35, HAND_Y + 0.1 * sin(avTime * 8.0 + i),
                       SRC_Z + 0.3, 0.03, 0.18, 0.03, 0.0, 0.2,
                       skin.tint[0], skin.tint[1], skin.tint[2], t);
            }
        }
        return;
    }
    // release
    local t = clamp01(avTimer / 0.78);
    if (skin.label == "WATER") updateWaterWhip(skin, t);
    else if (skin.label == "EARTH") updateEarthCones(skin, t);
    else if (skin.label == "FIRE") updateFireBurst(skin, t);
    else updateAirSlash(skin, t);
}

eve_init <- function() {
    if (avAction == null) {
        avAction = eve.StylizeAction();
        buildArena();
        playCycle();
        print("ATTACK_VFX_READY layers=mesh,trail,particles,camera,distortion,decal,audio,prefab\n");
    }
};

eve_update <- function(dt) {
    avTime += dt;
    avTimer += dt;
    if (avDecal != null) avDecal.update(dt);
    if (avParticles != null) avParticles.update(dt);

    if (avHandle != null && avAction != null) {
        local frame = avRequire(avAction.advance(dt), "advance");
        local summary = summarizeEvents(frame);
        if (summary != "") avLastEvents = summary;

        if (avPhase == "anticipate" && avTimer >= 0.38) {
            local signaled = avRequire(
                avAction.signal(avHandle.slot, avHandle.generation, "impact"), "signal");
            avPhase = "release";
            avTimer = 0.0;
            hideAllFx();
            local hitSummary = summarizeEvents(signaled);
            if (hitSummary != "") avLastEvents = "impact " + hitSummary;
            print("ATTACK_VFX_IMPACT " + avLastEvents + "\n");
        }

        if (avAction.activeCount() == 0) {
            avHandle = null;
            avPhase = "idle";
            avTimer = 0.0;
            hideAllFx();
        }
    } else {
        if (avTimer >= 0.65) {
            avSkinIndex = (avSkinIndex + 1) % skins.len();
            avCycle += 1;
            playCycle();
        }
    }

    updatePresentation(dt);

    // Slow orbit so screenshots read depth; keep video-like high angle.
    local yaw = 0.55 + avTime * 0.08;
    avCamera.setEye(5.8 * cos(yaw), 4.6, 7.2 * sin(yaw) + 1.5);
    avCamera.setTarget(1.6, 0.85, 0.0);

    // Element-reactive key light.
    local skin = skins[avSkinIndex];
    local glow = avPhase == "release" ? 1.55 : (avPhase == "anticipate" ? 1.25 : 1.1);
    gfx.setDirectionalLight(-0.35, -1.0, -0.25,
                            1.1 * glow * (0.85 + skin.tint[0] * 0.25),
                            1.05 * glow * (0.85 + skin.tint[1] * 0.2),
                            1.0 * glow * (0.85 + skin.tint[2] * 0.2));

    if (!avPassPrinted && avStarted >= 2 && avLayerHits >= 4) {
        print("ATTACK_VFX_PASS cycles=" + avCycle + " plays=" + avStarted +
              " layerStarts=" + avLayerHits + "\n");
        avPassPrinted = true;
    }
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    if (avParticles != null) avParticles.render(gfx);

    local skin = skins[avSkinIndex];
    // Thin brand strip (no cards): title matches video captions.
    gfx.drawSolidRect(0.0, 0.0, 1100.0, 48.0, 0.02, 0.025, 0.03, 0.55);
    gfx.drawSolidRect(0.0, 46.0, 1100.0, 2.0, skin.tint[0], skin.tint[1], skin.tint[2], 0.95);
    gfx.drawSolidRect(18.0, 14.0, 18.0, 18.0, skin.tint[0], skin.tint[1], skin.tint[2], 1.0);

    if (avFont != null) {
        gfx.setFont(avFont);
        gfx.print(skin.title, 48.0, 14.0, 0.95, 0.95, 0.97, 1.0, 0.72);
        local phaseLabel = avPhase == "release" ? "RELEASE" :
                           avPhase == "anticipate" ? "ANTICIPATE" : "IDLE";
        gfx.print(phaseLabel, 920.0, 16.0, skin.tint[0], skin.tint[1], skin.tint[2], 0.9, 0.55);
    }
};
