// mesh_data.nut is generated from the current native road baker, not an SDK recipe.
dofile("mesh_data.nut");
persist parityMesh = null;
persist parityCamera = null;
persist parityMaterial = null;
persist paritySourceMesh = null;

function eve_init() {
    local renderControl = gfx.getRenderControl();
    renderControl.disable("gbuffer");
    renderControl.disable("gpuDriven");
    renderControl.compile();
    if (renderControl.hasPass("gbuffer") || renderControl.isEnabled("gpuDriven"))
        throw "road parity forward calibration was not applied";
    local mesh = gfx.newMeshFromArrays(parity_data.positions, parity_data.normals, parity_data.uvs,
                                      parity_data.positions.len() / 3, parity_data.indices, parity_data.indices.len());
    if (mesh == null) throw "road parity mesh upload failed";
    paritySourceMesh = mesh;
    local material = gfx.newMaterial();
    parityMaterial = material; // Renderable3D borrows this native pointer across frames.
    material.setTint(1.0, 0.0, 1.0, 1.0);
    material.setShadingModel("unlit");
    material.setReceiveLight(false);
    material.setDoubleSided(false); // Match the EasyRoads reference's Unlit/Color back-face culling.
    parityMesh = eve.Renderable3D();
    parityMesh.setMesh(mesh);
    parityMesh.setMaterial(material);
    parityMesh.setCastShadow(false);
    parityMesh.setReceiveShadow(false);
    parityCamera = eve.Camera3D();
    parityCamera.setEye(parity_view.eye[0].tofloat(), parity_view.eye[1].tofloat(), parity_view.eye[2].tofloat());
    parityCamera.setTarget(parity_view.target[0].tofloat(), parity_view.target[1].tofloat(), parity_view.target[2].tofloat());
    parityCamera.setUp(parity_view.up[0].tofloat(), parity_view.up[1].tofloat(), parity_view.up[2].tofloat());
    parityCamera.setFov(parity_view.fov.tofloat());
    parityCamera.setClipPlanes(0.1, 250.0);
    parityCamera.setActive(true);
    gfx.setBackgroundColor(0.125, 0.125, 0.125, 1.0);
    print("ROAD_PARITY_READY\n");
}
function eve_update(dt) {}
function eve_render() { gfx.clear(); gfx.render3D(); }
