// Configurable 3D dungeon dressing showcase for level.roguelike.
dofile("assetpack.nut");

persist dungeonGrid = null
persist dungeonCamera = null
persist dungeonInstances = []
persist dungeonTemplates = {}
persist dungeonSeed = 20260828
persist dungeonFloorMesh = null

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

function addAsset(id, x, y, z, yaw, sx=1.0, sy=1.0, sz=1.0,
                  receiveLight=true, tintR=1.0, tintG=1.0, tintB=1.0) {
    foreach (template in templatesFor(id)) {
        local instance = eve.Renderable3D();
        instance.setMesh(template.mesh);
        instance.setMaterial(template.material);
        instance.setPosition(x, y, z);
        instance.setYaw(yaw);
        instance.setScale(sx, sy, sz);
        instance.setReceiveLight(receiveLight);
        instance.setTint(tintR, tintG, tintB, 1.0);
        instance.setCastShadow(receiveLight);
        instance.setReceiveShadow(true);
        dungeonInstances.append(instance);
    }
}

function addFloor(x, z) {
    if (dungeonFloorMesh == null) dungeonFloorMesh = gfx.newMeshCube(1.0);
    local floor = eve.Renderable3D();
    floor.setMesh(dungeonFloorMesh);
    floor.setPosition(x, -0.05, z);
    floor.setScale(DUNGEON_CELL, 0.18, DUNGEON_CELL);
    floor.setTint(0.56, 0.55, 0.52, 1.0);
    floor.setRoughness(0.96);
    floor.setReceiveLight(false);
    floor.setCastShadow(false);
    floor.setReceiveShadow(false);
    dungeonInstances.append(floor);
}

function walkable(x, y) {
    if (x < 0 || y < 0 || x >= DUNGEON_W || y >= DUNGEON_H) return false;
    local cell = dungeonGrid.getCell(x, y);
    return cell == 2 || cell == 3;
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
                local cx = ox + x * DUNGEON_CELL;
                local cz = oz + y * DUNGEON_CELL;
                // A neutral modular slab stays readable independently of the
                // external pack's material conventions. Packs can still map
                // every decorative role without renderer-specific coupling.
                addFloor(cx, cz);

                local detail = dungeonGrid.getDetail(x, y);
                if (detail >= 100 && dungeonAssets.details.len() > 0) {
                    local detailId = dungeonAssets.details[(detail - 100) % dungeonAssets.details.len()];
                    local detailYaw = ((x * 17 + y * 31 + dungeonSeed) % 4) * 1.5707963;
                    addAsset(detailId, cx, 0.06, cz, detailYaw);
                }

                // KayKit wall pieces are four units long and half a unit thick.
                // Place them on the walkable cell edges instead of at solid-cell
                // centres so corners and corridors form a continuous perimeter.
                if (!walkable(x, y - 1))
                    addAsset(dungeonAssets.wall, cx, 0.0, cz - DUNGEON_CELL * 0.5, 0.0);
                if (!walkable(x, y + 1))
                    addAsset(dungeonAssets.wall, cx, 0.0, cz + DUNGEON_CELL * 0.5, 3.1415926);
                if (!walkable(x - 1, y))
                    addAsset(dungeonAssets.wall, cx - DUNGEON_CELL * 0.5, 0.0, cz, 1.5707963);
                if (!walkable(x + 1, y))
                    addAsset(dungeonAssets.wall, cx + DUNGEON_CELL * 0.5, 0.0, cz, 4.7123890);
            }
        }
    }

    for (local i = 0; i < dungeonGrid.getObjectCount(); ++i) {
        local role = dungeonGrid.getObjectType(i);
        local id = dungeonGrid.getObjectAsset(i);
        if (id == "" && role in dungeonAssets.roles) id = dungeonAssets.roles[role];
        if (role == "spawn") continue;
        if (role == "stairs") id = dungeonAssets.stairs;
        if (id == "") continue;

        local rotationDegrees = dungeonGrid.getObjectRotation(i);
        local yaw = rotationDegrees * 0.01745329252;
        local flags = dungeonGrid.getObjectFlags(i);
        local px = ox + dungeonGrid.getObjectX(i) * DUNGEON_CELL;
        local pz = oz + dungeonGrid.getObjectY(i) * DUNGEON_CELL;
        local py = 0.0;

        // Bit 2 marks a wall-aligned prop. Move it from the tile centre to the
        // wall plane selected by its authored cardinal rotation.
        if ((flags & 2) != 0) {
            local edge = DUNGEON_CELL * 0.43;
            if (rotationDegrees == 90.0) px -= edge;
            else if (rotationDegrees == 180.0) pz -= edge;
            else if (rotationDegrees == 270.0) px += edge;
            else pz += edge;
        }

        // KayKit wall torches and weapon plaques are modelled around their
        // local origin; lift those roles to a natural eye-level mount. Food is
        // authored at floor origin, so overlays sharing a table cell are raised.
        if (role == "light" && (flags & 2) != 0) py = 2.15;
        else if (role == "weapon") py = 2.0;
        else if (role == "food") py = 1.88;

        addAsset(id, px, py, pz, yaw);
    }
}

if (dungeonCamera == null) {
    dungeonCamera = eve.Camera3D();
    dungeonCamera.setEye(90.0, 110.0, -110.0);
    dungeonCamera.setTarget(0.0, 0.0, 0.0);
    dungeonCamera.setUp(0.0, 1.0, 0.0);
    dungeonCamera.setFov(66.0);
    dungeonCamera.setClipPlanes(0.1, 400.0);
    dungeonCamera.setAmbient(0.62, 0.64, 0.70);
    dungeonCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.25, 1.75, 1.62, 1.45);
}
if (dungeonGrid == null) rebuildDungeon();
gfx.setBackgroundColor(0.025, 0.015, 0.055, 1.0);

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
