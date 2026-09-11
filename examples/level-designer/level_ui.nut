// Editor panels. All of this is host-owned presentation: replacing it does not
// change the session, the level document or any domain operation.
//
// The panel tree is rebuilt only when its structure changes (selection, tool,
// palette, mode). Value edits are applied in place so dragging a slider does not
// destroy the widget under the pointer.

function levelMarkPanelsDirty() { level.panelsDirty = true; }

function levelHierarchyOrder() {
    local ordered = [];
    function walk(parentId, depth) {
        foreach (object in level.objects) {
            if (object.parent != parentId) continue;
            ordered.push({id = object.id, name = object.name, depth = depth,
                          spawn = (object.id in level.spawns),
                          piece = (object.id in level.pieces)});
            walk(object.id, depth + 1);
        }
    }
    walk("", 0);
    return ordered;
}

function levelToolButton(label, tool) {
    ui.button(level.tool == tool ? "[" + label + "]" : label, "tool:" + tool);
}

function levelMountPanels() {
    // Cleared up front, not at the end: if a field in any section throws, the
    // panel must degrade to "stale" rather than re-running the whole build on
    // every subsequent frame.
    level.panelsDirty = false;
    ui.beginBuild();
    ui.beginWindow("Level Designer", "root");
    ui.textWrapped(level.status, 352.0, "status");

    ui.beginRow("tools", 4.0);
    levelToolButton("Select", "select");
    levelToolButton("Whitebox", "whitebox");
    levelToolButton("Spawn", "spawn");
    ui.end();

    ui.beginRow("transport", 4.0);
    ui.button(level.mode == "play" ? "Stop (Esc)" : "Play (F5)", "play");
    ui.button("Undo", "undo");
    ui.button("Redo", "redo");
    ui.end();

    ui.beginRow("file-actions", 4.0);
    ui.button("Save", "save");
    ui.button("Load", "load");
    ui.button("New", "new");
    ui.button("Frame all", "frame");
    ui.button("Focus selection", "focus");
    ui.end();

    // Terrain reference: either regenerate deterministically from the sampler
    // parameters, or reference a terrain asset that already exists on disk.
    local reference = level.terrain != null ? level.terrain.reference : terrainDefaultReference();
    ui.separator("terrain-sep");
    ui.text("Terrain", "terrain-title");
    ui.combo("Source", "Generated\nAsset file", reference.mode == "asset" ? 1 : 0, "terrainMode");
    if (reference.mode == "asset") ui.inputText("Asset path", reference.path, "terrainPath");
    ui.beginRow("terrain-actions", 4.0);
    ui.button("Rebuild terrain", "terrainRebuild");
    if (level.terrain != null)
        ui.text(level.terrain.width + "x" + level.terrain.height + " @ " +
                level.terrain.spacingX + "m", "terrain-info");
    ui.end();

    ui.separator("hierarchy-sep");
    ui.text("Hierarchy", "hierarchy-title");
    ui.beginScrollList("hierarchy", 190.0, 0.0);
    foreach (entry in levelHierarchyOrder()) {
        local indent = "";
        for (local i = 0; i < entry.depth; ++i) indent += "   ";
        local tag = entry.spawn ? " *" : (entry.piece ? " #" : "");
        ui.button(indent + (entry.id == level.selected ? "> " : "") + entry.name + tag,
            "select:" + entry.id);
    }
    ui.end();

    local object = levelObject(level.selected);
    if (object != null) {
        ui.separator("inspector-sep");
        ui.text("Selection", "selection-title");
        ui.inputText("Name", object.name, "name");
        ui.beginRow("position-row", 4.0);
        ui.inputText("x", object.transform.x.tostring(), "px");
        ui.inputText("y", object.transform.y.tostring(), "py");
        ui.inputText("z", object.transform.z.tostring(), "pz");
        ui.end();
        ui.beginRow("rot-scale-row", 4.0);
        ui.inputText("yaw", object.transform.rotationY.tostring(), "ry");
        ui.inputText("scale", object.transform.scaleX.tostring(), "sx");
        ui.end();
        ui.beginRow("object-actions", 4.0);
        ui.button("Apply", "apply");
        ui.button("Drop to terrain", "drop");
        ui.button("Delete", "delete");
        ui.end();
    }

    if (level.tool == "whitebox") {
        ui.separator("palette-sep");
        ui.text("Whitebox palette (" + level.recipes.len() + " pieces)", "palette-title");
        ui.beginScrollList("palette", 210.0, 0.0);
        foreach (recipe in level.recipes) {
            local label = recipe.slice("prototype.".len());
            ui.button(level.piece == recipe ? "> " + label : label, "piece:" + recipe);
        }
        ui.end();
        local piece = (level.selected in level.pieces) ? level.pieces[level.selected] : null;
        if (piece != null) {
            ui.separator("piece-params-sep");
            ui.text("Piece parameters", "piece-title");
            local schema = whiteboxSchema(piece.recipe);
            if (schema != null) {
                ui.beginScrollList("piece-params", 240.0, 0.0);
                for (local i = 0; i < schema.getParamCount(); ++i) {
                    local key = schema.getParamKey(i);
                    if (!(key in piece.values)) continue;
                    local kind = schema.getParamKind(i);
                    local id = "pp:" + key;
                    if (kind == "float" || kind == "int") {
                        local minimum = schema.paramHasMinimum(i) ? schema.getParamMinimum(i) : 0.0;
                        local maximum = schema.paramHasMaximum(i) ? schema.getParamMaximum(i) : 100.0;
                        ui.slider(schema.getParamLabel(i), piece.values[key].tofloat(),
                            minimum, maximum, id);
                    } else if (kind == "bool") {
                        ui.checkbox(schema.getParamLabel(i), piece.values[key].tointeger() != 0, id);
                    } else if (kind == "choice") {
                        local choices = "", selected = 0;
                        for (local c = 0; c < schema.getParamChoiceCount(i); ++c) {
                            local choice = schema.getParamChoice(i, c);
                            if (choice == piece.values[key]) selected = c;
                            choices += (c == 0 ? "" : "\n") + choice;
                        }
                        ui.combo(schema.getParamLabel(i), choices, selected, id);
                    } else {
                        ui.inputText(schema.getParamLabel(i), piece.values[key], id);
                    }
                }
                ui.end();
            }
        }
    }

    if (level.tool == "spawn") {
        ui.separator("spawn-sep");
        ui.text("Spawn points (" + spawnCount() + ")", "spawn-title");
        local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
        if (spawn == null) {
            ui.textWrapped("Select or place a spawn point to edit its facing and start character.",
                340.0, "spawn-help");
        } else {
            ui.slider("Facing yaw (deg)", spawn.yaw * 57.2957795, -180.0, 180.0, "spawnYaw");
            // The combo shows this spawn point's own start character, falling back to
            // the level default only while the spawn has not chosen one.
            local spawnCharacter = spawn.character != "" ? spawn.character : levelActiveCharacterName();
            if (levelCharacterNames().len() > 0) {
                local options = "", selected = 0, index = 0;
                foreach (name in levelCharacterNames()) {
                    if (name == spawnCharacter) selected = index;
                    options += (index == 0 ? "" : "\n") + name;
                    index += 1;
                }
                ui.combo("Character", options, selected, "spawnCharacter");
            }
            local spawnMotionSet = spawn.motionSet != "" ? spawn.motionSet : levelActiveMotionSetName();
            if (levelMotionSetNames().len() > 0) {
                local options = "", selected = 0, index = 0;
                foreach (name in levelMotionSetNames()) {
                    if (name == spawnMotionSet) selected = index;
                    options += (index == 0 ? "" : "\n") + name;
                    index += 1;
                }
                ui.combo("Motion set", options, selected, "spawnMotionSet");
            }
        }
    }

    ui.separator("help-sep");
    ui.textWrapped(level.mode == "play"
        ? "Mouse look turns the view (and with it WASD forward). Q/E zoom | WASD + Shift run | F1 character | F2 motion set | Esc stop."
        : "Whitebox: pick a piece, click the terrain to place it. Spawn: click the terrain to drop a player start. LMB select/drag gizmo, RMB orbit, MMB pan, Q/E zoom, F focus, Del deletes.",
        352.0, "help");
    ui.end();

    ui.mountBuildAs(levelHost());
    ui.select(levelHost());
    ui.setHostPos(14.0, 14.0, 0.0, 0.0);
    ui.setHostSize(400.0, config.height - 28.0);
    ui.setHostOverlay(true);
}

function levelRefreshPanels() {
    levelRefreshObjects();
    levelMountPanels();
}

function levelApplySelectionFields() {
    local object = levelObject(level.selected);
    if (object == null) return;
    local px, py, pz, ry, sx;
    try {
        px = ui.getValueText("px").tofloat();
        py = ui.getValueText("py").tofloat();
        pz = ui.getValueText("pz").tofloat();
        ry = ui.getValueText("ry").tofloat();
        sx = ui.getValueText("sx").tofloat();
    } catch (error) {
        level.status = "position, yaw and scale must be numbers";
        return;
    }
    if (sx == 0.0) { level.status = "scale must not be zero"; return; }
    levelCommand("scene.object.update.v1", {
        object = level.selected,
        name = ui.getValueText("name"),
        parent = object.parent,
        position = [px, py, pz],
        rotation = [object.transform.rotationX, ry, object.transform.rotationZ],
        scale = [sx, object.transform.scaleY, object.transform.scaleZ]
    });
}

/** @brief Put the selection back on the terrain surface, keeping its XZ. */
function levelDropSelectionToTerrain() {
    local object = levelObject(level.selected);
    if (object == null) return;
    local t = object.transform;
    local ground = terrainHeightAt(t.x, t.z);
    if (ground == null) { level.status = "outside the terrain extent"; return; }
    levelCommand("scene.transform.set.v1", {
        object = level.selected,
        position = [t.x, ground, t.z],
        rotation = [t.rotationX, t.rotationY, t.rotationZ],
        scale = [t.scaleX, t.scaleY, t.scaleZ]
    });
}

function levelUpdatePieceParam(id, key) {
    local piece = (level.selected in level.pieces) ? level.pieces[level.selected] : null;
    if (piece == null) return;
    local schema = whiteboxSchema(piece.recipe);
    if (schema == null) return;
    for (local i = 0; i < schema.getParamCount(); ++i) {
        if (schema.getParamKey(i) != key) continue;
        local kind = schema.getParamKind(i);
        if (kind == "bool") piece.values[key] = ui.getChecked(id) ? "1" : "0";
        else if (kind == "choice") piece.values[key] = ui.getValueText(id);
        else if (kind == "float" || kind == "int") piece.values[key] = ui.getValue(id).tostring();
        else piece.values[key] = ui.getValueText(id);
        whiteboxRebuildPiece(level.selected);
        return;
    }
}

function levelHandleUi() {
    ui.select(levelHost());
    local click = ui.consumeClick();
    while (click != "") {
        local slash = click.find("/");
        local id = slash == null ? click : click.slice(slash + 1);
        levelHandleAction(id);
        ui.select(levelHost());
        click = ui.consumeClick();
    }
    local change = ui.consumeChange();
    while (change != "") {
        local slash = change.find("/");
        local id = slash == null ? change : change.slice(slash + 1);
        if (id.find("pp:") == 0) levelUpdatePieceParam(id, id.slice(3));
        else if (id == "terrainMode") {
            if (level.terrain != null) {
                level.terrain.reference.mode = ui.getValue("terrainMode").tointeger() == 1 ? "asset" : "generated";
                level.status = "terrain source: " + level.terrain.reference.mode + " (press Rebuild terrain)";
            }
            levelMarkPanelsDirty();
        } else if (id == "terrainPath") {
            if (level.terrain != null) level.terrain.reference.path = ui.getValueText("terrainPath");
        } else if (id == "spawnYaw") {
            local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
            if (spawn != null) spawn.yaw = ui.getValue("spawnYaw") / 57.2957795;
        } else if (id == "spawnCharacter") {
            local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
            local names = levelCharacterNames();
            if (spawn != null && names.len() > 0) spawn.character = names[ui.getValue("spawnCharacter").tointeger()];
        } else if (id == "spawnMotionSet") {
            local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
            local names = levelMotionSetNames();
            if (spawn != null && names.len() > 0) spawn.motionSet = names[ui.getValue("spawnMotionSet").tointeger()];
        }
        ui.select(levelHost());
        change = ui.consumeChange();
    }
}

function levelHandleAction(id) {
    if (id == "tool:select" || id == "tool:whitebox" || id == "tool:spawn") {
        level.tool = id.slice(5);
        levelMarkPanelsDirty();
    } else if (id == "play") {
        levelTogglePlay();
    } else if (id == "undo" || id == "redo") {
        levelChecked(id == "undo" ? level.session.undo() : level.session.redo());
        level.status = id;
        levelRefreshPanels();
    } else if (id == "save") {
        levelSaveDocument();
    } else if (id == "load") {
        levelLoadDocument();
    } else if (id == "new") {
        levelNewDocument();
    } else if (id == "frame") {
        levelFrameTerrain();
    } else if (id == "focus") {
        levelFrameSelection();
    } else if (id == "terrainRebuild") {
        levelRebuildTerrain();
    } else if (id == "apply") {
        levelApplySelectionFields();
        levelRefreshPanels();
    } else if (id == "drop") {
        levelDropSelectionToTerrain();
        levelRefreshPanels();
    } else if (id == "delete") {
        local removed = level.selected;
        levelCommand("scene.object.delete.v1", {object = removed});
        if (removed in level.pieces) {
            local piece = level.pieces[removed];
            if (piece.entity != null) piece.entity.setVisible(false);
            delete level.pieces[removed];
        }
        if (removed in level.spawns) {
            local spawn = level.spawns[removed];
            if (spawn.entity != null) spawn.entity.setVisible(false);
            if (spawn.facing != null) spawn.facing.setVisible(false);
            delete level.spawns[removed];
        }
        level.selected = "";
        levelRefreshPanels();
    } else if (id.find("select:") == 0) {
        level.selected = id.slice(7);
        levelSyncGizmo();
        levelMarkPanelsDirty();
        levelMountPanels();
    } else if (id.find("piece:") == 0) {
        level.piece = id.slice(6);
        level.status = "active piece: " + level.piece;
        levelMarkPanelsDirty();
    }
}
