// Custom Mesh3D fragment shader; bounded analytic optics, no hardware RT requirement.
persist labCamera = null
persist labScreen = null
persist labShader = null
persist labIor = 1.52
persist labAbsorption = 1.0
persist labDispersion = 0.006
persist labExposure = 1.0
persist labYaw = 0.0
persist labRoughness = 0.06
persist labAbsorptionDistance = 3.0
persist labLightAngle = 0.0
persist labLightSize = 1.0
persist labKey = 9.0
persist labFill = 6.0
persist labTop = 5.0
persist labBack = 1.2
persist labEnvironment = 1.0
persist labAbsorptionColor = [0.82, 0.94, 0.98]

function updateLab() {
    labShader.sendFloat("ior", labIor);
    labShader.sendFloat("absorption", labAbsorption);
    labShader.sendFloat("dispersion", labDispersion);
    labShader.sendFloat("roughness", labRoughness);
    labShader.sendFloat("attenuationDistance", labAbsorptionDistance);
    labShader.sendFloat("keyIntensity", labKey);
    labShader.sendFloat("fillIntensity", labFill);
    labShader.sendFloat("topIntensity", labTop);
    labShader.sendFloat("lightSize", labLightSize);
    labShader.sendFloat("lightAngle", labLightAngle);
    labShader.sendFloat("environmentIntensity", labEnvironment);
    labShader.sendFloat("backIntensity", labBack);
    labShader.sendFloat("attenuationR", labAbsorptionColor[0]);
    labShader.sendFloat("attenuationG", labAbsorptionColor[1]);
    labShader.sendFloat("attenuationB", labAbsorptionColor[2]);
    labCamera.setExposure(labExposure);
    labCamera.setEye(9.8*sin(labYaw), 1.1, 9.8*cos(labYaw));
    labCamera.setTarget(0.0, 0.0, 0.0);
}

eve_init = function() {
    if (labShader == null) {
        local loaded = gfx.loadMeshShaderSpv("", "shaders/dielectric.frag.spv");
        if (!loaded.ok) throw "glass-crystal: " + loaded.status.summary;
        labShader = loaded.value;
        foreach (name in ["ior", "absorption", "dispersion",
                          "roughness", "attenuationDistance", "keyIntensity", "fillIntensity",
                          "topIntensity", "lightSize", "lightAngle",
                          "environmentIntensity", "backIntensity", "attenuationR", "attenuationG", "attenuationB"]) labShader.declareFloat(name);
        // Oversized ray-generation plane behind all analytic objects.
        local mesh = gfx.newMeshFromArrays(
            [-40.0,-30.0,-6.0, 40.0,-30.0,-6.0, 40.0,30.0,-6.0, -40.0,30.0,-6.0],
            [0.0,0.0,1.0, 0.0,0.0,1.0, 0.0,0.0,1.0, 0.0,0.0,1.0],
            [0.0,0.0, 1.0,0.0, 1.0,1.0, 0.0,1.0], 4, [0,1,2,0,2,3], 6);
        labScreen = eve.Renderable3D();
        labScreen.setMesh(mesh);
        labScreen.setShader(labShader);
        labScreen.setCastShadow(false);
        labScreen.setReceiveShadow(false);
        labCamera = eve.Camera3D();
        labCamera.setFov(42.0);
        labCamera.setActive(true);
    }
    local tone = gfx.setSceneToneMapping("aces");
    if (!tone.ok) throw "glass-crystal: tone mapping: " + tone.status.summary;
    updateLab();
    print("Dielectric Lab: crystal cube | crystal octahedron | crystal sphere (shared material)\n");
    print("1 glass IOR | 2 crystal IOR | 3 water IOR | D dispersion | C absorption | A/S orbit | Q/E lights | F roughness | Z/X exposure | V light size | R reset\n");
};

eve_update = function(dt) {
    if (key_just_pressed("1")) labIor = 1.52;
    if (key_just_pressed("2")) labIor = 2.42;
    if (key_just_pressed("3")) labIor = 1.333;
    if (key_just_pressed("d")) labDispersion = labDispersion > 0.0 ? 0.0 : 0.006;
    if (key_just_pressed("c")) labAbsorption = labAbsorption > 0.0 ? 0.0 : 2.0;
    if (key_just_pressed("a")) labYaw = max(-0.3, labYaw-0.05);
    if (key_just_pressed("s")) labYaw = min(0.3, labYaw+0.05);
    if (key_just_pressed("q")) labLightAngle -= 0.15;
    if (key_just_pressed("e")) labLightAngle += 0.15;
    if (key_just_pressed("f")) labRoughness = labRoughness < 0.1 ? 0.22 : 0.06;
    if (key_just_pressed("z")) labExposure = max(0.25, labExposure-0.25);
    if (key_just_pressed("x")) labExposure = min(3.0, labExposure+0.25);
    if (key_just_pressed("v")) labLightSize = labLightSize < 1.1 ? 1.5 : 1.0;
    if (key_just_pressed("r")) {
        labIor=1.52; labAbsorption=1.0; labDispersion=0.006; labYaw=0.0;
        labRoughness=0.06; labExposure=1.0; labLightAngle=0.0; labLightSize=1.0;
    }
    updateLab();
};
eve_render = function() { gfx.clear(); gfx.render3D(); };
