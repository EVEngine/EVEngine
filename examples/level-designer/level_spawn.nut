// Player spawn points.
//
// A spawn point is an ordinary scene node (so it moves with the same gizmo and
// participates in undo/hierarchy like everything else) carrying designer metadata
// the level file persists: facing yaw plus the character and motion set play mode
// should use when the level starts there.

function spawnDefault() {
    // Squirrel throws when reading an absent table slot, so every derived field a
    // spawn record may touch is declared up front.
    return {yaw = 0.0, character = "", motionSet = "", entity = null, facing = null};
}

function spawnBuildMarker(spawn, object) {
    if (spawn.entity == null) {
        local column = eve.Renderable3D();
        column.setMesh(gfx.newMeshCylinder(18, 1, true));
        column.setTint(0.30, 0.85, 0.95, 1.0);
        column.setRoughness(0.55);
        column.setCastShadow(true);
        spawn.entity = column;

        local facing = eve.Renderable3D();
        facing.setMesh(gfx.newMeshCube(1.0));
        facing.setTint(1.0, 0.85, 0.25, 1.0);
        facing.setRoughness(0.5);
        spawn.facing = facing;
    }
    spawn.entity.setVisible(true);
    spawn.facing.setVisible(true);
}

function spawnApplyTransform(spawn, object) {
    if (spawn.entity == null || object == null) return;
    // `base` is a Squirrel keyword, so the node origin is named explicitly.
    local origin = scene.localToWorldAt(levelHost(), object.id, 0.0, 0.0, 0.0);
    spawn.entity.setPosition(origin[0], origin[1] + 0.85, origin[2]);
    spawn.entity.setScale(0.34, 0.85, 0.34);
    spawn.entity.setYaw(spawn.yaw);

    // The nose cube shows the facing the player will be spawned with.
    local nose = scene.localToWorldAt(levelHost(), object.id,
        0.55 * sin(spawn.yaw), 0.55, 0.55 * cos(spawn.yaw));
    spawn.facing.setPosition(nose[0], nose[1], nose[2]);
    spawn.facing.setScale(0.30, 0.30, 0.55);
    spawn.facing.setYaw(spawn.yaw);
}

function spawnPlace(worldX, worldY, worldZ, yaw) {
    local id = levelNextId("spawn");
    local placed = levelCommand("scene.object.create.v1", {
        object = id,
        name = "PlayerStart " + level.serial,
        parent = levelObject("root") != null ? "root" : "",
        position = [worldX, worldY, worldZ]
    });
    if (!placed) return "";
    local spawn = spawnDefault();
    spawn.yaw = yaw;
    level.spawns[id] <- spawn;
    spawnBuildMarker(spawn, levelObject(id));
    spawnApplyTransform(spawn, levelObject(id));
    level.selected = id;
    levelSyncGizmo();
    level.status = "placed " + (levelObject(id) != null ? levelObject(id).name : id);
    return id;
}

function spawnReconcile() {
    local stale = [];
    foreach (id, spawn in level.spawns)
        if (levelObject(id) == null) stale.push(id);
    foreach (id in stale) {
        local spawn = level.spawns[id];
        if (spawn.entity != null) spawn.entity.setVisible(false);
        if (spawn.facing != null) spawn.facing.setVisible(false);
        delete level.spawns[id];
    }
}

function spawnSyncTransforms() {
    foreach (id, spawn in level.spawns) spawnApplyTransform(spawn, levelObject(id));
}

function spawnCount() {
    local total = 0;
    foreach (id, spawn in level.spawns) total += 1;
    return total;
}

/** @brief The spawn play mode starts from: a selected one, else the first. */
function spawnResolveStart() {
    if (level.selected != "" && (level.selected in level.spawns)) return level.selected;
    foreach (id, spawn in level.spawns) return id;
    return "";
}
