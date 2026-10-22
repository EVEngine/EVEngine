// Project MCP boundary, v1. No eval and no parallel scene model: all object
// mutations use the same SceneEditorSession as the mouse UI. Main-thread only.
// JSON inputs/outputs are owning data. Reject unknown fields and invalid values
// before generation/publication. Each create is one undoable scene transaction.

function wbFail(code, message) { throw {code = code, message = message}; }

function wbNumber(value, label, minimum, maximum) {
    local kind = typeof value;
    if ((kind != "integer" && kind != "float") || value != value ||
        value < minimum || value > maximum) wbFail("invalid_argument", label + " out of range");
    return value.tofloat();
}

function wbVector(args, key, fallback, minimum, maximum) {
    if (!(key in args)) return clone fallback;
    local value = args[key];
    if (typeof value != "array" || value.len() != 3)
        wbFail("invalid_argument", key + " must contain three numbers");
    local result = [];
    foreach (number in value) result.push(wbNumber(number, key, minimum, maximum));
    return result;
}

function wbString(value, label) {
    if (typeof value != "string" || value.len() == 0 || value.len() > 128)
        wbFail("invalid_argument", label + " must be a nonempty string (max 128)");
    return value;
}

function wbEditable(args) {
    if (level.mode != "edit" || level.buildDrag != null || level.gizmo.isDragging())
        wbFail("busy", "Stop play mode or finish the current mouse gesture first");
    if ("expectedRevision" in args) {
        if (typeof args.expectedRevision != "integer" || args.expectedRevision < 0)
            wbFail("invalid_argument", "expectedRevision must be a nonnegative integer");
        if (args.expectedRevision != level.session.getRevision())
            wbFail("stale_revision", "Read whitebox_scene_v1 and retry against its revision");
    }
}

function wbObject(id) {
    wbString(id, "object");
    local object = levelObject(id);
    if (object == null || !(id in level.pieces)) wbFail("not_found", "No whitebox object: " + id);
    return object;
}

/** @brief Copy and validate recipe parameters against the library's own schema. */
function wbValues(schema, overrides) {
    if (typeof overrides != "table") wbFail("invalid_argument", "params must be an object");
    local values = whiteboxDefaultValues(schema);
    foreach (key, value in overrides) {
        if (!(key in values)) wbFail("invalid_argument", "Unknown recipe parameter: " + key);
        for (local i = 0; i < schema.getParamCount(); ++i) {
            if (schema.getParamKey(i) != key) continue;
            local kind = schema.getParamKind(i);
            if (kind == "int" || kind == "float") {
                local minimum = schema.paramHasMinimum(i) ? schema.getParamMinimum(i) : -100000.0;
                local maximum = schema.paramHasMaximum(i) ? schema.getParamMaximum(i) : 100000.0;
                local number = wbNumber(value, key, minimum, maximum);
                if (kind == "int" && number != number.tointeger())
                    wbFail("invalid_argument", key + " must be integral");
                values[key] = kind == "int" ? number.tointeger().tostring() : number.tostring();
            } else if (kind == "bool") {
                if (typeof value != "bool") wbFail("invalid_argument", key + " must be boolean");
                values[key] = value ? "1" : "0";
            } else {
                wbString(value, key);
                if (kind == "choice") {
                    local found = false;
                    for (local c = 0; c < schema.getParamChoiceCount(i); ++c)
                        if (schema.getParamChoice(i, c) == value) found = true;
                    if (!found) wbFail("invalid_argument", "Invalid choice for " + key);
                }
                values[key] = value;
            }
        }
    }
    return values;
}

function wbCatalog(args) {
    if (!("recipe" in args)) return {recipes = clone level.recipes};
    if (level.recipes.find(args.recipe) == null) wbFail("not_found", "Unknown recipe");
    local schema = whiteboxSchema(args.recipe);
    if (schema == null) wbFail("generation_failed", level.status);
    local params = [];
    for (local i = 0; i < schema.getParamCount(); ++i) {
        local p = {key = schema.getParamKey(i), kind = schema.getParamKind(i),
            defaultValue = schema.getParamDefault(i), choices = []};
        if (schema.paramHasMinimum(i)) p.minimum <- schema.getParamMinimum(i);
        if (schema.paramHasMaximum(i)) p.maximum <- schema.getParamMaximum(i);
        for (local c = 0; c < schema.getParamChoiceCount(i); ++c)
            p.choices.push(schema.getParamChoice(i, c));
        params.push(p);
    }
    return {recipe = args.recipe, name = schema.getDisplayName(), params = params};
}

function wbScene(args) {
    local objects = [];
    foreach (object in level.objects) {
        if (!(object.id in level.pieces)) continue;
        if ("object" in args && args.object != object.id) continue;
        local p = level.pieces[object.id];
        objects.push({id = object.id, name = object.name, recipe = p.recipe,
            params = clone p.values, transform = clone object.transform});
    }
    return {objects = objects, mode = level.mode, selected = level.selected};
}

/** @brief Generate and publish one complete whitebox specification.
 * @cost Mesh generation/upload dominates a transform edit; amortized per object.
 * No scene mutation until all input validation and mesh generation succeed.
 */
function wbCreate(args) {
    wbEditable(args);
    local id = wbString(args.id, "id");
    if (levelObject(id) != null || id in level.pieceHistory)
        wbFail("already_exists", "Object id is present or reserved by undo history: " + id);
    if (level.recipes.find(args.recipe) == null) wbFail("not_found", "Unknown recipe");
    local position = wbVector(args, "position", [0, 0, 0], -10000, 10000);
    local rotation = wbVector(args, "rotation", [0, 0, 0], -360, 360);
    local scale = wbVector(args, "scale", [1, 1, 1], 0.01, 1000);
    local schema = whiteboxSchema(args.recipe);
    if (schema == null) wbFail("generation_failed", level.status);
    local name = "name" in args ? wbString(args.name, "name") : schema.getDisplayName();
    local values = wbValues(schema, "params" in args ? args.params : {});
    local piece = {recipe = args.recipe, values = values, entity = null, half = null};
    if (!whiteboxCreateRenderable(piece, schema)) wbFail("generation_failed", level.status);
    piece.entity.setVisible(false);
    local published = whiteboxPublishPrepared(piece, id, name, position, rotation, scale);
    if (!published.ok) wbFail(published.code, published.message);
    return {object = id};
}

function wbTransform(args) {
    wbEditable(args);
    local object = wbObject(args.object), t = object.transform;
    local payload = {object = object.id,
        position = wbVector(args, "position", [t.x, t.y, t.z], -10000, 10000),
        rotation = wbVector(args, "rotation", [t.rotationX, t.rotationY, t.rotationZ], -360, 360),
        scale = wbVector(args, "scale", [t.scaleX, t.scaleY, t.scaleZ], 0.01, 1000)};
    if (!levelCommand("scene.transform.set.v1", payload)) wbFail("scene_rejected", level.status);
    scene.updateTransformsAll();
    whiteboxSyncTransforms();
    levelMarkPanelsDirty();
    return {object = object.id};
}

function wbDelete(args) {
    wbEditable(args);
    local object = wbObject(args.object);
    if (!levelCommand("scene.object.delete.v1", {object = object.id}))
        wbFail("scene_rejected", level.status);
    levelMarkPanelsDirty();
    return {object = object.id};
}

function wbSave(args) {
    wbEditable(args);
    local name = "filename" in args ? wbString(args.filename, "filename") : "agent-whitebox.json";
    foreach (ch in name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.'))
            wbFail("invalid_argument", "Use a filename, not a path");
    if (name.len() < 6 || name.slice(name.len() - 5) != ".json" || name.find("..") != null)
        wbFail("invalid_argument", "Expected a .json filename without '..'");
    local previous = level.saveName;
    level.saveName = name;
    local saved = levelSaveDocument();
    level.saveName = previous;
    if (!saved) wbFail("save_failed", level.status);
    return {filename = name, location = "engine user save directory", objects = level.pieces.len()};
}

function wbView(args) {
    local focus = wbVector(args, "focus", [level.focusX, level.focusY, level.focusZ], -10000, 10000);
    local distance = "distance" in args ? wbNumber(args.distance, "distance", 3, 3000) : level.distance;
    local yaw = "yaw" in args ? wbNumber(args.yaw, "yaw", -360, 360) : level.yaw;
    local pitch = "pitch" in args ? wbNumber(args.pitch, "pitch", 0.08, 1.45) : level.pitch;
    if (level.mode != "edit") wbFail("busy", "Stop play mode first");
    level.focusX = focus[0]; level.focusY = focus[1]; level.focusZ = focus[2];
    level.distance = distance; level.yaw = yaw; level.pitch = pitch;
    levelUpdateOrbitCamera();
    return {focus = focus, distance = distance};
}

function wbSurface(args) {
    wbEditable(args);
    local reference = terrainDefaultReference();
    reference.heightScale = 0.0;
    if (terrainRebuild(reference) == null) wbFail("generation_failed", level.status);
    levelMarkPanelsDirty();
    return {surface = "flat", height = 0.0};
}

function wbRegister(name, description, properties, required, handler) {
    local registered = eve.mcp.tool(name, {
        description = description,
        inputSchema = {type = "object", properties = properties, required = required, additionalProperties = false},
        handler = function(args) {
            try {
                foreach (key, value in args)
                    if (!(key in properties)) wbFail("invalid_argument", "Unknown field: " + key);
                foreach (key in required)
                    if (!(key in args)) wbFail("invalid_argument", "Missing field: " + key);
                local value = handler(args);
                return {schema = "eve.whitebox.result", schemaVersion = 1, ok = true,
                    revision = level.session.getRevision(), value = value};
            } catch (error) {
                return {schema = "eve.whitebox.result", schemaVersion = 1, ok = false,
                    revision = level.session.getRevision(),
                    code = typeof error == "table" ? error.code : "invalid_argument",
                    message = typeof error == "table" ? error.message : error.tostring()};
            }
        }
    });
    if (!registered) throw "Cannot register agent tool " + name;
}

function whiteboxRegisterAgentTools() {
    local text = {type = "string"};
    local number = {type = "number"};
    local revision = {type = "integer", minimum = 0};
    local vector = {type = "array", minItems = 3, maxItems = 3, items = number};
    wbRegister("whitebox_transaction_v1", "Atomically create, transform or delete up to 128 whiteboxes. Required scene revision prevents lost updates. Retry identical transactionId and contents to recover the session-local receipt. One undo step; terrain and file writes are excluded.",
        {transactionId=text, expectedRevision=revision, operations={type="array", items={type="object"}}},
        ["transactionId","expectedRevision","operations"], wbTransaction);
    wbRegister("whitebox_catalog_v1", "List available prototype recipes, or inspect one recipe's parameter schema.",
        {recipe = text}, [], wbCatalog);
    wbRegister("whitebox_scene_v1", "Read whitebox objects and scene revision. Coordinates are metres, Y up; rotations are Euler radians.",
        {object = text}, [], wbScene);
    wbRegister("whitebox_create_v1", "Generate one complete model specification and publish through the scene editor. One undo step. Caller supplies a unique id; duplicate ids reject retries safely. Mesh generation is more expensive than transform edits.",
        {id = text, recipe = text, name = text, position = vector, rotation = vector, scale = vector,
         params = {type = "object"}, expectedRevision = revision}, ["id", "recipe", "position"], wbCreate);
    wbRegister("whitebox_transform_v1", "Edit an existing whitebox through an undoable scene command. Position in metres; Euler rotation in radians; positive scale.",
        {object = text, position = vector, rotation = vector, scale = vector, expectedRevision = revision}, ["object"], wbTransform);
    wbRegister("whitebox_delete_v1", "Delete one whitebox using the editor command; metadata is retained for UI undo.",
        {object = text, expectedRevision = revision}, ["object"], wbDelete);
    wbRegister("whitebox_save_v1", "Save the current versioned level to the engine user save directory; default agent-whitebox.json. Existing filename is overwritten atomically.",
        {filename = text, expectedRevision = revision}, [], wbSave);
    wbRegister("whitebox_view_v1", "Frame the editor camera for inspection. Use eve_screenshot after a rendered frame for visual evidence. Angles are radians.",
        {focus = vector, distance = number, yaw = number, pitch = number}, [], wbView);
    wbRegister("whitebox_flat_surface_v1", "Replace terrain with a flat work surface at Y=0. Existing whiteboxes stay in place. Terrain change is not undoable.",
        {expectedRevision = revision}, [], wbSurface);
}
