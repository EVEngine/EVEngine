// Composable editor SDK example.
//
// The C++ editor module owns only semantic workspace/tool/selection/command
// models. This file is a project-specific presenter: replacing panelBuilders
// creates a different editor without modifying the engine.

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
        camera = null
        panelsMounted = false
        objects = []
        orbiting = false
        lastX = 0.0
        lastY = 0.0
        yaw = 0.72
        pitch = 0.52
        distance = 23.0
        meshDirty = false
        meshCooldown = 0.0
        lastSelectionSequence = 0
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
    ui.text("Scene is the game runtime itself · LMB sculpt · RMB orbit", "scene-help");
    ui.text("", "scene-status");
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
        // The example chooses a transparent scene HUD over the live game view.
        // A different project can replace this builder with a docked viewport.
        ui.setHostOverlay(id == "scene");
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
    state.vm.status = "Generated terrain seed " + state.vm.seed;
}

function applyMaterial(name) {
    if (!(name in materials) || state.terrainEntity == null) return;
    state.vm.material = name;
    local m = materials[name];
    state.terrainEntity.setTint(m.r, m.g, m.b, 1.0);
    state.terrainEntity.setMetallic(m.metallic);
    state.terrainEntity.setRoughness(m.roughness);
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
}

function updateCamera() {
    local cx = (MAP_W - 1) * CELL * 0.5;
    local cz = (MAP_H - 1) * CELL * 0.5;
    state.camera.setEye(cx + state.distance * cos(state.pitch) * sin(state.yaw),
                        state.distance * sin(state.pitch) + 1.5,
                        cz + state.distance * cos(state.pitch) * cos(state.yaw));
    state.camera.setTarget(cx, 0.8, cz);
}

function terrainHit(mx, my) {
    state.camera.screenToRay(mx, my, config.width.tofloat(), config.height.tofloat());
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
            } else if (id == "terrain-regenerate") {
                state.vm.seed += 1;
                generateTerrain();
            } else if (id == "reflect-inspector") {
                ui.inspectObject(state.vm);
            } else if (id == "material-grass") applyMaterial("grass");
            else if (id == "material-rock") applyMaterial("rock");
            else if (id == "material-sand") applyMaterial("sand");
            else if (id == "spawn-rts") spawnArchetype("rts.unit");
            else if (id == "spawn-card") spawnArchetype("card.table");
            else if (id == "spawn-voxel") spawnArchetype("voxel.volume");
            else if (id == "spawn-dialogue") spawnArchetype("dialogue.node");
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
            change = ui.consumeChange();
        }
    }
}

function updateSceneInteraction(dt) {
    local mx = mouse.getX();
    local my = mouse.getY();
    local cx = state.workspace.getRegionX("center");
    local cy = state.workspace.getRegionY("center");
    local hovered = mx >= cx && my >= cy &&
                    mx < cx + state.workspace.getRegionW("center") &&
                    my < cy + state.workspace.getRegionH("center") &&
                    !ui.wantCaptureMouse();
    if (hovered && mouse.isDown(2)) {
        if (state.orbiting) {
            state.yaw -= (mx - state.lastX) * 0.008;
            state.pitch = clampf(state.pitch + (my - state.lastY) * 0.008, 0.08, 1.42);
        }
        state.lastX = mx;
        state.lastY = my;
        state.orbiting = true;
    } else state.orbiting = false;
    if (hovered && mouse.isDown(1)) {
        local hit = terrainHit(mx, my);
        if (hit != null) {
            local direction = state.vm.tool == "lower" ? -1.0 : 1.0;
            local changed = editor.applyHeightmapBrush(state.heightmap, hit[0], hit[1], state.vm.brushRadius,
                direction * state.vm.strength * clampf(dt * 60.0, 0.0, 2.0));
            if (changed > 0) {
                state.meshDirty = true;
                state.vm.status = state.vm.tool + " terrain · " + changed + " samples";
            }
        }
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
    configureWorkspace();
    registerProjectCommands();
    generateTerrain();
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
    // The editor and game use the same render path. This example leaves the
    // workspace center transparent and presents the authoritative scene there;
    // projects remain free to replace the scene presenter with another host.
    gfx.render3D();
    ui.beginFrameAndRender();
    state.frameCount += 1;
    if (!state.screenshotSaved && state.frameCount > 8 && gfx.saveFramePng("composable-editor.png")) {
        state.screenshotSaved = true;
        print("composable-editor: saved composable-editor.png\n");
    }
};
