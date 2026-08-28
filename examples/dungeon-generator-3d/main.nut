// Configurable 3D dungeon dressing showcase for level.roguelike.
dofile("assetpack.nut");

persist dungeonGrid = null
persist dungeonCamera = null
persist dungeonInstances = []
persist dungeonLights = []
persist dungeonTemplates = {}
persist dungeonSeed = 20260828
persist dungeonFloorMesh = null
persist dungeonFloorMaterials = {}

const DUNGEON_W = 42;
const DUNGEON_H = 32;
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

function addFloor(x, z, variant, tone) {
    if (("usePackFloors" in dungeonAssets) && dungeonAssets.usePackFloors) {
        local materialKey = tone[0].tostring() + ":" + tone[1].tostring() + ":" + tone[2].tostring();
        if (!(materialKey in dungeonFloorMaterials)) {
            local material = gfx.newMaterial();
            material.setTint(tone[0] * 0.72, tone[1] * 0.72, tone[2] * 0.72, 1.0);
            material.setRoughness(0.96);
            material.setDoubleSided(true);
            dungeonFloorMaterials[materialKey] <- material;
        }
        foreach (template in templatesFor(dungeonAssets.floor)) {
            local floor = eve.Renderable3D();
            floor.setMesh(template.mesh);
            floor.setMaterial(dungeonFloorMaterials[materialKey]);
            floor.setPosition(x, 0.0, z);
            floor.setCastShadow(false);
            floor.setReceiveShadow(true);
            dungeonInstances.append(floor);
        }
        return;
    }
    if (dungeonFloorMesh == null) dungeonFloorMesh = gfx.newMeshCube(1.0);
    local floor = eve.Renderable3D();
    floor.setMesh(dungeonFloorMesh);
    floor.setPosition(x, -0.05, z);
    floor.setScale(DUNGEON_CELL, 0.18, DUNGEON_CELL);
    local shade = 0.018 * ((variant % 5) - 2);
    floor.setTint(tone[0] + shade, tone[1] + shade, tone[2] + shade, 1.0);
    floor.setRoughness(0.96);
    floor.setReceiveLight(false);
    floor.setCastShadow(false);
    floor.setReceiveShadow(false);
    dungeonInstances.append(floor);
}

function floorTone(x, y) {
    for (local i = 0; i < dungeonGrid.getObjectCount(); ++i) {
        if (dungeonGrid.getObjectType(i) != "room") continue;
        local rx = dungeonGrid.getObjectX(i), ry = dungeonGrid.getObjectY(i);
        if (x < rx || y < ry || x >= rx + dungeonGrid.getObjectWidth(i) ||
            y >= ry + dungeonGrid.getObjectHeight(i)) continue;
        local theme = dungeonGrid.getObjectAsset(i);
        if (theme == "storage") return [0.43, 0.43, 0.42];
        if (theme == "quarters") return [0.50, 0.46, 0.41];
        if (theme == "dining") return [0.48, 0.44, 0.38];
        if (theme == "armory") return [0.40, 0.43, 0.45];
        if (theme == "treasury") return [0.49, 0.46, 0.39];
        if (theme == "shrine") return [0.50, 0.49, 0.46];
        return [0.43, 0.39, 0.34];
    }
    return [0.44, 0.45, 0.45];
}

function wallAsset(x, y, side) {
    if (!("walls" in dungeonAssets) || dungeonAssets.walls.len() == 0)
        return dungeonAssets.wall;
    local hash = x * 73856093 + y * 19349663 + side * 83492791 + dungeonSeed;
    if (hash < 0) hash = -hash;
    return dungeonAssets.walls[hash % dungeonAssets.walls.len()];
}

function architectureAsset(poolName, x, y, side) {
    if (!(poolName in dungeonAssets) || dungeonAssets[poolName].len() == 0) return "";
    local hash = x * 73856093 + y * 19349663 + side * 83492791 + dungeonSeed;
    if (hash < 0) hash = -hash;
    return dungeonAssets[poolName][hash % dungeonAssets[poolName].len()];
}

function walkable(x, y) {
    if (x < 0 || y < 0 || x >= DUNGEON_W || y >= DUNGEON_H) return false;
    local cell = dungeonGrid.getCell(x, y);
    return cell == 2 || cell == 3;
}

function clearDungeon() {
    foreach (instance in dungeonInstances) instance.setVisible(false);
    foreach (light in dungeonLights) light.setEnabled(false);
    dungeonInstances.clear();
    dungeonLights.clear();
}

function rebuildDungeon() {
    clearDungeon();
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) throw paramsResult.status.summary;
    local p = paramsResult.value;
    p.setSeed(dungeonSeed);
    p.setSize(DUNGEON_W, DUNGEON_H);
    p.setInt("roomCount", 11);
    p.setInt("roomMin", 5);
    p.setInt("roomMax", 9);
    p.setInt("spacing", 1);
    p.setInt("corridorWidth", 2);
    p.setString("corridorStyle", "l");
    p.setString("connectionStyle", "nearest");
    p.setString("floorPattern", "cobble");
    p.setString("decorSet", "mixed");
    p.setFloat("decorDensity", 0.055);
    p.setFloat("propDensity", 0.62);
    configureDungeonAssetPack(p);
    local generated = procgen.generate("level.roguelike", p);
    if (!generated.ok) throw generated.status.summary;
    dungeonGrid = generated.value;

    local ox = -DUNGEON_W * DUNGEON_CELL * 0.5;
    local oz = -DUNGEON_H * DUNGEON_CELL * 0.5;
    local minWalkX = DUNGEON_W, minWalkY = DUNGEON_H;
    local maxWalkX = 0, maxWalkY = 0;
    for (local y = 0; y < DUNGEON_H; ++y) {
        for (local x = 0; x < DUNGEON_W; ++x) {
            if (walkable(x, y)) {
                if (x < minWalkX) minWalkX = x;
                if (x > maxWalkX) maxWalkX = x;
                if (y < minWalkY) minWalkY = y;
                if (y > maxWalkY) maxWalkY = y;
                local cx = ox + x * DUNGEON_CELL;
                local cz = oz + y * DUNGEON_CELL;
                // A neutral modular slab stays readable independently of the
                // external pack's material conventions. Packs can still map
                // every decorative role without renderer-specific coupling.
                addFloor(cx, cz, dungeonGrid.getDetail(x, y), floorTone(x, y));

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
                    addAsset(wallAsset(x, y, 0), cx, 0.0, cz - DUNGEON_CELL * 0.5, 0.0);
                if (!walkable(x, y + 1))
                    addAsset(wallAsset(x, y, 1), cx, 0.0, cz + DUNGEON_CELL * 0.5, 3.1415926);
                if (!walkable(x - 1, y))
                    addAsset(wallAsset(x, y, 2), cx - DUNGEON_CELL * 0.5, 0.0, cz, 1.5707963);
                if (!walkable(x + 1, y))
                    addAsset(wallAsset(x, y, 3), cx + DUNGEON_CELL * 0.5, 0.0, cz, 4.7123890);

                // Small modular corner caps hide straight-piece intersections.
                // Their semantic pool is replaceable, so other packs can use a
                // pillar, corner prefab, or leave the array empty.
                if (!walkable(x, y - 1) && !walkable(x - 1, y)) {
                    local id = architectureAsset("corners", x, y, 0);
                    if (id != "") addAsset(id, cx - DUNGEON_CELL * 0.5, 0.0,
                                           cz - DUNGEON_CELL * 0.5, 1.5707963);
                }
                if (!walkable(x, y - 1) && !walkable(x + 1, y)) {
                    local id = architectureAsset("corners", x, y, 1);
                    if (id != "") addAsset(id, cx + DUNGEON_CELL * 0.5, 0.0,
                                           cz - DUNGEON_CELL * 0.5, 0.0);
                }
                if (!walkable(x, y + 1) && !walkable(x + 1, y)) {
                    local id = architectureAsset("corners", x, y, 2);
                    if (id != "") addAsset(id, cx + DUNGEON_CELL * 0.5, 0.0,
                                           cz + DUNGEON_CELL * 0.5, 4.7123890);
                }
                if (!walkable(x, y + 1) && !walkable(x - 1, y)) {
                    local id = architectureAsset("corners", x, y, 3);
                    if (id != "") addAsset(id, cx - DUNGEON_CELL * 0.5, 0.0,
                                           cz + DUNGEON_CELL * 0.5, 3.1415926);
                }

                // A floor-to-corridor transition is an authored doorway rather
                // than an unframed hole. Emit from the room side only.
                if (dungeonGrid.getCell(x, y) == 2) {
                    local doorway = "";
                    if (y > 0 && dungeonGrid.getCell(x, y - 1) == 3) {
                        doorway = architectureAsset("doorways", x, y, 0);
                        if (doorway != "") addAsset(doorway, cx, 0.0, cz - DUNGEON_CELL * 0.5, 0.0);
                    }
                    if (y + 1 < DUNGEON_H && dungeonGrid.getCell(x, y + 1) == 3) {
                        doorway = architectureAsset("doorways", x, y, 1);
                        if (doorway != "") addAsset(doorway, cx, 0.0, cz + DUNGEON_CELL * 0.5, 3.1415926);
                    }
                    if (x > 0 && dungeonGrid.getCell(x - 1, y) == 3) {
                        doorway = architectureAsset("doorways", x, y, 2);
                        if (doorway != "") addAsset(doorway, cx - DUNGEON_CELL * 0.5, 0.0, cz, 1.5707963);
                    }
                    if (x + 1 < DUNGEON_W && dungeonGrid.getCell(x + 1, y) == 3) {
                        doorway = architectureAsset("doorways", x, y, 3);
                        if (doorway != "") addAsset(doorway, cx + DUNGEON_CELL * 0.5, 0.0, cz, 4.7123890);
                    }
                }
            }
        }
    }

    // Frame the occupied bounds, not the configured grid. The diagonal span
    // maps cleanly to an isometric camera and remains stable across seeds.
    local targetX = ox + (minWalkX + maxWalkX) * DUNGEON_CELL * 0.5;
    local targetZ = oz + (minWalkY + maxWalkY) * DUNGEON_CELL * 0.5;
    local diagonalCells = (maxWalkX - minWalkX + 1) + (maxWalkY - minWalkY + 1);
    dungeonCamera.setTarget(targetX, 0.0, targetZ);
    dungeonCamera.setEye(targetX + 100.0, 120.0, targetZ - 120.0);
    dungeonCamera.setOrthographic(diagonalCells * DUNGEON_CELL * 0.54);

    for (local i = 0; i < dungeonGrid.getObjectCount(); ++i) {
        local role = dungeonGrid.getObjectType(i);
        if (role == "room") continue;
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

        local propScale = 1.0;
        if (role == "table" || role == "bed" || role == "tavern") propScale = 1.30;
        else if (role == "seating" || role == "container" || role == "treasure" ||
                 role == "clutter") propScale = 1.18;
        addAsset(id, px, py, pz, yaw, propScale, propScale, propScale);
        if (role == "light" && dungeonLights.len() < 8) {
            local light = eve.Light3D();
            light.setType("point");
            light.setPosition(px, py + 0.35, pz);
            light.setColor(1.0, 0.55, 0.25, 3.2);
            light.setRadius(DUNGEON_CELL * 3.0);
            dungeonLights.append(light);
        }
    }
}

if (dungeonCamera == null) {
    dungeonCamera = eve.Camera3D();
    dungeonCamera.setEye(100.0, 120.0, -120.0);
    dungeonCamera.setTarget(0.0, 0.0, 0.0);
    dungeonCamera.setUp(0.0, 1.0, 0.0);
    dungeonCamera.setFov(58.0);
    dungeonCamera.setOrthographic(190.0);
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
