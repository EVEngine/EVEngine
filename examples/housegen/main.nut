// Visual-regression example for the original HouseGen showcase, backed by canonical procgen graphs.
persist housegen = null
persist layout = null
persist camera = null
persist cubeMesh = null
persist boxes = []
persist boxIdx = 0
persist retained = []

function keep(value) { retained.append(value); return value; }
function need(result, label) {
    if (!result.ok) throw "housegen: " + label + " failed: " + result.status.summary;
    return result.value;
}

function box(x, y, z, sx, sy, sz, r, g, b, roll) {
    local e;
    if (boxIdx < boxes.len()) { e = boxes[boxIdx]; e.setVisible(true); }
    else { e = eve.Renderable3D(); e.setMesh(cubeMesh); boxes.append(e); }
    boxIdx++;
    e.setPosition(x, y, z); e.setScale(sx, sy, sz); e.setTint(r, g, b, 1.0);
    e.setRotation(0.0, 0.0, roll);
}

function addWall(id, x, y, floor, rot, fh) {
    local outX = rot == 90 ? 1.0 : (rot == 270 ? -1.0 : 0.0);
    local outZ = rot == 180 ? 1.0 : (rot == 0 ? -1.0 : 0.0);
    local px = x.tofloat() + outX * 0.50, py = floor.tofloat() * fh;
    local pz = y.tofloat() + outZ * 0.50;
    local alongX = rot == 0 || rot == 180;
    local sx = alongX ? 1.02 : 0.12, sz = alongX ? 0.12 : 1.02;
    if (id != "wall") {
        box(px, py + fh * 0.5, pz, sx, fh, sz, 0.76, 0.58, 0.38, 0.0);
        return;
    }
    box(px, py + 0.40, pz, sx, 0.80, sz, 0.76, 0.58, 0.38, 0.0);
    box(px, py + 2.08, pz, sx, 0.64, sz, 0.76, 0.58, 0.38, 0.0);
    local gx = px + outX * 0.015, gz = pz + outZ * 0.015;
    box(gx, py + 1.28, gz, alongX ? 0.72 : 0.07, 0.92, alongX ? 0.07 : 0.72,
        0.18, 0.55, 0.72, 0.0);
    foreach (side in [-0.40, 0.40])
        box(gx + (alongX ? side : 0.0), py + 1.28, gz + (alongX ? 0.0 : side),
            alongX ? 0.07 : 0.08, 1.08, alongX ? 0.08 : 0.07, 0.82, 0.64, 0.42, 0.0);
    foreach (height in [0.76, 1.80])
        box(gx, py + height, gz, alongX ? 0.86 : 0.08, 0.07, alongX ? 0.08 : 0.86,
            0.82, 0.64, 0.42, 0.0);
}

function addDoor(x, y, floor, rot, fh) {
    local outX = rot == 90 ? 1.0 : (rot == 270 ? -1.0 : 0.0);
    local outZ = rot == 180 ? 1.0 : (rot == 0 ? -1.0 : 0.0);
    local px = x.tofloat() + outX * 0.50, py = floor.tofloat() * fh;
    local pz = y.tofloat() + outZ * 0.50;
    local alongX = rot == 0 || rot == 180;
    box(px, py + 0.92, pz, alongX ? 0.58 : 0.12, 1.84, alongX ? 0.12 : 0.58,
        0.16, 0.24, 0.20, 0.0);
    foreach (side in [-0.39, 0.39])
        box(px + (alongX ? side : 0.0), py + fh * 0.5, pz + (alongX ? 0.0 : side),
            alongX ? 0.15 : 0.16, fh, alongX ? 0.16 : 0.15, 0.82, 0.64, 0.42, 0.0);
    box(px, py + 2.25, pz, alongX ? 0.64 : 0.16, 0.30, alongX ? 0.16 : 0.64,
        0.82, 0.64, 0.42, 0.0);
    box(px + (alongX ? 0.20 : outX * 0.075), py + 0.95,
        pz + (alongX ? outZ * 0.075 : 0.20), 0.055, 0.055, 0.055, 0.92, 0.72, 0.20, 0.0);
    local porchX = px + outX * 0.55, porchZ = pz + outZ * 0.55;
    box(porchX, py + 0.04, porchZ, alongX ? 1.55 : 0.80, 0.10, alongX ? 0.80 : 1.55,
        0.38, 0.28, 0.19, 0.0);
    box(porchX, py + 2.48, porchZ, alongX ? 1.75 : 0.92, 0.10, alongX ? 0.92 : 1.75,
        0.42, 0.16, 0.13, 0.0);
    foreach (side in [-0.67, 0.67])
        box(porchX + (alongX ? side : 0.0), py + 1.24, porchZ + (alongX ? 0.0 : side),
            0.07, 2.42, 0.07, 0.82, 0.64, 0.42, 0.0);
}

function buildHouse() {
    boxIdx = 0;
    layout = housegen.newLayout();
    local request = housegen.newRequest();
    request.setSeed(20260815); request.setPlot(8, 7); request.setFloors(2);
    request.setModuleSize(1.0); request.setFloorHeight(3.0);
    request.setFootprint("rectangle"); request.setRoof("gable"); request.setEntrance("auto");
    request.setRequiredRooms("living,kitchen,bedroom");
    need(housegen.generate(request, layout), "layout generation");

    local footprint = keep(need(procgen.newGrid(1, 1), "footprint grid"));
    local points = keep(need(procgen.newPointSet(), "component points"));
    need(layout.writeFootprintGrid(footprint), "Grid2D export");
    need(layout.writeComponentPoints(points), "PointSet export");
    local gridGraph = keep(need(procgen.newGridGraph(), "grid graph"));
    need(gridGraph.addNode("footprint", "grid.input"), "grid input node");
    need(gridGraph.addNode("occupied", "convert.grid_to_points"), "grid conversion node");
    need(gridGraph.connect("footprint", "occupied", 0), "grid graph edge");
    need(gridGraph.setNodeGrid("footprint", footprint), "grid graph input");
    local occupied = keep(need(gridGraph.execute("occupied"), "grid graph execution"));
    local pointGraph = keep(need(procgen.newPointGraph(), "point graph"));
    if (!pointGraph.addNode("components", "input") || !pointGraph.addNode("display", "transform"))
        throw "housegen: point graph node failed";
    if (!pointGraph.connect("components", "display", 0) ||
        !pointGraph.setNodePoints("components", points) || !pointGraph.setNodeFloat("display", "y", 0.15))
        throw "housegen: point graph setup failed";
    local displayed = keep(need(pointGraph.executeResult("display"), "point graph execution"));

    local fh = layout.getFloorHeight();
    for (local i = 0; i < layout.getInstanceCount(); ++i) {
        local id = layout.getInstanceComponentId(i);
        local x = layout.getInstanceX(i), y = layout.getInstanceY(i);
        local floor = layout.getInstanceZ(i), rot = layout.getInstanceRotationDeg(i);
        local wy = floor.tofloat() * fh;
        if (id == "foundation")
            box(x.tofloat(), -0.08, y.tofloat(), 0.96, 0.16, 0.96, 0.35, 0.34, 0.33, 0.0);
        else if (id == "floor")
            box(x.tofloat(), wy - 0.03, y.tofloat(), 0.96, 0.06, 0.96, 0.48, 0.33, 0.22, 0.0);
        else if (id == "wall" || id == "wall.block") addWall(id, x, y, floor, rot, fh);
        else if (id == "door") addDoor(x, y, floor, rot, fh);
        else if (id == "iwall") {
            local alongX = rot == 0 || rot == 180;
            box(x.tofloat(), wy + fh * 0.5, y.tofloat(), alongX ? 0.12 : 0.96, fh,
                alongX ? 0.96 : 0.12, 0.57, 0.44, 0.30, 0.0);
        } else if (id == "idoor")
            box(x.tofloat(), wy + fh * 0.35, y.tofloat(), 0.55, fh * 0.7, 0.55, 0.40, 0.30, 0.24, 0.0);
        else if (id == "stairs")
            box(x.tofloat(), wy + fh * 0.5, y.tofloat(), 0.70, fh, 0.70, 0.90, 0.82, 0.62, 0.0);
    }

    local roofWidth = 8.0, roofDepth = 7.0, halfRun = roofWidth * 0.5 + 0.30;
    local rise = roofWidth * 0.22, angle = atan2(rise, halfRun);
    local panelLength = sqrt(halfRun * halfRun + rise * rise), eaveY = 2.0 * fh;
    box(3.5 - halfRun * 0.5, eaveY + rise * 0.5, 3.0, panelLength, 0.18, roofDepth + 0.65,
        0.42, 0.16, 0.13, angle);
    box(3.5 + halfRun * 0.5, eaveY + rise * 0.5, 3.0, panelLength, 0.18, roofDepth + 0.65,
        0.42, 0.16, 0.13, -angle);
    foreach (z in [-0.5, 6.5])
        box(3.5, eaveY - 0.02, z, roofWidth, rise * 0.5, 0.12, 0.76, 0.58, 0.38, 0.0);
    box(3.5, -0.30, 3.0, 12.0, 0.25, 11.0, 0.20, 0.42, 0.18, 0.0);
    for (local i = boxIdx; i < boxes.len(); ++i) boxes[i].setVisible(false);
    camera.setTarget(3.5, 3.2, 3.0); camera.setEye(14.5, 10.5, -13.0);
    print("HOUSEGEN_VISUAL_BASELINE seed=20260815 grid=" + footprint.getWidth() + "x" +
          footprint.getHeight() + " occupied=" + occupied.getCount() +
          " components=" + displayed.getCount() + " boxes=" + boxIdx + "\n");
}

eve_init = function() {
    housegen = eve.HouseGen();
    need(housegen.loadComponentsFromFile("components.json"), "component library");
    cubeMesh = gfx.newMeshCube(1.0);
    gfx.setBackgroundColor(0.58, 0.75, 0.90, 1.0);
    gfx.setDirectionalLight(-0.7, -1.0, -0.45, 1.2, 1.1, 1.0);
    camera = keep(eve.Camera3D());
    camera.setUp(0.0, 1.0, 0.0); camera.setFov(45.0);
    camera.setAmbient(0.34, 0.34, 0.38); camera.setActive(true);
    buildHouse();
};

eve_render = function() { gfx.clear(); gfx.render3D(); };
