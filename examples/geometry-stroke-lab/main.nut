// Runtime geometry painting parity: quad, triangular prism, and cube trails.

persist strokeObjects = []
persist strokeCamera = null
persist strokeFrame = 0
persist strokeScreenshotSaved = false

function strokeRequire(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result;
}

function strokeBuild(shape, inputSpace, planeY, points, width, depth) {
    local stroke = eve.ProcgenGeometryStroke();
    strokeRequire(stroke.setShape(shape), "set stroke shape");
    strokeRequire(stroke.setInputSpace(inputSpace, planeY), "set stroke input space");
    strokeRequire(stroke.setSize(width, depth), "set stroke size");
    strokeRequire(stroke.setMinimumSpacing(0.08), "set stroke spacing");
    foreach (point in points)
        strokeRequire(stroke.addPoint(point[0], point[1], point[2]), "append stroke point");
    // Prove reversible runtime editing before producing the final mesh.
    strokeRequire(stroke.addPoint(points.top()[0] + 0.5, points.top()[1], points.top()[2]), "append undo point");
    strokeRequire(stroke.undo(), "undo stroke point");
    if (stroke.getPointCount() != points.len()) throw "stroke undo did not restore point count";
    return strokeRequire(stroke.buildMeshResult(), "build stroke mesh").value;
}

function strokeAdd(mesh, x, z, r, g, b) {
    local uploaded = strokeRequire(procgen.uploadMesh(mesh, gfx), "upload stroke mesh").value;
    local object = eve.Renderable3D();
    object.setMesh(uploaded);
    object.setPosition(x, 0.0, z);
    object.setTint(r, g, b, 1.0);
    object.setRoughness(0.38);
    object.setMetallic(0.08);
    object.setCastShadow(true);
    object.setReceiveShadow(true);
    strokeObjects.append(object);
}

if (strokeCamera == null) {
    strokeCamera = eve.Camera3D();
    strokeCamera.setEye(0.0, 5.0, -18.0);
    strokeCamera.setTarget(0.0, 1.3, 0.0);
    strokeCamera.setUp(0.0, 1.0, 0.0);
    strokeCamera.setFov(46.0);
    strokeCamera.setAmbient(0.25, 0.28, 0.34);
    strokeCamera.setActive(true);
    gfx.setDirectionalLight(-0.5, -1.0, -0.35, 1.35, 1.28, 1.15);
    gfx.setBackgroundColor(0.025, 0.038, 0.065, 1.0);
}

if (strokeObjects.len() == 0) {
    local trail = [[-1.7, 0.0, 0.0], [-0.8, 0.8, 0.3], [0.1, 0.3, 0.7], [0.9, 1.4, 0.2], [1.7, 0.6, 0.0]];
    strokeAdd(strokeBuild("quad", "planar", 0.0, trail, 0.65, 0.2), -5.0, 0.0, 0.18, 0.72, 1.0);
    strokeAdd(strokeBuild("triangularPrism", "spatial", 0.0, trail, 0.68, 0.56), 0.0, 0.0, 0.88, 0.38, 0.72);
    strokeAdd(strokeBuild("cube", "spatial", 0.0, trail, 0.62, 0.62), 5.0, 0.0, 1.0, 0.58, 0.16);

    local planeParams = strokeRequire(procgen.newParams(), "new plane params").value;
    planeParams.setFloat("width", 4.0);
    planeParams.setFloat("depth", 2.2);
    planeParams.setInt("segmentsX", 18);
    planeParams.setInt("segmentsZ", 8);
    planeParams.setFloat("curvature", 1.0);
    local plane = strokeRequire(procgen.buildMesh("mesh.extendedPlane", planeParams), "build extended plane").value;
    strokeAdd(plane, -2.7, 3.4, 0.28, 0.9, 0.5);

    local hexParams = strokeRequire(procgen.newParams(), "new hex params").value;
    hexParams.setInt("columns", 5);
    hexParams.setInt("rows", 4);
    hexParams.setFloat("radius", 0.35);
    hexParams.setFloat("height", 0.1);
    hexParams.setFloat("randomHeight", 0.7);
    local hex = strokeRequire(procgen.buildMesh("mesh.hexGrid", hexParams), "build hex grid").value;
    strokeAdd(hex, 2.4, 3.4, 0.6, 0.42, 1.0);
    print("GEOMETRY_STROKE_PASS shapes=quad,triangularPrism,cube input=planar,spatial undo=verified " +
          "extendedPlane=curved hexGrid=randomHeight gpuUpload=verified\n");
}

function eve_update(dt) {
    strokeFrame += 1;
    foreach (object in strokeObjects) object.setYaw(-10.0);
    if (!strokeScreenshotSaved && strokeFrame > 12 && gfx.saveFramePng("geometry-stroke-lab.png")) {
        strokeScreenshotSaved = true;
        print("geometry-stroke-lab: screenshot saved\n");
    }
}

function eve_render() { gfx.clear(); gfx.render3D(); }
