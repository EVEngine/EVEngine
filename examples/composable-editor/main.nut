// Composable editor SDK example.
//
// The C++ editor module owns only semantic workspace/tool/selection/command
// models. This file is a project-specific presenter: replacing panelBuilders
// creates a different editor without modifying the engine.

dofile("dialogue_component.nut");
dofile("gameplay_components.nut");

const MAP_W = 48;
const MAP_H = 48;
const CELL = 0.55;
const HEIGHT_SCALE = 3.5;

class WorldPosition extends eve.Component {
    x = 0.0
    y = 0.0
    z = 0.0
}

class WorldIdentity extends eve.Component {
    id = ""
    kind = "prop"
}

class RuntimeObject extends eve.Entity {
    position = WorldPosition
    identity = WorldIdentity
}

// ViewModel: reflected metadata drives the optional generated inspector; the
// custom panels below bind to the same live object.
class WorldEditorVM {
    </ editor = "combo", options = "raise,lower" />
    tool = "raise"
    </ editor = "slider", min = 1, max = 12 />
    brushRadius = 4.0
    </ editor = "slider", min = 0.01, max = 0.20 />
    strength = 0.06
    </ editor = "combo", options = "grass,rock,sand" />
    material = "grass"
    </ editor = "combo", options = "edit,play,simulate" />
    mode = "edit"
    seed = 20260825
    selected = "terrain"
    status = "Workspace composed from project descriptors"
}

state <- persist("composableEditor", function() {
    return {
        workspace = null
        session = null
        vm = null
        heightmap = null
        terrainMesh = null
        terrainEntity = null
        terrainTarget = null
        brushFalloff = null
        brushKernel = null
        brushOperation = null
        brushTool = null
        procgenAlgorithm = "cave.cellular"
        procgenParams = null
        procgenSchema = null
        procgenPreview = null
        materialRecipe = "pbr.rock"
        materialParams = null
        materialSchema = null
        materialResource = null
        materialSet = null
        dialogueComponent = null
        gameplayComponents = null
        camera = null
        panelsMounted = false
        objects = []
        orbiting = false
        sculpting = false
        sculptX = 0.0
        sculptY = 0.0
        lastX = 0.0
        lastY = 0.0
        yaw = 0.72
        pitch = 0.52
        distance = 23.0
        meshDirty = false
        meshCooldown = 0.0
        lastSelectionSequence = 0
        lastTargetRevision = 0
        frameCount = 0
        screenshotSaved = false
    };
});

materials <- {
    grass = { r=0.24, g=0.46, b=0.22, metallic=0.0, roughness=0.92 },
    rock = { r=0.42, g=0.44, b=0.48, metallic=0.08, roughness=0.76 },
    sand = { r=0.70, g=0.58, b=0.32, metallic=0.0, roughness=0.98 },
};

function configureWorkspace() {
    state.workspace = editor.newWorkspace("world-authoring", "Project World Authoring");
    state.workspace.setRegionSize("top", 54.0);
    state.workspace.setRegionSize("left", 230.0);
    state.workspace.setRegionSize("right", 280.0);
    state.workspace.setRegionSize("bottom", 180.0);
    state.workspace.layout(config.width.tofloat(), config.height.tofloat());

    // These are project descriptors, not engine-known panel types.
    state.workspace.registerPanel("toolbar", "World Tools", "top", 0);
    state.workspace.registerPanel("hierarchy", "Hierarchy", "left", 10);
    state.workspace.registerPanel("scene", "Scene View", "center", 20);
    state.workspace.registerPanel("inspector", "Terrain + Material", "right", 30);
    state.workspace.registerPanel("palette", "Project Extensions", "bottom", 40);
    state.workspace.setPanelCapability("scene", "scene.viewport.3d");
    state.workspace.setPanelCapability("inspector", "property.terrain");
    state.workspace.setPanelContext("scene", "scene");
    state.workspace.setPanelContext("palette", "asset");
}

function panelToolbar() {
    ui.beginRow("commands", 8.0);
    ui.button("Edit", "mode-edit");
    ui.button("Play", "mode-play");
    ui.button("Simulate", "mode-simulate");
    ui.separator("sep-mode");
    ui.button("Raise", "tool-raise");
    ui.button("Lower", "tool-lower");
    ui.button("Undo", "edit-undo");
    ui.button("Redo", "edit-redo");
    ui.button("Regenerate", "terrain-regenerate");
    ui.button("Reflected Inspector", "reflect-inspector");
    ui.text("", "mode-label");
    ui.end();
}

function panelHierarchy() {
    ui.text("Scene", "heading");
    ui.listItem("Terrain", "select-terrain");
    ui.listItem("Directional Light", "select-light");
    ui.listItem("Gameplay / RTS Spawn", "select-rts");
    ui.listItem("Gameplay / Card Table", "select-card");
    ui.listItem("Gameplay / Voxel Volume", "select-voxel");
    ui.separator("sep-hierarchy");
    ui.text("ECS runtime objects", "ecs-title");
    ui.text("", "ecs-count");
    ui.textWrapped("The hierarchy and game runtime share stable semantic IDs.", 190.0, "ecs-help");
}

function panelScene() {
    ui.text("Live runtime viewport · LMB sculpt · RMB orbit · wheel zoom", "scene-help");
    ui.viewport("scene-vp", state.workspace.getRegionW("center") - 20.0,
                state.workspace.getRegionH("center") - 58.0);
    ui.text("", "scene-status");
}

// The project decides layout and filtering; RecipeDescriptor only supplies
// portable metadata. This same builder handles algorithms, textures and PBR.
function renderRecipeFields(schema, params, prefix, horizontal, wanted) {
    if (horizontal) ui.beginRow(prefix + "schema-row", 8.0);
    for (local i = 0; i < schema.getParamCount(); ++i) {
        if (schema.isParamAdvanced(i)) continue;
        local key = schema.getParamKey(i);
        if (wanted != null && !(key in wanted)) continue;
        local label = schema.getParamLabel(i);
        local kind = schema.getParamKind(i);
        local id = prefix + key;
        local hiddenLabel = "##" + prefix + key;
        ui.beginColumn(prefix + "field-" + key, 2.0);
        ui.text(label, prefix + "label-" + key);
        if (kind == "float")
            ui.slider(hiddenLabel, params.getFloat(key, 0.0), schema.getParamMinimum(i), schema.getParamMaximum(i), id);
        else if (kind == "int")
            ui.slider(hiddenLabel, params.getInt(key, 0).tofloat(), schema.getParamMinimum(i), schema.getParamMaximum(i), id);
        else if (kind == "bool") ui.checkbox(hiddenLabel, params.getInt(key, 0) != 0, id);
        else if (kind == "choice") {
            local choices = "";
            local selected = 0;
            local current = params.getString(key, "");
            for (local c = 0; c < schema.getParamChoiceCount(i); ++c) {
                local value = schema.getParamChoice(i, c);
                if (c > 0) choices += ",";
                choices += value;
                if (value == current) selected = c;
            }
            ui.combo(hiddenLabel, choices, selected, id);
        } else ui.inputText(hiddenLabel, params.getString(key, ""), id);
        ui.setItemSize(horizontal ? 145.0 : 235.0, 0.0);
        ui.end();
        if (horizontal) ui.setItemSize(165.0, 0.0);
    }
    if (horizontal) ui.end();
}

function panelInspector() {
    ui.text("MVVM Terrain Tool", "inspector-title");
    ui.text("", "selection");
    ui.combo("Tool", "raise,lower", 0, "tool");
    ui.slider("Brush radius", state.vm.brushRadius, 1.0, 12.0, "brush");
    ui.slider("Strength", state.vm.strength, 0.01, 0.20, "strength");
    ui.separator("sep-material");
    ui.text("Material", "material-title");
    ui.button("Grass", "material-grass");
    ui.button("Rock", "material-rock");
    ui.button("Sand", "material-sand");
    ui.text("", "material-current");
    ui.separator("sep-pbr");
    ui.text(state.materialSchema.getDisplayName() + " · recipe schema", "pbr-title");
    renderRecipeFields(state.materialSchema, state.materialParams, "pbr-", false, {
        scale=true, roughnessLow=true, roughnessHigh=true, metallic=true,
        normalStrength=true, heightStrength=true
    });
    ui.button("Generate + Apply PBR", "material-generate-pbr");
    ui.textWrapped("All controls write the reflected ViewModel; the model then updates terrain/runtime data.",
                   235.0, "mvvm-help");
}

function panelPalette() {
    ui.text("Game modules inject editor actions through the same command service", "palette-title");
    ui.beginRow("extension-actions", 8.0);
    ui.button("Spawn RTS Unit", "spawn-rts");
    ui.button("Add Card Table", "spawn-card");
    ui.button("Add Voxel Volume", "spawn-voxel");
    ui.button("Dialogue Node", "spawn-dialogue");
    ui.button("Avatar Preview", "spawn-avatar");
    ui.end();
    state.dialogueComponent.render("dialogue-");
    ui.separator("sep-procgen");
    ui.text(state.procgenSchema.getDisplayName() + " · reflected schema", "procgen-title");
    renderRecipeFields(state.procgenSchema, state.procgenParams, "procgen-", true, null);
    ui.button("Generate", "procgen-generate");
    ui.text("", "palette-status");
}

panelBuilders <- {
    toolbar = panelToolbar,
    hierarchy = panelHierarchy,
    scene = panelScene,
    inspector = panelInspector,
    palette = panelPalette,
};

function mountWorkspacePanels() {
    for (local i = 0; i < state.workspace.getPanelCount(); ++i) {
        if (!state.workspace.getPanelVisible(i)) continue;
        local id = state.workspace.getPanelId(i);
        if (!(id in panelBuilders)) continue;
        print("composable-editor: mounting " + id + "\n");
        ui.beginBuild();
        ui.beginWindow(state.workspace.getPanelTitle(i), "root");
        panelBuilders[id]();
        ui.end();
        ui.mountBuildAs(id);
        ui.select(id);
        local region = state.workspace.getPanelRegion(i);
        ui.setHostPos(state.workspace.getRegionX(region), state.workspace.getRegionY(region), 0.0, 0.0);
        ui.setHostSize(state.workspace.getRegionW(region), state.workspace.getRegionH(region));
        ui.setHostOverlay(false);
        print("composable-editor: mounted " + id + "\n");
    }
    state.panelsMounted = true;
}

function generateTerrain() {
    local params = procgen.newParams();
    params.setSize(MAP_W, MAP_H);
    params.setSeed(state.vm.seed);
    params.setFloat("frequency", 1.0 / 18.0);
    params.setInt("octaves", 5);
    state.heightmap = procgen.generateHeightmap(params);
    if (state.heightmap == null) state.heightmap = procgen.newHeightmap(MAP_W, MAP_H);

    if (state.terrainMesh == null) {
        state.terrainMesh = editor.newHeightmapMeshSmooth(state.heightmap, CELL, HEIGHT_SCALE);
        state.terrainEntity = eve.Renderable3D();
        state.terrainEntity.setMesh(state.terrainMesh);
        state.terrainEntity.setPosition(0.0, 0.0, 0.0);
        state.terrainEntity.setVisible(true);
    } else {
        editor.updateHeightmapMeshSmooth(state.terrainMesh, gfx, state.heightmap, CELL, HEIGHT_SCALE);
    }
    applyMaterial(state.vm.material);
    if (state.brushTool != null) bindTerrainTarget();
    state.vm.status = "Generated terrain seed " + state.vm.seed;
}

function bindTerrainTarget() {
    state.terrainTarget = editor.newHeightmapTarget("world.terrain", state.heightmap);
    state.session.bindHeightmapTarget(state.terrainTarget);
    state.lastTargetRevision = state.terrainTarget.getRevision();
}

function configureTerrainTool() {
    state.brushFalloff = editor.newSmoothBrushFalloff();
    state.brushKernel = editor.newCircleBrushKernel();
    state.brushKernel.setSmoothFalloff(state.brushFalloff);
    state.brushOperation = editor.newAddScalarFieldOperation();
    state.brushTool = editor.newFieldBrushTool("terrain.sculpt", "Sculpt Terrain");
    state.brushTool.setCircleKernel(state.brushKernel);
    state.brushTool.setAddScalarOperation(state.brushOperation);
    state.session.addFieldTool(state.brushTool);
    bindTerrainTarget();
    state.session.activateTool("terrain.sculpt");
}

function applyMaterial(name) {
    if (!(name in materials) || state.terrainEntity == null) return;
    state.vm.material = name;
    local m = materials[name];
    state.terrainEntity.setTint(m.r, m.g, m.b, 1.0);
    state.terrainEntity.setMetallic(m.metallic);
    state.terrainEntity.setRoughness(m.roughness);
}

function generateAndApplyPbrMaterial() {
    if (state.terrainEntity == null) return;
    state.materialSet = procgen.generatePbrMaterial(state.materialRecipe, state.materialParams);
    if (state.materialSet == null) {
        state.vm.status = "Material generation failed: " + procgen.lastError();
        return;
    }
    local albedo = gfx.newTexture(state.materialSet.getAlbedo(), true, true);
    local normal = gfx.newTexture(state.materialSet.getNormal(), true, true);
    local height = gfx.newTexture(state.materialSet.getHeight(), true, true);
    state.materialResource = gfx.newMaterial();
    state.materialResource.setAlbedoTexture(albedo);
    state.materialResource.setNormalTexture(normal);
    state.materialResource.setHeightTexture(height);
    state.materialResource.setMetallic(state.materialParams.getFloat("metallic", 0.0));
    local roughness = (state.materialParams.getFloat("roughnessLow", 0.5) +
                       state.materialParams.getFloat("roughnessHigh", 0.8)) * 0.5;
    state.materialResource.setRoughness(roughness);
    state.materialResource.setParallax(state.materialParams.getFloat("heightStrength", 1.0) * 0.025, 8.0, 24.0);
    state.terrainEntity.setMaterial(state.materialResource);
    state.materialSet.destroy();
    state.vm.material = state.materialRecipe;
    state.vm.status = "Generated " + state.materialSchema.getDisplayName() + " from reflected recipe parameters";
}

function registerProjectCommands() {
    editor.registerScriptCommand("world.spawn-archetype", "Spawn Project Archetype", "World/Create",
        function(payload) {
            if (!("kind" in payload) || !("id" in payload)) return false;
            local entity = RuntimeObject.create();
            entity.identity.id = payload.id;
            entity.identity.kind = payload.kind;
            entity.position.x = 4.0 + state.objects.len() * 2.0;
            entity.position.z = 4.0 + state.objects.len() * 1.5;
            state.objects.push(entity);
            state.workspace.select("world", "scene", "demo-world", payload.id, payload.kind, false);
            state.vm.selected = payload.id;
            state.vm.status = "Created " + payload.kind + " through shared editor/game command";
            return true;
        });
    state.session = editor.newSession();
}

function spawnArchetype(kind) {
    local id = kind + "." + (state.objects.len() + 1);
    local result = state.session.executeCommand("world.spawn-archetype", { kind=kind, id=id });
    if (!result.accepted) state.vm.status = "Command rejected: " + kind;
    return result.accepted ? state.objects.top() : null;
}

function spawnCardTable() {
    local instance = state.gameplayComponents.createCardInstance();
    if (instance == null) {
        state.vm.status = "Card definition rejected";
        return;
    }
    spawnArchetype("card.table");
    state.vm.status = "Card runtime instance " + instance.getInstanceId() +
                      " from schema-validated definition";
}

function spawnRtsUnit() {
    local entity = spawnArchetype("rts.unit");
    if (entity == null) return;
    if (state.gameplayComponents.createRtsUnit(entity.identity.id, entity, 2.0, 2.0))
        state.vm.status = "RTS unit " + entity.identity.id + " uses stable Crowd identity + terrain costs";
}

function applyDialogueDocument() {
    if (state.dialogueComponent.apply(dialogueFlow)) {
        spawnArchetype("dialogue.document");
        state.vm.status = "Applied project-composed Dialogue document";
    } else {
        state.vm.status = "Dialogue validation failed: " + state.dialogueComponent.document.getLastError();
    }
}

function updateRecipeParam(schema, params, prefix, id) {
    local key = id.slice(prefix.len());
    for (local i = 0; i < schema.getParamCount(); ++i) {
        if (schema.getParamKey(i) != key) continue;
        local kind = schema.getParamKind(i);
        if (kind == "float") params.setFloat(key, ui.getValue(id));
        else if (kind == "int") params.setInt(key, ui.getValue(id).tointeger());
        else if (kind == "bool") params.setInt(key, ui.getChecked(id) ? 1 : 0);
        else params.setString(key, ui.getValueText(id));
        state.vm.status = "Schema property changed: " + key;
        return;
    }
}

function generateProcgenPreview() {
    state.procgenPreview = procgen.generate(state.procgenAlgorithm, state.procgenParams);
    if (state.procgenPreview == null) {
        state.vm.status = "Procgen failed: " + procgen.lastError();
    } else {
        state.vm.status = procgen.getAlgorithmDisplayName(state.procgenAlgorithm) + " preview " +
                          state.procgenPreview.getWidth() + "x" + state.procgenPreview.getHeight();
    }
}

function updateCamera() {
    local cx = (MAP_W - 1) * CELL * 0.5;
    local cz = (MAP_H - 1) * CELL * 0.5;
    state.camera.setEye(cx + state.distance * cos(state.pitch) * sin(state.yaw),
                        state.distance * sin(state.pitch) + 1.5,
                        cz + state.distance * cos(state.pitch) * cos(state.yaw));
    state.camera.setTarget(cx, 0.8, cz);
}

function terrainHit(mx, my, viewportW, viewportH) {
    state.camera.screenToRay(mx, my, viewportW, viewportH);
    local oy = state.camera.getScreenRayOriginY();
    local dy = state.camera.getScreenRayDirY();
    if (dy >= -0.0001) return null;
    local t = (HEIGHT_SCALE * 0.5 - oy) / dy;
    if (t < 0.0) return null;
    local wx = state.camera.getScreenRayOriginX() + state.camera.getScreenRayDirX() * t;
    local wz = state.camera.getScreenRayOriginZ() + state.camera.getScreenRayDirZ() * t;
    if (wx < 0.0 || wz < 0.0 || wx > (MAP_W - 1) * CELL || wz > (MAP_H - 1) * CELL) return null;
    return [wx / CELL, wz / CELL];
}

function handlePanelEvents() {
    foreach (host in ["toolbar", "hierarchy", "inspector", "palette"]) {
        ui.select(host);
        local click = ui.consumeClick();
        while (click != "") {
            local id = click.slice(host.len() + 1);
            if (id == "mode-edit" || id == "mode-play" || id == "mode-simulate") {
                state.vm.mode = id.slice(5);
                state.workspace.setMode(state.vm.mode);
            } else if (id == "tool-raise" || id == "tool-lower") {
                state.vm.tool = id.slice(5);
            } else if (id == "edit-undo") {
                if (state.session.undo()) state.vm.status = "Undo terrain stroke";
            } else if (id == "edit-redo") {
                if (state.session.redo()) state.vm.status = "Redo terrain stroke";
            } else if (id == "terrain-regenerate") {
                state.vm.seed += 1;
                generateTerrain();
            } else if (id == "reflect-inspector") {
                ui.inspectObject(state.vm);
            } else if (id == "procgen-generate") {
                generateProcgenPreview();
            } else if (id == "material-generate-pbr") {
                generateAndApplyPbrMaterial();
            } else if (id == "material-grass") applyMaterial("grass");
            else if (id == "material-rock") applyMaterial("rock");
            else if (id == "material-sand") applyMaterial("sand");
            else if (id == "spawn-rts") spawnRtsUnit();
            else if (id == "spawn-card") spawnCardTable();
            else if (id == "spawn-voxel") spawnArchetype("voxel.volume");
            else if (id == "spawn-dialogue") applyDialogueDocument();
            else if (id == "spawn-avatar") spawnArchetype("avatar.preview");
            else if (id.find("select-") == 0) {
                local selected = id.slice(7);
                state.workspace.select("world", "scene", "demo-world", selected, "scene.object", false);
                state.vm.selected = selected;
            }
            click = ui.consumeClick();
        }
        local change = ui.consumeChange();
        while (change != "") {
            local id = change.slice(host.len() + 1);
            if (id == "brush") state.vm.brushRadius = ui.getValue("brush");
            else if (id == "strength") state.vm.strength = ui.getValue("strength");
            else if (id == "tool") state.vm.tool = ui.getValueText("tool");
            else if (id.find("procgen-") == 0)
                updateRecipeParam(state.procgenSchema, state.procgenParams, "procgen-", id);
            else if (id.find("pbr-") == 0)
                updateRecipeParam(state.materialSchema, state.materialParams, "pbr-", id);
            else if (id.find("dialogue-") == 0) {
                if (state.dialogueComponent.consumeChange("dialogue-", id))
                    state.vm.status = "Dialogue field changed: " + id.slice(9);
            }
            change = ui.consumeChange();
        }
    }
}

function updateSceneInteraction(dt) {
    ui.select("scene");
    local hovered = ui.viewportHovered("scene-vp");
    local mx = ui.viewportMouseX("scene-vp");
    local my = ui.viewportMouseY("scene-vp");
    local canvas = ui.viewportCanvas("scene-vp");
    if (hovered) {
        state.distance = clampf(state.distance - ui.viewportWheel("scene-vp") * 1.4, 7.0, 42.0);
    }
    if (hovered && mouse.isDown(2)) {
        if (state.orbiting) {
            state.yaw -= (mx - state.lastX) * 0.008;
            state.pitch = clampf(state.pitch + (my - state.lastY) * 0.008, 0.08, 1.42);
        }
        state.lastX = mx;
        state.lastY = my;
        state.orbiting = true;
    } else state.orbiting = false;
    state.brushTool.setRadius(state.vm.brushRadius);
    local signedStrength = state.vm.tool == "lower" ? -state.vm.strength : state.vm.strength;
    state.brushTool.setStrength(signedStrength * clampf(dt * 60.0, 0.0, 2.0));
    if (state.vm.mode == "edit" && hovered && canvas != null && mouse.isDown(1)) {
        local hit = terrainHit(mx, my, canvas.getWidth().tofloat(), canvas.getHeight().tofloat());
        if (hit != null) {
            local phase = state.sculpting ? 1 : 0;
            state.session.dispatchPointer(phase, 1, 0, hit[0], hit[1],
                                          hit[0] - state.sculptX, hit[1] - state.sculptY, 1.0);
            state.sculpting = true;
            state.sculptX = hit[0];
            state.sculptY = hit[1];
        }
    } else if (state.sculpting) {
        state.session.dispatchPointer(2, 1, 0, state.sculptX, state.sculptY, 0.0, 0.0, 1.0);
        state.sculpting = false;
    }
    local revision = state.terrainTarget.getRevision();
    if (revision != state.lastTargetRevision) {
        state.lastTargetRevision = revision;
        state.meshDirty = true;
        state.vm.status = state.vm.tool + " terrain · revision " + revision;
    }
}

function syncViews() {
    ui.select("toolbar");
    ui.setText("mode-label", "Mode: " + state.vm.mode + " · Tool: " + state.vm.tool);
    ui.select("hierarchy");
    ui.setText("ecs-count", "RuntimeObject view: " + eve.view(RuntimeObject).len());
    ui.select("inspector");
    ui.setText("selection", "Selected: " + state.vm.selected);
    ui.setText("material-current", "Active: " + state.vm.material);
    ui.setValue("brush", state.vm.brushRadius);
    ui.setValue("strength", state.vm.strength);
    ui.select("scene");
    ui.setText("scene-status", state.vm.status);
    ui.select("palette");
    ui.setText("palette-status", "Commands create real ECS data · objects " + state.objects.len());
}

eve_init = function() {
    state.vm = WorldEditorVM();
    state.procgenParams = procgen.newParams();
    procgen.applyAlgorithmDefaults(state.procgenAlgorithm, state.procgenParams);
    state.procgenSchema = procgen.getAlgorithmSchema(state.procgenAlgorithm);
    state.materialParams = procgen.newParams();
    state.materialParams.setSize(128, 128);
    procgen.applyPbrRecipeDefaults(state.materialRecipe, state.materialParams);
    state.materialSchema = procgen.getPbrRecipeSchema(state.materialRecipe);
    state.dialogueComponent = DialogueEditorComponent(dialogueFlow, "quest.example");
    state.gameplayComponents = GameplayEditorComponents();
    configureWorkspace();
    registerProjectCommands();
    generateTerrain();
    state.gameplayComponents.bindTerrain(state.heightmap, CELL);
    generateAndApplyPbrMaterial();
    configureTerrainTool();
    state.camera = eve.Camera3D();
    state.camera.setFov(50.0);
    state.camera.setAmbient(0.14, 0.16, 0.20);
    state.camera.setActive(true);
    gfx.setBackgroundColor(0.055, 0.065, 0.085, 1.0);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 0.95, 0.98, 1.0);
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    mountWorkspacePanels();
    print("composable-editor: panels mounted\n");
    ui.dbRegister(state.vm, "WorldEditorVM");
    ui.dbClose();
    print("composable-editor: vm registered\n");
    state.workspace.select("world", "scene", "demo-world", "terrain", "terrain.heightmap", false);
    updateCamera();
    print("composable-editor: workspace panels=" + state.workspace.getPanelCount() + "\n");
};

eve_update = function(dt) {
    handlePanelEvents();
    updateSceneInteraction(dt);
    updateCamera();
    state.gameplayComponents.update(dt);
    state.meshCooldown -= dt;
    if (state.meshDirty && state.meshCooldown <= 0.0) {
        editor.updateHeightmapMeshSmooth(state.terrainMesh, gfx, state.heightmap, CELL, HEIGHT_SCALE);
        state.meshDirty = false;
        state.meshCooldown = 1.0 / 30.0;
    }
    syncViews();
};

eve_render = function() {
    gfx.clear();
    // Scene presentation is project code: another editor can replace this
    // builder/canvas with a card table, tilemap, voxel slice, or custom preview.
    ui.select("scene");
    local canvas = ui.viewportCanvas("scene-vp");
    if (canvas != null) gfx.renderScene3DToCanvas(canvas, state.camera);
    ui.beginFrameAndRender();
    state.frameCount += 1;
    if (!state.screenshotSaved && state.frameCount > 8 && gfx.saveFramePng("composable-editor.png")) {
        state.screenshotSaved = true;
        print("composable-editor: saved composable-editor.png\n");
    }
};
