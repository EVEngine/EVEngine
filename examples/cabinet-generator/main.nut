// Configuration is kept here so dimensions and counts can be edited together.
persist cabinetParts = []
persist cabinetCamera = null
persist cabinetStyle = 0
persist cabinetDoors = 2
persist cabinetDrawers = 2
persist cabinetLevels = 3
persist cabinetLegs = true
persist cabinetKeys = {}
persist cabinetYaw = 0.0
persist cabinetTextures = {}
persist cabinetProbe = null
persist cabinetGround = null
persist cabinetKeyLight = null
persist cabinetFillLight = null

function cabinetStudioEnvironment(camera) {
    local probe = gfx.newReflectionProbeCapture();
    probe.configure(0.0, 1.0, 0.0, 32, 0.1, 20.0);
    probe.setSkyFaceColor(0, 0.86, 0.71, 0.55);
    probe.setSkyFaceColor(1, 0.47, 0.59, 0.82);
    probe.setSkyFaceColor(2, 0.96, 0.96, 0.98);
    probe.setSkyFaceColor(3, 0.12, 0.13, 0.15);
    probe.setSkyFaceColor(4, 0.78, 0.82, 0.90);
    probe.setSkyFaceColor(5, 0.71, 0.63, 0.55);
    probe.setCaptureMask(0);
    probe.requestCapture();
    local attempts = 0;
    while (!probe.isCaptureComplete() && attempts < 24) { probe.update(6); attempts += 1; }
    if (!probe.isCaptureComplete()) throw "Cabinet studio environment capture incomplete";
    if (!probe.stageCapturedFaces()) throw "Cabinet studio environment staging failed";
    if (!probe.filterAndPublish(32)) throw "Cabinet studio environment filtering failed";
    local cubemap = probe.getActiveCubemap();
    if (cubemap == null) throw "Cabinet studio environment cubemap missing";
    camera.setEnvMap(cubemap);
    camera.setEnvIntensity(0.85);
    return probe;
}

// One shared texture pair per tile; style changes only replace per-part factors.
function cabinetLoadTile(name) {
    if (!(name in cabinetTextures)) {
        local texturePath = "materials/" + name;
        cabinetTextures[name] <- {
            albedo = gfx.newTextureFromFileRepeated(texturePath + "_albedo.png", true, true),
            packed = gfx.newTextureFromFileRepeated(texturePath + "_packed.png", true, true)
        };
    }
    return cabinetTextures[name];
}

// All roles are explicit and editable. Metallic is a material property, not a style-wide value.
function cabinetStyleMaterials(index) {
    local oak = { tile="wood", tint=[0.55,0.34,0.16], metallic=0.0 };
    local lightWood = { tile="wood", tint=[0.83,0.64,0.40], metallic=0.0 };
    local darkWood = { tile="wood", tint=[0.22,0.075,0.03], metallic=0.0 };
    local brass = { tile="brushed", tint=[0.90,0.65,0.26], metallic=1.0 };
    local bronze = { tile="brushed", tint=[0.52,0.32,0.12], metallic=1.0 };
    local steel = { tile="brushed", tint=[0.78,0.81,0.84], metallic=1.0 };
    local ivory = { tile="lacquer", tint=[0.92,0.90,0.84], metallic=0.0 };
    local bluePaint = { tile="lacquer", tint=[0.19,0.29,0.34], metallic=0.0 };
    local presets = [
        { exterior=oak, interior=lightWood, trim=brass, hardware=brass, legs=oak },
        { exterior=darkWood, interior=lightWood, trim=darkWood, hardware=bronze, legs=darkWood },
        { exterior=ivory, interior=lightWood, trim=ivory, hardware=steel, legs=lightWood },
        { exterior=bluePaint, interior=lightWood, trim=bluePaint, hardware=steel, legs=steel }
    ];
    return presets[index];
}

function cabinetPressed(key) {
    local down = keyboard.isDown(key);
    local was = key in cabinetKeys ? cabinetKeys[key] : false;
    cabinetKeys[key] <- down;
    return down && !was;
}

function rebuildCabinet() {
    local pr = procgen.newParams();
    if (!pr.ok) { print(pr.status.summary); return; }
    local p = pr.value;
    p.setFloat("cabinetWidth", 1.8);
    p.setFloat("cabinetHeight", 2.0);
    p.setFloat("depth", 0.55);
    p.setInt("columns", 2);
    p.setInt("doors", cabinetDoors);
    p.setInt("drawers", cabinetDrawers);
    p.setInt("levels", cabinetLevels);
    p.setBool("legs", cabinetLegs);
    local styles = ["european", "chinese", "minimal", "modern"];
    p.setString("style", styles[cabinetStyle]);
    local result = procgen.buildMesh("mesh.cabinet", p);
    if (!result.ok) { print("cabinet generation failed: " + result.status.summary); return; }
    local build = result.value;
    // Upload a complete replacement before publishing the new renderable list.
    local next = [];
    local finishes = cabinetStyleMaterials(cabinetStyle);
    for (local i=0; i<build.getGroupCount(); ++i) {
        local component = build.copyGroup(i);
        if (component == null) continue;
        local upload = procgen.uploadMesh(component, gfx);
        if (!upload.ok) { print(upload.status.summary); return; }
        local name = build.getGroupName(i);
        local role = build.getMeta("material." + name, "");
        if (!(role in finishes)) throw "Missing cabinet material role: " + role;
        local finish = finishes[role];
        // Fail before publishing when an asset is missing; preserve the previous cabinet.
        local tile = cabinetLoadTile(finish.tile);
        next.append({ mesh=upload.value, finish=finish, tile=tile });
    }
    foreach (part in cabinetParts) part.setVisible(false);
    for (local i=0; i<next.len(); ++i) {
        if (i >= cabinetParts.len()) cabinetParts.append(eve.Renderable3D());
        local part = cabinetParts[i];
        local item = next[i];
        part.setMesh(item.mesh);
        part.setTexture(item.tile.albedo);
        part.setNormalTexture(item.tile.packed);
        part.setPackedNormalMask(true);
        part.setMetallic(item.finish.metallic);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        local tint = item.finish.tint;
        part.setVisible(true);
        part.setTint(tint[0],tint[1],tint[2],1.0);
        // Packed alpha supplies smoothness (1 - roughness); no height displacement.
        part.setRoughness(1.0);
        part.setYaw(cabinetYaw);
    }
    print("Cabinet: " + styles[cabinetStyle] + " doors=" + cabinetDoors + " drawers=" + cabinetDrawers + " levels=" + cabinetLevels);
}
if (cabinetCamera == null) cabinetCamera = eve.Camera3D();
cabinetCamera.setActive(true);
cabinetCamera.setEye(3.2,2.7,4.2);
cabinetCamera.setTarget(0.0,1.0,0.0);
cabinetCamera.setUp(0.0,1.0,0.0);
cabinetCamera.setFov(40.0);
cabinetCamera.setAmbient(0.07,0.08,0.10);
if (cabinetProbe == null) cabinetProbe = cabinetStudioEnvironment(cabinetCamera);
if (cabinetGround == null) {
    cabinetGround = eve.Renderable3D();
    cabinetGround.setMesh(gfx.newMeshCube(1.0));
    cabinetGround.setPosition(0.0,-0.03,0.0);
    cabinetGround.setScale(20.0,0.06,20.0);
    cabinetGround.setTint(0.045,0.052,0.065,1.0);
    cabinetGround.setRoughness(0.85);
    cabinetGround.setCastShadow(false);
    cabinetGround.setReceiveShadow(true);
}
if (cabinetKeyLight == null) {
    cabinetKeyLight = eve.Light3D();
    cabinetKeyLight.setType("dir");
    cabinetKeyLight.setDirection(-0.42,0.88,0.55);
    cabinetKeyLight.setColor(1.0,0.96,0.90,1.0);
    cabinetKeyLight.setCastShadow(true);
    cabinetKeyLight.setShadowStrength(0.75);
    cabinetKeyLight.setShadowBias(0.001);
    cabinetFillLight = eve.Light3D();
    cabinetFillLight.setType("point");
    cabinetFillLight.setPosition(-2.8,1.6,3.5);
    cabinetFillLight.setColor(0.75,0.85,1.0,0.5);
    cabinetFillLight.setRadius(12.0);
}
gfx.setBackgroundColor(0.025,0.035,0.05,1.0);
rebuildCabinet();

function eve_update(dt) {
    local dirty = false;
    if (cabinetPressed("1")) { cabinetStyle = 0; dirty = true; }
    if (cabinetPressed("2")) { cabinetStyle = 1; dirty = true; }
    if (cabinetPressed("3")) { cabinetStyle = 2; dirty = true; }
    if (cabinetPressed("4")) { cabinetStyle = 3; dirty = true; }
    if (cabinetPressed("d")) { cabinetDoors = (cabinetDoors+1)%5; dirty = true; }
    if (cabinetPressed("r")) { cabinetDrawers = (cabinetDrawers+1)%5; dirty = true; }
    if (cabinetPressed("s")) { cabinetLevels = cabinetLevels%5+1; dirty = true; }
    if (cabinetPressed("l")) { cabinetLegs = !cabinetLegs; dirty = true; }
    if (keyboard.isDown("a")) cabinetYaw -= dt;
    if (keyboard.isDown("f")) cabinetYaw += dt;
    if (dirty) rebuildCabinet();
    foreach (part in cabinetParts) part.setYaw(cabinetYaw);
}
function eve_render() { gfx.clear(); gfx.render3D(); }
