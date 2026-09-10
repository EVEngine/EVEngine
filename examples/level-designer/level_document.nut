// Level document persistence.
//
// Schema: `eve.level3d`, version 1.
//
//   {"schema":"eve.level3d","schemaVersion":1,
//    "terrain":{...terrain reference: mode/seed/sampler or asset path...},
//    "scene":{...verbatim eve.scene.hierarchy v1 payload from the session...},
//    "pieces":[{"object":..,"recipe":..,"values":{..}}],
//    "spawns":[{"object":..,"yaw":..,"character":..,"motionSet":..}],
//    "player":{"character":..,"motionSet":..}}
//
// Ownership: the embedded `scene` payload stays the single authority for node
// identity, parenting and TRS (it round-trips through SceneEditorSession), while
// this document owns the semantic layer the scene format deliberately excludes:
// which node is which whitebox recipe, which node is a spawn point, and how to
// obtain the terrain.
//
// Unknown-field policy: unknown keys are preserved on load only for the embedded
// `scene` payload (owned by the scene module); unknown keys elsewhere are
// rejected with a diagnostic instead of being dropped silently.

function levelDocumentSchema() { return "eve.level3d"; }
function levelDocumentVersion() { return 1; }

function levelDocumentFilesystem() {
    local fs = eve.Filesystem();
    if (fs.getIdentity() == "" && !fs.setIdentity("level-designer", false))
        throw "level document: cannot claim a save identity";
    return fs;
}

function levelDocumentPath() { return level.saveName; }

function levelDocumentBuild() {
    local pieces = [];
    foreach (id, piece in level.pieces)
        pieces.push({object = id, recipe = piece.recipe, values = piece.values});
    local spawns = [];
    foreach (id, spawn in level.spawns)
        spawns.push({object = id, yaw = spawn.yaw,
                     character = spawn.character, motionSet = spawn.motionSet});

    local sceneText = level.session.saveJson();
    return {
        schema = levelDocumentSchema(),
        schemaVersion = levelDocumentVersion(),
        terrain = level.terrain != null ? level.terrain.reference : terrainDefaultReference(),
        scene = levelJsonDecode(sceneText),
        pieces = pieces,
        spawns = spawns,
        player = {character = levelActiveCharacterName(), motionSet = levelActiveMotionSetName()}
    };
}

function levelSaveDocument() {
    try {
        local text = levelJsonEncode(levelDocumentBuild());
        local fs = levelDocumentFilesystem();
        if (!fs.setupWriteDirectory()) { level.status = "save directory unavailable"; return false; }
        if (!fs.writeTextAtomic(levelDocumentPath(), text)) { level.status = "save failed"; return false; }
        level.status = "saved " + levelDocumentPath() + " (" + text.len() + " bytes)";
        levelMarkPanelsDirty();
        return true;
    } catch (error) {
        level.status = "save failed: " + error;
        return false;
    }
}

function levelRequireTable(document, key) {
    if (!(key in document)) throw "missing '" + key + "'";
    if (typeof document[key] != "table") throw "'" + key + "' must be an object";
    return document[key];
}

function levelRequireArray(document, key) {
    if (!(key in document)) throw "missing '" + key + "'";
    if (typeof document[key] != "array") throw "'" + key + "' must be an array";
    return document[key];
}

/** @brief Validate the envelope before any observable state is mutated. */
function levelDocumentValidate(document) {
    foreach (key, value in document) {
        if (key == "schema" || key == "schemaVersion" || key == "terrain" || key == "scene" ||
            key == "pieces" || key == "spawns" || key == "player") continue;
        throw "unknown field '" + key + "'";
    }
    if (!("schema" in document) || document.schema != levelDocumentSchema())
        throw "not an " + levelDocumentSchema() + " document";
    if (!("schemaVersion" in document)) throw "missing 'schemaVersion'";
    local version = document.schemaVersion;
    // JSON has one number type, so an integral float is accepted as a version.
    if (typeof version == "float" && version == version.tointeger().tofloat()) version = version.tointeger();
    if (typeof version != "integer") throw "'schemaVersion' must be an integer";
    if (version != levelDocumentVersion())
        throw "unsupported schemaVersion " + version +
              " (this build reads version " + levelDocumentVersion() + ")";
    levelRequireTable(document, "terrain");
    levelRequireTable(document, "scene");
    levelRequireArray(document, "pieces");
    levelRequireArray(document, "spawns");
    if ("player" in document) levelRequireTable(document, "player");
    return true;
}

/** @brief Reference table -> the terrain reference shape level_terrain expects. */
function levelDocumentTerrainReference(raw) {
    local reference = terrainDefaultReference();
    if ("mode" in raw) reference.mode = raw.mode;
    if ("path" in raw) reference.path = raw.path;
    if ("format" in raw) reference.format = raw.format;
    if ("seed" in raw) reference.seed = raw.seed.tointeger();
    if ("width" in raw) reference.width = raw.width.tointeger();
    if ("height" in raw) reference.height = raw.height.tointeger();
    if ("spacing" in raw) reference.spacing = raw.spacing.tofloat();
    if ("heightScale" in raw) reference.heightScale = raw.heightScale.tofloat();
    if ("originX" in raw) reference.originX = raw.originX.tofloat();
    if ("originZ" in raw) reference.originZ = raw.originZ.tofloat();
    if ("sampler" in raw && typeof raw.sampler == "table")
        foreach (key, value in raw.sampler) reference.sampler[key] = value;
    return reference;
}

/**
 * @brief Replace the live level with `document`.
 *
 * Everything is validated and the replacement is staged before the session is
 * mutated, so a rejected document leaves the editor exactly as it was.
 */
function levelDocumentApply(document) {
    local terrainReference = levelDocumentTerrainReference(levelRequireTable(document, "terrain"));
    local scenePayload = levelJsonEncode(levelRequireTable(document, "scene"));
    local pieces = levelRequireArray(document, "pieces");
    local spawns = levelRequireArray(document, "spawns");

    // Validate the piece/spawn records before touching the session.
    foreach (record in pieces) {
        if (typeof record != "table") throw "piece records must be objects";
        if (!("object" in record) || !("recipe" in record)) throw "piece record needs object and recipe";
        if (!("values" in record) || typeof record.values != "table") throw "piece record needs a values object";
    }
    foreach (record in spawns) {
        if (typeof record != "table") throw "spawn records must be objects";
        if (!("object" in record)) throw "spawn record needs object";
    }

    local restored = level.session.restoreJson(scenePayload);
    if (!restored.ok) throw restored.status.summary;

    terrainDispose();
    if (terrainBuild(terrainReference) == null) throw level.status;

    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(false);
    foreach (id, spawn in level.spawns) {
        if (spawn.entity != null) spawn.entity.setVisible(false);
        if (spawn.facing != null) spawn.facing.setVisible(false);
    }
    level.pieces = {};
    level.spawns = {};

    levelRefreshObjects();
    foreach (record in pieces) {
        local piece = {recipe = record.recipe, values = {}, entity = null, half = null};
        foreach (key, value in record.values) piece.values[key] <- value.tostring();
        level.pieces[record.object] <- piece;
        whiteboxRebuildPiece(record.object);
    }
    foreach (record in spawns) {
        local spawn = spawnDefault();
        if ("yaw" in record) spawn.yaw = record.yaw.tofloat();
        if ("character" in record) spawn.character = record.character;
        if ("motionSet" in record) spawn.motionSet = record.motionSet;
        level.spawns[record.object] <- spawn;
        spawnBuildMarker(spawn, levelObject(record.object));
    }
    if ("player" in document) {
        local player = document.player;
        if ("character" in player) level.characterName = player.character;
        if ("motionSet" in player) level.motionSetName = player.motionSet;
    }
    level.selected = "";
    whiteboxSyncTransforms();
    spawnSyncTransforms();
    return true;
}

function levelLoadDocument() {
    try {
        local fs = levelDocumentFilesystem();
        local text = fs.readText(levelDocumentPath());
        if (text == null || text.len() == 0) { level.status = "no saved level at " + levelDocumentPath(); return false; }
        local document = levelJsonDecode(text);
        if (typeof document != "table") throw "document root must be an object";
        levelDocumentValidate(document);
        levelDocumentApply(document);
        levelFrameTerrain();
        level.status = "loaded " + levelDocumentPath();
        levelRefreshPanels();
        return true;
    } catch (error) {
        // A rejected document never leaves a partially mutated editor behind.
        level.status = "load failed: " + error;
        levelRefreshPanels();
        return false;
    }
}

/** @brief Discard authored content and start from an empty level. */
function levelNewDocument() {
    if (level.mode == "play") levelStopPlay();
    // Delete authored nodes through the session so the hierarchy stays the single
    // authority. Deepest nodes go first: the session refuses to delete a node that
    // still has children.
    local order = levelHierarchyOrder();
    for (local i = order.len() - 1; i >= 0; --i) {
        if (order[i].id == "root") continue;
        local removed = level.session.execute("scene.object.delete.v1", {object = order[i].id});
        if (!removed.ok) level.status = removed.status.summary;
    }
    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(false);
    foreach (id, spawn in level.spawns) {
        if (spawn.entity != null) spawn.entity.setVisible(false);
        if (spawn.facing != null) spawn.facing.setVisible(false);
    }
    level.pieces = {};
    level.spawns = {};
    level.selected = "";
    terrainDispose();
    if (terrainBuild(terrainDefaultReference()) == null) { level.status = "terrain rebuild failed"; return; }
    levelFrameTerrain();
    levelRefreshPanels();
    level.status = "new level";
}
