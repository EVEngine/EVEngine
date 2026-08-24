// Editor API V2 runtime-builder example.
//
// The game injects its own build command into EditorCommandService. The same
// command is discovered, planned and executed by EditorSession; the UI only
// creates command payloads and renders results.

const VIEW_X = 24.0;
const VIEW_Y = 32.0;
const CELL = 52.0;
const COLS = 12;
const ROWS = 11;

editorV2 <- persist("editorV2", function() {
    return {
        editor = null
        session = null
        objects = []
        selectedAsset = 0
        selectedMaterial = 0
        credits = 600
        revision = 0
        lastMouseDown = false
        status = "Choose an asset and click the scene"
    };
});

assets <- [
    { id = "park.asset.tree", label = "Tree", cost = 20 },
    { id = "park.asset.bench", label = "Bench", cost = 35 },
    { id = "park.asset.ride", label = "Ride", cost = 120 }
];

materials <- [
    { id = "park.material.forest", label = "Forest", r = 0.24, g = 0.72, b = 0.38 },
    { id = "park.material.ocean", label = "Ocean", r = 0.22, g = 0.56, b = 0.92 },
    { id = "park.material.sunset", label = "Sunset", r = 0.94, g = 0.48, b = 0.25 }
];

function findObjectAt(x, y) {
    foreach (object in editorV2.objects)
        if (object.x == x && object.y == y) return object;
    return null;
}

function registerGameCommands() {
    // This callback is game logic. It can also be registered by a native
    // IGameEditorExtension; the session and UI do not know which host supplied it.
    return editorV2.editor.registerScriptCommand(
        "park.scene.place-asset", "Place Park Asset", "Park/Build",
        function(payload) {
            if (!("x" in payload) || !("y" in payload) ||
                !("asset" in payload) || !("material" in payload) ||
                !("cost" in payload)) return false;
            if (payload.x < 0 || payload.y < 0 || payload.x >= COLS || payload.y >= ROWS)
                return false;
            if (findObjectAt(payload.x, payload.y) != null || payload.cost > editorV2.credits)
                return false;

            editorV2.objects.append({
                x = payload.x,
                y = payload.y,
                asset = payload.asset,
                material = payload.material
            });
            editorV2.credits -= payload.cost;
            editorV2.revision += 1;
            return true;
        });
}

function buildAt(gridX, gridY) {
    local asset = assets[editorV2.selectedAsset];
    local material = materials[editorV2.selectedMaterial];
    local planned = editorV2.session.planCommand("park.scene.place-asset", {
        x = gridX,
        y = gridY,
        asset = asset.id,
        material = material.id,
        cost = asset.cost
    });
    if (!planned.accepted) {
        editorV2.status = "Plan rejected: " + planned.status;
        return;
    }

    local receipt = editorV2.session.executePlan(planned.planId, {});
    if (receipt.accepted) {
        editorV2.status = "Committed " + asset.label + " / " + material.label +
                          " · transaction=" + receipt.transactionId;
        print("editor-api-v2: added " + asset.label + " at " + gridX + "," + gridY + "\n");
    } else {
        editorV2.status = "Commit rejected: occupied cell or insufficient credits";
    }
}

function addSelectedAsset() {
    // Palette actions must have an immediate, visible result. Keep precise
    // placement available through the viewport, while using the same command
    // plan/execute path for quick-add.
    for (local y = 0; y < ROWS; ++y) {
        for (local x = 0; x < COLS; ++x) {
            if (findObjectAt(x, y) == null) {
                buildAt(x, y);
                return;
            }
        }
    }
    editorV2.status = "Commit rejected: scene grid is full";
}

function rebuildUi() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("Runtime Park Builder", "root");
    ui.text("Editor API V2", "title");
    ui.text("", "profile");
    ui.text("", "credits");
    ui.text("", "status");

    ui.text("Quick add (click once to create)", "asset-title");
    ui.beginRow("assets", 6.0);
    ui.button("Add Tree", "asset-tree");
    ui.button("Add Bench", "asset-bench");
    ui.button("Add Ride", "asset-ride");
    ui.end();

    ui.text("Material palette", "material-title");
    ui.beginRow("materials", 6.0);
    ui.button("Forest", "material-forest");
    ui.button("Ocean", "material-ocean");
    ui.button("Sunset", "material-sunset");
    ui.end();

    ui.text("", "selection");
    ui.text("Quick-add above, or click a grid cell for precise placement", "hint");
    ui.text("", "commands");
    ui.end();
    ui.mountBuildAs("editor-v2");
    ui.select("editor-v2");
    ui.setHostOverlay(true);
    ui.setHostPos(680.0, 20.0, 0.0, 0.0);
    ui.setHostSize(380.0, 560.0);
}

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.065, 0.09, 1.0);
    editorV2.editor = eve.Editor();
    registerGameCommands();
    editorV2.session = editorV2.editor.newSession();
    rebuildUi();

    local discovered = "Discovered commands: ";
    for (local i = 0; i < editorV2.session.getCommandCount(); ++i) {
        if (i > 0) discovered += ", ";
        discovered += editorV2.session.getCommandName(i);
    }
    ui.setText("commands", discovered);
    print("editor-api-v2: game command injected; discovery/plan/execute ready\n");
};

eve_reload <- function() {
    registerGameCommands();
    editorV2.session = editorV2.editor.newSession();
    rebuildUi();
};

eve_update = function(dt) {
    ui.select("editor-v2");
    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "editor-v2/asset-tree") {
            editorV2.selectedAsset = 0;
            addSelectedAsset();
        } else if (clicked == "editor-v2/asset-bench") {
            editorV2.selectedAsset = 1;
            addSelectedAsset();
        } else if (clicked == "editor-v2/asset-ride") {
            editorV2.selectedAsset = 2;
            addSelectedAsset();
        } else if (clicked == "editor-v2/material-forest") editorV2.selectedMaterial = 0;
        else if (clicked == "editor-v2/material-ocean") editorV2.selectedMaterial = 1;
        else if (clicked == "editor-v2/material-sunset") editorV2.selectedMaterial = 2;
        clicked = ui.consumeClick();
    }

    local mouseDown = mouse.isDown(1);
    if (mouseDown && !editorV2.lastMouseDown && !ui.wantCaptureMouse()) {
        local gridX = ((mouse.getX() - VIEW_X) / CELL).tointeger();
        local gridY = ((mouse.getY() - VIEW_Y) / CELL).tointeger();
        if (gridX >= 0 && gridY >= 0 && gridX < COLS && gridY < ROWS)
            buildAt(gridX, gridY);
    }
    editorV2.lastMouseDown = mouseDown;

    local asset = assets[editorV2.selectedAsset];
    local material = materials[editorV2.selectedMaterial];
    ui.setText("profile", "Host: developer · game/editor isomorphic command path");
    ui.setText("credits", "Credits: " + editorV2.credits + " · Revision: " + editorV2.revision);
    ui.setText("selection", "Selected: " + asset.label + " / " + material.label +
                            " · Cost " + asset.cost);
    ui.setText("status", editorV2.status);
};

eve_render = function() {
    gfx.clear();

    // Scene viewport and grid.
    gfx.drawSolidRect(VIEW_X - 4.0, VIEW_Y - 4.0, COLS * CELL + 8.0, ROWS * CELL + 8.0,
                      0.12, 0.14, 0.18, 1.0);
    for (local y = 0; y < ROWS; ++y) {
        for (local x = 0; x < COLS; ++x) {
            local shade = ((x + y) % 2 == 0) ? 0.105 : 0.125;
            gfx.drawSolidRect(VIEW_X + x * CELL + 1.0, VIEW_Y + y * CELL + 1.0,
                              CELL - 2.0, CELL - 2.0, shade, shade + 0.025, shade + 0.035, 1.0);
        }
    }

    foreach (object in editorV2.objects) {
        local material = materials[0];
        foreach (candidate in materials)
            if (candidate.id == object.material) material = candidate;
        local inset = object.asset == "park.asset.ride" ? 5.0 :
                      (object.asset == "park.asset.bench" ? 12.0 : 16.0);
        gfx.drawSolidRect(VIEW_X + object.x * CELL + inset, VIEW_Y + object.y * CELL + inset,
                          CELL - inset * 2.0, CELL - inset * 2.0,
                          material.r, material.g, material.b, 1.0);
    }

    ui.select("editor-v2");
    ui.beginFrameAndRender();
};
