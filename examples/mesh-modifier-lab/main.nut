// Mesh Modifier Lab — source mesh plus graph-compiled and interactive deformation variants.

persist modifierObjects = []
persist modifierCamera = null
persist modifierFrame = 0
persist modifierScreenshotSaved = false

function modifierRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function modifierSourceMesh() {
    local params = modifierRequire(procgen.newParams(), "new params").value;
    modifierRequire(procgen.applyMeshRecipeDefaults("prototype.cylinder1", params), "recipe defaults");
    params.setFloat("width", 2.2);
    params.setFloat("height", 4.0);
    params.setFloat("depth", 2.2);
    params.setInt("detail", 32);
    return modifierRequire(procgen.buildMesh("prototype.cylinder1", params), "source mesh").value;
}

function modifierGraph(source, nodes, links, parameters, outputNode) {
    local graph = modifierRequire(procgen.newMeshModifierGraph(), "new modifier graph").value;
    modifierRequire(graph.addNode("source", "mesh.input"), "add source");
    foreach (node in nodes) modifierRequire(graph.addNode(node[0], node[1]), "add " + node[0]);
    modifierRequire(graph.setNodeMesh("source", source), "bind source mesh");
    foreach (link in links) modifierRequire(graph.connect(link[0], link[1], link.len() > 2 ? link[2] : 0), "connect");
    foreach (parameter in parameters) {
        if (parameter[2] == "int")
            modifierRequire(graph.setNodeInt(parameter[0], parameter[1], parameter[3]), "set int");
        else if (parameter[2] == "string")
            modifierRequire(graph.setNodeString(parameter[0], parameter[1], parameter[3]), "set string");
        else
            modifierRequire(graph.setNodeFloat(parameter[0], parameter[1], parameter[3]), "set float");
    }
    local output = modifierRequire(graph.executeResult(outputNode), "execute " + outputNode);
    print("mesh-modifier-lab: " + outputNode + " segments=" + graph.getCompiledSegmentCount() +
          " fusedOps=" + graph.getFusedOperationCount() + "\n");
    return output.value;
}

function modifierAdd(mesh, x, tintR, tintG, tintB) {
    local uploaded = modifierRequire(procgen.uploadMesh(mesh, gfx), "upload mesh").value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(x, -2.0, 0.0);
    object.setTint(tintR, tintG, tintB, 1.0);
    object.setRoughness(0.72);
    object.setMetallic(0.04);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    modifierObjects.append(object);
}

function modifierColliderArrays(mesh) {
    local vertices = [], indices = [];
    for (local vertex = 0; vertex < mesh.getVertexCount(); ++vertex) {
        vertices.push(mesh.getPositionX(vertex));
        vertices.push(mesh.getPositionY(vertex));
        vertices.push(mesh.getPositionZ(vertex));
    }
    for (local index = 0; index < mesh.getIndexCount(); ++index) indices.push(mesh.getIndex(index));
    return { vertices = vertices, indices = indices };
}

function modifierBuildShowcase() {
    local source = modifierSourceMesh();
    modifierAdd(source, -6.0, 0.62, 0.67, 0.75);

    local twist = modifierGraph(source,
        [["bend", "deform.bend"], ["twist", "deform.twist"], ["out", "mesh.output"]],
        [["source", "bend"], ["bend", "twist"], ["twist", "out"]],
        [["bend", "axis", "string", "y"], ["bend", "angle", "float", 28.0],
         ["bend", "extent", "float", 4.0], ["twist", "axis", "string", "y"],
         ["twist", "angle", "float", 150.0], ["twist", "extent", "float", 4.0]], "out");
    modifierAdd(twist, -3.0, 0.28, 0.72, 0.95);

    local organic = modifierGraph(source,
        [["noise", "deform.noise"], ["sphere", "deform.spherify"], ["out", "mesh.output"]],
        [["source", "noise"], ["noise", "sphere"], ["sphere", "out"]],
        [["noise", "amplitude", "float", 0.28], ["noise", "frequency", "float", 3.2],
         ["noise", "seed", "int", 17], ["sphere", "radius", "float", 1.7],
         ["sphere", "y", "float", 2.0], ["sphere", "weight", "float", 0.68]], "out");
    modifierAdd(organic, 0.0, 0.87, 0.45, 0.25);

    local ffd = modifierGraph(source,
        [["ffd", "deform.ffd"], ["out", "mesh.output"]],
        [["source", "ffd"], ["ffd", "out"]],
        [["ffd", "p010x", "float", -0.9], ["ffd", "p011x", "float", -0.9],
         ["ffd", "p110x", "float", 0.9], ["ffd", "p111x", "float", 0.9]], "out");
    modifierAdd(ffd, 3.0, 0.46, 0.84, 0.43);

    local cut = modifierGraph(source,
        [["cut", "mesh.cutPlane"], ["out", "mesh.output"]],
        [["source", "cut"], ["cut", "out"]],
        [["cut", "normalX", "float", 0.0],
         ["cut", "normalY", "float", 1.0], ["cut", "distance", "float", 0.0],
         ["cut", "cap", "int", 1]], "out");
    modifierAdd(cut, 6.0, 0.82, 0.65, 0.25);

    local path = modifierRequire(procgen.newSplinePath(), "new spline path").value;
    modifierRequire(path.setKind("catmullRom"), "set spline kind");
    modifierRequire(path.addPoint(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), "spline p0");
    modifierRequire(path.addPoint(-0.5, 1.3, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), "spline p1");
    modifierRequire(path.addPoint(0.5, 2.7, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), "spline p2");
    modifierRequire(path.addPoint(1.3, 4.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0), "spline p3");
    local splineLength = modifierRequire(path.lengthResult(32), "spline length").value;
    local splineGraph = modifierRequire(procgen.newMeshModifierGraph(), "new spline graph").value;
    modifierRequire(splineGraph.addNode("source", "mesh.input"), "add spline source");
    modifierRequire(splineGraph.addNode("subdivide", "mesh.subdivide"), "add spline subdivision");
    modifierRequire(splineGraph.addNode("path", "deform.splinePath"), "add spline path node");
    modifierRequire(splineGraph.addNode("out", "mesh.output"), "add spline output");
    modifierRequire(splineGraph.setNodeMesh("source", source), "bind spline source");
    modifierRequire(splineGraph.setNodeInt("subdivide", "levels", 2), "set spline subdivision");
    modifierRequire(splineGraph.setNodeSplinePath("path", path), "bind spline path");
    modifierRequire(splineGraph.setNodeString("path", "axis", "y"), "set spline axis");
    modifierRequire(splineGraph.setNodeFloat("path", "scale", 0.55), "set spline cross-section scale");
    modifierRequire(splineGraph.connect("source", "subdivide", 0), "connect spline subdivision");
    modifierRequire(splineGraph.connect("subdivide", "path", 0), "connect spline path");
    modifierRequire(splineGraph.connect("path", "out", 0), "connect spline output");
    local spline = modifierRequire(splineGraph.executeResult("out"), "execute spline path").value;
    modifierAdd(spline, 9.0, 0.68, 0.48, 0.92);
    print("mesh-modifier-lab: splinePath length=" + splineLength + " segments=" + path.getSegmentCount() + "\n");

    local sculpt = modifierRequire(procgen.newMeshDeformationSession(), "new deformation session").value;
    modifierRequire(sculpt.initialize(source), "initialize deformation session");
    modifierRequire(sculpt.applyBrush("inflate", 0.0, 2.0, -1.0, 1.35, 0.85, 1.7), "inflate brush");
    modifierRequire(sculpt.applyDirectionalBrush(0.0, 1.0, 0.0, 1.15, 0.65, 1.4, 0.8, 0.2, 0.0),
                    "directional brush");
    modifierRequire(sculpt.applyImpact(0.0, 2.8, -1.0, 0.4, -1.8, 0.2, 1.1, 0.22, 1.6, 0.55),
                    "plastic impact");
    local sculpted = modifierRequire(sculpt.currentMeshResult(), "sculpt snapshot").value;
    modifierAdd(sculpted, 12.0, 0.82, 0.34, 0.58);

    local effector = modifierGraph(source,
        [["effector", "deform.effector"], ["out", "mesh.output"]],
        [["source", "effector"], ["effector", "out"]],
        [["effector", "pointCount", "int", 2], ["effector", "density", "float", 1.8],
         ["effector", "p0y", "float", 0.0], ["effector", "p0radius", "float", 3.0],
         ["effector", "p0dx", "float", -3.0], ["effector", "p0dy", "float", 0.4],
         ["effector", "p1y", "float", 4.0], ["effector", "p1radius", "float", 3.0],
         ["effector", "p1dx", "float", 3.0], ["effector", "p1dy", "float", -0.3]], "out");
    modifierAdd(effector, 15.0, 0.35, 0.82, 0.72);

    local surface = modifierRequire(procgen.newMeshDeformationSession(), "new interactive surface").value;
    modifierRequire(surface.initialize(source), "initialize interactive surface");
    modifierRequire(surface.applySurfaceContact(0.0, 2.0, -1.0, 0.0, 0.0, 1.0,
                                                1.2, 0.0, 0.0, 2.0, 1.1, 0.45, 1.4, 0.15),
                    "apply interactive surface contact");
    modifierRequire(surface.recoverSurface(0.12, 1.5), "advance interactive surface recovery");
    modifierRequire(surface.configureColliderRefresh("interval", 0.1, 0.0, -0.05, 0.0),
                    "configure collider refresh");
    if (modifierRequire(surface.updateColliderRefresh(0.04), "advance collider refresh a").value)
        throw "collider refresh fired before interval";
    if (!modifierRequire(surface.updateColliderRefresh(0.06), "advance collider refresh b").value)
        throw "collider refresh did not fire at interval";
    local colliderSnapshot = modifierRequire(surface.colliderMeshResult(), "build owning collider snapshot").value;
    local colliderGeometry = modifierColliderArrays(colliderSnapshot);
    local colliderWorld = physics.newWorld3D(0.0, 0.0, 0.0, false);
    local colliderBody = colliderWorld.newBody("static", 0.0, 0.0, 0.0);
    local oldCollider = colliderBody.newTriangleMeshShape(colliderGeometry.vertices, colliderGeometry.indices);
    colliderWorld.rayCast(0.0, 2.0, -5.0, 0.0, 2.0, 5.0);
    if (!colliderWorld.hasRayHit()) throw "initial deformation collider was not queryable";
    oldCollider.destroy();
    modifierRequire(surface.applyBrush("inflate", 0.0, 2.0, -1.0, 0.8, 0.25, 1.0),
                    "deform before collider replacement");
    modifierRequire(surface.configureColliderRefresh("manual", 0.0, 0.0, -0.05, 0.0),
                    "configure manual collider replacement");
    modifierRequire(surface.requestColliderRefresh(), "request collider replacement");
    if (!modifierRequire(surface.updateColliderRefresh(0.0), "consume manual collider refresh").value)
        throw "manual collider replacement was not scheduled";
    colliderSnapshot = modifierRequire(surface.colliderMeshResult(), "build replacement collider snapshot").value;
    colliderGeometry = modifierColliderArrays(colliderSnapshot);
    local newCollider = colliderBody.newTriangleMeshShape(colliderGeometry.vertices, colliderGeometry.indices);
    colliderWorld.rayCast(0.0, 2.0, -5.0, 0.0, 2.0, 5.0);
    if (!colliderWorld.hasRayHit()) throw "replacement deformation collider was not queryable";
    modifierAdd(modifierRequire(surface.currentMeshResult(), "interactive surface snapshot").value,
                18.0, 0.92, 0.48, 0.30);

    local slime = modifierRequire(procgen.newMeshDeformationSession(), "new mesh slime").value;
    modifierRequire(slime.initialize(source), "initialize mesh slime");
    modifierRequire(slime.applySlimeImpulse(0.0, 2.0, -1.0, 2.8, 0.8, -1.2, 3.0, 1.3),
                    "inject mesh slime impulse");
    for (local step = 0; step < 18; ++step)
        modifierRequire(slime.stepSlime(0.016, 9.0, 1.2, 4.0), "advance mesh slime");
    modifierAdd(modifierRequire(slime.currentMeshResult(), "mesh slime snapshot").value,
                21.0, 0.52, 0.76, 0.98);
    local fitGraph = modifierRequire(procgen.newMeshModifierGraph(), "new mesh fit graph").value;
    modifierRequire(fitGraph.addNode("source", "mesh.input"), "add mesh fit source");
    modifierRequire(fitGraph.addNode("surface", "mesh.input"), "add mesh fit surface");
    modifierRequire(fitGraph.addNode("moveSurface", "deform.transform"), "add surface transform");
    modifierRequire(fitGraph.addNode("fit", "deform.meshFit"), "add mesh fit");
    modifierRequire(fitGraph.setNodeMesh("source", source), "bind mesh fit source");
    modifierRequire(fitGraph.setNodeMesh("surface", source), "bind mesh fit surface");
    modifierRequire(fitGraph.setNodeFloat("moveSurface", "x", 1.4), "offset fit surface");
    modifierRequire(fitGraph.setNodeFloat("fit", "directionX", 1.0), "set fit direction x");
    modifierRequire(fitGraph.setNodeFloat("fit", "directionY", 0.0), "clear fit direction y");
    modifierRequire(fitGraph.setNodeInt("fit", "bidirectional", 1), "enable bidirectional fit");
    modifierRequire(fitGraph.connect("surface", "moveSurface", 0), "connect fit target transform");
    modifierRequire(fitGraph.connect("source", "fit", 0), "connect mesh fit source");
    modifierRequire(fitGraph.connect("moveSurface", "fit", 1), "connect mesh fit target");
    modifierAdd(modifierRequire(fitGraph.executeResult("fit"), "execute mesh fit").value,
                24.0, 0.70, 0.48, 0.92);
    local vertexEditor = modifierRequire(procgen.newMeshDeformationSession(), "new runtime vertex editor").value;
    modifierRequire(vertexEditor.initialize(source), "initialize runtime vertex editor");
    local selected = modifierRequire(vertexEditor.selectVerticesBox(-2.0, 1.8, -2.0, 2.0, 5.0, 2.0, true),
                                     "select upper vertices").value;
    if (selected <= 0 || vertexEditor.getSelectedVertexCount() != selected)
        throw "runtime vertex editor selection mismatch";
    modifierRequire(vertexEditor.moveSelectedVertices(0.55, 0.2, 0.0), "axis gizmo move");
    modifierRequire(vertexEditor.manipulateSelectedVertices("pull", 0.0, 1.8, -1.0,
                                                            0.0, 0.0, 0.0, 0.42),
                    "pull selected vertices");
    modifierAdd(modifierRequire(vertexEditor.currentMeshResult(), "runtime vertex editor snapshot").value,
                27.0, 0.96, 0.58, 0.20);
    local soundReact = modifierGraph(source,
        [["sound", "deform.soundReact"], ["out", "mesh.output"]],
        [["source", "sound"], ["sound", "out"]],
        [["sound", "level", "float", 0.9], ["sound", "threshold", "float", 0.2],
         ["sound", "strength", "float", 0.85], ["sound", "frequency", "float", 0.42],
         ["sound", "phase", "float", 1.1], ["sound", "axis", "string", "y"]], "out");
    modifierAdd(soundReact, 30.0, 0.86, 0.36, 0.82);
    print("MESH_MODIFIER_LAB_PASS variants=13 graph=typed fusion=enabled effector=two-point meshFit=surface-snapshot " +
          "surface=elastic-plastic slime=spring-damper colliderRefresh=replace-tested sculpt=undoable impact=bounded " +
          "vertexEditor=box-axis-pull soundReact=analyzed-level splinePath=arcLength cap=closed\n");
}

if (modifierCamera == null) {
    modifierCamera = eve.Camera3D();
    modifierCamera.setEye(12.0, 5.8, -43.0);
    modifierCamera.setTarget(12.0, 0.2, 0.0);
    modifierCamera.setUp(0.0, 1.0, 0.0);
    modifierCamera.setFov(48.0);
    modifierCamera.setAmbient(0.24, 0.27, 0.32);
    modifierCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, -1.0, -0.35, 1.35, 1.25, 1.10);
    gfx.setBackgroundColor(0.055, 0.07, 0.105, 1.0);
}
if (modifierObjects.len() == 0) modifierBuildShowcase();

function eve_update(dt) {
    modifierFrame += 1;
    foreach (object in modifierObjects) object.setYaw(8.0);
    if (!modifierScreenshotSaved && modifierFrame > 12 && gfx.saveFramePng("mesh-modifier-lab.png")) {
        modifierScreenshotSaved = true;
        print("mesh-modifier-lab: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
