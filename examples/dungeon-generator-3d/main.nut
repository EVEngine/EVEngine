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
persist dungeonFloorMeshes = []

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

function floorMaterial(tone) {
    local key = tone[0].tostring() + ":" + tone[1].tostring() + ":" + tone[2].tostring();
    if (!(key in dungeonFloorMaterials)) {
        local material = gfx.newMaterial();
        material.setTint(tone[0] * 1.08, tone[1] * 1.08, tone[2] * 1.08, 1.0);
        material.setRoughness(0.98);
        dungeonFloorMaterials[key] <- material;
    }
    return dungeonFloorMaterials[key];
}

function queueFloor(groups, x, z, variant, tone) {
    local shade = (((variant * 7 + (x * 0.25).tointeger() * 3 +
                     (z * 0.25).tointeger() * 5) % 5) - 2) * 0.012;
    local variedTone = [tone[0] + shade, tone[1] + shade, tone[2] + shade];
    local key = variedTone[0].tostring() + ":" + variedTone[1].tostring() +
                ":" + variedTone[2].tostring();
    if (!(key in groups)) groups[key] <- { tone=variedTone, cells=[] };
    groups[key].cells.append([x, z, variant]);
}

function buildMergedFloor(group) {
    local pos = [], nrm = [], uv = [], idx = [];
    local points = [[-0.76,-0.98], [0.76,-0.98], [0.98,-0.76], [0.98,0.76],
                    [0.76,0.98], [-0.76,0.98], [-0.98,0.76], [-0.98,-0.76]];
    foreach (cell in group.cells) {
        for (local sy = 0; sy < 2; ++sy) {
            for (local sx = 0; sx < 2; ++sx) {
                local cx = cell[0] + (sx == 0 ? -1.0 : 1.0);
                local cz = cell[1] + (sy == 0 ? -1.0 : 1.0);
                local py = -0.035 + ((cell[2] + sx * 3 + sy * 5) % 3) * 0.008;
                local vertexBase = pos.len() / 3;
                pos.push(cx); pos.push(py); pos.push(cz);
                nrm.push(0.0); nrm.push(1.0); nrm.push(0.0);
                uv.push(0.5); uv.push(0.5);
                foreach (point in points) {
                    pos.push(cx + point[0]); pos.push(py); pos.push(cz + point[1]);
                    nrm.push(0.0); nrm.push(1.0); nrm.push(0.0);
                    uv.push((point[0] + 1.0) * 0.5); uv.push((point[1] + 1.0) * 0.5);
                }
                for (local edge = 0; edge < 8; ++edge) {
                    idx.push(vertexBase);
                    idx.push(vertexBase + 1 + edge);
                    idx.push(vertexBase + 1 + ((edge + 1) % 8));
                }
            }
        }
    }
    local mesh = gfx.newMeshFromArrays(pos, nrm, uv, pos.len() / 3, idx, idx.len());
    dungeonFloorMeshes.append(mesh);
    local floor = eve.Renderable3D();
    floor.setMesh(mesh);
    floor.setMaterial(floorMaterial(group.tone));
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
    local hash = x * 73856093 + y * 19349663 + side * 83492791 + dungeonSeed;
    if (hash < 0) hash = -hash;
    local straight = false;
    if (side == 0 || side == 1) {
        local outsideY = y + (side == 0 ? -1 : 1);
        straight = walkable(x - 1, y) && walkable(x + 1, y) &&
                   !walkable(x - 1, outsideY) && !walkable(x + 1, outsideY);
    } else {
        local outsideX = x + (side == 2 ? -1 : 1);
        straight = walkable(x, y - 1) && walkable(x, y + 1) &&
                   !walkable(outsideX, y - 1) && !walkable(outsideX, y + 1);
    }
    local rates = dungeonAssets.wallVariantRates;
    local roll = hash % 100;
    local cursor = 0;
    if (straight) {
        cursor += rates.window;
        if (roll < cursor && dungeonAssets.windows.len() > 0)
            return dungeonAssets.windows[hash % dungeonAssets.windows.len()];
    }
    cursor += rates.broken;
    if (roll < cursor && dungeonAssets.brokenWalls.len() > 0)
        return dungeonAssets.brokenWalls[hash % dungeonAssets.brokenWalls.len()];
    if (straight) {
        cursor += rates.scaffold;
        if (roll < cursor && dungeonAssets.scaffoldWalls.len() > 0)
            return dungeonAssets.scaffoldWalls[hash % dungeonAssets.scaffoldWalls.len()];
        cursor += rates.gated;
        if (roll < cursor && dungeonAssets.gatedWalls.len() > 0)
            return dungeonAssets.gatedWalls[hash % dungeonAssets.gatedWalls.len()];
    }
    if (!("walls" in dungeonAssets) || dungeonAssets.walls.len() == 0)
        return dungeonAssets.wall;
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

function openingKey(x, y, side) {
    return x.tostring() + ":" + y.tostring() + ":" + side.tostring();
}

function clearDungeon() {
    foreach (instance in dungeonInstances) instance.setVisible(false);
    foreach (light in dungeonLights) light.setEnabled(false);
    dungeonInstances.clear();
    dungeonLights.clear();
    dungeonFloorMeshes.clear();
}

function rebuildDungeon() {
    clearDungeon();
    local paramsResult = procgen.newParams();
    if (!paramsResult.ok) throw paramsResult.status.summary;
    local p = paramsResult.value;
    p.setSeed(dungeonSeed);
    p.setSize(DUNGEON_W, DUNGEON_H);
    p.setInt("roomCount", 12);
    p.setInt("roomMin", 4);
    p.setInt("roomMax", 7);
    p.setInt("spacing", 0);
    p.setInt("clusterGapMin", 1);
    p.setInt("clusterGapMax", 1);
    p.setInt("clusterBranchBias", 2);
    p.setInt("corridorWidth", 1);
    p.setInt("stairCount", 2);
    p.setInt("stairSideMask", 9); // north + east, facing this isometric view
    p.setString("layoutStyle", "clustered");
    p.setString("corridorStyle", "l");
    p.setString("connectionStyle", "growth");
    p.setString("floorPattern", "cobble");
    p.setString("decorSet", "mixed");
    p.setFloat("decorDensity", 0.055);
    p.setFloat("propDensity", 0.78);
    p.setFloat("corridorLightDensity", 0.08);
    configureDungeonAssetPack(p);
    local generated = procgen.generate("level.roguelike", p);
    if (!generated.ok) throw generated.status.summary;
    dungeonGrid = generated.value;

    local stairOpenings = {};
    for (local i = 0; i < dungeonGrid.getObjectCount(); ++i) {
        if (dungeonGrid.getObjectType(i) != "stairs") continue;
        local rotation = dungeonGrid.getObjectRotation(i);
        local side = rotation == 180.0 ? 0 : (rotation == 0.0 ? 1 :
                     (rotation == 270.0 ? 2 : 3));
        stairOpenings[openingKey(dungeonGrid.getObjectX(i).tointeger(),
                                 dungeonGrid.getObjectY(i).tointeger(), side)] <- true;
    }

    local ox = -DUNGEON_W * DUNGEON_CELL * 0.5;
    local oz = -DUNGEON_H * DUNGEON_CELL * 0.5;
    local floorGroups = {};
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
                local tone = floorTone(x, y);
                if (("usePackFloors" in dungeonAssets) && dungeonAssets.usePackFloors)
                    addFloor(cx, cz, dungeonGrid.getDetail(x, y), tone);
                else
                    queueFloor(floorGroups, cx, cz, dungeonGrid.getDetail(x, y), tone);

                local detail = dungeonGrid.getDetail(x, y);
                if (detail >= 100 && dungeonAssets.details.len() > 0) {
                    local detailId = dungeonAssets.details[(detail - 100) % dungeonAssets.details.len()];
                    local detailYaw = ((x * 17 + y * 31 + dungeonSeed) % 4) * 1.5707963;
                    addAsset(detailId, cx, 0.06, cz, detailYaw);
                }

                // KayKit wall pieces are four units long and half a unit thick.
                // Place them on the walkable cell edges instead of at solid-cell
                // centres so corners and corridors form a continuous perimeter.
                if (!walkable(x, y - 1) && !(openingKey(x, y, 0) in stairOpenings))
                    addAsset(wallAsset(x, y, 0), cx, 0.0, cz - DUNGEON_CELL * 0.5, 0.0);
                if (!walkable(x, y + 1) && !(openingKey(x, y, 1) in stairOpenings))
                    addAsset(wallAsset(x, y, 1), cx, 0.0, cz + DUNGEON_CELL * 0.5, 3.1415926);
                if (!walkable(x - 1, y) && !(openingKey(x, y, 2) in stairOpenings))
                    addAsset(wallAsset(x, y, 2), cx - DUNGEON_CELL * 0.5, 0.0, cz, 1.5707963);
                if (!walkable(x + 1, y) && !(openingKey(x, y, 3) in stairOpenings))
                    addAsset(wallAsset(x, y, 3), cx + DUNGEON_CELL * 0.5, 0.0, cz, 4.7123890);

                // Small modular corner caps hide straight-piece intersections.
                // Their semantic pool is replaceable, so other packs can use a
                // pillar, corner prefab, or leave the array empty.
                if (!walkable(x, y - 1) && !walkable(x - 1, y) &&
                    !(openingKey(x, y, 0) in stairOpenings) &&
                    !(openingKey(x, y, 2) in stairOpenings)) {
                    local id = architectureAsset("corners", x, y, 0);
                    if (id != "") addAsset(id, cx - DUNGEON_CELL * 0.5, 0.0,
                                           cz - DUNGEON_CELL * 0.5, 1.5707963);
                }
                if (!walkable(x, y - 1) && !walkable(x + 1, y) &&
                    !(openingKey(x, y, 0) in stairOpenings) &&
                    !(openingKey(x, y, 3) in stairOpenings)) {
                    local id = architectureAsset("corners", x, y, 1);
                    if (id != "") addAsset(id, cx + DUNGEON_CELL * 0.5, 0.0,
                                           cz - DUNGEON_CELL * 0.5, 0.0);
                }
                if (!walkable(x, y + 1) && !walkable(x + 1, y) &&
                    !(openingKey(x, y, 1) in stairOpenings) &&
                    !(openingKey(x, y, 3) in stairOpenings)) {
                    local id = architectureAsset("corners", x, y, 2);
                    if (id != "") addAsset(id, cx + DUNGEON_CELL * 0.5, 0.0,
                                           cz + DUNGEON_CELL * 0.5, 4.7123890);
                }
                if (!walkable(x, y + 1) && !walkable(x - 1, y) &&
                    !(openingKey(x, y, 1) in stairOpenings) &&
                    !(openingKey(x, y, 2) in stairOpenings)) {
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

    foreach (group in floorGroups) buildMergedFloor(group);

    // Frame the occupied bounds, not the configured grid. The diagonal span
    // maps cleanly to an isometric camera and remains stable across seeds.
    local targetX = ox + (minWalkX + maxWalkX) * DUNGEON_CELL * 0.5;
    local targetZ = oz + (minWalkY + maxWalkY) * DUNGEON_CELL * 0.5;
    local diagonalCells = (maxWalkX - minWalkX + 1) + (maxWalkY - minWalkY + 1);
    dungeonCamera.setTarget(targetX, 0.0, targetZ);
    dungeonCamera.setEye(targetX + 100.0, 120.0, targetZ - 120.0);
    dungeonCamera.setOrthographic(diagonalCells * DUNGEON_CELL * 0.49);

    local lightCandidates = [];
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

        if (role == "stairs" && (flags & 64) != 0) {
            local edge = DUNGEON_CELL * 0.5;
            if (rotationDegrees == 0.0) pz += edge;
            else if (rotationDegrees == 180.0) pz -= edge;
            else if (rotationDegrees == 90.0) px += edge;
            else px -= edge;
            py = dungeonAssets.stairsVerticalOffset;
        }

        local anchorRole = role;
        if ((flags & 2) != 0 && role == "light") anchorRole = "wallLight";
        else if ((flags & 2) != 0 && role == "shelf") anchorRole = "wallShelf";
        if (("verticalOffsets" in dungeonAssets) &&
            (anchorRole in dungeonAssets.verticalOffsets))
            py = dungeonAssets.verticalOffsets[anchorRole];

        local propScale = 1.0;
        if (("roleScales" in dungeonAssets) && (role in dungeonAssets.roleScales))
            propScale = dungeonAssets.roleScales[role];
        addAsset(id, px, py, pz, yaw, propScale, propScale, propScale);
        if (role == "light") lightCandidates.append([px, py + 0.35, pz]);
    }
    local lightCount = lightCandidates.len() < 8 ? lightCandidates.len() : 8;
    for (local i = 0; i < lightCount; ++i) {
        local candidate = lightCandidates[((i * lightCandidates.len()) / lightCount).tointeger()];
        local light = eve.Light3D();
        light.setType("point");
        light.setPosition(candidate[0], candidate[1], candidate[2]);
        light.setColor(1.0, 0.62, 0.34, 2.45);
        light.setRadius(DUNGEON_CELL * 3.6);
        dungeonLights.append(light);
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
    dungeonCamera.setAmbient(0.76, 0.78, 0.84);
    dungeonCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.25, 2.05, 1.92, 1.76);
}
if (dungeonGrid == null) rebuildDungeon();
gfx.setBackgroundColor(0.045, 0.018, 0.085, 1.0);

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
