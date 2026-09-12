// Classic level-editor chrome: top toolbar, left outliner, center viewport,
// right inspector, bottom asset palette. Presentation only — session /
// document / domain ops stay in the other files.
//
// Pattern matches examples/composable-editor and the domain-editor workspace
// shell (docs/dev/2026-09-04-domain-editor-workspace-ui.md): one UI host per
// region, layout from editor.newWorkspace.

function levelMarkPanelsDirty() { level.panelsDirty = true; }

function levelUiHosts() {
    return ["toolbar", "hierarchy", "viewport", "inspector", "palette"];
}

function levelEventParts(path) {
    local slash = path.find("/");
    return slash == null ? ["", path] : [path.slice(0, slash), path.slice(slash + 1)];
}

function levelConfigureWorkspace() {
    level.workspace = editor.newWorkspace("level-designer.ui", "Level Designer");
    // Top is a single toolbar row (Unity-style); bottom holds the asset strip.
    level.workspace.setRegionSize("top", 64.0);
    level.workspace.setRegionSize("left", 260.0);
    level.workspace.setRegionSize("right", 310.0);
    level.workspace.setRegionSize("bottom", 200.0);
    level.workspace.layout(config.width.tofloat(), config.height.tofloat());
    level.workspace.registerPanel("toolbar", "Toolbar", "top", 0);
    level.workspace.registerPanel("hierarchy", "Outliner", "left", 10);
    level.workspace.registerPanel("viewport", "Viewport", "center", 20);
    level.workspace.registerPanel("inspector", "Inspector", "right", 30);
    level.workspace.registerPanel("palette", "Assets", "bottom", 40);
    level.workspace.setPanelCapability("viewport", "scene.viewport.3d");
    level.workspace.setPanelCapability("inspector", "property.selection");
    level.workspace.setPanelContext("hierarchy", "list");
    level.workspace.setPanelContext("viewport", "preview");
    level.workspace.setPanelContext("inspector", "inspector");
    level.workspace.setPanelContext("palette", "asset");
}

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

function levelToolLabel(tool) {
    if (tool == "select") return "Select";
    if (tool == "whitebox") return "Whitebox";
    if (tool == "spawn") return "Spawn";
    return tool;
}

function levelGizmoModeLabel() {
    if (level.gizmoMode == "rotate") return "Rotate";
    if (level.gizmoMode == "scale") return "Scale";
    return "Move";
}

function levelPanelToolbar() {
    // One horizontal strip — stacked rows get clipped by the top region height.
    ui.beginRow("toolbar-row", 6.0);
    ui.button("New", "new");
    ui.button("Save", "save");
    ui.button("Load", "load");
    ui.separator("tb-sep-file");
    ui.button(level.tool == "select" ? "[Select]" : "Select", "tool:select");
    ui.button(level.tool == "whitebox" ? "[Whitebox]" : "Whitebox", "tool:whitebox");
    ui.button(level.tool == "spawn" ? "[Spawn]" : "Spawn", "tool:spawn");
    ui.separator("tb-sep-gizmo");
    ui.button(level.gizmoMode == "translate" ? "[Move]" : "Move", "gizmo:translate");
    ui.button(level.gizmoMode == "rotate" ? "[Rotate]" : "Rotate", "gizmo:rotate");
    ui.button(level.gizmoMode == "scale" ? "[Scale]" : "Scale", "gizmo:scale");
    ui.separator("tb-sep-hist");
    ui.button("Undo", "undo");
    ui.button("Redo", "redo");
    ui.button(level.mode == "play" ? "Stop" : "Play", "play");
    ui.button("Frame", "frame");
    ui.button("Focus", "focus");
    ui.spacer("tb-fill", 1.0);
    ui.badge(level.mode == "play" ? "PLAY" : "EDIT", "mode-badge");
    ui.end();
}

function levelPanelHierarchy() {
    ui.sectionHeader("Terrain", "terrain-header");
    local reference = level.terrain != null ? level.terrain.reference : terrainDefaultReference();
    ui.combo("Source", "Generated\nAsset file", reference.mode == "asset" ? 1 : 0, "terrainMode");
    if (reference.mode == "asset") ui.inputText("Asset path", reference.path, "terrainPath");
    ui.button("Rebuild terrain", "terrainRebuild");
    if (level.terrain != null)
        ui.text(level.terrain.width + "x" + level.terrain.height + " @ " +
                level.terrain.spacingX + "m", "terrain-info");

    ui.separator("hierarchy-sep");
    ui.sectionHeader("Hierarchy", "hierarchy-header");
    ui.text(level.objects.len() + " objects · rev " + level.revision, "hierarchy-meta");
    local listH = level.workspace.getRegionH("left") - 210.0;
    if (listH < 120.0) listH = 120.0;
    ui.beginScrollList("hierarchy", listH, 0.0);
    foreach (entry in levelHierarchyOrder()) {
        local indent = "";
        for (local i = 0; i < entry.depth; ++i) indent += "  ";
        local tag = entry.spawn ? "  [spawn]" : (entry.piece ? "  [box]" : "");
        local mark = entry.id == level.selected ? "> " : "  ";
        ui.button(indent + mark + entry.name + tag, "select:" + entry.id);
    }
    ui.end();
}

function levelPanelViewport() {
    local help = level.mode == "play"
        ? "Mouse look · WASD + Shift run · Q/E zoom · F1 character · F2 motion · Esc stop"
        : "LMB select/place · drag gizmo · RMB orbit · MMB pan · Q/E zoom · F focus · Del delete · 1/2/3 tools";
    ui.text(help, "viewport-help");
    local vpW = level.workspace.getRegionW("center") - 16.0;
    local vpH = level.workspace.getRegionH("center") - 78.0;
    if (vpW < 64.0) vpW = 64.0;
    if (vpH < 64.0) vpH = 64.0;
    ui.viewport("level-vp", vpW, vpH);
    ui.text(level.status, "status");
    if (level.mode == "play" && level.play != null) {
        local play = level.play;
        local speed = sqrt(play.rig.matcher.getDesiredVelocityX() * play.rig.matcher.getDesiredVelocityX() +
                           play.rig.matcher.getDesiredVelocityZ() * play.rig.matcher.getDesiredVelocityZ());
        ui.progress(levelClamp(speed / 4.4, 0.0, 1.0), "play-speed", "Speed");
        ui.text((play.grounded ? "Grounded" : "Airborne") + " · " + play.characterName +
                " / " + play.motionSetName, "play-state");
    }
}

function levelPanelInspector() {
    ui.sectionHeader("Selection", "selection-header");
    local object = levelObject(level.selected);
    if (object == null) {
        ui.textWrapped("Nothing selected. Click an object in the viewport or hierarchy, or switch to Whitebox / Spawn and click the terrain.",
            268.0, "selection-empty");
        ui.separator("tool-context-sep");
        ui.text("Active tool: " + levelToolLabel(level.tool), "tool-context");
        ui.text("Gizmo: " + levelGizmoModeLabel(), "gizmo-context");
        return;
    }

    ui.inputText("Name", object.name, "name");
    ui.text("id: " + object.id, "object-id");
    ui.separator("trs-sep");
    ui.sectionHeader("Transform", "trs-header");
    ui.beginRow("position-row", 4.0);
    ui.inputText("X", object.transform.x.tostring(), "px");
    ui.inputText("Y", object.transform.y.tostring(), "py");
    ui.inputText("Z", object.transform.z.tostring(), "pz");
    ui.end();
    ui.beginRow("rot-scale-row", 4.0);
    ui.inputText("Yaw", object.transform.rotationY.tostring(), "ry");
    ui.inputText("Scale", object.transform.scaleX.tostring(), "sx");
    ui.end();
    ui.beginRow("object-actions", 4.0);
    ui.button("Apply", "apply");
    ui.button("Drop to terrain", "drop");
    ui.button("Delete", "delete");
    ui.end();

    if (object.id in level.pieces) {
        local piece = level.pieces[object.id];
        ui.separator("piece-params-sep");
        ui.sectionHeader("Whitebox parameters", "piece-title");
        ui.text(piece.recipe.slice("prototype.".len()), "piece-recipe");
        local schema = whiteboxSchema(piece.recipe);
        if (schema != null) {
            local paramsH = level.workspace.getRegionH("right") - 340.0;
            if (paramsH < 100.0) paramsH = 100.0;
            ui.beginScrollList("piece-params", paramsH, 0.0);
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

    if (object.id in level.spawns) {
        local spawn = level.spawns[object.id];
        ui.separator("spawn-sep");
        ui.sectionHeader("Spawn point", "spawn-title");
        ui.slider("Facing yaw (deg)", spawn.yaw * 57.2957795, -180.0, 180.0, "spawnYaw");
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

function levelPanelPalette() {
    if (level.tool == "whitebox") {
        ui.sectionHeader("Whitebox palette · " + level.recipes.len() + " recipes", "palette-header");
        ui.text("Active: " + level.piece.slice("prototype.".len()) + "  ·  click terrain to place",
            "palette-active");
        local listH = level.workspace.getRegionH("bottom") - 70.0;
        if (listH < 80.0) listH = 80.0;
        // Flat scroll list — nested beginRow wrapping clipped most recipes.
        ui.beginScrollList("palette", listH, 0.0);
        foreach (recipe in level.recipes) {
            local label = recipe.slice("prototype.".len());
            ui.button(level.piece == recipe ? "> " + label : label, "piece:" + recipe);
        }
        ui.end();
    } else if (level.tool == "spawn") {
        ui.sectionHeader("Spawn tool", "palette-header");
        ui.textWrapped("Click the terrain in the viewport to drop a player start. Select a spawn in the outliner to edit facing, character, and motion set in the inspector.",
            level.workspace.getRegionW("bottom") - 24.0, "palette-spawn-help");
        ui.text("Spawn points: " + spawnCount(), "palette-spawn-count");
    } else {
        ui.sectionHeader("Select tool", "palette-header");
        ui.textWrapped("Click objects in the viewport or outliner. Drag the visible gizmo to move / rotate / scale. Use the inspector for precise transforms.",
            level.workspace.getRegionW("bottom") - 24.0, "palette-select-help");
        local sel = levelObject(level.selected);
        ui.text(sel == null ? "No selection" : ("Selected: " + sel.name), "palette-selection");
    }
}

levelPanelBuilders <- {
    toolbar = levelPanelToolbar,
    hierarchy = levelPanelHierarchy,
    viewport = levelPanelViewport,
    inspector = levelPanelInspector,
    palette = levelPanelPalette
};

function levelMountPanels() {
    // Cleared up front: if a field throws, degrade to stale instead of
    // rebuilding every frame.
    level.panelsDirty = false;
    if (level.workspace == null) levelConfigureWorkspace();
    level.workspace.layout(config.width.tofloat(), config.height.tofloat());
    for (local i = 0; i < level.workspace.getPanelCount(); ++i) {
        if (!level.workspace.getPanelVisible(i)) continue;
        local id = level.workspace.getPanelId(i);
        if (!(id in levelPanelBuilders)) continue;
        ui.beginBuild();
        ui.beginWindow(level.workspace.getPanelTitle(i), "root");
        levelPanelBuilders[id]();
        ui.end();
        ui.mountBuildAs(id);
        ui.select(id);
        local region = level.workspace.getPanelRegion(i);
        ui.setHostPos(level.workspace.getRegionX(region), level.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(level.workspace.getRegionW(region), level.workspace.getRegionH(region));
        ui.setHostOverlay(false);
    }
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
        ui.select("inspector");
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
    ui.select("inspector");
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
    // Global click/change queues carry "host/widget" paths — route by prefix.
    local click = ui.consumeClick();
    while (click != "") {
        local parts = levelEventParts(click);
        levelHandleAction(parts[1]);
        click = ui.consumeClick();
    }
    local change = ui.consumeChange();
    while (change != "") {
        local parts = levelEventParts(change);
        local id = parts[1];
        if (id.find("pp:") == 0) levelUpdatePieceParam(id, id.slice(3));
        else if (id == "terrainMode") {
            ui.select("hierarchy");
            if (level.terrain != null) {
                level.terrain.reference.mode = ui.getValue("terrainMode").tointeger() == 1 ? "asset" : "generated";
                level.status = "terrain source: " + level.terrain.reference.mode + " (press Rebuild terrain)";
            }
            levelMarkPanelsDirty();
        } else if (id == "terrainPath") {
            ui.select("hierarchy");
            if (level.terrain != null) level.terrain.reference.path = ui.getValueText("terrainPath");
        } else if (id == "spawnYaw") {
            ui.select("inspector");
            local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
            if (spawn != null) spawn.yaw = ui.getValue("spawnYaw") / 57.2957795;
        } else if (id == "spawnCharacter") {
            ui.select("inspector");
            local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
            local names = levelCharacterNames();
            if (spawn != null && names.len() > 0) spawn.character = names[ui.getValue("spawnCharacter").tointeger()];
        } else if (id == "spawnMotionSet") {
            ui.select("inspector");
            local spawn = (level.selected in level.spawns) ? level.spawns[level.selected] : null;
            local names = levelMotionSetNames();
            if (spawn != null && names.len() > 0) spawn.motionSet = names[ui.getValue("spawnMotionSet").tointeger()];
        }
        change = ui.consumeChange();
    }
}

function levelHandleAction(id) {
    if (id == "tool:select" || id == "tool:whitebox" || id == "tool:spawn") {
        level.tool = id.slice(5);
        level.status = "tool: " + levelToolLabel(level.tool);
        levelMarkPanelsDirty();
    } else if (id == "gizmo:translate" || id == "gizmo:rotate" || id == "gizmo:scale") {
        level.gizmoMode = id.slice(6);
        if (level.gizmo != null) level.gizmo.setMode(level.gizmoMode);
        level.status = "gizmo: " + levelGizmoModeLabel();
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
