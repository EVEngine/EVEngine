// Spline Tube Lab — source-free modifier graph generation for open and closed paths.

persist tubeObjects = []
persist tubeLines = []
persist tubeCamera = null
persist tubeFrame = 0
persist tubeScreenshotSaved = false

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

function ribbonBuild(path, width, thickness, pathSegments) {
    local graph = tubeRequire(procgen.newMeshModifierGraph(), "new ribbon graph").value;
    tubeRequire(graph.addNode("ribbon", "mesh.splineRibbon"), "add ribbon generator");
    tubeRequire(graph.addNode("out", "mesh.output"), "add ribbon output");
    tubeRequire(graph.setNodeSplinePath("ribbon", path), "bind ribbon path");
    tubeRequire(graph.setNodeFloat("ribbon", "width", width), "set ribbon width");
    tubeRequire(graph.setNodeFloat("ribbon", "thickness", thickness), "set ribbon thickness");
    tubeRequire(graph.setNodeInt("ribbon", "pathSegments", pathSegments), "set ribbon path segments");
    tubeRequire(graph.connect("ribbon", "out", 0), "connect ribbon output");
    return tubeRequire(graph.executeResult("out"), "execute ribbon graph").value;
}

function profileBuild(path, coordinates, closed, pathSegments) {
    local graph = tubeRequire(procgen.newMeshModifierGraph(), "new profile graph").value;
    tubeRequire(graph.addNode("profile", "mesh.splineExtrude"), "add profile generator");
    tubeRequire(graph.addNode("out", "mesh.output"), "add profile output");
    tubeRequire(graph.setNodeSplinePath("profile", path), "bind profile path");
    tubeRequire(graph.setNodeSplineProfile("profile", coordinates, closed), "bind owning profile");
    tubeRequire(graph.setNodeInt("profile", "pathSegments", pathSegments), "set profile path segments");
    tubeRequire(graph.connect("profile", "out", 0), "connect profile output");
    return tubeRequire(graph.executeResult("out"), "execute profile graph").value;
}

function tubeAdd(mesh, x, tintR, tintG, tintB) {
    local uploaded = tubeRequire(procgen.uploadMesh(mesh, gfx), "upload tube").value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(x, -1.0, 0.0);
    object.setTint(tintR, tintG, tintB, 1.0);
    object.setRoughness(0.5);
    object.setMetallic(0.08);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    tubeObjects.append(object);
}

function tubeAddPathLine(path, xOffset, yOffset, r, g, b) {
    local polyline = tubeRequire(path.polylineResult(64, true, 32), "sample spline polyline").value;
    for (local chunk = 0; chunk < polyline.getChunkCount(); ++chunk) {
        local coordinates = [];
        for (local index = 0; index < polyline.getChunkPointCount(chunk); ++index) {
            local point = tubeRequire(polyline.getChunkPointResult(chunk, index), "read spline chunk point").value;
            coordinates.append(point.getX() + xOffset);
            coordinates.append(point.getY() + yOffset);
            coordinates.append(point.getZ());
        }
        local closeChunk = polyline.isClosed() && polyline.getChunkCount() == 1;
        tubeLines.append(tubeRequire(gfx.newPrimitivePolyline3D(coordinates, closeChunk, r, g, b, 1.0, 3.0),
                                     "create spline line renderer").value);
    }
}

if (tubeCamera == null) {
    tubeCamera = eve.Camera3D();
    tubeCamera.setEye(0.0, 5.0, -20.0);
    tubeCamera.setTarget(0.0, 1.0, 0.0);
    tubeCamera.setUp(0.0, 1.0, 0.0);
    tubeCamera.setFov(47.0);
    tubeCamera.setAmbient(0.22, 0.25, 0.32);
    tubeCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.4, 1.45, 1.35, 1.2);
    gfx.setBackgroundColor(0.035, 0.05, 0.085, 1.0);
}

if (tubeObjects.len() == 0) {
    local openPath = tubePath([[-1.0, 0.0, 0.0], [0.8, 1.2, 0.6], [-0.6, 2.7, 1.0], [1.0, 4.2, 0.0]], false);
    local loopPath = tubeRequire(procgen.newSplinePath(), "new preset spline").value;
    tubeRequire(loopPath.applyShapePreset("circle", 16, 2.0, 0.0, 1.0), "apply circle preset");
    local roadPath = tubePath([[-2.2, 0.0, 0.0], [-0.8, 0.7, 0.7], [0.9, 1.3, -0.4], [2.2, 2.1, 0.4]], false);
    tubeRequire(openPath.setPointProfile(1, -20.0, 1.35, 0.75), "profile open spline p1");
    tubeRequire(openPath.setPointProfile(2, 28.0, 0.7, 1.3), "profile open spline p2");
    tubeRequire(openPath.setPointRotation(1, 12.0, -8.0, -20.0), "orient open spline p1");
    tubeRequire(openPath.setPointRotation(2, -10.0, 14.0, 28.0), "orient open spline p2");
    tubeRequire(openPath.setPointChunkBreak(2, true), "split open spline chunks");
    tubeRequire(roadPath.setPointProfile(1, 18.0, 1.35, 1.0), "bank road p1");
    tubeRequire(roadPath.setPointProfile(2, -14.0, 0.72, 1.0), "bank road p2");
    local placements = tubeRequire(roadPath.distributeResult(6, true, 32), "distribute road samples").value;
    local travel = tubeRequire(roadPath.travelResult(2.4, "pingPong", 32), "travel road sample").value;
    local travelFrame = tubeRequire(roadPath.travelFrameResult(2.4, "pingPong", 32), "travel road frame").value;
    if (placements.getCount() != 6) throw "unexpected spline distribution count";
    tubeRequire(placements.getSampleResult(5), "read distributed endpoint");
    local placementFrame = tubeRequire(placements.getFrameResult(5), "read distributed frame").value;
    local channel = [-0.7, 0.65, -0.7, -0.35, 0.7, -0.35, 0.7, 0.65];
    tubeAdd(tubeBuild(openPath, 0.48, 48, 16, 1), -6.3, 0.25, 0.72, 0.98);
    tubeAdd(ribbonBuild(roadPath, 1.8, 0.28, 48), -2.1, 0.42, 0.88, 0.38);
    tubeAdd(profileBuild(openPath, channel, false, 48), 2.1, 0.74, 0.38, 0.96);
    tubeAddPathLine(openPath, 2.1, -1.0, 1.0, 0.86, 0.22);
    tubeAdd(tubeBuild(loopPath, 0.32, 64, 14, 1), 6.3, 0.98, 0.52, 0.22);
    print("SPLINE_TUBE_LAB_PASS variants=4 open=capped closed=seamless chunks=" + openPath.getChunkCount() +
          " ribbon=solid profile=banked-scaled custom=U-open " +
          "preset=circle distribution=" + placements.getCount() + " travelX=" + travel.getX() +
          " travelUpY=" + travelFrame.getUpY() + " placementSideX=" + placementFrame.getSideX() +
          " sourceMesh=none\n");
}

function eve_update(dt) {
    tubeFrame += 1;
    foreach (object in tubeObjects) object.setYaw(-12.0);
    if (!tubeScreenshotSaved && tubeFrame > 12 && gfx.saveFramePng("spline-tube-lab.png")) {
        tubeScreenshotSaved = true;
        print("spline-tube-lab: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
