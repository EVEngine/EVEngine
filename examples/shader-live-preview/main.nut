persist previewShader = null
persist previewCamera = null
persist previewModels = []
persist previewTime = 0.0
persist reloadMessage = "edit shaders/preview.frag and save"

function addPreviewModel(mesh, x, y, z, sx, sy, sz, r, g, b, roughness) {
    local model = eve.Renderable3D();
    model.setMesh(mesh);
    model.setShader(previewShader);
    model.setPosition(x, y, z);
    model.setScale(sx, sy, sz);
    model.setTint(r, g, b, 1.0);
    model.setRoughness(roughness);
    model.setCastShadow(true);
    model.setReceiveShadow(true);
    previewModels.push(model);
}

function buildPreviewScene() {
    previewCamera = eve.Camera3D();
    previewCamera.setEye(0.0, 2.8, 8.5);
    previewCamera.setTarget(0.0, 0.35, 0.0);
    previewCamera.setUp(0.0, 1.0, 0.0);
    previewCamera.setFov(45.0);
    previewCamera.setAmbient(0.12, 0.14, 0.19);
    previewCamera.setActive(true);
    gfx.setDirectionalLight(-0.45, 0.85, 0.35, 1.5, 1.35, 1.2);

    addPreviewModel(gfx.newMeshSphere(48, 24), -2.35, 0.25, 0.0,
                    1.25, 1.25, 1.25, 0.35, 0.72, 1.0, 0.2);
    addPreviewModel(gfx.newMeshCube(1.0), 0.0, 0.2, 0.0,
                    1.7, 1.7, 1.7, 1.0, 0.46, 0.28, 0.5);
    addPreviewModel(gfx.newMeshCylinder(48, 1, true), 2.35, 0.15, 0.0,
                    1.15, 2.2, 1.15, 0.62, 0.95, 0.42, 0.8);
    addPreviewModel(gfx.newMeshCube(1.0), 0.0, -1.35, 0.0,
                    7.2, 0.18, 3.6, 0.24, 0.27, 0.34, 0.95);
}

function reloadPreviewShader() {
    local source = fs.readText("shaders/preview.frag");
    local result = gfx.replaceShaderFromGlsl(previewShader, "", source);
    if (result.ok) {
        reloadMessage = "shader reloaded";
        print("[shader-preview] reload applied\n");
    } else {
        reloadMessage = "compile failed; showing last good shader";
        local detail = result.status;
        if (result.diagnostics.len() > 0) detail = result.diagnostics[0].message;
        print("[shader-preview] " + detail + "\n");
    }
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.03, 0.055, 1.0);
    if (previewShader == null) {
        previewShader = gfx.newMeshShader(fs.readText("shaders/preview.frag"));
        previewShader.declareFloat("time");
    }
    if (previewModels.len() == 0) buildPreviewScene();
};

eve_asset_reload <- function(path) {
    if (path.find("preview.frag") != null) reloadPreviewShader();
};

eve_update = function(dt) {
    previewTime += dt;
    previewShader.sendFloat("time", previewTime);
    for (local i = 0; i < 3; ++i)
        previewModels[i].setYaw(previewTime * (0.22 + i * 0.08));
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
};
