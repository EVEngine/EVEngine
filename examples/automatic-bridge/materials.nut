// Material selection is driven by the CPU plan's semantic @surface suffix.
persist bridgeMaterials = {}

function bridgeMaterial(name) {
    if (name in bridgeMaterials) return bridgeMaterials[name];
    local material = gfx.newMaterial();
    local recipe = "pbr.cement";
    if (name == "timber") recipe = "pbr.wood";
    if (name == "asphalt") recipe = "pbr.asphalt";
    if (name == "paintedSteel" || name == "galvanizedSteel") recipe = "pbr.cable.steel";
    if (name == "water") recipe = "pbr.water";
    if (name == "gravel") recipe = "pbr.rock";
    if (name == "whitePaint" || name == "yellowPaint") {
        if (name == "yellowPaint") material.setTint(0.96, 0.72, 0.13, 1.0);
        else material.setTint(0.9, 0.89, 0.83, 1.0);
        material.setRoughness(0.76);
    } else {
        local params = bridgeRequire(procgen.newParams(), "material parameters");
        bridgeRequire(params.setSeed(37), "material seed");
        bridgeRequire(params.setSize(256, 256), "material resolution");
        params.setInt("colors", 64);
        params.setFloat("scale", name == "water" ? 3.0 : 1.0);
        params.setFloat("normalStrength", name == "water" ? 5.0 : 0.8);
        local generated = bridgeRequire(procgen.generatePbrMaterial(recipe, params), "generate " + recipe);
        local albedo = generated.getAlbedo();
        if (name == "concrete" || name == "limestone" || name == "water" || name == "gravel") {
            local normal = generated.getNormal();
            local height = generated.getHeight();
            for (local y = 0; y < 256; ++y) for (local x = 0; x < 256; ++x) {
                local sample = albedo.getPixelR(x, y);
                local tone = 0.62 + (sample - 0.45) * 0.28;
                if (name == "limestone") {
                    local u = x.tofloat() / 256.0 * 3.0;
                    local v = y.tofloat() / 256.0 * 6.0;
                    local row = floor(v).tointeger();
                    u += row % 2 == 0 ? 0.0 : 0.5;
                    local fx = u - floor(u), fy = v - floor(v);
                    local seamX = fx < 0.018 || fx > 0.982;
                    local seamY = fy < 0.036 || fy > 0.964;
                    local seam = seamX || seamY;
                    tone = 0.64 + (sample - 0.45) * 0.22 - (seam ? 0.12 : 0.0);
                    local nx = seamX ? (fx < 0.5 ? -0.35 : 0.35) : 0.0;
                    local ny = seamY ? (fy < 0.5 ? -0.35 : 0.35) : 0.0;
                    local nz = sqrt(1.0 - nx * nx - ny * ny);
                    normal.setPixel(x, y, nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5, 1.0);
                    local h = seam ? 0.2 : 0.55 + sample * 0.12;
                    height.setPixel(x, y, h, h, h, 1.0);
                    albedo.setPixel(x, y, tone + 0.035, tone + 0.015, tone - 0.035, 1.0);
                } else if (name == "water") {
                    local shade = 0.37 + sample * 0.16;
                    albedo.setPixel(x, y, shade * 0.80, shade * 0.95, shade, 1.0);
                } else if (name == "gravel") {
                    local shade = 0.48 + sample * 0.25;
                    albedo.setPixel(x, y, shade, shade * 0.96, shade * 0.88, 1.0);
                } else {
                    local joint = x % 128 < 2 || y % 128 < 2;
                    if (joint) tone -= 0.035;
                    albedo.setPixel(x, y, tone, tone * 0.99, tone * 0.96, 1.0);
                }
            }
        }
        material.setAlbedoTexture(gfx.newTexture(albedo, true, true));
        material.setNormalTexture(gfx.newTexture(generated.getNormal(), true, true));
        material.setHeightTexture(gfx.newTexture(generated.getHeight(), true, true));
        material.setTint(1.0, 1.0, 1.0, 1.0);
        material.setRoughness(0.84);
        material.setMetallic(0.0);
        if (name == "asphalt") { material.setTint(0.28, 0.29, 0.30, 1.0); material.setRoughness(0.96); }
        if (name == "paintedSteel") { material.setTint(0.46, 0.55, 0.59, 1.0); material.setMetallic(0.62); material.setRoughness(0.43); }
        if (name == "galvanizedSteel") { material.setTint(0.72, 0.75, 0.76, 1.0); material.setMetallic(0.45); material.setRoughness(0.38); }
        if (name == "timber") { material.setTint(0.65, 0.60, 0.54, 1.0); material.setRoughness(0.92); }
        if (name == "water") { material.setTint(1.0, 1.0, 1.0, 1.0); material.setMetallic(0.35); material.setRoughness(0.28); }
        if (name == "gravel") { material.setTint(1.0, 1.0, 1.0, 1.0); material.setRoughness(0.98); }
        if (name == "limestone") material.setParallax(0.012, 6.0, 12.0);
        generated.destroy();
    }
    material.setReceiveLight(true);
    bridgeMaterials[name] <- material;
    return material;
}
