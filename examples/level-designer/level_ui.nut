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
    level.workspace.setRegionSize("right", 340.0);
    level.workspace.setRegionSize("bottom", 200.0);
    level.workspace.layout(win.getWidth().tofloat(), win.getHeight().tofloat());
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
    if (tool == "spawn") return "Character";
    if(tool=="terrain") return "Terrain";
    if(tool=="blocks") return "Blocks";
    return tool;
}

function levelGizmoModeLabel() {
    if (level.gizmoMode == "rotate") return "Rotate";
    if (level.gizmoMode == "scale") return "Scale";
    return "Move";
}

// Semantic Font Awesome icons use the engine's bundled font. Keep widget IDs
// stable for input routing; compact actions retain tooltips and accessible names.
function levelIconButton(icon,label,id,hint="",selected=false,compact=false) {
    local description=hint!="" ? hint : label;
    ui.iconButton(icon,compact ? "" : label,id);
    ui.setItemTooltip(description);
    ui.setItemAccessibility("button",label,description);
    ui.setItemSelected(selected);
}

function levelPanelToolbar() {
    ui.beginRow("toolbar-row",6.0);
    levelIconButton("file","New","new","New empty scene",false,true);
    levelIconButton("save","Save","save","Save scene · Ctrl+S",false,true);
    levelIconButton("folder-open","Load","load","Load saved scene",false,true);
    levelIconButton("home","Demo","demo","Open the editable courtyard demo",false,true);
    ui.separator("tb-sep-file");
    levelIconButton("pointer","Select","tool:select","Select objects · 1",level.tool=="select");
    levelIconButton("cube","Build","tool:whitebox","Place a construction piece · 2",level.tool=="whitebox");
    levelIconButton("user","Character","tool:spawn","Place a character · 3",level.tool=="spawn");
    levelIconButton("paint-brush","Terrain","tool:terrain","Sculpt terrain · 4",level.tool=="terrain");
    levelIconButton("layers","Blocks","tool:blocks","Place reusable blocks · 5",level.tool=="blocks");
    ui.separator("tb-sep-gizmo");
    levelIconButton("move","Move","gizmo:translate","Move selection · W",level.gizmoMode=="translate");
    levelIconButton("redo","Rotate","gizmo:rotate","Rotate selection · R",level.gizmoMode=="rotate");
    levelIconButton("grid","Scale","gizmo:scale","Scale selection · T",level.gizmoMode=="scale");
    ui.separator("tb-sep-hist");
    levelIconButton("undo","Undo","undo","Undo · Ctrl+Z",false,true);
    levelIconButton("redo","Redo","redo","Redo · Ctrl+Y",false,true);
    levelIconButton(level.mode=="play" ? "stop" : "play",level.mode=="play" ? "Stop" : "Play","play",
        "Start / stop physics simulation · F5");
    if(level.mode=="play") {
        levelIconButton(level.play.paused ? "play" : "pause",level.play.paused ? "Resume" : "Pause","pause");
        levelIconButton("chevron-right","Step","step","Advance physics by one frame");
    }
    levelIconButton("grid","Frame","frame","Frame the entire terrain",false,true);
    levelIconButton("search","Focus","focus","Focus selection · F",false,true);
    ui.spacer("tb-fill",1.0);
    ui.badge(level.mode=="play" ? "PLAY" : "EDIT","mode-badge");
    ui.end();
}

function levelPanelHierarchy() {
    ui.sectionHeader("Terrain", "terrain-header");
    local reference = level.terrainDraft != null ? level.terrainDraft :
        (level.terrain != null ? level.terrain.reference : terrainDefaultReference());
    ui.combo("Source", "Generated\nAsset file", reference.mode == "asset" ? 1 : 0, "terrainMode");
    if (reference.mode == "asset") ui.inputText("Asset path", reference.path, "terrainPath");
    levelIconButton("refresh","Rebuild terrain","terrainRebuild");
    if (level.terrain != null)
        ui.text(level.terrain.width + "x" + level.terrain.height + " @ " +
                level.terrain.spacingX + "m", "terrain-info");

    ui.separator("hierarchy-sep");
    ui.sectionHeader("Hierarchy", "hierarchy-header");
    ui.text(level.objects.len() + " objects · " + level.selection.len() + " selected", "hierarchy-meta");
    ui.text("Editing: " + (levelObject(level.scope)!=null ? levelObject(level.scope).name : "World"),"scope");
    ui.beginRow("block-navigation",4.0);
    levelIconButton("chevron-up","Up","leave","Return to parent block · Backspace");
    levelIconButton("folder-open","Enter","enter","Edit the selected block · Enter");
    levelIconButton("cubes","Group","group","Group selection · Ctrl+G");ui.end();
    local listH = level.workspace.getRegionH("left") - 280.0;
    if (listH < 120.0) listH = 120.0;
    ui.beginScrollList("hierarchy", listH, 0.0);
    foreach (entry in levelHierarchyOrder()) {
        local indent = "";
        for (local i = 0; i < entry.depth; ++i) indent += "  ";
        local icon=entry.spawn ? "user" : (entry.piece ? "cube" : "layers");
        local kind=entry.spawn ? "Character" : (entry.piece ? "Construction piece" : "Building block");
        levelIconButton(icon,indent+entry.name,"select:"+entry.id,kind+" · "+entry.name,
            level.selection.find(entry.id)!=null);
    }
    ui.end();
}

function levelPanelViewport() {
    local help = level.mode == "play"
        ? "Mouse look · WASD+Shift · Space jump · Esc stop"
        : "LMB select/place · Ctrl multi-select · RMB orbit · MMB pan · 1-5 tools · F focus";
    ui.text(help, "viewport-help");
    local vpW = level.workspace.getRegionW("center") - 16.0;
    local vpH = level.workspace.getRegionH("center") - (level.mode=="play" ? 150.0 : 104.0);
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
    if(level.mode=="play") {
        ui.sectionHeader("Physics simulation","simulation-title");
        ui.text(level.play.paused ? "PAUSED" : "RUNNING","simulation-state");
        ui.text("Fixed timestep: 60 Hz","simulation-rate");
        ui.text("Dynamic objects: " + level.play.dynamics.len(),"simulation-dynamics");
        ui.textWrapped("WASD move · Shift run · Space jump. Push dynamic objects with the character. Pause / Step inspects physics one frame at a time. Stop restores the authored scene.",290.0,"simulation-help");
        return;
    }
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

    if(levelIsGroup(object.id) && object.id!="root") {
        ui.sectionHeader("Building block","block-header");
        ui.beginRow("block-actions",4.0);
        levelIconButton("folder-open","Enter","enter");
        levelIconButton("cubes","Unpack","unpack");
        levelIconButton("save","Capture","capture");ui.end();
        ui.textWrapped("A block can contain pieces, characters, and other blocks. Enter to edit the next level.",290.0,"block-help");
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
    levelIconButton("check","Apply","apply");
    levelIconButton("chevron-down","Drop to terrain","drop");
    levelIconButton("close","Delete","delete");
    ui.end();

    levelIconButton("cubes","Duplicate selection","duplicate");
    if (object.id in level.pieces) {
        local piece = level.pieces[object.id];
        ui.sectionHeader("Physics","physics-header");
        ui.combo("Body", "Static\nDynamic", piece.motion=="dynamic" ? 1 : 0,"bodyMotion");
        ui.textWrapped("Dynamic pieces fall, collide, and can be pushed by the character in Play. Static: exact triangle mesh. Dynamic: convex hull (concave gaps are filled).",290.0,"physics-help");
        ui.separator("piece-params-sep");
        ui.sectionHeader("Whitebox parameters", "piece-title");
        ui.text(piece.recipe.slice("prototype.".len()), "piece-recipe");
        local schema = whiteboxSchema(piece.recipe);
        if (schema != null) {
            local paramsH = level.workspace.getRegionH("right") - 455.0;
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
    if(level.tool=="terrain") {
        ui.sectionHeader("Terrain sculpt · world-space brush","palette-header");
        ui.beginRow("brush-modes",6.0);
        local brushIcons={raise="plus",lower="minus",smooth="paint-brush",flatten="sliders"};
        foreach(mode in ["raise","lower","smooth","flatten"])
            levelIconButton(brushIcons[mode],mode,"brush:"+mode,"Terrain brush · "+mode,level.brushMode==mode);
        ui.end();
        ui.slider("Radius (m)",level.brushRadius,1.0,24.0,"brushRadius");
        ui.slider("Strength (m/s)",level.brushStrength,0.1,8.0,"brushStrength");
        ui.text("Hold LMB on terrain · flatten samples height at stroke start · Undo/Redo restores entire strokes","brush-help");
    } else if(level.tool=="blocks") {
        ui.sectionHeader("Reusable building blocks","palette-header");
        levelIconButton("save","Capture selection","capture");
        ui.text("Select a saved block, then click the terrain to place an independent instance.","blocks-help");
        ui.beginScrollList("block-library",90.0,0.0);
        foreach(index,record in level.library)
            levelIconButton("layers",record.name+" ("+record.objects.len()+" nodes)","block:"+index,
                "Place reusable block · "+record.name,index==level.blockIndex);
        ui.end();
    } else if (level.tool == "whitebox") {
        ui.sectionHeader("Whitebox palette · " + level.recipes.len() + " recipes", "palette-header");
        ui.text("Active: " + level.piece.slice("prototype.".len()) + "  ·  click terrain to place",
            "palette-active");
        local listH = level.workspace.getRegionH("bottom") - 70.0;
        if (listH < 80.0) listH = 80.0;
        // Flat scroll list — nested beginRow wrapping clipped most recipes.
        ui.beginScrollList("palette", listH, 0.0);
        foreach (recipe in level.recipes) {
            local label = recipe.slice("prototype.".len());
            levelIconButton("cube",label,"piece:"+recipe,"Place "+label,level.piece==recipe);
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
    level.layoutWidth=win.getWidth();level.layoutHeight=win.getHeight();
    if (level.workspace == null) levelConfigureWorkspace();
    level.workspace.layout(win.getWidth().tofloat(), win.getHeight().tofloat());
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
    if (sx <= 0.0) { level.status = "scale must not be zero"; return; }
    levelCommand("scene.object.update.v1", {
        object = level.selected,
        name = ui.getValueText("name"),
        parent = object.parent,
        position = [px, py, pz],
        rotation = [object.transform.rotationX, ry, object.transform.rotationZ],
        scale = [sx, sx, sx]
    });
}

/** @brief Put the selection back on the terrain surface, keeping its XZ. */
function levelDropSelectionToTerrain() {
    local object = levelObject(level.selected);
    if (object == null || object.id=="root") return;
    local t=object.transform;
    local world=scene.localToWorldAt(levelHost(),object.id,0.0,0.0,0.0);
    local ground=terrainHeightAt(world[0],world[2]);
    if(ground==null) { level.status="outside the terrain extent";return; }
    local localPoint=scene.worldToLocalAt(levelHost(),object.parent,world[0],ground,world[2]);
    levelCommand("scene.transform.set.v1",{object=object.id,position=localPoint,
        rotation=[t.rotationX,t.rotationY,t.rotationZ],scale=[t.scaleX,t.scaleY,t.scaleZ]});

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
        if (level.mode == "play") { change=ui.consumeChange(); continue; }
        if (id == "brushRadius" || id == "brushStrength") {
            ui.select("palette");
            if(id=="brushRadius") level.brushRadius=ui.getValue(id); else level.brushStrength=ui.getValue(id);
        } else if(id=="bodyMotion") {
            ui.select("inspector");
            if(level.selected in level.pieces) {
                level.pieces[level.selected].motion=ui.getValue(id).tointeger()==1 ? "dynamic" : "static";
                whiteboxRebuildPiece(level.selected);
            }
        } else if (id.find("pp:") == 0) levelUpdatePieceParam(id, id.slice(3));
        else if (id == "terrainMode") {
            ui.select("hierarchy");
            if (level.terrain != null) {
                if(level.terrainDraft==null) level.terrainDraft=levelCopy(level.terrain.reference);
                level.terrainDraft.mode = ui.getValue("terrainMode").tointeger() == 1 ? "asset" : "generated";
                level.status = "terrain source: " + level.terrainDraft.mode + " (press Rebuild terrain)";
            }
            levelMarkPanelsDirty();
        } else if (id == "terrainPath") {
            ui.select("hierarchy");
            if (level.terrain != null) {
                if(level.terrainDraft==null) level.terrainDraft=levelCopy(level.terrain.reference);
                level.terrainDraft.path=ui.getValueText("terrainPath");
            }
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
    if(level.mode=="play" && ["play","pause","step"].find(id)==null) {
        levelStatus("Stop Play to edit the scene");return;
    }
    if(id=="pause") { level.play.paused=!level.play.paused;levelMarkPanelsDirty();return; }
    if(id=="step") { levelStepPlay();return; }
    if(id=="demo") { levelBuildDemo();return; }
    if(id=="group") { levelGroupSelection();return; }
    if(id=="enter") { levelEnterBlock();return; }
    if(id=="leave") { levelLeaveBlock();return; }
    if(id=="unpack") { levelUnpackBlock();return; }
    if(id=="capture") { levelCaptureBlock();return; }
    if(id=="duplicate") { levelDuplicateSelection();return; }
    if(id.find("brush:")==0) { terrainEndStroke();level.brushMode=id.slice(6);levelMarkPanelsDirty();return; }
    if(id.find("block:")==0) { level.blockIndex=id.slice(6).tointeger();levelMarkPanelsDirty();return; }
    if((id=="undo" || id=="redo") && level.tool=="terrain") { terrainHistory(id=="redo");return; }
    if(id.find("tool:")==0) terrainEndStroke();
    if (id == "tool:select" || id == "tool:whitebox" || id == "tool:spawn" || id == "tool:terrain" || id == "tool:blocks") {
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
        levelDeleteSelection();
        levelRefreshPanels();
    } else if (id.find("select:") == 0) {
        levelSelect(id.slice(7),levelKeyEither("lctrl","LCtrl") || levelKeyEither("rctrl","RCtrl"));
        levelMarkPanelsDirty();
        levelMountPanels();
    } else if (id.find("piece:") == 0) {
        level.piece = id.slice(6);
        level.status = "active piece: " + level.piece;
        levelMarkPanelsDirty();
    }
}
