// Presentation helper for assets/models/type055 (metres, Y-up, +X bow).
// Simulation authority stays in eve.Warship(); this file only loads meshes/materials.

function warshipLoadType055() {
    if (!fs.mountExternalReadOnly("../../assets/models/type055", "type055"))
        throw "Warship example requires repository assets/models/type055";
    local data = model3d.newModelDataFromFile("type055/type055.obj");
    if (data == null || data.getMeshCount() == 0)
        throw "Type 055 model contains no meshes";
    local paint = {
        hull = [0.18, 0.23, 0.27],
        fittings = [0.28, 0.33, 0.37],
        dark = [0.025, 0.045, 0.06],
        markings = [0.68, 0.70, 0.65]
    };
    local parts = [];
    for (local i = 0; i < data.getMeshCount(); ++i) {
        local r = model3d.createRenderable(gfx, data, i);
        if (r == null) throw "Type 055 mesh creation failed";
        local name = data.getMaterialName(data.getMaterialIndex(i));
        if (!(name in paint)) throw "Unexpected Type 055 material: " + name;
        local color = paint[name];
        local material = r.getMaterial();
        if (material == null) throw "Type 055 material missing";
        material.setTint(color[0], color[1], color[2], 1.0);
        material.setRoughness(0.85);
        material.setMetallic(0.05);
        material.setCastShadow(true);
        material.setReceiveShadow(true);
        r.setCastShadow(true);
        r.setReceiveShadow(true);
        parts.push(r);
    }
    return { data = data, parts = parts };
}
