// Interactive spline tube authoring. Meshes rebuild only after edits.
pipeObject <- null;
pipeCamera <- null;
pipePoints <- [[-3.0,-1.5,0.0],[-1.0,1.3,0.0],[1.0,-0.5,1.0],[3.0,1.5,0.0]];
pipeScales <- [1.0,1.0,1.0,1.0];
pipeSelected <- 0;
pipeRadius <- 0.3;
pipeSegments <- 64;
pipeSides <- 20;
pipeClosed <- false;
pipeCap <- true;
pipeYaw <- 0.0;
pipeDirty <- true;
pipeDelay <- 0.0;

function tubeRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function tubePath(points, closed) {
    local path = tubeRequire(procgen.newSplinePath(), "new spline path").value;
    tubeRequire(path.setKind("catmullRom"), "set spline kind");
    path.setClosed(closed);
    foreach (point in points)
        tubeRequire(path.addPoint(point[0], point[1], point[2], 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), "add point");
    return path;
}

function tubeBuild(path, radius, pathSegments, radialSegments, cap) {
    local graph = tubeRequire(procgen.newMeshModifierGraph(), "new modifier graph").value;
    tubeRequire(graph.addNode("tube", "mesh.splineTube"), "add tube generator");
    tubeRequire(graph.addNode("out", "mesh.output"), "add output");
    tubeRequire(graph.setNodeSplinePath("tube", path), "bind spline path");
    tubeRequire(graph.setNodeFloat("tube", "radius", radius), "set radius");
    tubeRequire(graph.setNodeInt("tube", "pathSegments", pathSegments), "set path segments");
    tubeRequire(graph.setNodeInt("tube", "radialSegments", radialSegments), "set radial segments");
    tubeRequire(graph.setNodeInt("tube", "cap", cap), "set cap");
    tubeRequire(graph.connect("tube", "out", 0), "connect output");
    return tubeRequire(graph.executeResult("out"), "execute tube graph").value;
}


function pipeStatus(message) {
    ui.select("pipe");
    ui.setText("status", message);
}

function rebuildPipe() {
    try {
        local path = tubePath(pipePoints, pipeClosed);
        foreach (i, scale in pipeScales)
            tubeRequire(path.setPointProfile(i, 0.0, scale, scale), "point radius");
        local mesh = tubeBuild(path, pipeRadius, pipeSegments, pipeSides, pipeCap ? 1 : 0);
        local uploaded = tubeRequire(procgen.uploadMesh(mesh, gfx), "upload pipe").value;
        // Publish only after the entire build and upload succeeded.
        pipeObject.setMesh(uploaded);
        pipeStatus("Ready / " + pipePoints.len() + " points / " + pipeSegments + " x " + pipeSides);
        print("PIPE_GENERATOR_BUILT points=" + pipePoints.len() + " radius=" + pipeRadius +
              " closed=" + pipeClosed + " cap=" + pipeCap + "\n");
    } catch (error) {
        pipeStatus("Build rejected: " + error);
        print("PIPE_GENERATOR_REJECTED " + error + "\n");
    }
    pipeDirty = false;
}

function syncPointPanel() {
    ui.select("pipe");
    ui.setText("point", "Control point " + (pipeSelected + 1) + " / " + pipePoints.len());
    ui.setValue("x", pipePoints[pipeSelected][0]);
    ui.setValue("y", pipePoints[pipeSelected][1]);
    ui.setValue("z", pipePoints[pipeSelected][2]);
    ui.setValue("scale", pipeScales[pipeSelected]);
}

function buildPipePanel() {
    ui.setTheme("dark");
    ui.setScale(0.7);
    ui.beginBuild();
    ui.beginWindow("PIPE GENERATOR", "root");
    ui.text("Geometry", "geometry");
    ui.slider("Radius", pipeRadius, 0.05, 1.0, "radius");
    ui.slider("Segments", pipeSegments.tofloat(), 8.0, 192.0, "segments");
    ui.slider("Sides", pipeSides.tofloat(), 3.0, 48.0, "sides");
    ui.button(pipeClosed ? "Path: closed" : "Path: open", "closed");
    ui.button(pipeCap ? "End caps: on" : "End caps: off", "cap");
    ui.text("", "point");
    ui.button("Next control point", "next");
    ui.slider("X", 0.0, -5.0, 5.0, "x");
    ui.slider("Y", 0.0, -5.0, 5.0, "y");
    ui.slider("Z", 0.0, -5.0, 5.0, "z");
    ui.slider("Local scale", 1.0, 0.2, 2.0, "scale");
    ui.beginRow("points", 8.0);
    ui.button("Insert point", "insert");
    ui.button("Remove point", "remove");
    ui.end();
    ui.slider("View angle", pipeYaw, -180.0, 180.0, "yaw");
    ui.button("Reset path", "reset");
    ui.text("Closed paths ignore end caps. Radius is in scene units.", "hint");
    ui.text("Generating...", "status");
    ui.end();
    ui.mountBuildAs("pipe");
    ui.select("pipe");
    ui.setHostOverlay(false);
    ui.setHostMovable(false);
    ui.setHostResizable(false);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
    ui.setHostSize(220.0, 360.0);
    syncPointPanel();
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.04, 0.065, 1.0);
    gfx.setDirectionalLight(-0.5, -0.8, -0.4, 1.6, 1.5, 1.3);
    pipeCamera = eve.Camera3D();
    pipeCamera.setEye(0.0, 3.0, -16.0);
    pipeCamera.setTarget(1.8, 0.0, 0.0);
    pipeCamera.setUp(0.0, 1.0, 0.0);
    pipeCamera.setFov(48.0);
    pipeCamera.setAmbient(0.3, 0.35, 0.4);
    pipeCamera.setActive(true);
    pipeObject = eve.Renderable3D();
    pipeObject.setTint(0.16, 0.72, 0.85, 1.0);
    pipeObject.setRoughness(0.32);
    pipeObject.setMetallic(0.35);
    pipeObject.setYaw(pipeYaw);
    buildPipePanel();
    rebuildPipe();
};

eve_update = function(dt) {
    ui.select("pipe");
    local click = ui.consumeClick();
    while (click != "") {
        local edited = true;
        if (click == "pipe/next") {
            pipeSelected = (pipeSelected + 1) % pipePoints.len();
            edited = false;
        } else if (click == "pipe/closed") {
            pipeClosed = !pipeClosed;
            ui.setText("closed", pipeClosed ? "Path: closed" : "Path: open");
        } else if (click == "pipe/cap") {
            pipeCap = !pipeCap;
            ui.setText("cap", pipeCap ? "End caps: on" : "End caps: off");
        } else if (click == "pipe/insert" && pipePoints.len() < 16) {
            local a = pipePoints[pipeSelected];
            local b = pipePoints[(pipeSelected + 1) % pipePoints.len()];
            pipePoints.insert(pipeSelected + 1, [(a[0]+b[0])*0.5,(a[1]+b[1])*0.5,(a[2]+b[2])*0.5]);
            pipeScales.insert(pipeSelected + 1, (pipeScales[pipeSelected]+pipeScales[(pipeSelected+1)%pipeScales.len()])*0.5);
            pipeSelected += 1;
        } else if (click == "pipe/remove" && pipePoints.len() > 3) {
            pipePoints.remove(pipeSelected);
            pipeScales.remove(pipeSelected);
            pipeSelected = pipeSelected % pipePoints.len();
        } else if (click == "pipe/reset") {
            pipePoints = [[-3.0,-1.5,0.0],[-1.0,1.3,0.0],[1.0,-0.5,1.0],[3.0,1.5,0.0]];
            pipeScales = [1.0,1.0,1.0,1.0];
            pipeSelected = 0;
        } else edited = false;
        syncPointPanel();
        if (edited) { pipeDirty = true; pipeDelay = 0.08; }
        click = ui.consumeClick();
    }
    local changed = ui.consumeChange();
    while (changed != "") {
        local edited = true;
        if (changed == "pipe/radius") pipeRadius = ui.getValue("radius");
        else if (changed == "pipe/segments") pipeSegments = ui.getValue("segments").tointeger();
        else if (changed == "pipe/sides") pipeSides = ui.getValue("sides").tointeger();
        else if (changed == "pipe/scale") pipeScales[pipeSelected] = ui.getValue("scale");
        else if (changed == "pipe/x") pipePoints[pipeSelected][0] = ui.getValue("x");
        else if (changed == "pipe/y") pipePoints[pipeSelected][1] = ui.getValue("y");
        else if (changed == "pipe/z") pipePoints[pipeSelected][2] = ui.getValue("z");
        else {
            edited = false;
            if (changed == "pipe/yaw") {
                pipeYaw = ui.getValue("yaw");
                pipeObject.setYaw(pipeYaw);
            }
        }
        if (edited) { pipeDirty = true; pipeDelay = 0.08; }
        changed = ui.consumeChange();
    }
    pipeDelay -= dt;
    if (pipeDirty && pipeDelay <= 0.0) rebuildPipe();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
