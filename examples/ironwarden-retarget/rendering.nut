// Studio lighting and the full engine render graph. Owned resources live in ward.
function wardRenderChecked(result, operation) {
    if (!result.ok) throw operation + " failed";
}

function wardStudioEnvironment() {
    local probe=gfx.newReflectionProbeCapture();
    probe.configure(0.0,1.0,0.0,64,0.1,30.0);
    local faces=[[0.78,0.73,0.66],[0.49,0.60,0.78],[0.93,0.95,1.0],
        [0.12,0.14,0.18],[0.68,0.73,0.82],[0.45,0.49,0.58]];
    foreach (i,c in faces) probe.setSkyFaceColor(i,c[0],c[1],c[2]);
    probe.setCaptureMask(0); probe.requestCapture();
    for (local attempt=0;attempt<24;++attempt) {
        if (probe.update(6)) break;
    }
    if (!probe.isCaptureComplete() || !probe.stageCapturedFaces() || !probe.filterAndPublish(64))
        throw "Studio reflection environment could not be published";
    local cube=probe.getActiveCubemap();
    if (cube==null) throw "Studio reflection environment is missing";
    ward.camera.setEnvMap(cube);
    ward.probe=probe;
}

function wardSetQuality(enabled) {
    ward.quality=enabled;
    local rc=ward.renderControl;
    foreach (feature in ["gbuffer","shadow","ao","gi","aa","msaa","ssr"]) {
        if (!rc.supports(feature)) throw "Required render feature is unavailable: "+feature;
        if (enabled) rc.enable(feature); else rc.disable(feature);
    }
    rc.setPostProcessQuality("high");
    ward.camera.setAmbient(enabled ? 0.16 : 0.10,enabled ? 0.18 : 0.11,enabled ? 0.22 : 0.13);
    ward.camera.setEnvIntensity(enabled ? 0.72 : 0.0);
    ward.camera.setExposure(-0.25);
    ward.camera.setBloom(enabled ? 0.10 : 0.0,1.20);
    wardRenderChecked(gfx.setSceneToneMapping(enabled ? "aces" : "none"),"Scene tone mapping");
    wardRenderChecked(gfx.setScenePhotographicVignette(enabled ? 0.12 : 0.0),"Photographic vignette");
    rc.compile();
    if (ward.uiBuilt) {
        ui.select("ward");
        ui.setText("quality",enabled ? "Quality: HIGH" : "Quality: BASELINE");
        ui.setText("rendering",enabled ? "8x MSAA requested + FXAA high | CSM shadows | AO + GI + IBL | SSR | ACES + bloom + vignette"
            : "Baseline comparison: antialiasing, shadows, AO, IBL, reflections and post effects disabled");
    }
}

function wardStageBox(position,scale,color,metallic,roughness,castShadow) {
    local part=eve.Renderable3D(); part.setMesh(gfx.newMeshCube(1.0));
    part.setPosition(position[0],position[1],position[2]); part.setScale(scale[0],scale[1],scale[2]);
    part.setTint(color[0],color[1],color[2],1.0); part.setMetallic(metallic); part.setRoughness(roughness);
    part.setReceiveLight(true); part.setReceiveShadow(true); part.setCastShadow(castShadow);
    ward.stage.push(part);
}

function wardInitRendering() {
    ward.renderControl=gfx.getRenderControl();
    if (!ward.renderControl.setLightingMode("forwardPlus")) throw "Forward+ lighting unavailable";
    ward.renderControl.enable("forward"); ward.renderControl.enable("depthTest");
    // Animated rigid armour stays sharp without a temporal history trailing behind it.
    ward.renderControl.disable("taa");
    gfx.setMsaaSamples(8);
    gfx.setShadowSchemeDirectionalEnabled(true);
    gfx.setShadowSchemeSpotEnabled(true);
    gfx.setShadowSchemeMaxSpotCasters(2);
    wardRenderChecked(gfx.setBloomFilter("karisTent",0.65,5,8.0),"Bloom filter");
    ward.camera.setClipPlanes(0.08,25.0);
    ward.camera.setAutoExposure(false,-4.0,2.0);
    wardStudioEnvironment();
    local key=eve.Light3D(); key.setType("dir"); key.setDirection(-0.55,0.95,0.65);
    key.setColor(1.0,0.94,0.85,1.05); key.setCastShadow(true); key.setShadowMethod("csm");
    key.setShadowStrength(0.88); key.setShadowBias(0.0004); ward.lights.push(key);
    gfx.setDirectionalLight(-0.55,0.95,0.65,1.05,0.99,0.89);
    local fill=eve.Light3D(); fill.setType("point"); fill.setPosition(3.4,3.0,3.2);
    fill.setColor(0.62,0.76,1.0,0.75); fill.setRadius(8.0); ward.lights.push(fill);
    local rim=eve.Light3D(); rim.setType("point"); rim.setPosition(0.0,3.6,-2.0);
    rim.setColor(0.64,0.78,1.0,1.25); rim.setRadius(6.0); ward.lights.push(rim);
    wardStageBox([0.0,-0.18,0.0],[30.0,0.1,25.0],[0.055,0.07,0.095],0.22,0.48,false);
    wardStageBox([0.0,2.7,-3.1],[40.0,5.7,0.12],[0.035,0.045,0.065],0.05,0.82,false);
    foreach (x in [-1.2,1.1])
        wardStageBox([x,-0.07,0.0],[1.9,0.12,1.9],[0.18,0.21,0.26],0.36,0.42,true);
    gfx.setBackgroundColor(0.055,0.07,0.095,1.0);
    wardSetQuality(true);
    print("IRONWARDEN_RENDER_HIGH full graph / MSAA requested="+gfx.getMsaaSamples()+
        " / FXAA high / CSM / AO / GI / studio IBL / SSR / ACES / bloom / vignette\n");
}
