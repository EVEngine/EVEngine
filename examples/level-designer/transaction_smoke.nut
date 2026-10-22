// Diagnostic fault injection only; normal agents use whitebox_transaction_v1.
local capturePieces = function() {
    local value = wbScene({});
    value.objects.sort(function(a,b) { return a.id <=> b.id; });
    return levelJsonEncode(value);
};
local beforeScene = level.session.saveJson();
local beforePieces = capturePieces();
local beforeRevision = level.session.getRevision();
local originalGenerate = whiteboxCreateRenderable;
local attempts = 0;
whiteboxCreateRenderable = function(piece, schema) {
    attempts += 1;
    if (attempts == 2) throw "Injected second mesh upload failure";
    return originalGenerate(piece, schema);
};
local failed = false;
try {
    wbTransaction({transactionId="fault-stage-" + beforeRevision, expectedRevision=beforeRevision,
        operations=[
            {type="create", id="fault-stage-a", recipe="prototype.cube", position=[0,0,0]},
            {type="create", id="fault-stage-b", recipe="prototype.cube", position=[1,0,0]}]});
} catch (error) { failed = true; print("INJECT_CAUGHT " + (typeof error == "table" ? error.message : error) + "\n"); }
whiteboxCreateRenderable = originalGenerate;
if (!(failed && attempts == 2)) throw "fault not reached: attempts=" + attempts;
if (!(level.session.getRevision() == beforeRevision)) throw "transaction assertion 1";
if (!(level.session.saveJson() == beforeScene)) throw "transaction assertion 2";
if (!(capturePieces() == beforePieces)) throw "transaction assertion 3";

local request = {transactionId="undo-batch-" + beforeRevision, expectedRevision=beforeRevision,
    operations=[
        {type="create", id="undo-batch-a-" + beforeRevision, recipe="prototype.stairs", position=[0,0,-4]},
        {type="create", id="undo-batch-b-" + beforeRevision, recipe="prototype.ramp", position=[3,0,-4]}]};
wbTransaction(request);
local afterScene = level.session.saveJson();
local afterPieces = capturePieces();
local undone = level.session.undo();
if (!(undone.ok)) throw "transaction assertion 4";
levelRefreshObjects();
if (!(level.session.saveJson() == beforeScene)) throw "transaction assertion 5";
if (!(capturePieces() == beforePieces)) throw "transaction assertion 6";
local redone = level.session.redo();
if (!(redone.ok)) throw "transaction assertion 7";
levelRefreshObjects();
if (!(level.session.saveJson() == afterScene)) throw "transaction assertion 8";
if (!(capturePieces() == afterPieces)) throw "transaction assertion 9";
print("WHITEBOX_TRANSACTION_SMOKE_PASS: staged upload failure unchanged; batch undo/redo with metadata\n");

// Reject at the real command authorization boundary after mesh preparation.
// An isolated session borrows the same live host, preserving the UI session's history.
local uiSession = level.session;
local isolated = eve.SceneEditorModule().createLiveSession("transaction-reject-test", levelHost());
assert(isolated.ok);
local limited = isolated.value.restrictCommands(["scene.transform.set.v1"]);
assert(limited.ok);
local rejectBefore = uiSession.saveJson();
local rejectPieces = capturePieces();
level.session = isolated.value;
local rejected = false;
try {
    wbTransaction({transactionId="native-reject", expectedRevision=level.session.getRevision(), operations=[
        {type="create", id="native-reject-a", recipe="prototype.cube", position=[0,0,0]}]});
} catch(error) {
    rejected = typeof error == "table" && error.code == "scene_rejected";
}
level.session = uiSession;
assert(rejected);
assert(uiSession.saveJson() == rejectBefore);
assert(capturePieces() == rejectPieces);
print("WHITEBOX_TRANSACTION_NATIVE_REJECTION_PASS\n");
