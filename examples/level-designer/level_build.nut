// Transient build gesture. Only release creates a scene command; cancellation
// never edits the document. Recipe meshes remain owned by the existing library.
function whiteboxSnap(value) {
    local step = level.gridSnap;
    return step > 0.0 ? floor(value / step + 0.5) * step : value;
}

function whiteboxBuildPoint(hit) {
    local x = whiteboxSnap(hit.x), z = whiteboxSnap(hit.z);
    local y = terrainHeightAt(x, z);
    return y == null ? null : {x = x, y = y + 0.05, z = z};
}

function whiteboxCancelDrag() {
    if (level.buildDrag == null) return;
    level.buildDrag.preview.entity.setVisible(false);
    level.buildDrag = null;
}

function whiteboxBeginDrag(hit) {
    whiteboxCancelDrag();
    local point = whiteboxBuildPoint(hit);
    local schema = whiteboxSchema(level.piece);
    if (point == null || schema == null) return;
    local preview = {recipe = level.piece, values = whiteboxDefaultValues(schema), entity = null};
    if (!whiteboxCreateRenderable(preview, schema)) return;
    level.buildDrag = {preview = preview, start = point, point = point,
        half = whiteboxHalfExtents(preview), scale = [1.0, 1.0, 1.0]};
    whiteboxUpdateDrag(hit);
}

function whiteboxUpdateDrag(hit) {
    local drag = level.buildDrag;
    local point = whiteboxBuildPoint(hit);
    if (drag == null || point == null) return;
    local width = fabs(point.x - drag.start.x), depth = fabs(point.z - drag.start.z);
    local stretching = width > 0.25 || depth > 0.25;
    drag.point = stretching ? {x = (point.x + drag.start.x) * 0.5,
        y = drag.start.y, z = (point.z + drag.start.z) * 0.5} : drag.start;
    drag.scale = stretching ? [levelClamp(width / (drag.half.x * 2.0), 0.1, 100.0),
        1.0, levelClamp(depth / (drag.half.z * 2.0), 0.1, 100.0)] : [1.0, 1.0, 1.0];
    drag.preview.entity.setPosition(drag.point.x, drag.point.y, drag.point.z);
    drag.preview.entity.setScale(drag.scale[0], drag.scale[1], drag.scale[2]);
}

function whiteboxCommitDrag() {
    local drag = level.buildDrag;
    if (drag == null) return;
    local id = whiteboxPlace(drag.preview.recipe, drag.point.x, drag.point.y, drag.point.z, drag.scale);
    whiteboxCancelDrag();
    if (id != "") level.status = "Built piece. W/R/T: move / rotate / scale; Esc: cancel drag";
    levelMarkPanelsDirty();
}

function whiteboxConsumeDrops() {
    local target = ui.consumeDrop();
    while (target != "") {
        if (level.mode == "edit" && target == "viewport/level-vp" &&
            ui.getDropType() == "level.whitebox.recipe") {
            local recipe = ui.getDropText();
            if (level.recipes.find(recipe) != null) {
                // Delivery already proves the pointer hit this viewport. ImGui
                // may report hovered=false while the source owns the mouse.
                ui.select("viewport");
                local canvas = ui.viewportCanvas("level-vp");
                local hit = null;
                if (canvas != null) {
                    local x = ui.viewportMouseX("level-vp"), y = ui.viewportMouseY("level-vp");
                    local w = canvas.getWidth().tofloat(), h = canvas.getHeight().tofloat();
                    if (x >= 0 && y >= 0 && x < w && y < h) {
                        level.camera.screenToRay(x, y, w, h);
                        hit = terrainRayPick(level.camera.getScreenRayOriginX(),
                            level.camera.getScreenRayOriginY(), level.camera.getScreenRayOriginZ(),
                            level.camera.getScreenRayDirX(), level.camera.getScreenRayDirY(),
                            level.camera.getScreenRayDirZ(), 6000.0);
                    }
                }
                local point = hit == null ? null : whiteboxBuildPoint(hit);
                if (point != null) {
                    local id = whiteboxPlace(recipe, point.x, point.y, point.z);
                    if (id != "") { level.piece = recipe; level.tool = "select"; }
                    levelMarkPanelsDirty();
                }
            }
        }
        target = ui.consumeDrop();
    }
}
