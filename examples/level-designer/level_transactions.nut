// One synchronous main-thread publication through the canonical scene command.
// Detached JSON is a candidate, never a second live scene authority.
// Receipts belong to this editor session; they are not durable across restarts.
local wbTransactions = {};
local wbTransactionSession = null;
local wbPreparedPool = [];

function wbTransactionFields(op, allowed, required) {
    if (typeof op != "table") wbFail("invalid_argument", "Operation must be an object");
    foreach (key, value in op)
        if (allowed.find(key) == null) wbFail("invalid_argument", "Unknown operation field: " + key);
    foreach (key in required)
        if (!(key in op)) wbFail("invalid_argument", "Missing operation field: " + key);
}

function wbTransactionTransform(op, previous) {
    local p = wbVector(op, "position", [previous.x, previous.y, previous.z], -10000, 10000);
    local r = wbVector(op, "rotation", [previous.rotationX, previous.rotationY, previous.rotationZ], -360, 360);
    local s = wbVector(op, "scale", [previous.scaleX, previous.scaleY, previous.scaleZ], 0.01, 1000);
    return {x=p[0], y=p[1], z=p[2], rotationX=r[0], rotationY=r[1], rotationZ=r[2],
        scaleX=s[0], scaleY=s[1], scaleZ=s[2]};
}

// Preserve exact VM values: the document JSON writer is intentionally not a hash.
function wbTransactionCopy(value) {
    local kind = typeof value;
    if (kind == "table") {
        local result = {};
        foreach (key, item in value) result[key] <- wbTransactionCopy(item);
        return result;
    }
    if (kind == "array") {
        local result = [];
        foreach (item in value) result.push(wbTransactionCopy(item));
        return result;
    }
    return value;
}

function wbTransactionEqual(left, right) {
    if (typeof left != typeof right) return false;
    local kind = typeof left;
    if (kind == "table" || kind == "array") {
        if (left.len() != right.len()) return false;
        foreach (key, item in left) {
            if (kind == "table" && !(key in right)) return false;
            if (!wbTransactionEqual(item, right[key])) return false;
        }
        return true;
    }
    return left == right;
}

function wbTransaction(args) {
    wbTransactionFields(args, ["transactionId", "expectedRevision", "operations"],
        ["transactionId", "expectedRevision", "operations"]);
    if (wbTransactionSession != level.session) {
        wbTransactions = {};
        wbTransactionSession = level.session;
    }
    local id = wbString(args.transactionId, "transactionId");
    local request = wbTransactionCopy(args);
    if (id in wbTransactions) {
        local old = wbTransactions[id];
        if (!wbTransactionEqual(old.request, request)) wbFail("transaction_id_conflict", "Transaction ID has different contents");
        return old.receipt;
    }
    wbEditable(args);
    if (typeof args.operations != "array" || args.operations.len() == 0 || args.operations.len() > 128)
        wbFail("invalid_argument", "Expected 1 to 128 operations");
    if (wbTransactions.len() >= 4096) wbFail("receipt_capacity", "Start a new editor session before further transactions");
    local candidate = levelJsonDecode(level.session.saveJson());
    local objects = {};
    foreach (object in candidate.objects) {
        // JSON may encode integral TRS values as integers; native render setters require floats.
        foreach (key, value in object.transform) object.transform[key] = value.tofloat();
        objects[object.id] <- object;
    }
    local pieces = clone level.pieces, history = clone level.pieceHistory;
    local touched = {}, create = [], removed = [], prepared = [];
    // Validate the entire request before allocating render resources.
    foreach (op in args.operations) {
        wbTransactionFields(op, ["type","id","recipe","name","position","rotation","scale","params"], ["type","id"]);
        local objectId = wbString(op.id, "id");
        if (objectId in touched) wbFail("invalid_argument", "Only one operation per object in a transaction");
        touched[objectId] <- true;
        if (op.type == "create") {
            wbTransactionFields(op, ["type","id","recipe","name","position","rotation","scale","params"], ["recipe","position"]);
            if (objectId in objects || objectId in history || objectId in pieces) wbFail("already_exists", "Object ID is reserved: " + objectId);
            if (level.recipes.find(op.recipe) == null) wbFail("not_found", "Unknown recipe");
            local schema = whiteboxSchema(op.recipe);
            if (schema == null) wbFail("generation_failed", level.status);
            local piece = {recipe=op.recipe, values=wbValues(schema, "params" in op ? op.params : {}), entity=null, half=null};
            local object = {id=objectId, parent="root" in objects ? "root" : "",
                name="name" in op ? wbString(op.name,"name") : schema.getDisplayName(),
                transform=wbTransactionTransform(op, {x=0.0,y=0.0,z=0.0,rotationX=0.0,rotationY=0.0,rotationZ=0.0,scaleX=1.0,scaleY=1.0,scaleZ=1.0})};
            objects[objectId] <- object;
            pieces[objectId] <- piece;
            create.push({piece=piece, schema=schema, object=object});
        } else {
            if (!(objectId in pieces) || !(objectId in objects)) wbFail("not_found", "Whitebox not found: " + objectId);
            if (op.type == "transform") {
                wbTransactionFields(op, ["type","id","position","rotation","scale"], []);
                objects[objectId].transform = wbTransactionTransform(op, objects[objectId].transform);
            } else if (op.type == "delete") {
                wbTransactionFields(op, ["type","id"], []);
                foreach (childId, child in objects)
                    if (child.parent == objectId) wbFail("invalid_argument", "Cannot delete a parent whitebox");
                removed.push(pieces[objectId]);
                history[objectId] <- pieces[objectId];
                delete pieces[objectId]; delete objects[objectId];
            } else wbFail("invalid_argument", "Unknown operation type");
        }
    }
    candidate.objects = [];
    foreach (key in levelSortedKeys(objects)) candidate.objects.push(objects[key]);
    levelValidateHierarchyGeometry(candidate.objects);
    local receipt = {transactionId=id, baseRevision=args.expectedRevision, committedRevision=0,
        operations=args.operations.len(), presentationError=""};
    local nextReceipts = clone wbTransactions;
    nextReceipts[id] <- {request=request, receipt=receipt};
    try {
        foreach (item in create) {
            // Failed prepares retain hidden reusable renderers, not scene objects.
            if (wbPreparedPool.len() > 0) item.piece.entity = wbPreparedPool.pop();
            prepared.push(item.piece);
            if (!whiteboxCreateRenderable(item.piece, item.schema)) wbFail("generation_failed", level.status);
            item.piece.entity.setVisible(false);
            whiteboxApplyTransform(item.piece, item.object);
        }
        wbEditable(args);
        local result = level.session.execute("scene.snapshot.restore.v1", candidate);
        if (!result.ok) wbFail("scene_rejected", result.status.summary);
    } catch (error) {
        foreach (piece in prepared) if (piece.entity != null) {
            piece.entity.setVisible(false);
            wbPreparedPool.push(piece.entity);
        }
        throw error;
    }
    // Everything owning/allocating above succeeded. Publish prepared references.
    level.pieces = pieces; level.pieceHistory = history; level.objects = candidate.objects;
    level.revision = level.session.getRevision();
    receipt.committedRevision = level.revision;
    wbTransactions = nextReceipts;
    try {
        foreach (piece in removed) piece.entity.setVisible(false);
        foreach (piece in prepared) piece.entity.setVisible(true);
        whiteboxSyncTransforms();
        levelMarkPanelsDirty();
    } catch (error) {
        // The authoritative transaction committed. Never report rejection after
        // publication; return its receipt even when a presentation observer fails.
        receipt.presentationError = error.tostring();
    }
    return receipt;
}
