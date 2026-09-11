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
    local generated = procgen.generateMesh(piece.recipe, params, gfx);
    if (!generated.ok) { level.status = generated.status.summary; return null; }
    return generated.value;
}

function whiteboxValue(piece, key, fallback) {
    if (!(key in piece.values)) return fallback;
    return piece.values[key].tofloat();
}

/**
 * @brief Bounding half-extents used for the play-mode collider.
 *
 * Prototype pieces share a common parameter vocabulary, so the box collider is
 * taken from width/height/depth when the recipe exposes them. Pieces without
 * those parameters (or with stepped silhouettes) fall back to their declared
 * footprint, which is documented as an approximation for whitebox geometry.
 */
function whiteboxHalfExtents(piece) {
    local width = whiteboxValue(piece, "width", 1.0);
    local height = whiteboxValue(piece, "height", 1.0);
    local depth = whiteboxValue(piece, "depth", width);
    if (width <= 0.0) width = 1.0;
    if (height <= 0.0) height = 1.0;
    if (depth <= 0.0) depth = width;
    return {x = width * 0.5, y = height * 0.5, z = depth * 0.5};
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
    piece.entity.setVisible(true);
    return true;
}

function whiteboxApplyTransform(piece, object) {
    if (piece.entity == null || object == null) return;
    local world = scene.localToWorldAt(levelHost(), object.id, 0.0, 0.0, 0.0);
    piece.entity.setPosition(world[0], world[1], world[2]);
    local t = object.transform;
    piece.entity.setRotation(t.rotationY, t.rotationX, t.rotationZ);
    piece.entity.setScale(t.scaleX, t.scaleY, t.scaleZ);
}

/** @brief Place the palette's active piece at a world point. */
function whiteboxPlace(recipe, worldX, worldY, worldZ) {
    local schema = whiteboxSchema(recipe);
    if (schema == null) return "";
    local id = levelNextId("piece");
    local placed = levelCommand("scene.object.create.v1", {
        object = id,
        name = schema.getDisplayName(),
        parent = levelObject("root") != null ? "root" : "",
        position = [worldX, worldY, worldZ]
    });
    if (!placed) return "";
    local piece = {
        recipe = recipe,
        values = whiteboxDefaultValues(schema),
        entity = null,
        half = null
    };
    level.pieces[id] <- piece;
    if (!whiteboxCreateRenderable(piece, schema)) return "";
    piece.half = whiteboxHalfExtents(piece);
    level.selected = id;
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
    piece.half = whiteboxHalfExtents(piece);
}

/** @brief Rebuild every piece whose recipe parameters changed. */
function whiteboxRebuildAll() {
    foreach (id, piece in level.pieces) whiteboxRebuildPiece(id);
}

/** @brief Drop pieces whose scene object no longer exists. */
function whiteboxReconcile() {
    local stale = [];
    foreach (id, piece in level.pieces)
        if (levelObject(id) == null) stale.push(id);
    foreach (id in stale) {
        local piece = level.pieces[id];
        if (piece.entity != null) piece.entity.setVisible(false);
        delete level.pieces[id];
    }
}

function whiteboxSyncTransforms() {
    foreach (id, piece in level.pieces) whiteboxApplyTransform(piece, levelObject(id));
}

/** @brief Play-mode collider for one piece, mirroring its node transform. */
function whiteboxBuildCollider(world, id) {
    local piece = (id in level.pieces) ? level.pieces[id] : null;
    local object = levelObject(id);
    if (piece == null || object == null || piece.entity == null) return null;
    local half = piece.half != null ? piece.half : whiteboxHalfExtents(piece);
    local t = object.transform;
    local body = world.newBody("static", t.x, t.y, t.z);
    local shape = body.newBoxShape(half.x * 2.0, half.y * 2.0, half.z * 2.0, 1.0, 0.7, 0.0);
    if (shape == null) return null;
    // Prototype pieces sit on their own base, so the box is lifted by half its
    // height along the node's own up axis before the rotation is applied.
    local up = scene.localToWorldAt(levelHost(), id, 0.0, half.y, 0.0);
    local rotation = levelEulerQuaternion(t.rotationX, t.rotationY, t.rotationZ);
    body.setRotation(rotation[0], rotation[1], rotation[2], rotation[3]);
    body.setPosition(up[0], up[1], up[2]);
    return body;
}
