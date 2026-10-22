// Destructive test fixture: run in a fresh editor. Generates every library
// recipe through the same placement path as drag/drop, then frames the gallery.
if (level.pieces.len() != 0 || level.spawns.len() != 0)
    throw "Run model_gallery_check.nut in an empty editor";
levelHandleAction("build:flat");
local failures = [];
foreach (index, recipe in level.recipes) {
    local id = whiteboxPlace(recipe, (index % 9 - 4) * 6.0,
        0.05, (index / 9 - 4) * 6.0);
    if (id == "" || !(id in level.pieces) || level.pieces[id].entity == null)
        failures.push(recipe + ": " + level.status);
}
level.selected = "";
level.tool = "select";
level.focusX = 0.0; level.focusY = 0.0; level.focusZ = 0.0;
level.distance = 70.0; level.pitch = 0.85; level.yaw = 0.35;
levelUpdateOrbitCamera();
scene.updateTransformsAll();
whiteboxSyncTransforms();
level.status = "Library gallery: " + level.pieces.len() + "/" + level.recipes.len() + " generated";
levelRefreshPanels();
foreach (failure in failures) print("MODEL_GALLERY_FAILURE " + failure + "\n");
if (failures.len() != 0) throw "Whitebox recipe generation failed";
print("MODEL_GALLERY_PASS " + level.pieces.len() + " recipes\n");
