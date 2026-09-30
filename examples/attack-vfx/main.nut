// AttackVfx showcase — one recipe, three elemental skins, every layer role.
// Auto-cycles fire → water → lightning. No input required (CI smoke friendly).

persist avCamera = null
persist avFloor = null
persist avSource = null
persist avTarget = null
persist avDecal = null
persist avAction = null
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

skins <- [
    { id = "skin:fire", label = "FIRE", tint = [0.95, 0.35, 0.12], meshUri = "skill:weaponSlash" },
    { id = "skin:water", label = "WATER", tint = [0.25, 0.65, 0.95], meshUri = "skill:weaponSlash" },
    { id = "skin:lightning", label = "LIGHTNING", tint = [0.85, 0.9, 1.0], meshUri = "skill:impactFlash" }
];

function avRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function recipeJson(skinId, meshUri, audioUri) {
    return "{" +
        "\"schema\":\"eve.stylize.attack-vfx\",\"schemaVersion\":1," +
        "\"id\":\"attackvfx:showcase\"," +
        "\"skin\":{" +
            "\"id\":\"" + skinId + "\"," +
            "\"styleHints\":{\"meshStyle\":\"slash\"}," +
            "\"shakeProfile\":{\"posAmp\":0.08,\"rotAmp\":0.4,\"duration\":0.12}," +
            "\"distortionProfile\":{\"strength\":0.35}" +
        "}," +
        "\"phases\":[" +
            "{" +
                "\"kind\":\"anticipate\",\"durationSeconds\":0.35," +
                "\"layers\":[" +
                    "{\"role\":\"meshVfx\",\"uri\":\"skill:chargeAura\"}," +
                    "{\"role\":\"prefab\",\"uri\":\"prefab://charge-ring\"}," +
                    "{\"role\":\"particles\",\"uri\":\"burst.particle.json\"}" +
                "]" +
            "}," +
            "{" +
                "\"kind\":\"release\",\"startCue\":\"impact\",\"durationSeconds\":0.55," +
                "\"layers\":[" +
                    "{\"role\":\"meshVfx\",\"uri\":\"" + meshUri + "\"}," +
                    "{\"role\":\"trail\",\"uri\":\"trail://blade\",\"floatParams\":{\"lifetime\":0.25}}," +
                    "{\"role\":\"camera\",\"floatParams\":{\"posAmp\":0.12,\"duration\":0.1}}," +
                    "{\"role\":\"distortion\",\"floatParams\":{\"strength\":0.4}}," +
                    "{\"role\":\"decal\",\"uri\":\"decal://scorch\",\"floatParams\":{\"size\":1.6,\"lifetime\":1.2,\"y\":0.06}}," +
                    "{\"role\":\"audio\",\"uri\":\"" + audioUri + "\",\"floatParams\":{\"volume\":0.7}}," +
                    "{\"role\":\"prefab\",\"uri\":\"prefab://impact-burst\"}," +
                    "{\"role\":\"particles\",\"uri\":\"burst.particle.json\"}" +
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

function applySkinTint() {
    local skin = skins[avSkinIndex];
    if (avSource != null)
        avSource.setTint(skin.tint[0], skin.tint[1], skin.tint[2], 1.0);
    if (avTarget != null)
        avTarget.setTint(1.0 - skin.tint[0] * 0.3, 0.35, 0.45, 1.0);
}

function playCycle() {
    local skin = skins[avSkinIndex];
    avRequire(avAction.registerRecipeJson(recipeJson(skin.id, skin.meshUri, "hit.wav")),
              "register recipe");
    local played = avRequire(avAction.play("attackvfx:showcase", 0, 2), "play");
    avHandle = played.value;
    avPhase = "anticipate";
    avTimer = 0.0;
    avStarted += 1;
    avLastEvents = "play " + skin.label + " slot=" + avHandle.slot;
    print("ATTACK_VFX_PLAY skin=" + skin.label + " cycle=" + avCycle + "\n");
}

function buildArena() {
    avCamera = eve.Camera3D();
    avCamera.setEye(6.5, 5.2, 8.5);
    avCamera.setTarget(1.0, 0.6, 0.0);
    avCamera.setUp(0.0, 1.0, 0.0);
    avCamera.setFov(48.0);
    avCamera.setAmbient(0.22, 0.24, 0.30);
    avCamera.setActive(true);

    gfx.setDirectionalLight(-0.45, -1.0, -0.3, 1.25, 1.15, 1.05);
    gfx.setBackgroundColor(0.04, 0.055, 0.08, 1.0);

    local cube = gfx.newMeshCube(1.0);
    avFloor = eve.Renderable3D();
    avFloor.setMesh(cube);
    avFloor.setPosition(1.0, -0.05, 0.0);
    avFloor.setScale(8.0, 0.1, 6.0);
    avFloor.setTint(0.14, 0.16, 0.20, 1.0);
    avFloor.setRoughness(0.92);

    avSource = eve.Renderable3D();
    avSource.setMesh(cube);
    avSource.setPosition(0.0, 0.7, 0.0);
    avSource.setScale(0.7, 1.4, 0.7);
    avSource.setRoughness(0.45);

    avTarget = eve.Renderable3D();
    avTarget.setMesh(cube);
    avTarget.setPosition(2.2, 0.55, 0.0);
    avTarget.setScale(0.9, 1.1, 0.9);
    avTarget.setRoughness(0.7);

    avDecal = eve.Decal();
    avDecal.setEnabled(gfx, true);
    applySkinTint();
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

    if (avHandle != null && avAction != null) {
        local frame = avRequire(avAction.advance(dt), "advance");
        local summary = summarizeEvents(frame);
        if (summary != "") avLastEvents = summary;

        if (avPhase == "anticipate" && avTimer >= 0.32) {
            local signaled = avRequire(
                avAction.signal(avHandle.slot, avHandle.generation, "impact"), "signal");
            avPhase = "release";
            avTimer = 0.0;
            local hitSummary = summarizeEvents(signaled);
            if (hitSummary != "") avLastEvents = "impact " + hitSummary;
            print("ATTACK_VFX_IMPACT " + avLastEvents + "\n");
        }

        if (avAction.activeCount() == 0) {
            avHandle = null;
            avPhase = "idle";
            avTimer = 0.0;
        }
    } else {
        // Brief pause between elemental cycles.
        if (avTimer >= 0.55) {
            avSkinIndex = (avSkinIndex + 1) % skins.len();
            avCycle += 1;
            applySkinTint();
            playCycle();
        }
    }

    // Orbit lightly so the arena reads in screenshots.
    local yaw = 0.35 + avTime * 0.15;
    avCamera.setEye(6.5 * cos(yaw), 5.2, 8.5 * sin(yaw) + 2.0);
    avCamera.setTarget(1.0, 0.6, 0.0);

    if (!avPassPrinted && avStarted >= 2 && avLayerHits >= 4) {
        print("ATTACK_VFX_PASS cycles=" + avCycle + " plays=" + avStarted +
              " layerStarts=" + avLayerHits + "\n");
        avPassPrinted = true;
    }
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();

    // Compact HUD strip (no cards): brand + live recipe state.
    gfx.drawSolidRect(0.0, 0.0, 1100.0, 54.0, 0.02, 0.03, 0.05, 0.72);
    gfx.drawSolidRect(0.0, 52.0, 1100.0, 2.0, skins[avSkinIndex].tint[0],
                      skins[avSkinIndex].tint[1], skins[avSkinIndex].tint[2], 0.95);

    local skin = skins[avSkinIndex];
    // Color chips for the active elemental skin / phase.
    gfx.drawSolidRect(18.0, 14.0, 22.0, 22.0, skin.tint[0], skin.tint[1], skin.tint[2], 1.0);
    local phaseColor = avPhase == "release" ? [1.0, 0.75, 0.25] :
                       avPhase == "anticipate" ? [0.45, 0.85, 1.0] : [0.35, 0.4, 0.48];
    gfx.drawSolidRect(48.0, 14.0, 14.0, 22.0, phaseColor[0], phaseColor[1], phaseColor[2], 1.0);

    // Source→target intent bar under the chips.
    local pulse = 0.35 + 0.65 * (0.5 + 0.5 * sin(avTime * 6.0));
    gfx.drawSolidRect(80.0, 22.0, 220.0 * pulse, 6.0, skin.tint[0], skin.tint[1], skin.tint[2], 0.85);
};
