// Whitebox pieces.
//
// A piece is a procedural mesh from the ProcgenKit recipe registry plus the
// parameter values the designer chose. The level stores only {recipe, values};
// the GPU mesh, the renderable and the play-mode collider are all derived, so a
// level file never embeds geometry and stays small and diffable.

function whiteboxRecipes() {
    local ids = [];
    local count = procgen.getMeshRecipeCount();
    for (local i = 0; i < count; ++i) {
        local id = procgen.getMeshRecipeId(i);
        if (id.find("prototype.") == 0) ids.push(id);
    }
    return ids;
}

function whiteboxSchema(recipe) {
    local result = procgen.getMeshRecipeSchema(recipe);
    if (!result.ok) { level.status = result.status.summary; return null; }
    return result.value;
}

/** @brief Schema defaults as an editable key -> text table. */
function whiteboxDefaultValues(schema) {
    local values = {};
    for (local i = 0; i < schema.getParamCount(); ++i)
        values[schema.getParamKey(i)] <- schema.getParamDefault(i);
    return values;
}

function whiteboxGenerateMesh(piece, schema) {
    local created = procgen.newParams();
    if (!created.ok) { level.status = created.status.summary; return null; }
    local params = created.value;
    foreach(key,value in piece.values) {
        local known=false;
        for(local i=0;i<schema.getParamCount();++i) if(schema.getParamKey(i)==key) known=true;
        if(!known) { level.status="Unknown recipe parameter: " + key;return null; }
    }
    local applied = procgen.applyMeshRecipeDefaults(piece.recipe, params);
    if (!applied.ok) { level.status = applied.status.summary; return null; }
    for (local i = 0; i < schema.getParamCount(); ++i) {
        local key = schema.getParamKey(i);
        if (!(key in piece.values)) continue;
        local text = piece.values[key];
        local kind = schema.getParamKind(i);
        if (kind == "int") params.setInt(key, text.tointeger());
        else if (kind == "float") params.setFloat(key, text.tofloat());
        else if (kind == "bool") params.setBool(key, text.tointeger() != 0);
        else params.setString(key, text);
    }
    local built=procgen.buildMesh(piece.recipe,params);
    if(!built.ok) { level.status=built.status.summary;return null; }
    local cpu=built.value, vertices=[], indices=[];
    for(local i=0;i<cpu.getVertexCount();++i)
        vertices.extend([cpu.getPositionX(i),cpu.getPositionY(i),cpu.getPositionZ(i)]);
    for(local i=0;i<cpu.getIndexCount();++i) indices.push(cpu.getIndex(i));
    local generated=procgen.uploadMesh(cpu,gfx);
    if(!generated.ok) { level.status=generated.status.summary;return null; }
    piece.geometry <- {vertices=vertices,indices=indices};
    return generated.value;
}

function whiteboxCreateRenderable(piece, schema) {
    local mesh = whiteboxGenerateMesh(piece, schema);
    if (mesh == null) return false;
    if (piece.entity == null) {
        piece.entity = eve.Renderable3D();
        piece.entity.setRoughness(0.85);
        piece.entity.setCastShadow(true);
        piece.entity.setReceiveShadow(true);
    }
    piece.entity.setMesh(mesh);
    local bounds = mesh.getLocalBounds();
    if (!bounds.ok) { level.status = bounds.status.summary; return false; }
    piece.bounds = bounds.value;
    piece.half = {x=(piece.bounds[3]-piece.bounds[0])*0.5,
        y=(piece.bounds[4]-piece.bounds[1])*0.5,z=(piece.bounds[5]-piece.bounds[2])*0.5};
    piece.entity.setTint(piece.motion == "dynamic" ? 0.87 : 0.59,
        piece.motion == "dynamic" ? 0.55 : 0.68, piece.motion == "dynamic" ? 0.25 : 0.78, 1.0);
    piece.entity.setVisible(true);
    return true;
}

function whiteboxApplyTransform(piece, object) {
    if (piece.entity == null || object == null) return;
    local t = levelTransformRelativeTo(object.id, "");
    piece.entity.setPosition(t.x,t.y,t.z);
    piece.entity.setRotation(t.rotationY,t.rotationX,t.rotationZ);
    piece.entity.setScale(t.scaleX,t.scaleY,t.scaleZ);
    if(piece.bounds!=null) {
        local b=piece.bounds;
        if(!scene.setNodeBoundsAt(levelHost(),object.id,b[0],b[1],b[2],b[3],b[4],b[5]))
            throw "Picking bounds rejected: " + object.id;
    }

}

/** @brief Place the palette's active piece at a world point. */
function whiteboxPlace(recipe, worldX, worldY, worldZ) {
    local schema = whiteboxSchema(recipe);
    if (schema == null) return "";
    local piece={recipe=recipe,values=whiteboxDefaultValues(schema),entity=null,
        half=null,bounds=null,motion="static"};
    if(!whiteboxCreateRenderable(piece,schema)) { levelReleaseRenderable(piece.entity);return ""; }
    piece.entity.setVisible(false);
    local id=levelNextId("piece");
    local position=scene.worldToLocalAt(levelHost(),level.scope,worldX,worldY,worldZ);
    if(!levelCommand("scene.object.create.v1",{object=id,name=schema.getDisplayName(),parent=level.scope,position=position})) {
        levelReleaseRenderable(piece.entity);return "";
    }
    level.pieces[id] <- piece;piece.entity.setVisible(true);
    whiteboxApplyTransform(piece,levelObject(id));
    levelSelect(id);
    levelSyncGizmo();
    level.status = "placed " + schema.getDisplayName();
    return id;
}

function whiteboxRebuildPiece(id) {
    local piece = (id in level.pieces) ? level.pieces[id] : null;
    if (piece == null) return;
    local schema = whiteboxSchema(piece.recipe);
    if (schema == null) return;
    whiteboxCreateRenderable(piece, schema);

}

/** @brief Rebuild every piece whose recipe parameters changed. */
function whiteboxRebuildAll() {
    foreach (id, piece in level.pieces) whiteboxRebuildPiece(id);
}

/** @brief Drop pieces whose scene object no longer exists. */
function whiteboxReconcile() {
    // Keep semantic records while scene history can resurrect their stable IDs.
    foreach (id, piece in level.pieces)
        if (piece.entity != null) piece.entity.setVisible(levelObject(id) != null);
}

function whiteboxSyncTransforms() {
    foreach (id, piece in level.pieces) whiteboxApplyTransform(piece, levelObject(id));
}

/** @brief Play-mode collider for one piece, mirroring its node transform. */
function whiteboxBuildCollider(world, id) {
    local piece = (id in level.pieces) ? level.pieces[id] : null;
    local object = levelObject(id);
    if (piece == null || object == null || piece.entity == null) return null;
    local t=levelTransformRelativeTo(id, ""), vertices=[];
    for(local i=0;i<piece.geometry.vertices.len();i+=3)
        vertices.extend([piece.geometry.vertices[i]*t.scaleX,piece.geometry.vertices[i+1]*t.scaleY,
            piece.geometry.vertices[i+2]*t.scaleZ]);
    local body=world.newBody(piece.motion,t.x,t.y,t.z);
    local shape=null;
    try {
        shape=piece.motion=="static"
            ? body.newTriangleMeshShape(vertices,piece.geometry.indices)
            : body.newConvexHullShape(vertices,64);
        if(shape==null) throw "Piece collider creation failed: " + id;
        local rotation=levelEulerQuaternion(t.rotationX,t.rotationY,t.rotationZ);
        body.setRotation(rotation[0],rotation[1],rotation[2],rotation[3]);
        body.setLinearDamping(0.12);body.setAngularDamping(0.25);
    } catch(error) { body.destroy();throw error; }
    return body;
}
