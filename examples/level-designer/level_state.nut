// Shared authoring state for the level designer.
//
// One table, one owner per field: level_terrain.nut owns `terrain`,
// level_whitebox.nut owns `pieces`, level_spawn.nut owns `spawns`,
// level_play.nut owns `play`, level_document.nut owns persistence.
// The table itself lives in a `persist` slot so a hot reload keeps the session.

persist level = null;

// `const` in a dofile'd script is file-scoped, so shared names are functions.
function levelHost() { return "level-designer"; }

function levelInit() {
    if (level != null) return;
    level = {
        // module handles
        session = null, camera = null, controller = null, gizmo = null,
        model3d = null, animation = null, physics = null, heightmapTargets = null,
        world = null, workspace = null,

        mode = "edit",          // "edit" | "play"
        tool = "select",        // select | whitebox | spawn
        gizmoMode = "translate", // translate | rotate | scale
        piece = "prototype.cube",
        recipes = [],           // whitebox palette (ProcgenKit prototype recipes)
        selected = "",
        objects = [],
        pieces = {},            // objectId -> {recipe, values, entity, half}
        spawns = {},            // objectId -> {yaw, character, motionSet, entity, facing}
        gizmoPrims = [],        // live TransformGizmo overlays (Primitive3D)
        rigCache = {},          // "character|motionSet" -> rigged character
        characterName = "Mannequin",
        motionSetName = "Locomotion",
        terrain = null,         // level_terrain.nut
        play = null,            // level_play.nut
        serial = 0,
        revision = -1,
        status = "ready",
        panelsDirty = true,

        // editor camera: orbit around a pannable focus point
        focusX = 0.0, focusY = 0.0, focusZ = 0.0,
        yaw = 0.85, pitch = 0.62, distance = 46.0,
        lastX = 0.0, lastY = 0.0, orbit = false, pan = false,

        // input edge state
        keys = {},
        mouseDown = false,

        saveName = "level-designer.json"
    };
}

function levelStatus(text) {
    level.status = text;
    // Status lives on the center viewport panel after the workspace redesign.
    try {
        ui.select("viewport");
        ui.setText("status", text);
    } catch (error) {}
}

// Every scene mutation goes through the session command registry.
function levelChecked(result) {
    if (!result.ok) {
        level.status = result.status.summary;
        return false;
    }
    return true;
}

function levelCommand(command, payload) {
    local applied = levelChecked(level.session.execute(command, payload));
    if (applied) level.status = command;
    levelRefreshObjects();
    return applied;
}

function levelObject(id) {
    foreach (object in level.objects) if (object.id == id) return object;
    return null;
}

function levelRefreshObjects() {
    local snapshot = level.session.snapshot();
    if (!levelChecked(snapshot)) return;
    level.objects = snapshot.value.objects;
    level.revision = level.session.getRevision();
    if (levelObject(level.selected) == null) level.selected = "";
    whiteboxReconcile();
    spawnReconcile();
    levelSyncGizmo();
}

function levelSyncGizmo() {
    local object = levelObject(level.selected);
    if (object == null) return;
    local t = object.transform;
    level.gizmo.setPosition(t.x, t.y, t.z);
    level.gizmo.setRotationEuler(t.rotationX, t.rotationY, t.rotationZ);
    level.gizmo.setScale(t.scaleX, t.scaleY, t.scaleZ);
    if (level.gizmoMode != "") level.gizmo.setMode(level.gizmoMode);
}

function levelNextId(prefix) {
    do { level.serial += 1; } while (levelObject(prefix + "-" + level.serial) != null);
    return prefix + "-" + level.serial;
}

function levelKeyPressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in level.keys) ? level.keys[name] : false;
    level.keys[name] <- down;
    return down && !was;
}

function levelKeyEither(lower, upper) {
    return keyboard.isDown(lower) || keyboard.isDown(upper);
}

function levelClamp(value, minimum, maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

// Scene nodes compose as translate * Ry(yaw) * Rx(pitch) * Rz(roll) * scale with
// pitch = rotationX, yaw = rotationY, roll = rotationZ (scene::SceneRuntime), and
// physics bodies take a quaternion, so the same rotation is rebuilt here.
function levelQuaternionMultiply(a, b) {
    return [
        a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
        a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
        a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
        a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]
    ];
}

/** @brief Euler TRS rotation as a [x,y,z,w] quaternion. */
function levelEulerQuaternion(rx, ry, rz) {
    local qx = [sin(rx * 0.5), 0.0, 0.0, cos(rx * 0.5)];
    local qy = [0.0, sin(ry * 0.5), 0.0, cos(ry * 0.5)];
    local qz = [0.0, 0.0, sin(rz * 0.5), cos(rz * 0.5)];
    return levelQuaternionMultiply(levelQuaternionMultiply(qy, qx), qz);
}
