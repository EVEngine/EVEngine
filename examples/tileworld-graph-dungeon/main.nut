// TileWorldCreator-style runtime example implemented with EVEngine-native graphs.
// R regenerates with the next deterministic seed. Space toggles the camera orbit.

persist twSeed = 435
persist twCamera = null
persist twIncrementalBuild = null
persist twClusterParts = {}
persist twPhysicsWorld = null
persist twWalls = null
persist twPath = null
persist twYaw = -0.72
persist twRotate = true
persist twPreviousKeys = {}

const TW_WIDTH = 25;
const TW_HEIGHT = 19;
const TW_CELL = 2.0;

function twRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function twPressed(key) {
    local down = keyboard.isDown(key);
    local previous = key in twPreviousKeys ? twPreviousKeys[key] : false;
    twPreviousKeys[key] <- down;
    return down && !previous;
}

function twBuildRock() {
    local params = twRequire(procgen.newParams(), "rock parameters");
    params.setSeed(twSeed + 991);
    params.setInt("subdivisions", 0);
    params.setString("baseShape", "block");
    params.setFloat("radius", 0.32);
    params.setFloat("variation", 0.18);
    params.setFloat("flattening", 0.25);
    params.setFloat("angularity", 0.65);
    return twRequire(procgen.buildMesh("mesh.rock", params), "rock mesh");
}

function twBuildClusterCollider(meshBuild) {
    if (twPhysicsWorld == null) return null;
    local vertices = [];
    local indices = [];
    for (local i = 0; i < meshBuild.getVertexCount(); ++i) {
        vertices.append(meshBuild.getPositionX(i));
        vertices.append(meshBuild.getPositionY(i));
        vertices.append(meshBuild.getPositionZ(i));
    }
    for (local i = 0; i < meshBuild.getIndexCount(); ++i)
        indices.append(meshBuild.getIndex(i));
    if (vertices.len() < 9 || indices.len() < 3) return null;

    // Construct-before-destroy keeps the previous collision authoritative if
    // validation or Box3D mesh creation rejects the replacement.
    local body = twPhysicsWorld.newBody("static", 0.0, 0.0, 0.0);
    try {
        body.newTriangleMeshShape(vertices, indices);
    } catch (error) {
        twPhysicsWorld.destroyBody(body);
        throw error;
    }
    return body;
}

function twFirstOccupied(grid) {
    for (local y = 0; y < grid.getHeight(); ++y)
        for (local x = 0; x < grid.getWidth(); ++x)
            if (grid.getCell(x, y) != 0) return [x, y];
    return null;
}

function twLastOccupied(grid) {
    for (local y = grid.getHeight() - 1; y >= 0; --y)
        for (local x = grid.getWidth() - 1; x >= 0; --x)
            if (grid.getCell(x, y) != 0) return [x, y];
    return null;
}

function twBuild() {
    if (twPhysicsWorld == null && has_module("physics"))
        twPhysicsWorld = physics.newWorld3D(0.0, 0.0, 0.0, false);

    // PointGraph is nested in GridGraph: selected grid cells become elevated points.
    local pointGraph = twRequire(procgen.newPointGraph(), "point graph");
    if (!pointGraph.addNode("input", "input")) throw "PointGraph input node failed";
    if (!pointGraph.addNode("raise", "transform")) throw "PointGraph transform node failed";
    if (!pointGraph.connect("input", "raise", 0)) throw "PointGraph connection failed";
    if (!pointGraph.setNodeFloat("raise", "y", 0.62)) throw "PointGraph parameter failed";

    local gridGraph = twRequire(procgen.newGridGraph(), "grid graph");
    // Reuse EVEngine's mature semantic dungeon generator as a graph source.
    twRequire(gridGraph.addNode("rooms", "generate.registry"), "registry generator node");
    twRequire(gridGraph.setNodeString("rooms", "algorithm", "level.roguelike"), "generator algorithm");
    twRequire(gridGraph.setNodeInt("rooms", "width", TW_WIDTH), "dungeon width");
    twRequire(gridGraph.setNodeInt("rooms", "height", TW_HEIGHT), "dungeon height");
    twRequire(gridGraph.setNodeInt("rooms", "seed", twSeed), "dungeon seed");
    twRequire(gridGraph.setNodeInt("rooms", "roomCount", 8), "room budget");
    twRequire(gridGraph.setNodeInt("rooms", "roomMin", 4), "minimum room size");
    twRequire(gridGraph.setNodeInt("rooms", "roomMax", 7), "maximum room size");
    twRequire(gridGraph.setNodeString("rooms", "layoutStyle", "clustered"), "room layout");
    twRequire(gridGraph.setNodeString("rooms", "connectionStyle", "nearest"), "room connections");
    twRequire(gridGraph.setNodeString("rooms", "corridorStyle", "l"), "corridor style");
    twRequire(gridGraph.setNodeString("rooms", "floorPattern", "brick"), "floor pattern");
    twRequire(gridGraph.setNodeFloat("rooms", "propDensity", 0.14), "room prop density");
    twRequire(gridGraph.setNodeFloat("rooms", "corridorLightDensity", 0.04), "corridor lights");

    twRequire(gridGraph.addNode("floor", "select.semantic"), "floor semantic selector");
    twRequire(gridGraph.addNode("corridors", "select.semantic"), "corridor semantic selector");
    twRequire(gridGraph.addNode("walls", "select.semantic"), "wall semantic selector");
    twRequire(gridGraph.setNodeInt("floor", "semantic", 2), "floor semantic");
    twRequire(gridGraph.setNodeInt("corridors", "semantic", 3), "corridor semantic");
    twRequire(gridGraph.setNodeInt("walls", "semantic", 1), "wall semantic");
    twRequire(gridGraph.connect("rooms", "floor", 0), "floor extraction");
    twRequire(gridGraph.connect("rooms", "corridors", 0), "corridor extraction");
    twRequire(gridGraph.connect("rooms", "walls", 0), "wall extraction");
    twRequire(gridGraph.addNode("wall_edges", "select.detail_range"), "wall edge selector");
    twRequire(gridGraph.setNodeInt("wall_edges", "min", 1), "wall edge minimum mask");
    twRequire(gridGraph.setNodeInt("wall_edges", "max", 255), "wall edge maximum mask");
    twRequire(gridGraph.connect("walls", "wall_edges", 0), "wall edge connection");
    twRequire(gridGraph.addNode("walkable", "grid.union"), "walkable union node");
    twRequire(gridGraph.connect("floor", "walkable", 0), "floor union connection");
    twRequire(gridGraph.connect("corridors", "walkable", 1), "corridor union connection");
    twRequire(gridGraph.addNode("autotile", "grid.autotile"), "autotile node");
    twRequire(gridGraph.connect("walkable", "autotile", 0), "autotile connection");

    twRequire(gridGraph.addNode("scatter", "select.random"), "scatter node");
    twRequire(gridGraph.setNodeFloat("scatter", "weight", 0.055), "scatter weight");
    twRequire(gridGraph.setNodeInt("scatter", "seed", twSeed + 17), "scatter seed");
    twRequire(gridGraph.connect("autotile", "scatter", 0), "scatter connection");
    twRequire(gridGraph.addNode("points", "convert.grid_to_points"), "point conversion node");
    twRequire(gridGraph.setNodeFloat("points", "cellSize", TW_CELL), "point cell size");
    twRequire(gridGraph.connect("scatter", "points", 0), "point conversion connection");
    twRequire(gridGraph.addNode("point_pass", "point.subgraph"), "PointGraph bridge node");
    twRequire(gridGraph.connect("points", "point_pass", 0), "PointGraph bridge connection");
    twRequire(gridGraph.setNodePointSubgraph("point_pass", pointGraph, "input", "raise"),
              "PointGraph bridge binding");

    local level = twRequire(gridGraph.execute("rooms"), "semantic dungeon execution");
    local grid = twRequire(gridGraph.execute("autotile"), "grid execution");
    local border = twRequire(gridGraph.execute("wall_edges"), "wall execution");
    local points = twRequire(gridGraph.execute("point_pass"), "point execution");

    // Deterministic Objects Build Layer: weighted assets, random transforms and
    // child placements remain ordinary PointSet data for downstream graphs.
    local objectLayer = eve.ProcgenObjectBuildLayer();
    twRequire(objectLayer.addAsset("dungeon/rock_a", 3.0), "rock A asset");
    twRequire(objectLayer.addAsset("dungeon/rock_b", 1.0), "rock B asset");
    objectLayer.setSeed(twSeed);
    twRequire(objectLayer.setPositionRadius(0.22), "object scatter radius");
    twRequire(objectLayer.setRandomRotation(0.0, 0.0, -180.0, 180.0, 0.0, 0.0),
              "object rotation range");
    twRequire(objectLayer.setRandomScale(0.72, 1.18, 0.72, 1.18, 0.72, 1.18, true),
              "object scale range");
    twRequire(objectLayer.addChild("dungeon/rubble", 1, 0.48, 0.25, 0.5, -180.0, 180.0),
              "rubble child rule");
    local buildStack = eve.ProcgenBuildLayerStack();
    twRequire(buildStack.addTileLayer("floor", true, TW_CELL, 0.34, "dungeon"), "floor build layer");
    twRequire(buildStack.addObjectLayer("props", true, objectLayer), "object build layer");
    local persistedStack = buildStack.serializeDefinition();
    local restoredStack = eve.ProcgenBuildLayerStack();
    twRequire(restoredStack.deserializeDefinition(persistedStack), "build layer restore");
    if (twIncrementalBuild == null) twIncrementalBuild = eve.ProcgenIncrementalBuildExecutor();
    local clusterDelta = twRequire(twIncrementalBuild.update(restoredStack, grid, points, 8, TW_CELL),
                                   "incremental cluster build");
    local stableDelta = twRequire(twIncrementalBuild.update(restoredStack, grid, points, 8, TW_CELL),
                                  "stable incremental cluster build");

    // Scene-side cluster cache consumes only upserts/removals from the executor.
    // No full BuildLayerStack execution is needed for the visible floor/props.
    local rockBuild = twBuildRock();
    for (local i = 0; i < clusterDelta.getCount(); ++i) {
        local clusterX = clusterDelta.getClusterX(i);
        local clusterZ = clusterDelta.getClusterZ(i);
        local key = clusterX + ":" + clusterZ;
        if (clusterDelta.isRemoved(i)) {
            if (key in twClusterParts) {
                twClusterParts[key].floor.setVisible(false);
                twClusterParts[key].props.setVisible(false);
                if (twPhysicsWorld != null && "collider" in twClusterParts[key] &&
                    twClusterParts[key].collider != null)
                    twPhysicsWorld.destroyBody(twClusterParts[key].collider);
                delete twClusterParts[key];
            }
            continue;
        }
        local artifacts = twRequire(clusterDelta.getArtifacts(i), "cluster artifacts");
        local floorBuild = twRequire(artifacts.getMesh(0), "cluster floor artifact");
        local objectPoints = twRequire(artifacts.getPoints(1), "cluster object artifact");
        local floorMesh = twRequire(procgen.uploadMesh(floorBuild, gfx), "cluster floor upload");
        if (!(key in twClusterParts))
            twClusterParts[key] <- {
                floor = eve.Renderable3D(), props = eve.Renderable3D(), collider = null
            };
        local part = twClusterParts[key];
        if (!("collider" in part)) part.collider <- null;
        local replacementCollider = twBuildClusterCollider(floorBuild);
        if (twPhysicsWorld != null && part.collider != null)
            twPhysicsWorld.destroyBody(part.collider);
        part.collider = replacementCollider;
        part.floor.setMesh(floorMesh);
        part.floor.setVisible(true);
        part.floor.setTint(0.31 + 0.025 * ((clusterX + clusterZ) & 1), 0.43, 0.53, 1.0);
        part.floor.setRoughness(0.94);
        part.floor.setMetallic(0.02);
        part.floor.setCastShadow(true);
        part.floor.setReceiveShadow(true);
        if (objectPoints.getCount() == 0) {
            part.props.setVisible(false);
        } else {
            local propGraph = twRequire(procgen.newMeshGraph(), "cluster prop mesh graph");
            twRequire(propGraph.addNode("rock", "mesh.input"), "cluster rock input");
            twRequire(propGraph.addNode("points", "point.input"), "cluster point input");
            twRequire(propGraph.addNode("instances", "mesh.instance_points"), "cluster instance node");
            twRequire(propGraph.setNodeMesh("rock", rockBuild), "cluster rock binding");
            twRequire(propGraph.setNodePoints("points", objectPoints), "cluster point binding");
            twRequire(propGraph.setNodeFloat("instances", "scale", 1.0), "cluster instance scale");
            twRequire(propGraph.connect("rock", "instances", 0), "cluster rock connection");
            twRequire(propGraph.connect("points", "instances", 1), "cluster point connection");
            local propBuild = twRequire(propGraph.execute("instances"), "cluster instance execution");
            local propMesh = twRequire(procgen.uploadMesh(propBuild, gfx), "cluster prop upload");
            part.props.setMesh(propMesh);
            part.props.setVisible(true);
            part.props.setTint(0.80, 0.48, 0.19, 1.0);
            part.props.setRoughness(0.78);
            part.props.setMetallic(0.08);
            part.props.setCastShadow(true);
            part.props.setReceiveShadow(true);
        }
    }

    // Feed explicit owned Grid2D endpoint masks into GridGraph. This exercises the
    // public newGrid -> GridGraph mixed-composition path, not an internal shortcut.
    local startCell = null;
    local targetCell = null;
    for (local i = 0; i < level.getObjectCount(); ++i) {
        local role = level.getObjectType(i);
        if (role == "spawn") startCell = [level.getObjectX(i).tointeger(), level.getObjectY(i).tointeger()];
        if (role == "stairs") targetCell = [level.getObjectX(i).tointeger(), level.getObjectY(i).tointeger()];
    }
    if (startCell == null) startCell = twFirstOccupied(grid);
    if (targetCell == null) targetCell = twLastOccupied(grid);
    if (startCell == null || targetCell == null ||
        (startCell[0] == targetCell[0] && startCell[1] == targetCell[1]))
        throw "generated layout has no distinct navigable endpoint pair";
    local startSelection = twRequire(procgen.newGrid(TW_WIDTH, TW_HEIGHT), "route start grid");
    local targetSelection = twRequire(procgen.newGrid(TW_WIDTH, TW_HEIGHT), "route target grid");
    twRequire(startSelection.fill(0), "route start clear");
    twRequire(targetSelection.fill(0), "route target clear");
    twRequire(startSelection.setCell(startCell[0], startCell[1], 1), "route start cell");
    twRequire(targetSelection.setCell(targetCell[0], targetCell[1], 1), "route target cell");
    twRequire(gridGraph.addNode("route_start", "grid.input"), "route start input");
    twRequire(gridGraph.addNode("route_target", "grid.input"), "route target input");
    twRequire(gridGraph.setNodeGrid("route_start", startSelection), "route start binding");
    twRequire(gridGraph.setNodeGrid("route_target", targetSelection), "route target binding");
    twRequire(gridGraph.addNode("route", "grid.path"), "path node");
    twRequire(gridGraph.connect("autotile", "route", 0), "navigation connection");
    twRequire(gridGraph.connect("route_start", "route", 1), "start connection");
    twRequire(gridGraph.connect("route_target", "route", 2), "target connection");
    local route = twRequire(gridGraph.execute("route"), "path execution");
    local routeCells = 0;
    for (local y = 0; y < TW_HEIGHT; ++y)
        for (local x = 0; x < TW_WIDTH; ++x)
            if (route.getCell(x, y) != 0) routeCells += 1;
    if (routeCells < 2) throw "path output is unexpectedly short";

    // A second build layer raises only the selected boundary cells into walls.
    local wallGraph = twRequire(procgen.newMeshGraph(), "wall mesh graph");
    twRequire(wallGraph.addNode("grid", "grid.input"), "wall grid input");
    twRequire(wallGraph.addNode("walls", "mesh.grid_tiles"), "wall mesh node");
    twRequire(wallGraph.setNodeGrid("grid", border), "wall grid binding");
    twRequire(wallGraph.setNodeFloat("walls", "cellSize", TW_CELL), "wall cell size");
    twRequire(wallGraph.setNodeFloat("walls", "height", 1.65), "wall height");
    twRequire(wallGraph.setNodeString("walls", "group", "walls"), "wall group");
    twRequire(wallGraph.connect("grid", "walls", 0), "wall connection");
    local wallBuild = twRequire(wallGraph.execute("walls"), "wall mesh execution");
    local wallMesh = twRequire(procgen.uploadMesh(wallBuild, gfx), "wall mesh upload");

    // Navigation is another independent build layer laid just above the floor.
    local pathGraph = twRequire(procgen.newMeshGraph(), "path mesh graph");
    twRequire(pathGraph.addNode("grid", "grid.input"), "path grid input");
    twRequire(pathGraph.addNode("path", "mesh.grid_tiles"), "path mesh node");
    twRequire(pathGraph.setNodeGrid("grid", route), "path grid binding");
    twRequire(pathGraph.setNodeFloat("path", "cellSize", TW_CELL), "path cell size");
    twRequire(pathGraph.setNodeFloat("path", "height", 0.43), "path height");
    twRequire(pathGraph.setNodeString("path", "group", "navigation"), "path group");
    twRequire(pathGraph.connect("grid", "path", 0), "path mesh connection");
    local pathBuild = twRequire(pathGraph.execute("path"), "path mesh execution");
    local pathMesh = twRequire(procgen.uploadMesh(pathBuild, gfx), "path mesh upload");

    if (twWalls == null) twWalls = eve.Renderable3D();
    twWalls.setMesh(wallMesh);
    twWalls.setTint(0.18, 0.24, 0.31, 1.0);
    twWalls.setRoughness(0.88);
    twWalls.setMetallic(0.06);
    twWalls.setCastShadow(true);
    twWalls.setReceiveShadow(true);

    if (twPath == null) twPath = eve.Renderable3D();
    twPath.setMesh(pathMesh);
    twPath.setTint(0.92, 0.56, 0.12, 1.0);
    twPath.setRoughness(0.72);
    twPath.setMetallic(0.04);
    twPath.setCastShadow(false);
    twPath.setReceiveShadow(true);

    print("tileworld-graph-dungeon: seed=" + twSeed +
          " cells=" + grid.getWidth() + "x" + grid.getHeight() +
          " dungeonObjects=" + level.getObjectCount() +
          " liveClusters=" + twClusterParts.len() +
          " wallTriangles=" + (wallBuild.getIndexCount() / 3) +
          " routeCells=" + routeCells +
          " props=" + points.getCount() +
          " colliderBodies=" + (twPhysicsWorld == null ? 0 : twPhysicsWorld.getBodyCount()) +
          " physics=" + (twPhysicsWorld == null ? "disabled" : "clustered") +
          " dirtyClusters=" + clusterDelta.getCount() +
          " stableClusters=" + stableDelta.getCount() + "\n");
}

if (twCamera == null) {
    twCamera = eve.Camera3D();
    twCamera.setActive(true);
}
local twCenterX = TW_WIDTH * TW_CELL * 0.5;
local twCenterZ = TW_HEIGHT * TW_CELL * 0.5;
twCamera.setEye(twCenterX + cos(twYaw) * 76.0, 58.0,
                twCenterZ + sin(twYaw) * 76.0);
twCamera.setTarget(twCenterX, 0.0, twCenterZ);
twCamera.setUp(0.0, 1.0, 0.0);
twCamera.setFov(42.0);
twCamera.setClipPlanes(0.1, 300.0);
twCamera.setAmbient(0.31, 0.34, 0.39);
gfx.setDirectionalLight(-0.55, -1.0, -0.35, 1.65, 1.48, 1.24);
gfx.setBackgroundColor(0.035, 0.055, 0.085, 1.0);

if (twClusterParts.len() == 0 || twWalls == null || twPath == null) twBuild();

eve_update = function(dt) {
    if (twPressed("r") || twPressed("R")) {
        twSeed = (twSeed + 1).tointeger();
        twBuild();
    }
    if (twPressed("space")) twRotate = !twRotate;
    if (twRotate) twYaw = (twYaw + dt * 0.10).tofloat();
    twCamera.setEye(twCenterX + cos(twYaw) * 76.0, 58.0,
                    twCenterZ + sin(twYaw) * 76.0);
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
