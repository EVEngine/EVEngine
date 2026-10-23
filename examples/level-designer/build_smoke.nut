// Run in an empty editor using eve_run_script: dofile("build_smoke.nut").
// Exercises the same gesture functions as pointer input; does not emulate OS drag.
function buildCheck(condition, message) {
    if (!condition) throw "whitebox build smoke: " + message;
    print("PASS whitebox build: " + message + "\n");
}
levelHandleAction("build:flat");
level.piece = "prototype.cube";
local before = level.objects.len();
whiteboxBeginDrag({x = -4.0, y = 0.0, z = -3.0});
whiteboxUpdateDrag({x = 3.0, y = 0.0, z = 2.0});
buildCheck(level.objects.len() == before, "preview does not mutate scene");
whiteboxCancelDrag();
buildCheck(level.buildDrag == null && level.objects.len() == before, "cancel leaves scene intact");
whiteboxBeginDrag({x = -4.0, y = 0.0, z = -3.0});
whiteboxUpdateDrag({x = 3.0, y = 0.0, z = 2.0});
whiteboxCommitDrag();
local id = level.selected;
buildCheck(id in level.pieces && level.objects.len() == before + 1, "release creates one piece");
local t = levelObject(id).transform;
buildCheck(t.scaleX > 1.0 && t.scaleZ > 1.0, "drag sizes footprint");
// Observe the renderer boundary: a transient transform must not resolve to
// the committed node's old origin (the scene itself must remain unchanged).
local observed = {position = null};
local probe = {
    setPosition = function(x, y, z) { observed.position = [x, y, z]; },
    setRotation = function(y, x, z) {},
    setScale = function(x, y, z) {}
};
local preview = clone t;
preview.x += 4.0;
// Use the studio's owner of transient hierarchy previews.
local realGizmo = level.gizmo;
level.gizmo = {
    isDragging = function() { return true; },
    getPositionX = function() { return preview.x; }, getPositionY = function() { return preview.y; },
    getPositionZ = function() { return preview.z; }, getRotationX = function() { return preview.rotationX; },
    getRotationY = function() { return preview.rotationY; }, getRotationZ = function() { return preview.rotationZ; },
    getScaleX = function() { return preview.scaleX; }, getScaleY = function() { return preview.scaleY; },
    getScaleZ = function() { return preview.scaleZ; }
};
try { whiteboxApplyTransform({entity = probe, bounds = null}, {id = id, transform = preview}); }
catch(error) { level.gizmo = realGizmo; throw error; }
level.gizmo = realGizmo;
buildCheck(fabs(observed.position[0] - preview.x) < 0.001,
    "move preview uses proposed position");
buildCheck(levelObject(id).transform.x == t.x, "move preview does not commit scene");
levelHandleAction("undo");
buildCheck(levelObject(id) == null, "one undo removes whole gesture");
levelHandleAction("redo");
buildCheck(levelObject(id) != null && id in level.pieces, "redo restores recipe and mesh");
local encoded = levelJsonEncode(levelDocumentBuild());
local decoded = levelJsonDecode(encoded);
levelDocumentValidate(decoded);
buildCheck(decoded.pieces.len() == level.pieces.len(), "document retains procedural recipes");
buildCheck(whiteboxSnap(-1.24) == -1.0 && whiteboxSnap(1.26) == 1.5, "signed grid snapping");
whiteboxSyncTransforms();
levelRefreshPanels();

local unicodeText = levelJsonDecode("\"\\u4e2d\\u6587\\ud83d\\ude00\"");
buildCheck(unicodeText == "中文😀", "Unicode and surrogate pairs decode losslessly");
local controls = "A" + (1).tochar() + "B";
buildCheck(levelJsonDecode(levelJsonEncode(controls)) == controls, "control characters round trip");
foreach (invalid in ["\"\\u12\"", "\"\\ud800\"", "\"\\udc00\"", "\"\\uZZZZ\""]) {
    local rejected = false;
    try { levelJsonDecode(invalid); } catch (error) { rejected = true; }
    buildCheck(rejected, "invalid Unicode escape rejected");
}
print("WHITEBOX_BUILD_SMOKE_PASS\n");
