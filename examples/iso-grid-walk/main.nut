// Developer-evaluation sample for independent 300x300 isometric PNG tiles.
// Arrow keys / WASD move one logical cell. Click a cell to follow an A* path.

persist isoMap = null
persist logicLayer = null
persist groundLayer = null
persist routeLayer = null
persist objectLayer = null
persist pathfinder = null
persist textures = {}
persist playerX = 1
persist playerY = 1
persist activePath = null
persist pathIndex = 0
persist stepClock = 0.0
persist statusText = "Ready"
persist hudBuilt = false
persist previousKeys = {}
persist previousMouseDown = false

const MAP_W = 4;
const MAP_H = 4;
const TILE_W = 150.0;
const TILE_H = 75.0;
const DRAW_SIZE = 150.0;
const RENDER_SPACING = 2.0;
const GID_FLOOR = 1;
const GID_BLOCKED = 2;
const GID_PLAYER_VISUAL = 5;
const GID_GOAL_VISUAL = 7;
const GID_ROUTE_VISUAL = 10;
const GID_GROUND_A = 76;
const GID_GROUND_B = 77;
const GID_WALL_VISUAL = 78;
const GID_TOWER_VISUAL = 81;

local obstacleKinds = {};
obstacleKinds["2,0"] <- "wall";
obstacleKinds["2,1"] <- "tower";
obstacleKinds["0,2"] <- "wall";
obstacleKinds["1,2"] <- "tower";

function cellKey(x, y) { return x + "," + y; }

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local wasDown = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !wasDown;
}

function configureDisplayLayer(target) {
    target.applyConfig(@"{
      ""orientation"":""isometric"",
      ""tilewidth"":150,""tileheight"":75,
      ""offsetx"":475,""offsety"":62
    }");
    target.setOrigin(475.0, 62.0);
    target.setRenderSpacing(RENDER_SPACING, RENDER_SPACING);
    // World-space tiles share one painter-sorted queue. Separate layer values
    // are reserved for intentional hard barriers such as HUD overlays.
    target.setLayer(0);
    if (!target.loadTilesetManifest("assets/generated/tiles.tileset.json"))
        print("failed to load generated TileSet manifest\n");
}

function buildLogicalMap() {
    isoMap = eve.Map();
    logicLayer = isoMap.newLayer(MAP_W, MAP_H, TILE_W, TILE_H);
    logicLayer.applyConfig(@"{
      ""orientation"":""isometric"",
      ""tilewidth"":150,""tileheight"":75,
      ""offsetx"":475,""offsety"":62
    }");
    logicLayer.setOrigin(475.0, 62.0);
    logicLayer.setRenderSpacing(RENDER_SPACING, RENDER_SPACING);
    logicLayer.fill(GID_FLOOR);
    foreach (key, kind in obstacleKinds) {
        local comma = key.find(",");
        local x = key.slice(0, comma).tointeger();
        local y = key.slice(comma + 1).tointeger();
        logicLayer.setTile(x, y, GID_BLOCKED);
    }
    // This layer is deliberately logical-only: the source pack contains one
    // full 300x300 PNG per tile instead of a regular atlas.
    logicLayer.setVisible(false);
    groundLayer = isoMap.newLayer(MAP_W, MAP_H, TILE_W, TILE_H);
    routeLayer = isoMap.newLayer(MAP_W, MAP_H, TILE_W, TILE_H);
    objectLayer = isoMap.newLayer(MAP_W, MAP_H, TILE_W, TILE_H);
    configureDisplayLayer(groundLayer);
    configureDisplayLayer(routeLayer);
    configureDisplayLayer(objectLayer);
    for (local y = 0; y < MAP_H; y += 1)
        for (local x = 0; x < MAP_W; x += 1)
            groundLayer.setTile(x, y, ((x + y) % 3 == 0) ? GID_GROUND_B : GID_GROUND_A);
    foreach (key, kind in obstacleKinds) {
        local comma = key.find(",");
        local x = key.slice(0, comma).tointeger();
        local y = key.slice(comma + 1).tointeger();
        objectLayer.setTile(x, y, kind == "wall" ? GID_WALL_VISUAL : GID_TOWER_VISUAL);
    }
    objectLayer.setTile(MAP_W - 1, MAP_H - 1, GID_GOAL_VISUAL);
    objectLayer.setTile(playerX, playerY, GID_PLAYER_VISUAL);
    pathfinder = isoMap.newPathfinder(logicLayer);
    pathfinder.setTopology("auto");
    pathfinder.blockGid(GID_BLOCKED);
    pathfinder.setBlockEmpty(true);
}

function isWalkable(x, y) {
    return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H && pathfinder.isWalkable(x, y);
}

function setPlayerCell(x, y) {
    if (!isWalkable(x, y)) {
        statusText = "Blocked (" + x + ", " + y + ")";
        return false;
    }
    movePlayerVisual(x, y);
    activePath = null;
    refreshRouteLayer();
    statusText = "Moved to (" + x + ", " + y + ")";
    return true;
}

function movePlayerVisual(x, y) {
    if (objectLayer != null) {
        objectLayer.setTile(playerX, playerY,
            (playerX == MAP_W - 1 && playerY == MAP_H - 1) ? GID_GOAL_VISUAL : 0);
        objectLayer.setTile(x, y, GID_PLAYER_VISUAL);
    }
    playerX = x;
    playerY = y;
}

function refreshRouteLayer() {
    if (routeLayer == null) return;
    routeLayer.clear();
    if (activePath == null) return;
    for (local i = pathIndex; i < activePath.getLength(); i += 1)
        routeLayer.setTile(activePath.getX(i), activePath.getY(i), GID_ROUTE_VISUAL);
}

function requestPath(x, y) {
    if (!isWalkable(x, y)) {
        statusText = "Target is blocked";
        activePath = null;
        return;
    }
    activePath = pathfinder.findPath(playerX, playerY, x, y);
    pathIndex = (activePath != null && activePath.getLength() > 1) ? 1 : 0;
    stepClock = 0.0;
    statusText = activePath == null || activePath.getLength() == 0
        ? "No route" : "A* route: " + activePath.getLength() + " cells";
    refreshRouteLayer();
}

function refreshHud() {
    if (!hudBuilt) return;
    ui.setText("state", "Cell (" + playerX + ", " + playerY + ")  |  " + statusText +
        "  |  tiles " + isoMap.getLastVisibleTileCount() +
        " custom " + isoMap.getLastCustomVisualCount() +
        " atlases " + isoMap.getLastAtlasCount());
}

eve_init = function() {
    gfx.setBackgroundColor(0.035, 0.055, 0.085, 1.0);
    buildLogicalMap();
    if (!hudBuilt) {
        ui.beginBuild();
        ui.beginWindow("Grid Expedition", "root");
        ui.text("2.5D Grid Expedition", "title");
        ui.text("", "state");
        ui.text("Arrow keys / WASD: one-cell move", "keys");
        ui.text("Click a floor tile: A* path movement", "mouseHelp");
        ui.text("White/yellow cells are walkable; towers and walls block", "legend");
        ui.end();
        ui.mountBuildAs("hud");
        ui.select("hud");
        ui.setHostOverlay(true);
        ui.setHostPos(14.0, 12.0, 0.0, 0.0);
        hudBuilt = true;
    }
    refreshHud();
    print("iso-grid-walk ready: independent PNG asset path active\n");
};

eve_reload <- function() {
    buildLogicalMap();
    refreshHud();
};

eve_update = function(dt) {
    local dx = 0;
    local dy = 0;
    if (keyPressed("left") || keyPressed("a") || keyPressed("A")) dx = -1;
    if (keyPressed("right") || keyPressed("d") || keyPressed("D")) dx = 1;
    if (keyPressed("up") || keyPressed("w") || keyPressed("W")) dy = -1;
    if (keyPressed("down") || keyPressed("s") || keyPressed("S")) dy = 1;
    if (dx != 0 || dy != 0) setPlayerCell(playerX + dx, playerY + dy);

    local mouseDown = mouse.isDown(1);
    if (mouseDown && !previousMouseDown) {
        local tx = logicLayer.worldToTileX(mouse.getX(), mouse.getY());
        local ty = logicLayer.worldToTileY(mouse.getX(), mouse.getY());
        requestPath(tx, ty);
    }
    previousMouseDown = mouseDown;

    if (activePath != null && pathIndex > 0 && pathIndex < activePath.getLength()) {
        stepClock += dt;
        if (stepClock >= 0.16) {
            stepClock = 0.0;
            movePlayerVisual(activePath.getX(pathIndex), activePath.getY(pathIndex));
            pathIndex += 1;
            refreshRouteLayer();
            if (pathIndex >= activePath.getLength()) statusText = "Destination reached";
        }
    }
    refreshHud();
};

eve_render = function() {
    gfx.clear();
    isoMap.render(gfx);
    ui.beginFrameAndRender();
};
