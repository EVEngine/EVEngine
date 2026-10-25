// Level document persistence.
//
// Schema: `eve.level3d`, version 2 (version 1 migrates with static pieces and an empty block library).
//
// The v2 envelope adds actual terrain `surface`, reusable block `library`, and
// per-piece `motion`; v1 loads with no library and static pieces.
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
function levelDocumentVersion() { return 2; }

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
        if (levelObject(id) != null) pieces.push({object = id, recipe = piece.recipe, values = piece.values, motion = piece.motion});
    local spawns = [];
    foreach (id, spawn in level.spawns)
        if (levelObject(id) != null) spawns.push({object = id, yaw = spawn.yaw,
                     character = spawn.character, motionSet = spawn.motionSet});

    local sceneText = level.session.saveJson();
    return {
        schema = levelDocumentSchema(),
        schemaVersion = levelDocumentVersion(),
        terrain = level.terrain != null ? level.terrain.reference : terrainDefaultReference(),
        surface = level.terrain != null ? terrainEncodeSurface(level.terrain) : null,
        library = levelCopy(level.library),
        scene = levelJsonDecode(sceneText),
        pieces = pieces,
        spawns = spawns,
        player = {character = levelActiveCharacterName(), motionSet = levelActiveMotionSetName()}
    };
}

function levelSaveDocument() {
    terrainEndStroke();
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
            key == "pieces" || key == "spawns" || key == "player" || key == "surface" || key == "library") continue;
        throw "unknown field '" + key + "'";
    }
    if (!("schema" in document) || document.schema != levelDocumentSchema())
        throw "not an " + levelDocumentSchema() + " document";
    if (!("schemaVersion" in document)) throw "missing 'schemaVersion'";
    local version = document.schemaVersion;
    // JSON has one number type, so an integral float is accepted as a version.
    if (typeof version == "float" && version == version.tointeger().tofloat()) version = version.tointeger();
    if (typeof version != "integer") throw "'schemaVersion' must be an integer";
    if (version != 1 && version != levelDocumentVersion())
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
    foreach(key,value in raw) if(!(key in reference)) throw "Unknown terrain reference field " + key;
    if("sampler" in raw) {
        if(typeof raw.sampler!="table") throw "Invalid terrain sampler";
        foreach(key,value in raw.sampler) if(!(key in reference.sampler)) throw "Unknown terrain sampler field " + key;
    }
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
    if(level.mode!="edit") throw "Stop Play before loading a document";
    levelDocumentValidate(document);
    local reference=levelDocumentTerrainReference(document.terrain);
    if(reference.mode!="generated" && reference.mode!="asset") throw "Unknown terrain mode";
    if(reference.width<2 || reference.height<2 || reference.width>1025 || reference.height>1025 ||
        reference.spacing<=0.0 || reference.heightScale<=0.0) throw "Invalid terrain dimensions or scale";
    local surface=("surface" in document) ? document.surface : null;
    if(surface!=null && typeof surface!="string") throw "surface must be a terrain document string";
    local library=("library" in document) ? document.library : [];
    if(typeof library!="array" || library.len()>256) throw "Invalid block library";
    local sceneText=levelJsonEncode(document.scene);
    // Validate hierarchy offline, including cycles and TRS, before resource staging.
    local opened=eve.SceneEditorModule().createSession("level.load.validation");
    if(!opened.ok) throw opened.status.summary;
    local checked=opened.value.restoreJson(sceneText);
    if(!checked.ok) throw checked.status.summary;
    levelValidateHierarchyGeometry(document.scene.objects);
    local ids={},seen={};
    foreach(object in document.scene.objects) ids[object.id] <- true;
    function validateRecord(record,kind) {
        if(typeof record!="table" || !("object" in record) || !(record.object in ids) || record.object=="root")
            throw "Invalid " + kind + " scene object";
        if(record.object in seen) throw "Duplicate semantic object " + record.object;
        seen[record.object] <- true;
        foreach(key,value in record) {
            if(key=="object" || (kind=="piece" && (key=="recipe" || key=="values" || key=="motion")) ||
                (kind=="spawn" && (key=="yaw" || key=="character" || key=="motionSet"))) continue;
            throw "Unknown " + kind + " field: " + key;
        }
        if(kind=="piece") {
            if(!("recipe" in record) || typeof record.recipe!="string" || record.recipe.find("prototype.")!=0 ||
                !("values" in record) || typeof record.values!="table") throw "Invalid piece recipe";
            if("motion" in record && record.motion!="static" && record.motion!="dynamic") throw "Invalid body motion";
        } else {
            if("yaw" in record && typeof record.yaw!="float" && typeof record.yaw!="integer") throw "Invalid spawn yaw";
            if("character" in record && record.character!="" && levelCharacterNames().find(record.character)==null)
                throw "Unknown spawn character";
            if("motionSet" in record && record.motionSet!="" && levelMotionSetNames().find(record.motionSet)==null)
                throw "Unknown spawn motion set";
        }
    }
    foreach(record in document.pieces) validateRecord(record,"piece");
    foreach(record in document.spawns) validateRecord(record,"spawn");
    if("player" in document) {
        foreach(key,value in document.player) {
            if(key!="character" && key!="motionSet") throw "Unknown player field " + key;
            if(key=="character" && levelCharacterNames().find(value)==null) throw "Unknown player character";
            if(key=="motionSet" && levelMotionSetNames().find(value)==null) throw "Unknown player motion set";
        }
    }
    foreach(record in library) levelValidateBlockRecord(record);
    local stagedTerrain=null,stagedPieces={};
    try {
        stagedTerrain=terrainBuild(reference,surface,false);
        if(stagedTerrain==null) throw level.status;
        foreach(record in document.pieces) {
            local schema=whiteboxSchema(record.recipe);
            if(schema==null) throw "Unknown recipe " + record.recipe;
            local piece={recipe=record.recipe,values={},motion=("motion" in record) ? record.motion : "static",
                entity=null,half=null,bounds=null};
            foreach(key,value in record.values) piece.values[key] <- value.tostring();
            stagedPieces[record.object] <- piece;
            if(!whiteboxCreateRenderable(piece,schema)) throw level.status;
            piece.entity.setVisible(false);
        }
        local restored=level.session.restoreJson(sceneText);
        if(!restored.ok) throw restored.status.summary;
    } catch(error) {
        if(stagedTerrain!=null) levelReleaseRenderable(stagedTerrain.entity);
        foreach(id,piece in stagedPieces) levelReleaseRenderable(piece.entity);
        throw error;
    }
    whiteboxCancelDrag();
    foreach(id,piece in level.pieceHistory) levelReleaseRenderable(piece.entity);
    level.pieceHistory = {};
    terrainDispose();level.terrain=stagedTerrain;stagedTerrain.entity.setVisible(true);
    foreach(id,piece in level.pieces) levelReleaseRenderable(piece.entity);
    foreach(id,spawn in level.spawns) {
        spawnReleaseProjection(spawn);
    }
    spawnReleaseRigCache();
    level.pieces=stagedPieces;level.spawns={};level.library=levelCopy(library);
    foreach(record in document.spawns) {
        local spawn=spawnDefault();
        if("yaw" in record) spawn.yaw=record.yaw;
        if("character" in record) spawn.character=record.character;
        if("motionSet" in record) spawn.motionSet=record.motionSet;
        level.spawns[record.object] <- spawn;
    }
    if("player" in document) {
        if("character" in document.player) level.characterName=document.player.character;
        if("motionSet" in document.player) level.motionSetName=document.player.motionSet;
    }
    level.selected="";level.selection.clear();level.scope="root";level.blockIndex=-1;
    level.terrainUndo.clear();level.terrainRedo.clear();level.strokeBefore=null;level.terrainDraft=null;
    scene.updateTransformsAll();levelRefreshObjects();
    foreach(id,spawn in level.spawns) spawnBuildMarker(spawn,levelObject(id));
    whiteboxReconcile();whiteboxSyncTransforms();spawnSyncTransforms();
    // A load establishes a new editing baseline; old semantic records are released.
    // Reopen on the loaded host to establish an empty command history.
    local reopened=eve.SceneEditorModule().createLiveSession("level-designer.scene",levelHost());
    if(!reopened.ok) throw reopened.status.summary;
    level.session=reopened.value;
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
    terrainEndStroke();
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
        levelReleaseRenderable(piece.entity);
    foreach (id, spawn in level.spawns) {
        spawnReleaseProjection(spawn);
    }
    whiteboxCancelDrag();
    foreach(id,piece in level.pieceHistory) levelReleaseRenderable(piece.entity);
    level.pieceHistory = {};
    spawnReleaseRigCache();
    level.pieces = {};
    level.spawns = {};
    level.selected = ""; level.selection.clear(); level.scope = "root";
    level.library.clear(); level.blockIndex = -1;
    level.terrainUndo.clear(); level.terrainRedo.clear();
    terrainDispose();
    if (terrainBuild(terrainDefaultReference()) == null) { level.status = "terrain rebuild failed"; return; }
    local reopened=eve.SceneEditorModule().createLiveSession("level-designer.scene",levelHost());
    if(!reopened.ok) throw reopened.status.summary;
    level.session=reopened.value;
    level.terrainDraft=null;
    levelFrameTerrain();
    levelRefreshPanels();
    level.status = "new level";
}
