// Player spawn points.
//
// A spawn point is an ordinary scene node (so it moves with the same gizmo and
// participates in undo/hierarchy like everything else) carrying designer metadata
// the level file persists: facing yaw plus the character and motion set play mode
// should use when the level starts there.

function spawnDefault() {
    // Squirrel throws when reading an absent table slot, so every derived field a
    // spawn record may touch is declared up front.
    return {yaw = 0.0, character = "", motionSet = "", entity = null, facing = null, rig = null};
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
    spawn.entity.setVisible(false);
    spawn.facing.setVisible(true);
}

function spawnApplyTransform(spawn, object) {
    if (spawn.entity == null || object == null) return;
    // `base` is a Squirrel keyword, so the node origin is named explicitly.
    local origin = levelProjectedPoint(object.id, 0.0, 0.0, 0.0);
    local character=spawn.character!="" ? spawn.character : levelActiveCharacterName();
    local motion=spawn.motionSet!="" ? spawn.motionSet : levelActiveMotionSetName();
    if(spawn.rig==null || spawn.rig.name!=character || spawn.rig.motionSet!=motion) {
        if(spawn.rig!=null) foreach(part in spawn.rig.parts) part.setVisible(false);
        spawn.rig=levelBuildRig(character,motion,"spawn:" + object.id);
        if(spawn.rig==null) throw level.status;
        spawn.rig.matcher.setDesiredVelocity(0.0,0.0);spawn.rig.matcher.setDesiredYaw(0.0);spawn.rig.matcher.update(0.016);
        local pose=spawn.rig.matcher.getPose();pose.computeWorld(spawn.rig.skeleton);
        foreach(binding in spawn.rig.skins) binding.skin.applyToMesh(gfx,binding.part.getMesh(),pose);
    }
    local front=levelProjectedPoint(object.id,sin(spawn.yaw),0.0,cos(spawn.yaw));
    local worldTransform=levelTransformRelativeTo(object.id,"");
    foreach(part in spawn.rig.parts) {
        part.setScale(worldTransform.scaleX,worldTransform.scaleY,worldTransform.scaleZ);
        part.setPosition(origin[0],origin[1],origin[2]);part.setYaw(atan2(front[0]-origin[0],front[2]-origin[2]));
        part.setVisible(true);
    }
    spawn.entity.setPosition(origin[0], origin[1] + 0.85, origin[2]);
    spawn.entity.setScale(0.34, 0.85, 0.34);
    spawn.entity.setYaw(spawn.yaw);

    // The nose cube shows the facing the player will be spawned with.
    local nose = levelProjectedPoint(object.id,
        0.55 * sin(spawn.yaw), 0.55, 0.55 * cos(spawn.yaw));
    spawn.facing.setPosition(nose[0], nose[1], nose[2]);
    spawn.facing.setScale(0.30, 0.30, 0.55);
    spawn.facing.setYaw(spawn.yaw);
}

function spawnPlace(worldX, worldY, worldZ, yaw) {
    local id = levelNextId("spawn");
    local position = scene.worldToLocalAt(levelHost(),level.scope,worldX,worldY,worldZ);
    local placed = levelCommand("scene.object.create.v1", {
        object = id,
        name = "PlayerStart " + level.serial,
        parent = level.scope,
        position = position
    });
    if (!placed) return "";
    local spawn = spawnDefault();
    spawn.yaw = yaw;
    level.spawns[id] <- spawn;
    spawnBuildMarker(spawn, levelObject(id));
    spawnApplyTransform(spawn, levelObject(id));
    levelSelect(id);
    levelSyncGizmo();
    level.status = "placed " + (levelObject(id) != null ? levelObject(id).name : id);
    return id;
}

function spawnReconcile() {
    foreach (id, spawn in level.spawns) {
        local visible=levelObject(id)!=null;
        if(spawn.entity!=null) spawn.entity.setVisible(false);
        if(spawn.rig!=null) foreach(part in spawn.rig.parts) part.setVisible(visible);
        if(spawn.facing!=null) spawn.facing.setVisible(visible);
    }
}

function spawnSyncTransforms() {
    foreach (id, spawn in level.spawns) spawnApplyTransform(spawn, levelObject(id));
}

function spawnCount() {
    local total = 0;
    foreach (id, spawn in level.spawns) if(levelObject(id)!=null) total += 1;
    return total;
}

/** @brief The spawn play mode starts from: a selected one, else the first. */
function spawnResolveStart() {
    if (level.selected != "" && (level.selected in level.spawns)) return level.selected;
    foreach (id, spawn in level.spawns) if(levelObject(id)!=null) return id;
    return "";
}

function spawnReleaseProjection(spawn) {
    levelReleaseRenderable(spawn.entity);levelReleaseRenderable(spawn.facing);
    if(spawn.rig!=null) foreach(part in spawn.rig.parts) part.setVisible(false);
}

function spawnReleaseRigCache() {
    local stale=[];
    foreach(key,rig in level.rigCache) if(key.find("|spawn:")!=null) stale.push(key);
    foreach(key in stale) {
        foreach(part in level.rigCache[key].parts) levelReleaseRenderable(part);
        delete level.rigCache[key];
    }
}
