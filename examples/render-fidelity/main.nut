// Render fidelity lab: toggle P0 / P1 / P2 quality options on one stage.
// Keys 1-0 toggle features; F1-F4 apply presets; R resets camera.
// fidelityCapture=true cycles presets and writes comparison PNGs.

if (!("rf" in getroottable())) rf <- {};

function rfChecked(result, operation) {
    if (typeof result == "table" && ("ok" in result) && !result.ok)
        throw operation + " failed: " + (result.error != null ? result.error : result.status);
}

function rfMakeBox(x, y, z, sx, sy, sz, r, g, b, metallic, roughness, castShadow) {
    local o = eve.Renderable3D();
    o.setMesh(gfx.newMeshCube(1.0));
    o.setPosition(x, y, z);
    o.setScale(sx, sy, sz);
    o.setTint(r, g, b, 1.0);
    o.setMetallic(metallic);
    o.setRoughness(roughness);
    o.setCastShadow(castShadow);
    o.setReceiveShadow(true);
    rf.objects.append(o);
    return o;
}

function rfMakeSphere(x, y, z, scale, r, g, b, metallic, roughness, castShadow) {
    local o = eve.Renderable3D();
    // Dense tessellation so chrome IBL / SSR wrap without faceted normals.
    o.setMesh(gfx.newMeshSphere(64, 40));
    o.setPosition(x, y, z);
    o.setScale(scale, scale, scale);
    o.setTint(r, g, b, 1.0);
    o.setMetallic(metallic);
    o.setRoughness(roughness);
    o.setCastShadow(castShadow);
    o.setReceiveShadow(true);
    rf.objects.append(o);
    return o;
}

function rfMakeLayer(r, g, b) {
    local image = eve.Image().newEmptyImageData(64, 64, "RGBA8");
    for (local y = 0; y < 64; ++y) {
        for (local x = 0; x < 64; ++x) {
            local checker = ((x / 8 + y / 8) % 2 == 0) ? 0.05 : -0.05;
            image.setPixel(x, y, r + checker, g + checker, b + checker, 1.0);
        }
    }
    return image;
}

function rfMakeHeightTex() {
    local image = eve.Image().newEmptyImageData(64, 64, "RGBA8");
    for (local y = 0; y < 64; ++y) {
        for (local x = 0; x < 64; ++x) {
            local h = 0.5 + 0.5 * sin(x * 0.35) * cos(y * 0.35);
            image.setPixel(x, y, h, h, h, 1.0);
        }
    }
    return gfx.newTexture(image, false, false);
}

function rfBuildVtMaterial() {
    local mat = gfx.newMaterial();
    mat.enableVirtualTexture(128, 64, 64, 2, 2, 2);
    local rock = rfMakeLayer(0.34, 0.30, 0.25);
    local moss = rfMakeLayer(0.18, 0.42, 0.20);
    local flat = eve.Image().newEmptyImageData(64, 64, "RGBA8");
    for (local y = 0; y < 64; ++y)
        for (local x = 0; x < 64; ++x)
            flat.setPixel(x, y, 0.5, 0.5, 1.0, 1.0);
    mat.addVirtualTextureLayer(rock, flat, 1.0, 1.0, 0.0);
    mat.addVirtualTextureLayer(moss, flat, 1.0, 1.0, 0.85);
    mat.setRoughness(0.82);
    mat.setMetallic(0.0);
    return mat;
}

function rfBuildStudioProbe() {
    // Capture the closed room into a prefiltered cubemap. Sky face colors only
    // fill empty / out-of-far pixels — keep them soft (never near-white) so chrome
    // does not bake as blocky rectangles (the old setCaptureMask(0) path).
    local probe = gfx.newReflectionProbeCapture();
    // Capture near the chrome ladder so floor / walls / ceiling / window fill faces.
    // 512 keeps mirror-ball faces readable after GGX prefilter on Lavapipe.
    probe.configure(0.0, 1.15, 0.2, 256, 0.05, 32.0);
    local faces = [
        [0.48, 0.42, 0.38], // +X
        [0.30, 0.38, 0.55], // -X
        [0.62, 0.66, 0.74], // +Y ceiling gap fill
        [0.38, 0.39, 0.41], // -Y floor gap fill (match bright floor albedo)
        [0.42, 0.46, 0.52], // +Z
        [0.40, 0.44, 0.50]  // -Z
    ];
    foreach (i, c in faces) probe.setSkyFaceColor(i, c[0], c[1], c[2]);
    // MeshRenderer.reflectionCaptureMask defaults to bit0=1; -1 keeps room props.
    probe.setCaptureMask(-1);
    probe.requestCapture();
    for (local attempt = 0; attempt < 96; ++attempt) {
        if (probe.update(6)) break;
    }
    if (!probe.isCaptureComplete() || !probe.stageCapturedFaces() || !probe.filterAndPublish(128))
        throw "render-fidelity: reflection probe publish failed";
    if (probe.getActiveCubemap() == null)
        throw "render-fidelity: reflection cubemap missing";
    return probe;
}

function rfResetCamera() {
    // Inside the closed room, pulled back enough to see the chrome ladder.
    // Mirror-ball centers reflect behind the camera — stay indoors so that
    // direction is the bright front window, not the exterior void.
    rf.camera.setEye(0.0, 2.05, 3.9);
    rf.camera.setTarget(0.0, 0.85, -0.8);
    rf.camera.setFov(55.0);
}

function rfSyncVolume() {
    if (rf.volume == null || rf.camera == null) return;
    rf.volume.setCamera(
        rf.camera.getEyeX(), rf.camera.getEyeY(), rf.camera.getEyeZ(),
        rf.camera.getTargetX(), rf.camera.getTargetY(), rf.camera.getTargetZ(),
        0.0, 1.0, 0.0, rf.camera.getFov(), 1280.0 / 720.0, 0.1, 64.0);
}

function rfRebuildFog() {
    if (rf.volume == null) return;
    rfSyncVolume();
    rf.volume.configureFroxelGrid(48, 28, 20, 0.1, 40.0);
    rf.volume.clearFroxelGrid();
    local density = rf.opts.volumetricFog ? 0.028 : 0.0;
    rf.volume.injectFroxelHeightFog(density, 0.70, 0.76, 0.88, -0.4, 0.12, -1.0, 6.5);
    local integ = rf.volume.integrateFroxelFromSceneLights(
        0.05, 0.06, 0.08, -10.0, -1.0, -8.0, 10.0, 8.0, 10.0, 8);
    rfChecked(integ, "integrateFroxelFromSceneLights");
    rf.volume.uploadFroxel(gfx);
}

function rfApplyMaterials() {
    if (rf.leafMat != null && rf.leaf != null) {
        rf.leafMat = gfx.newMaterial();
        rf.leafMat.setTint(0.22, 0.48, 0.16, 1.0);
        rf.leafMat.setRoughness(0.72);
        rf.leafMat.setMetallic(0.0);
        rf.leafMat.setDoubleSided(true);
        if (rf.opts.materialExt) {
            local r = rf.leafMat.setFoliageTranslucency(0.35, 0.85, 0.18, 0.85, 8.0, 0.45, 4.0, 0.9, 0.15, 0.4);
            rfChecked(r, "setFoliageTranslucency");
        }
        rf.leaf.setMaterial(rf.leafMat);
    }
    if (rf.coatMat != null && rf.coat != null) {
        rf.coatMat = gfx.newMaterial();
        rf.coatMat.setTint(0.78, 0.12, 0.10, 1.0);
        rf.coatMat.setMetallic(0.05);
        rf.coatMat.setRoughness(0.28);
        if (rf.opts.materialExt) {
            local r = rf.coatMat.setClearcoat(0.92, 0.12);
            rfChecked(r, "setClearcoat");
        }
        rf.coat.setMaterial(rf.coatMat);
    }
}

function rfApplyGround() {
    if (rf.ground == null) return;
    if (rf.opts.virtualTexture) {
        if (rf.vtMat == null) rf.vtMat = rfBuildVtMaterial();
        rf.ground.setMaterial(rf.vtMat);
        rf.ground.setTexCellBomb(0.0, 0.0, 1.0);
        rf.ground.setParallax(0.0, 8.0, 32.0);
    } else {
        if (rf.plainGroundMat == null) {
            rf.plainGroundMat = gfx.newMaterial();
            rf.plainGroundMat.setTint(0.16, 0.17, 0.19, 1.0);
            rf.plainGroundMat.setMetallic(0.08);
            rf.plainGroundMat.setRoughness(0.88);
        }
        rf.ground.setMaterial(rf.plainGroundMat);
        if (rf.opts.meshDetail) {
            if (rf.heightTex == null) rf.heightTex = rfMakeHeightTex();
            rf.ground.setHeightTexture(rf.heightTex);
            rf.ground.setParallax(0.045, 8.0, 24.0);
            rf.ground.setTexCellBomb(6.0, 0.65, 1.0);
        } else {
            rf.ground.setParallax(0.0, 8.0, 32.0);
            rf.ground.setTexCellBomb(0.0, 0.0, 1.0);
        }
    }
}

function rfApplyIbl() {
    if (rf.opts.iblProbes) {
        if (rf.probe == null) {
            // Probe publish can fail on software Vulkan; keep ambient-only then.
            local ok = true;
            try { rf.probe = rfBuildStudioProbe(); }
            catch (e) {
                print("render-fidelity: IBL probe unavailable: " + e + "\n");
                ok = false;
                rf.probe = null;
            }
            if (!ok) {
                rf.camera.setEnvIntensity(0.0);
                rf.camera.clearEnvProbe();
                rf.camera.setAmbient(0.18, 0.20, 0.24);
                return;
            }
        }
        local cube = rf.probe.getActiveCubemap();
        // Global env is a dim fallback; the local reflection-probe slot owns the room
        // box projection so chrome samples the captured walls with parallax.
        rf.camera.setEnvMap(cube);
        rf.camera.setEnvIntensity(0.25);
        rf.camera.clearEnvProbe();
        rf.camera.setReflectionProbe(0, cube, 0.0, 1.15, 0.2, 5.1, 2.5, 5.0, 1.05, 0.75, 10);
        rf.camera.setAmbient(0.07, 0.08, 0.10);
    } else {
        rf.camera.setEnvIntensity(0.0);
        rf.camera.clearEnvProbe();
        rf.camera.clearReflectionProbe(0);
        rf.camera.setAmbient(0.22, 0.24, 0.28);
    }
}

function rfApplyCsmQuality() {
    if (rf.opts.csmQuality) {
        gfx.setShadowSchemeDirectionalMapSize(2048);
        gfx.setShadowSchemeLocalMapSize(1024);
        gfx.setShadowSchemeDirectionalMaxDistance(36.0);
        gfx.setShadowSchemeAdaptiveCascadeResolution(true);
        rf.sun.setShadowStrength(0.92);
        rf.sun.setShadowBias(0.00035);
    } else {
        gfx.setShadowSchemeDirectionalMapSize(1024);
        gfx.setShadowSchemeLocalMapSize(1024);
        gfx.setShadowSchemeDirectionalMaxDistance(64.0);
        gfx.setShadowSchemeAdaptiveCascadeResolution(false);
        rf.sun.setShadowStrength(0.82);
        rf.sun.setShadowBias(0.0008);
    }
}

function rfApplyRenderControl() {
    local rc = gfx.getRenderControl();
    rc.enable("shadow");
    rc.enable("gbuffer");
    rc.enable("gbufferAlbedo");
    rc.enable("forward");
    rc.enable("depthTest");

    if (rf.opts.ao) rc.enable("ao"); else rc.disable("ao");
    if (rf.opts.indirectGi) {
        rc.enable("gi");
        rc.enable("rtgi");
    } else {
        rc.disable("gi");
        rc.disable("rtgi");
    }
    if (rf.opts.contactShadow) rc.enable("contactShadow"); else rc.disable("contactShadow");
    if (rf.opts.volumetricFog) rc.enable("volumetricFog"); else rc.disable("volumetricFog");
    if (rf.opts.reflectionChain) {
        // Enable the temporal/RTGI half of the chain. Skip SSR on this stage:
        // convex chrome + screen-space hits produce a hard vertical seam that
        // overrides the room probe (the "broken metal ball" look). Floor SSR
        // belongs in a dedicated planar-reflection demo.
        rc.enable("taa");
        rc.enable("rtgi");
        rc.disable("ssr");
        rc.enable("aa");
        rc.disable("msaa");
        rc.setPostProcessQuality("high");
    } else {
        rc.disable("reflectionChain");
        rc.disable("ssr");
        rc.disable("taa");
        if (!rf.opts.indirectGi) rc.disable("rtgi");
        rc.enable("aa");
        rc.enable("msaa");
        gfx.setMsaaSamples(4);
        rc.setPostProcessQuality(rf.opts.ao || rf.opts.indirectGi ? "medium" : "low");
    }
    rf.camera.setBloom(rf.opts.reflectionChain || rf.opts.iblProbes ? 0.12 : 0.10, 1.05);
    // IBL chrome already carries room radiance — pull exposure down so reflections
    // do not clip to flat white patches on the lower hemisphere.
    local exposure = 0.82;
    if (rf.opts.iblProbes) exposure = 0.58;
    else if (rf.opts.volumetricFog) exposure = 0.62;
    rf.camera.setExposure(exposure);
    rc.compile();
}

function rfRefreshHud() {
    if (!rf.uiBuilt) return;
    ui.select("rf-hud");
    ui.setText("preset", "Preset: " + rf.presetName + "   (F1 baseline / F2 P0 / F3 P0+P1 / F4 all)");
    ui.setText("p0a", (rf.opts.indirectGi ? "[ON] " : "[  ] ") + "1 Indirect GI (gi+rtgi)");
    ui.setText("p0b", (rf.opts.contactShadow ? "[ON] " : "[  ] ") + "2 Contact shadow");
    ui.setText("p0c", (rf.opts.csmQuality ? "[ON] " : "[  ] ") + "3 CSM quality");
    ui.setText("p0d", (rf.opts.ao ? "[ON] " : "[  ] ") + "4 AO");
    ui.setText("p0e", (rf.opts.volumetricFog ? "[ON] " : "[  ] ") + "5 Volumetric fog");
    ui.setText("p1a", (rf.opts.materialExt ? "[ON] " : "[  ] ") + "6 Material SSS/clearcoat");
    ui.setText("p1b", (rf.opts.iblProbes ? "[ON] " : "[  ] ") + "7 IBL + box probe");
    ui.setText("p1c", (rf.opts.reflectionChain ? "[ON] " : "[  ] ") + "8 Reflection chain (TAA+RTGI)");
    ui.setText("p2a", (rf.opts.virtualTexture ? "[ON] " : "[  ] ") + "9 Virtual texture ground");
    ui.setText("p2b", (rf.opts.meshDetail ? "[ON] " : "[  ] ") + "0 POM + cell bomb");
}

function rfApplyAll() {
    rfApplyCsmQuality();
    rfApplyIbl();
    rfApplyMaterials();
    rfApplyGround();
    rfApplyRenderControl();
    rf.fogReady <- false;
    if (rf.opts.volumetricFog) {
        rfRebuildFog();
        rf.fogReady = true;
    }
    rfRefreshHud();
}

function rfSetOpts(table) {
    foreach (k, v in table) rf.opts[k] <- v;
}

function rfPresetBaseline() {
    rf.presetName = "baseline";
    rfSetOpts({
        indirectGi = false, contactShadow = false, csmQuality = false, ao = false, volumetricFog = false,
        materialExt = false, iblProbes = false, reflectionChain = false,
        virtualTexture = false, meshDetail = false
    });
    rfApplyAll();
}

function rfPresetP0() {
    rf.presetName = "p0";
    rfSetOpts({
        indirectGi = true, contactShadow = true, csmQuality = true, ao = true, volumetricFog = true,
        materialExt = false, iblProbes = false, reflectionChain = false,
        virtualTexture = false, meshDetail = false
    });
    rfApplyAll();
}

function rfPresetP1() {
    rf.presetName = "p0+p1";
    rfSetOpts({
        indirectGi = true, contactShadow = true, csmQuality = true, ao = true, volumetricFog = true,
        materialExt = true, iblProbes = true, reflectionChain = true,
        virtualTexture = false, meshDetail = false
    });
    rfApplyAll();
}

function rfPresetP2() {
    rf.presetName = "p0+p1+p2";
    rfSetOpts({
        indirectGi = true, contactShadow = true, csmQuality = true, ao = true, volumetricFog = true,
        materialExt = true, iblProbes = true, reflectionChain = true,
        virtualTexture = true, meshDetail = true
    });
    // VT and POM compete on the same ground — prefer VT when both requested.
    rf.opts.meshDetail = false;
    rfApplyAll();
}

function rfPresetAll() {
    rfPresetP2();
    rf.presetName = "all";
    rfRefreshHud();
}

function rfToggle(name) {
    rf.opts[name] = !rf.opts[name];
    if (name == "virtualTexture" && rf.opts.virtualTexture) rf.opts.meshDetail = false;
    if (name == "meshDetail" && rf.opts.meshDetail) rf.opts.virtualTexture = false;
    rf.presetName = "custom";
    rfApplyAll();
}

function rfBuildScene() {
    rf.objects = [];
    // Fully closed Cornell-like room. Mirror balls need every direction filled —
    // an open front reflects as a dark equatorial band from typical camera angles.
    rf.ground = rfMakeBox(0.0, -0.12, -0.4, 12.0, 0.16, 10.0, 0.62, 0.63, 0.65, 0.02, 0.70, false);
    rfMakeBox(0.0, 3.85, -0.4, 12.0, 0.16, 10.0, 0.82, 0.84, 0.88, 0.0, 0.90, false); // ceiling
    rfMakeBox(0.0, 1.9, -5.2, 12.0, 4.0, 0.20, 0.62, 0.63, 0.65, 0.02, 0.82, true);   // back
    rfMakeBox(-5.0, 1.9, -0.4, 0.20, 4.0, 10.0, 0.78, 0.12, 0.08, 0.02, 0.70, true);  // left
    rfMakeBox(5.0, 1.9, -0.4, 0.20, 4.0, 10.0, 0.10, 0.28, 0.78, 0.02, 0.70, true);   // right
    // Front wall with a bright window band so chrome center is not a black void.
    rfMakeBox(0.0, 1.9, 4.9, 12.0, 4.0, 0.20, 0.58, 0.60, 0.64, 0.02, 0.80, true);
    rfMakeBox(0.0, 2.35, 4.75, 4.8, 1.7, 0.08, 0.95, 0.96, 0.98, 0.0, 0.32, false);

    // Contact-shadow casters — warmer / cooler tints so they read in chrome IBL.
    rfMakeBox(-2.4, 0.55, 0.4, 0.9, 1.1, 0.9, 0.72, 0.55, 0.28, 0.08, 0.48, true);
    rfMakeBox(0.2, 1.05, -1.8, 0.7, 2.1, 0.7, 0.28, 0.55, 0.42, 0.25, 0.38, true);
    rfMakeBox(2.8, 0.42, 0.1, 1.4, 0.84, 0.7, 0.82, 0.78, 0.70, 0.05, 0.62, true);

    // Chrome roughness ladder. Exclude from probe capture so the cubemap sees the
    // room (walls / ceiling / boxes) instead of baking the spheres into themselves.
    // Slightly rough the mirror end so GGX mips hide cubemap texel edges;
    // still reads as chrome next to the satin / matte siblings.
    local roughness = [0.08, 0.20, 0.42, 0.75];
    for (local i = 0; i < 4; ++i) {
        local x = -3.0 + i * 2.0;
        local ball = rfMakeSphere(x, 0.55, 1.2, 0.50, 0.70, 0.72, 0.74, 1.0, roughness[i], true);
        ball.setReflectionCaptureMask(0);
    }

    rf.coatMat = gfx.newMaterial();
    rf.coatMat.setTint(0.78, 0.12, 0.10, 1.0);
    rf.coatMat.setMetallic(0.05);
    rf.coatMat.setRoughness(0.28);
    rf.coat = eve.Renderable3D();
    rf.coat.setMesh(gfx.newMeshSphere(48, 28));
    rf.coat.setPosition(4.0, 0.62, 1.0);
    rf.coat.setScale(0.58, 0.58, 0.58);
    rf.coat.setMaterial(rf.coatMat);
    rf.coat.setCastShadow(true);
    rf.coat.setReceiveShadow(true);
    rf.coat.setReflectionCaptureMask(0);
    rf.objects.append(rf.coat);

    // Foliage card kept away from chrome so it does not read as a "green pole" in IBL.
    rf.leafMat = gfx.newMaterial();
    rf.leafMat.setTint(0.22, 0.48, 0.16, 1.0);
    rf.leafMat.setRoughness(0.72);
    rf.leafMat.setMetallic(0.0);
    rf.leafMat.setDoubleSided(true);
    rf.leaf = eve.Renderable3D();
    rf.leaf.setMesh(gfx.newMeshCube(1.0));
    rf.leaf.setPosition(-3.6, 1.35, -2.4);
    rf.leaf.setScale(0.04, 1.7, 1.1);
    rf.leaf.setYaw(0.55);
    rf.leaf.setMaterial(rf.leafMat);
    rf.leaf.setCastShadow(true);
    rf.leaf.setReceiveShadow(true);
    // Keep the card in the live scene, but out of the probe — a 4cm slab reads as
    // a mysterious green "pole" stripe inside chrome reflections.
    rf.leaf.setReflectionCaptureMask(0);
    rf.objects.append(rf.leaf);
}

eve_init = function() {
    rf.opts <- {
        indirectGi = false, contactShadow = false, csmQuality = false, ao = false, volumetricFog = false,
        materialExt = false, iblProbes = false, reflectionChain = false,
        virtualTexture = false, meshDetail = false
    };
    rf.presetName <- "baseline";
    rf.objects <- [];
    rf.uiBuilt <- false;
    rf.frame <- 0;
    rf.volAccum <- 0.0;
    rf.probe <- null;
    rf.vtMat <- null;
    rf.plainGroundMat <- null;
    rf.heightTex <- null;
    rf.capture <- ("fidelityCapture" in config) && config.fidelityCapture;
    rf.captureDone <- false;
    rf.camera <- null;
    rf.sun <- null;
    rf.spot <- null;
    rf.fill <- null;
    rf.volume <- null;
    rf.ground <- null;
    rf.coat <- null;
    rf.coatMat <- null;
    rf.leaf <- null;
    rf.leafMat <- null;
    rf.fogReady <- false;

    if ("dev" in eve) {
        eve.dev.ai.setVisible(false);
        eve.dev.console.setVisible(false);
    }
    gfx.setBackgroundColor(0.045, 0.055, 0.075, 1.0);
    local lighting = gfx.getRenderControl().setLightingMode("forwardPlus");
    if (typeof lighting == "table" && ("ok" in lighting) && !lighting.ok)
        throw "render-fidelity: forward lighting unavailable";
    if (typeof lighting == "bool" && !lighting)
        throw "render-fidelity: forward lighting unavailable";

    rf.camera = eve.Camera3D();
    rf.camera.setUp(0.0, 1.0, 0.0);
    rf.camera.setFov(46.0);
    rf.camera.setAmbient(0.22, 0.24, 0.28);
    rf.camera.setExposure(0.82);
    rf.camera.setBloom(0.10, 1.08);
    rf.camera.setActive(true);
    rfResetCamera();

    gfx.setShadowSchemeDirectionalEnabled(true);
    gfx.setShadowSchemeSpotEnabled(true);
    gfx.setShadowSchemePointEnabled(false); // cube path reserved
    gfx.setShadowSchemeMaxSpotCasters(3);
    gfx.setShadowSchemePagingEnabled(true);

    rf.sun = eve.Light3D();
    rf.sun.setType("dir");
    rf.sun.setDirection(-0.55, 0.92, 0.30);
    rf.sun.setColor(1.0, 0.94, 0.86, 2.05);
    rf.sun.setCastShadow(true);
    rf.sun.setShadowMethod("csm");
    rf.sun.setVolumetric(true);
    rf.sun.setVolumetricIntensity(0.22);
    gfx.setDirectionalLight(-0.55, 0.92, 0.30, 0.50, 0.46, 0.40);

    rf.spot = eve.Light3D();
    rf.spot.setType("spot");
    rf.spot.setPosition(3.8, 4.6, 3.2);
    rf.spot.setDirection(-0.35, -1.0, -0.55);
    rf.spot.setColor(1.0, 0.78, 0.48, 6.5);
    rf.spot.setRadius(16.0);
    rf.spot.setSpotAngle(28.0);
    rf.spot.setSpotSoftness(0.32);
    rf.spot.setCastShadow(true);
    rf.spot.setShadowMethod("perspective");
    rf.spot.setVolumetric(true);
    rf.spot.setVolumetricIntensity(1.4);

    rf.fill = eve.Light3D();
    rf.fill.setType("point");
    rf.fill.setPosition(-3.6, 2.8, 3.5);
    rf.fill.setColor(0.45, 0.65, 1.0, 2.2);
    rf.fill.setRadius(12.0);
    rf.fill.setCastShadow(false);

    rf.volume = gfx.newVolumetric();
    rf.volume.setMode("froxel");
    rf.volume.setQuality("medium");

    rfBuildScene();

    if (!rf.uiBuilt) {
        ui.beginBuild();
        ui.beginWindow("RenderFidelity", "root");
        ui.text("EVENGINE / RENDER FIDELITY (P0-P2)", "title");
        ui.text("", "preset");
        ui.text("", "p0a"); ui.text("", "p0b"); ui.text("", "p0c"); ui.text("", "p0d"); ui.text("", "p0e");
        ui.text("", "p1a"); ui.text("", "p1b"); ui.text("", "p1c");
        ui.text("", "p2a"); ui.text("", "p2b");
        ui.text("R: reset camera", "hint");
        ui.end();
        ui.mountBuildAs("rf-hud");
        ui.select("rf-hud");
        ui.setHostOverlay(true);
        ui.setHostPos(14.0, 12.0, 0.0, 0.0);
        rf.uiBuilt = true;
    }

    local start = ("fidelityPreset" in config) ? config.fidelityPreset : "all";
    if (start == "baseline") rfPresetBaseline();
    else if (start == "p0") rfPresetP0();
    else if (start == "p1" || start == "p0+p1") rfPresetP1();
    else if (start == "p2" || start == "p0+p1+p2" || start == "all") rfPresetAll();
    else rfPresetAll();

    print("render-fidelity: preset=" + rf.presetName + " capture=" + rf.capture + "\n");
};

eve_update = function(dt) {
    if (rf.opts.volumetricFog) {
        rf.volAccum += dt;
        if (rf.volAccum >= 0.10) {
            rf.volAccum = 0.0;
            rfRebuildFog();
        }
    }

    if (key_just_pressed("1")) rfToggle("indirectGi");
    if (key_just_pressed("2")) rfToggle("contactShadow");
    if (key_just_pressed("3")) rfToggle("csmQuality");
    if (key_just_pressed("4")) rfToggle("ao");
    if (key_just_pressed("5")) rfToggle("volumetricFog");
    if (key_just_pressed("6")) rfToggle("materialExt");
    if (key_just_pressed("7")) rfToggle("iblProbes");
    if (key_just_pressed("8")) rfToggle("reflectionChain");
    if (key_just_pressed("9")) rfToggle("virtualTexture");
    if (key_just_pressed("0")) rfToggle("meshDetail");
    if (key_just_pressed("f1") || key_just_pressed("F1")) rfPresetBaseline();
    if (key_just_pressed("f2") || key_just_pressed("F2")) rfPresetP0();
    if (key_just_pressed("f3") || key_just_pressed("F3")) rfPresetP1();
    if (key_just_pressed("f4") || key_just_pressed("F4")) rfPresetAll();
    if (key_just_pressed("r") || key_just_pressed("R")) rfResetCamera();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if (rf.opts.volumetricFog && rf.fogReady && rf.volume != null) {
        local gb = gfx.getRenderControl().getGBuffer();
        if (gb != null && gb.isValid()) {
            local depth = gb.getDepthTexture();
            if (depth != null) rf.volume.applyFroxel(gfx, depth);
        }
    }
    ui.beginFrameAndRender();
    rf.frame += 1;
    // saveFramePng reads the previously presented frame. First call enables readback;
    // retry until a presented frame is available (same pattern as lighting-showcase).
    if (rf.capture && !rf.captureDone && rf.frame > 48) {
        local tag = rf.presetName;
        if (tag == "p0+p1") tag = "p0-p1";
        if (tag == "p0+p1+p2" || tag == "all") tag = "all";
        local name = "render-fidelity-" + tag + ".png";
        if (gfx.saveFramePng(name)) {
            print("render-fidelity: wrote " + name + "\n");
            rf.captureDone = true;
        }
    }
};
