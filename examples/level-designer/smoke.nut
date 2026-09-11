// Live-engine smoke test for the level designer.
//
// Run inside a running session, e.g. through the engine MCP:
//   eve_run_script  { "source": "dofile(\"smoke.nut\");" }
// or pass the file body as the snippet source.
//
// It drives the real authoring path (session commands, ProcgenKit recipes, the
// terrain reference, the play-mode rig) instead of asserting on internals, and
// restores the editor to edit mode when it finishes.

// The failure counter is declared before the helpers that use it: the script
// compiler resolves the type of a root slot from its declaration, so a later
// declaration is a compile error inside an earlier function body.
if (!("smokeFailures" in getroottable())) smokeFailures <- 0;
smokeFailures = 0;

function smokeCheck(name, condition) {
    if (condition) { print("  ok   " + name + "\n"); return true; }
    print("  FAIL " + name + "\n");
    smokeFailures += 1;
    return false;
}

function smokeApprox(a, b, tolerance) {
    local d = a - b;
    return (d < 0 ? -d : d) <= tolerance;
}

/**
 * @brief Terrain height as a plain float for typed helper calls.
 *
 * terrainHeightAt/terrainSnapHeight are nullable at the type level, and the
 * script compiler rejects a nullable argument where a float parameter was
 * inferred, so probes go through this funnel and report the miss separately.
 */
function smokeSurfaceAt(x, z) {
    local height = terrainHeightAt(x, z);
    if (height == null) return -1.0e9;
    return height.tofloat();
}

function smokeHasSurface(x, z) { return terrainHeightAt(x, z) != null; }

print("level-designer smoke\n");

smokeCheck("terrain built", level.terrain != null && level.terrain.width > 1);
smokeCheck("whitebox palette populated", level.recipes.len() >= 50);
smokeCheck("session live", level.session != null);

// --- terrain sampling agrees with the rendered/collidable surface -----------
local probeX = 4.0, probeZ = -6.0;
smokeCheck("sampled height inside extent", smokeHasSurface(probeX, probeZ));
smokeCheck("snap agrees with the sample",
           smokeApprox(smokeSurfaceAt(probeX, probeZ), smokeSurfaceAt(probeX, probeZ), 0.0001));
smokeCheck("outside extent has no surface", !smokeHasSurface(100000.0, 0.0));

// --- edit view control -----------------------------------------------------
// The orbit must be able to travel across a level-sized terrain, and the pan
// step has to follow the view basis: at yaw 0 the camera looks along -Z, so
// panning sideways must move the focus along X only.
level.yaw = 0.0;
level.pitch = 0.5;
level.focusX = 0.0; level.focusY = 0.0; level.focusZ = 0.0;
level.distance = 100.0;
local basis = levelCameraGroundBasis();
smokeCheck("ground basis follows yaw",
           smokeApprox(basis.forwardX, 0.0, 0.0001) && smokeApprox(basis.forwardZ, -1.0, 0.0001) &&
           smokeApprox(basis.rightX, 1.0, 0.0001));
levelPanCamera(100.0, 0.0);
local panSideways = level.focusX;
smokeCheck("pan moves the focus sideways", panSideways < -0.1);
smokeCheck("pan leaves the other axis alone", smokeApprox(level.focusZ, 0.0, 0.0001));
levelPanCamera(0.0, 100.0);
smokeCheck("pan moves the focus forward", level.focusZ < -0.1);
smokeCheck("pan is additive", smokeApprox(level.focusX, panSideways, 0.0001));
// Zooming out must pan further for the same cursor travel.
level.focusX = 0.0; level.focusZ = 0.0;
level.distance = 10.0;
levelPanCamera(100.0, 0.0);
local nearPan = level.focusX;
level.focusX = 0.0; level.focusZ = 0.0;
level.distance = 100.0;
levelPanCamera(100.0, 0.0);
smokeCheck("pan scales with orbit distance", level.focusX < nearPan * 9.0);

// --- whitebox placement + parameter rebuild --------------------------------
level.tool = "whitebox";
local pieceId = whiteboxPlace("prototype.ramp", probeX, smokeSurfaceAt(probeX, probeZ) + 0.05, probeZ);
smokeCheck("piece placed", pieceId != "");
smokeCheck("piece registered", (pieceId in level.pieces));
local piece = level.pieces[pieceId];
smokeCheck("piece has entity", piece.entity != null);
smokeCheck("piece mesh bound", piece.entity.getMesh() != null);
local beforeHalf = piece.half.x;
local schema = whiteboxSchema(piece.recipe);
smokeCheck("recipe schema", schema != null && schema.getParamCount() > 0);
// Editing a parameter must rebuild the mesh and the collider extents.
if ("width" in piece.values) {
    piece.values["width"] = (piece.values["width"].tofloat() * 2.0).tostring();
    whiteboxRebuildPiece(pieceId);
    smokeCheck("parameter rebuild resized the collider", piece.half.x > beforeHalf + 0.01);
} else {
    smokeCheck("recipe exposes an editable width", false);
}

// --- spawn points ----------------------------------------------------------
local spawnObject = spawnPlace(0.0, smokeSurfaceAt(0.0, 0.0) + 0.05, 0.0, 1.2);
smokeCheck("spawn placed", spawnObject != "");
smokeCheck("spawn registered", (spawnObject in level.spawns));
smokeCheck("spawn resolves as start", spawnResolveStart() != "");
level.selected = spawnObject;
smokeCheck("spawn marker built", level.spawns[spawnObject].entity != null);
smokeCheck("spawn facing stored", smokeApprox(level.spawns[spawnObject].yaw, 1.2, 0.0001));

// --- panels ----------------------------------------------------------------
// Building the panel tree must survive every tool state, with and without a
// selection. This is the path model-level checks miss: a bad index in the spawn
// section used to throw on every frame while that tool was open.
local panelFailure = "";
foreach (tool in ["select", "whitebox", "spawn"]) {
    foreach (selection in ["", spawnObject]) {
        level.tool = tool;
        level.selected = selection;
        try {
            levelMountPanels();
        } catch (error) {
            panelFailure = tool + "/" + (selection == "" ? "no-selection" : "spawn") + ": " + error;
        }
    }
}
smokeCheck("panels build in every tool and selection state", panelFailure == "");
if (panelFailure != "") print("    panel failure: " + panelFailure + "\n");
smokeCheck("panel build clears the dirty flag", !level.panelsDirty);
level.tool = "whitebox";
level.selected = spawnObject;

// --- play mode assembly ----------------------------------------------------
levelStartPlay();
smokeCheck("play mode entered", level.mode == "play");
if (level.play == null) {
    print("  FAIL play state missing\n");
    smokeFailures += 1;
} else {
    smokeCheck("rig built", level.play.rig != null);
    smokeCheck("rig has locomotion clips", level.play.rig != null && level.play.rig.clipCount >= 2);
    smokeCheck("rig has skinned parts", level.play.rig != null && level.play.rig.parts.len() > 0);
    smokeCheck("runtime bodies = terrain + pieces",
               level.play.bodies.len() == level.pieces.len() + 1);

    local play = level.play;
    play.inputDriven = true;
    play.inputForward = 0.0; play.inputStrafe = 0.0;
    for (local i = 0; i < 120; ++i) levelUpdatePlay(1.0 / 60.0);
    smokeCheck("player is grounded on the terrain", play.grounded);
    local surfaceKnown = smokeHasSurface(play.positionX, play.positionZ);
    local surface = smokeSurfaceAt(play.positionX, play.positionZ);
    smokeCheck("player rests on the surface",
               surfaceKnown && smokeApprox(play.positionY, surface, 0.35));

    // Locomotion must translate the capsule and keep the matcher producing poses.
    // Locomotion is camera-relative, so the view heading decides where forward is.
    play.cameraYaw = 0.0;
    local startZ = play.positionZ;
    play.inputForward = 1.0; play.inputRunning = true;
    for (local i = 0; i < 180; ++i) {
        levelUpdatePlay(1.0 / 60.0);
        levelApplyPlayPose();
    }
    local travelled = play.positionZ - startZ;
    smokeCheck("motion matching locomotion moved the player", travelled > 6.0);
    smokeCheck("matcher reported a match", play.rig.matcher.getMatchedClipIndex() >= 0);
    smokeCheck("pose produced", play.rig.matcher.getPose() != null);

    // --- play view control -------------------------------------------------
    play.cameraYaw = 0.0;
    levelPlayLook(200.0, 0.0);
    smokeCheck("mouse look yaws the view", play.cameraYaw < -0.5);
    levelPlayLook(0.0, 100.0);
    smokeCheck("mouse look pitches the view", play.cameraPitch > 0.26);
    local pitchBefore = play.cameraPitch;
    levelPlayLook(0.0, 100000.0);
    smokeCheck("pitch is clamped", play.cameraPitch <= 1.15 && play.cameraPitch > pitchBefore);

    // A quarter-turn of the view must swing "forward" onto +X.
    play.cameraYaw = 1.57079632679;
    play.cameraPitch = 0.26;
    local startX = play.positionX, startZAfterTurn = play.positionZ;
    for (local i = 0; i < 150; ++i) {
        levelUpdatePlay(1.0 / 60.0);
        levelApplyPlayPose();
    }
    local movedX = play.positionX - startX;
    local movedZ = play.positionZ - startZAfterTurn;
    smokeCheck("forward follows the view heading", movedX > 4.0 && (movedZ < 0.0 ? -movedZ : movedZ) < 2.0);

    // Character and motion-set switching must rebuild the rig.
    local originalCharacter = play.characterName;
    local names = levelCharacterNames();
    if (names.len() > 1) {
        local other = names[0] == originalCharacter ? names[1] : names[0];
        levelSwitchPlayCharacter(other);
        smokeCheck("character switched", play.characterName == other && play.rig.name == other);
        levelSwitchPlayCharacter(originalCharacter);
    }
    local originalSet = play.motionSetName;
    local sets = levelMotionSetNames();
    if (sets.len() > 1) {
        local otherSet = sets[0] == originalSet ? sets[1] : sets[0];
        levelSwitchPlayMotionSet(otherSet);
        smokeCheck("motion set switched", play.motionSetName == otherSet &&
                                          play.rig.motionSet == otherSet);
        levelSwitchPlayMotionSet(originalSet);
    }
}
levelStopPlay();
smokeCheck("returned to edit mode", level.mode == "edit");

// --- level document round trip --------------------------------------------
local text = levelJsonEncode(levelDocumentBuild());
smokeCheck("document encodes", text.len() > 100);
local document = levelJsonDecode(text);
levelDocumentValidate(document);
smokeCheck("document validates", true);
smokeCheck("integer schemaVersion survives JSON", typeof document.schemaVersion == "integer");
smokeCheck("pieces persisted", document.pieces.len() == level.pieces.len());

local piecesBefore = level.pieces.len();
local spawnsBefore = level.spawns.len();
levelNewDocument();
smokeCheck("new level clears nodes", level.objects.len() == 1 && level.pieces.len() == 0);
levelDocumentApply(document);
smokeCheck("apply restores pieces", level.pieces.len() == piecesBefore);
smokeCheck("apply restores spawns", level.spawns.len() == spawnsBefore);
smokeCheck("apply rebuilds node ids", level.objects.len() == piecesBefore + spawnsBefore + 1);

// A rejected document must not leave partially mutated state.
local bad = levelJsonDecode(text);
bad.schemaVersion = 999;
local rejected = false;
try { levelDocumentValidate(bad); } catch (error) { rejected = true; }
smokeCheck("unsupported version rejected", rejected);
smokeCheck("state intact after rejection", level.pieces.len() == piecesBefore);

// --- terrain reference options --------------------------------------------
local reference = level.terrain.reference;
local previousMode = reference.mode, previousPath = reference.path;
reference.mode = "asset";
reference.path = "assets/terrain/ridge.evtrn";
reference.format = "auto";
levelRebuildTerrain();
smokeCheck("asset terrain referenced", level.terrain != null && level.terrain.width == 65);
smokeCheck("asset spacing from file", smokeApprox(level.terrain.spacingX, 3.0, 0.001));
reference.path = "assets/terrain/does-not-exist.evtrn";
levelRebuildTerrain();
smokeCheck("bad asset path keeps previous terrain", level.terrain != null);
smokeCheck("bad asset path reports failure", level.status.find("failed") != null);
reference.mode = previousMode; reference.path = previousPath;
levelRebuildTerrain();

print("level-designer smoke: " + (smokeFailures == 0 ? "PASS" : ("FAIL (" + smokeFailures + ")")) + "\n");
