// Configurable 3D dungeon dressing showcase for level.roguelike.
dofile("assetpack.nut");

persist dungeonGrid = null
persist dungeonCamera = null
persist dungeonInstances = []
persist dungeonTemplates = {}
persist dungeonSeed = 20260828

const DUNGEON_W = 34;
const DUNGEON_H = 26;
const DUNGEON_CELL = 4.0;

function modelPath(id) {
    return dungeonAssets.root + id + dungeonAssets.extension;
}

function templatesFor(id) {
    if (id in dungeonTemplates) return dungeonTemplates[id];
    local data = model3d.newModelDataFromFile(modelPath(id));
    local templates = [];
    for (local i = 0; i < data.getMeshCount(); ++i) {
        local source = model3d.createRenderable(gfx, data, i);
        templates.append({mesh=source.getMesh(), material=source.getMaterial()});
        source.setVisible(false);
    }
    dungeonTemplates[id] <- templates;
    return templates;
}

function addAsset(id, x, y, z, yaw, sx=1.0, sy=1.0, sz=1.0) {
    foreach (template in templatesFor(id)) {
        local instance = eve.Renderable3D();
        instance.setMesh(template.mesh);
        instance.setMaterial(template.material);
        instance.setPosition(x, y, z);
        instance.setYaw(yaw);
        instance.setScale(sx, sy, sz);
        instance.setCastShadow(true);
        instance.setReceiveShadow(true);
        dungeonInstances.append(instance);
    }
}

function walkable(x, y) {
    if (x < 0 || y < 0 || x >= DUNGEON_W || y >= DUNGEON_H) return false;
    local cell = dungeonGrid.getCell(x, y);
    return cell == 2 || cell == 3;
}

function wallFacing(x, y) {
    if (walkable(x, y + 1)) return 0.0;
    if (walkable(x + 1, y)) return 1.5707963;
    if (walkable(x, y - 1)) return 3.1415926;
    return 4.7123890;
}

function clearDungeon() {
    foreach (instance in dungeonInstances) instance.setVisible(false);
    dungeonInstances.clear();
}

function rebuildDungeon() {
    clearDungeon();
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) throw paramsResult.status.summary;
    local p = paramsResult.value;
    p.setSeed(dungeonSeed);
    p.setSize(DUNGEON_W, DUNGEON_H);
    p.setInt("roomCount", 11);
    p.setInt("roomMin", 4);
    p.setInt("roomMax", 7);
    p.setInt("spacing", 1);
    p.setString("corridorStyle", "l");
    p.setString("floorPattern", "cobble");
    p.setString("decorSet", "mixed");
    p.setFloat("decorDensity", 0.08);
    p.setFloat("propDensity", 0.28);
    configureDungeonAssetPack(p);
    local generated = procgen.generate("level.roguelike", p);
    if (!generated.ok) throw generated.status.summary;
    dungeonGrid = generated.value;

    local ox = -DUNGEON_W * DUNGEON_CELL * 0.5;
    local oz = -DUNGEON_H * DUNGEON_CELL * 0.5;
    for (local y = 0; y < DUNGEON_H; ++y) {
        for (local x = 0; x < DUNGEON_W; ++x) {
            if (walkable(x, y)) {
                addAsset(dungeonAssets.floor, ox + x * DUNGEON_CELL, 0.0,
                         oz + y * DUNGEON_CELL, 0.0);
            } else if (dungeonGrid.getCell(x, y) == 1 &&
                       (walkable(x + 1, y) || walkable(x - 1, y) ||
                        walkable(x, y + 1) || walkable(x, y - 1))) {
                addAsset(dungeonAssets.wall, ox + x * DUNGEON_CELL, 0.0,
                         oz + y * DUNGEON_CELL, wallFacing(x, y));
            }
        }
    }

    for (local i = 0; i < dungeonGrid.getObjectCount(); ++i) {
        local role = dungeonGrid.getObjectType(i);
        local id = role in dungeonAssets.roles ? dungeonAssets.roles[role] : "";
        if (role == "spawn") continue;
        if (role == "stairs") id = dungeonAssets.stairs;
        if (id == "") continue;
        addAsset(id, ox + dungeonGrid.getObjectX(i) * DUNGEON_CELL, 0.0,
                 oz + dungeonGrid.getObjectY(i) * DUNGEON_CELL, 0.0);
    }
}

if (dungeonCamera == null) {
    dungeonCamera = eve.Camera3D();
    dungeonCamera.setEye(45.0, 55.0, -55.0);
    dungeonCamera.setTarget(0.0, 0.0, 0.0);
    dungeonCamera.setUp(0.0, 1.0, 0.0);
    dungeonCamera.setFov(66.0);
    dungeonCamera.setAmbient(0.34, 0.36, 0.42);
    dungeonCamera.setActive(true);
    gfx.setDirectionalLight(-0.55, -1.0, -0.35, 1.35, 1.24, 1.08);
}
if (dungeonGrid == null) rebuildDungeon();
gfx.setBackgroundColor(0.035, 0.025, 0.075, 1.0);

function eve_update(dt) {
    if (keyboard.isDown("r") || keyboard.isDown("R")) {
        dungeonSeed = procgen.randomSeed();
        rebuildDungeon();
    }
}

function eve_render() {
    gfx.clear();
    gfx.render3D();
}
