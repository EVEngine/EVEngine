// Integration scene for native terrain generation and grass using the documented source texture.
dofile("validate-image-mask.nut");
local retained = [];
local camera = null;
local terrainGrass = null;
local windSeconds = 0.0;
local windState = null;
local windProfile = null;
local windAudioState = null;
local windAudioSource = null;
local waterSystemState = null;
local waterSystemSettings = null;
local waterSceneConditions = null;
local terrainWater = null;
local terrainWaterEntity = null;
local treeWindShader = null;
local treeWindProfile = null;
local sharedTreeMesh = null;
local windPaused = false;
local gtsDetailGreyscale = null;
local gtsGlobalBlend = null;
local gtsHeightSplat = null;
local gtsGeoStrength = null;
local gtsLayerDetailStrength = null;
local gtsLayerDisplacement = null;
local gtsLayerTessellation = null;
pcg_validation <- { ready = false, frames = 0, changed = 0, sediment = 0.0 };

function retain(value) {
    if (value == null) throw "terrain-stamping: required resource is null";
    retained.append(value);
    return value;
}

function requireResult(result, operation) {
    if (!result.ok) throw "terrain-stamping: " + operation + " failed";
    return result.value;
}

function heightmap(width, height) {
    return retain(requireResult(procgen.newHeightmap(width, height), "newHeightmap"));
}

function setupWaterSystem() {
    waterSystemState=retain(eve.WaterSystemState());waterSystemSettings=retain(eve.WaterSystemSettings());
    waterSceneConditions=retain(eve.WaterSceneConditions());
    waterSystemState.seaLevel=3.5;waterSystemSettings.refreshRate=0.5;
    waterSystemSettings.infiniteMode=true;waterSystemSettings.autoRefresh=true;
    waterSystemSettings.ignoreSceneConditions=true;waterSystemSettings.setAutoUpdateMode(0);
    waterSceneConditions.setSunColor(1.0,0.92,0.78);waterSceneConditions.setSunDirection(35.0,120.0,0.0);
    waterSceneConditions.sunIntensity=1.25;waterSceneConditions.sunAvailable=true;
    requireResult(eve.initializeWaterSystem(waterSystemState,waterSystemSettings,waterSceneConditions,7),"water initialize");
    local refresh=requireResult(eve.advanceWaterSystem(waterSystemState,waterSystemSettings,waterSceneConditions,
                                                       137.0,151.0,0.6,9),"water advance");
    if(!refresh || waterSystemState.positionX!=137.0 || waterSystemState.positionY!=3.5 ||
       waterSystemState.positionZ!=151.0 || waterSystemState.refreshRevision!=1) throw "water controller mismatch";
    local changed=requireResult(eve.updateWaterSeaLevel(waterSystemState,4.25,true),"water sea level");
    if(!changed || waterSystemState.positionY!=4.25 || waterSystemState.refreshRevision!=2)
        throw "water sea level mismatch";
    terrainWater=retain(gfx.newWater());
    local meshSettings=eve.WaterMeshSettings();meshSettings.setType(1);
    meshSettings.sizeX=20.0;meshSettings.sizeY=1.0;meshSettings.sizeZ=20.0;
    meshSettings.densityX=12.0;meshSettings.densityY=12.0;meshSettings.height=0.0;
    local triangles=requireResult(eve.calculateWaterMeshTriangles(meshSettings),"water polygon count");
    local vertices=requireResult(terrainWater.createProceduralMesh(meshSettings),"water circle mesh");
    if(vertices!=37 || triangles!=54) throw "water circle topology mismatch";
    terrainWater.setWaveAmplitude(0.22);terrainWater.setRippleAmplitude(0.3);
    requireResult(terrainWater.setWaveDirectionAngle(52.5),"water wave direction");
    if(terrainWater.getWaveDirectionAngle()!=52.5) throw "water wave direction mismatch";
    terrainWater.setReflectionIntensity(0.72);terrainWater.setSunIntensity(0.65);
    local depthGradient=eve.WaterDepthGradient();
    requireResult(depthGradient.addColorStop(0.0,0.10,0.68,0.78),"water shallow gradient");
    requireResult(depthGradient.addColorStop(0.42,0.02,0.30,0.48),"water middle gradient");
    requireResult(depthGradient.addColorStop(1.0,0.005,0.035,0.09),"water deep gradient");
    requireResult(depthGradient.addAlphaStop(0.0,0.42),"water shallow alpha");
    requireResult(depthGradient.addAlphaStop(1.0,0.94),"water deep alpha");
    local rampPixels=requireResult(terrainWater.setDepthGradient(depthGradient,16),"water depth gradient");
    if(rampPixels!=256) throw "water depth gradient size mismatch";
    local reflectionSettings=eve.WaterPlanarReflectionSettings();
    reflectionSettings.textureResolution=600;reflectionSettings.setResolutionMultiplier(2);
    reflectionSettings.clipPlaneOffset=0.2;reflectionSettings.shadows=true;
    reflectionSettings.enableRenderDistance=true;reflectionSettings.customRenderDistance=275.0;
    local reflectionInput=eve.WaterPlanarReflectionInput();reflectionInput.waterPlaneY=4.25;
    reflectionInput.renderScale=0.8;reflectionInput.setCameraPosition(70.0,18.0,56.0);
    reflectionInput.setCameraForward(0.0,-0.6,-0.8);
    local reflectionPlan=eve.WaterPlanarReflectionPlan();
    requireResult(eve.buildWaterPlanarReflectionPlan(reflectionPlan,reflectionSettings,reflectionInput),"water planar reflection");
    if(!reflectionPlan.shouldRender || reflectionPlan.textureWidth!=158 ||
       abs(reflectionPlan.getCameraY()-(-8.7))>0.001 || reflectionPlan.getLayerDistance(31)!=275.0)
        throw "water planar reflection plan mismatch";
    local underwaterSettings=eve.WaterUnderwaterSettings();underwaterSettings.causticTextureCount=16;
    underwaterSettings.setFogColorMultiplier(0.1,-1.0,0.0);
    local underwaterInput=eve.WaterUnderwaterInput();underwaterInput.seaLevel=4.25;underwaterInput.cameraY=8.0;
    underwaterInput.setMainLightColor(1.0,0.9,0.8);
    local underwaterState=eve.WaterUnderwaterState(),underwaterOutput=eve.WaterUnderwaterOutput();
    local underwaterDepth=eve.UnderwaterColorGradient(),underwaterTime=eve.UnderwaterColorGradient();
    local underwaterExposure=eve.UnderwaterScalarCurve(),underwaterPostColor=eve.UnderwaterColorGradient();
    requireResult(underwaterDepth.addStop(0.0,0.4,0.7,1.0),"underwater shallow fog");
    requireResult(underwaterDepth.addStop(1.0,0.03,0.08,0.18),"underwater deep fog");
    requireResult(underwaterTime.addStop(0.0,0.1,0.2,0.4),"underwater dawn fog");
    requireResult(underwaterTime.addStop(1.0,0.8,0.5,0.2),"underwater day fog");
    requireResult(underwaterExposure.addKey(0.0,-0.4),"underwater night exposure");
    requireResult(underwaterExposure.addKey(1.0,0.1),"underwater day exposure");
    requireResult(underwaterPostColor.addStop(0.0,0.4,0.6,0.8),"underwater night filter");
    requireResult(underwaterPostColor.addStop(1.0,0.8,0.95,1.0),"underwater day filter");
    requireResult(eve.advanceWaterUnderwaterEffects(underwaterState,underwaterOutput,underwaterSettings,
                                                    underwaterInput,underwaterDepth,underwaterTime,
                                                    underwaterExposure,underwaterPostColor),"underwater surface state");
    underwaterInput.cameraY=-20.75;underwaterInput.causticTicks=18;
    requireResult(eve.advanceWaterUnderwaterEffects(underwaterState,underwaterOutput,underwaterSettings,
                                                    underwaterInput,underwaterDepth,underwaterTime,
                                                    underwaterExposure,underwaterPostColor),"underwater submerged state");
    if(!underwaterOutput.entered || !underwaterOutput.loopAudio || !underwaterOutput.particles ||
       underwaterOutput.causticFrame!=2 || abs(underwaterOutput.depth01-0.25)>0.001)
        throw "underwater effects state mismatch";
    terrainWaterEntity=retain(eve.Renderable3D());terrainWaterEntity.setMesh(terrainWater.getMesh());
    terrainWaterEntity.setShader(terrainWater.getShader());terrainWaterEntity.setPosition(70.0,4.25,32.0);
    terrainWaterEntity.setNormalTexture(terrainWater.getDepthGradientTexture());
    terrainWaterEntity.setReceiveShadow(false);terrainWaterEntity.setCastShadow(false);
    print("TERRAIN_WATER_SYSTEM_PASS sea=4.25 infinite=1 refresh=2 mesh=circle vertices=37 triangles=54 ramp=16 planar=158 underwater=1 caustic=2\n");
}

function applyGtsColorMap(image) {
    local colorMap=retain(eve.Image().newEmptyImageData(32,32,"RGBA8"));
    for(local z=0;z<32;++z) for(local x=0;x<32;++x) {
        local farBlend=x/31.0;colorMap.setPixel(x,z,0.16+0.20*farBlend,0.38+0.12*farBlend,0.12,0.42);
    }
    local settings=eve.GtsColorMapSettings();settings.alphaIntensity=1.15;settings.colorIntensity=1.1;
    settings.nearIntensity=0.22;settings.farIntensity=0.72;
    requireResult(eve.bakeGtsColorMapAlbedo(image,colorMap,gtsGlobalBlend,settings),"GTS colormap albedo");
    print("TERRAIN_GTS_COLORMAP_PASS near=0.22 far=0.72\n");
}

function applyGtsMacroVariation(image) {
    local map=retain(eve.Image().newEmptyImageData(4,4,"RGBA8"));
    for(local z=0;z<4;++z) for(local x=0;x<4;++x) map.setPixel(x,z,0.16+0.18*((x+z)%3),0.0,0.0,1.0);
    local settings=eve.GtsMacroVariationSettings();settings.sizeA=13.0;settings.sizeB=31.0;settings.sizeC=67.0;settings.intensity=0.72;
    requireResult(eve.bakeGtsMacroVariationAlbedo(image,map,settings,78.0,0.0,2.0,2.0),"GTS macro variation");
    print("TERRAIN_GTS_VARIATION_PASS scales=3 intensity=0.72\n");
}

function applyGtsGeological(albedo, packedNormal, heights) {
    local color=retain(eve.Image().newEmptyImageData(1,4,"RGBA8"));
    color.setPixel(0,0,0.20,0.16,0.10,1.0);color.setPixel(0,1,0.44,0.35,0.20,1.0);
    color.setPixel(0,2,0.16,0.21,0.11,1.0);color.setPixel(0,3,0.56,0.50,0.34,1.0);
    local normal=retain(eve.Image().newEmptyImageData(1,4,"RGBA8"));
    normal.setPixel(0,0,0.0,0.46,0.0,0.53);normal.setPixel(0,1,0.0,0.55,0.0,0.48);
    normal.setPixel(0,2,0.0,0.43,0.0,0.56);normal.setPixel(0,3,0.0,0.52,0.0,0.46);
    local settings=eve.GtsGeologicalSettings();settings.enabled=true;settings.nearStrength=0.12;
    settings.nearNormalStrength=0.22;settings.nearScale=8.0;settings.farStrength=0.08;
    settings.farNormalStrength=0.14;settings.farScale=19.0;settings.farOffset=0.17;
    requireResult(eve.bakeGtsGeologicalSurface(albedo,packedNormal,heights,gtsGeoStrength,gtsGlobalBlend,color,normal,settings,0.0),"GTS geological surface");
    print("TERRAIN_GTS_GEOLOGICAL_PASS near=8 far=19 outputs=2\n");
}

function applyGtsDetail(albedo, packedNormal) {
    local map=retain(eve.Image().newEmptyImageData(4,4,"RGBA8"));
    for(local mz=0;mz<4;++mz) for(local mx=0;mx<4;++mx) {
        map.setPixel(mx,mz,0.40+0.07*((mx+mz)%3),0.44+0.06*((mx*2+mz)%3),0.0,1.0);
    }
    local settings=eve.GtsDetailNormalSettings();settings.nearTiling=7.0;settings.nearStrength=0.34;
    settings.farTiling=23.0;settings.farStrength=0.20;
    gtsDetailGreyscale=heightmap(32,32);
    requireResult(eve.bakeGtsDetailSurface(albedo,packedNormal,gtsDetailGreyscale,gtsLayerDetailStrength,gtsGlobalBlend,map,settings,78.0,0.0,2.0,2.0),"GTS detail surface");
    print("TERRAIN_GTS_DETAIL_PASS near=7 far=23 outputs=3\n");
}

function applyGtsLayerHeightBlend(input, vegetationHeight, rockHeight) {
    local sandHeight=heightmap(32,32);
    for(local z=0;z<32;++z) for(local x=0;x<32;++x) sandHeight.setHeight(x,z,0.58-0.28*vegetationHeight.height(x,z));
    local layers=eve.GtsHeightBlendSet();
    requireResult(layers.addLayer(sandHeight,1.0,1.0,0.0),"GTS sand height layer");
    requireResult(layers.addLayer(vegetationHeight,1.15,1.0,0.03),"GTS vegetation height layer");
    requireResult(layers.addLayer(rockHeight,0.8,1.2,0.05),"GTS rock height layer");
    gtsHeightSplat=eve.TerrainSplatmap();
    requireResult(eve.applyGtsHeightBlend(gtsHeightSplat,input,layers,0.18),"GTS layer height blend");
    print("TERRAIN_GTS_HEIGHT_BLEND_PASS layers=3 transition=0.18\n");
}

function bakeGtsLayerTextures(splat, albedo, normal, heights) {
    local layers=eve.GtsPackedLayerSet();
    local colors=[[0.52,0.38,0.20],[0.18,0.48,0.16],[0.42,0.44,0.46]];
    for(local i=0;i<3;++i) {
        local a=retain(eve.Image().newEmptyImageData(2,2,"RGBA8"));
        local n=retain(eve.Image().newEmptyImageData(2,2,"RGBA8"));
        for(local z=0;z<2;++z) for(local x=0;x<2;++x) {
            local grain=0.88+0.12*((x+z+i)%2);a.setPixel(x,z,colors[i][0]*grain,colors[i][1]*grain,colors[i][2]*grain,0.3+0.25*i);
            n.setPixel(x,z,0.47+0.04*x,0.47+0.04*z,0.72+0.08*i,0.24+0.22*i);
        }
        local s=eve.GtsPackedLayerSettings();s.tileSizeX=6.0+5.0*i;s.tileSizeZ=7.0+4.0*i;
        s.normalStrength=0.35+0.15*i;s.geoAmount=0.25+0.3*i;s.detailAmount=0.45+0.2*i;
        s.displacementContrast=0.8+0.1*i;s.displacementBrightness=0.10+0.03*i;s.tessellationAmount=8.0+4.0*i;
        if(i==1)s.stochastic=true;if(i==2){s.triPlanar=true;s.triPlanarSizeX=0.8;s.triPlanarSizeZ=1.2;}
        requireResult(layers.addLayer(a,n,s),"GTS packed layer");
    }
    gtsGeoStrength=heightmap(32,32);gtsLayerDetailStrength=heightmap(32,32);
    requireResult(eve.bakeGtsPackedLayers(albedo,normal,gtsGeoStrength,gtsLayerDetailStrength,heights,splat,layers,78.0,0.0,0.0,2.0,2.0),"GTS packed layers");
    print("TERRAIN_GTS_PACKED_LAYERS_PASS layers=3 outputs=4\n");
    print("TERRAIN_GTS_LAYER_PROJECTION_PASS triplanar=1 stochastic=1\n");
    gtsLayerDisplacement=heightmap(32,32);gtsLayerTessellation=heightmap(32,32);
    requireResult(eve.bakeGtsPackedLayerDisplacement(gtsLayerDisplacement,gtsLayerTessellation,heights,splat,layers,137.0,96.0,151.0,1.0,78.0,0.0,0.0,2.0,2.0),"GTS layer displacement");
    print("TERRAIN_GTS_LAYER_DISPLACEMENT_PASS top=4 outputs=2\n");
}

function showTerrain(heights, offset, splatAlbedo=null, packedNormalTexture=null, heightTexture=null) {
    local layers = retain(procgen.analyzeTerrain(heights, 200.0, 0.12, 0.4));
    for (local y=0; y<2; ++y) for (local x=0; x<2; ++x) {
        local chunk = retain(procgen.buildTerrainChunk(heights, layers, x*32, y*32,
            32, 32, 0, 1.0, 28.0, 0.0));
        local mesh = retain(procgen.generateTerrainChunkMesh(chunk, gfx));
        local image = splatAlbedo!=null ? splatAlbedo : retain(procgen.generateTerrainAlbedoMap(chunk));
        local texture = retain(gfx.newTexture(image, false, false));
        local entity = retain(eve.Renderable3D());
        entity.setMesh(mesh);
        entity.setTexture(texture);
        if(packedNormalTexture!=null) {
            entity.setNormalTexture(packedNormalTexture);entity.setPackedNormalMask(true);
        }
        if(heightTexture!=null) { entity.setHeightTexture(heightTexture);entity.setParallax(0.045,8.0,24.0); }
        entity.setTint(1.0, 1.0, 1.0, 1.0);
        entity.setMetallic(0.0);
        entity.setRoughness(0.9);
        entity.setPosition(offset+x*32.0, 0.0, y*32.0);
    }
}

function showTree(mesh, x, y, z, yaw, scaleX, scaleY, scaleZ, red, green, blue) {
    local renderable=retain(eve.Renderable3D());
    renderable.setMesh(mesh);
    if(treeWindShader!=null) renderable.setShader(treeWindShader);
    renderable.setPosition(x,y,z);renderable.setYaw(yaw);
    renderable.setScale(scaleX,scaleY,scaleZ);
    renderable.setTint(red,green,blue,1.0);renderable.setRoughness(0.88);renderable.setCastShadow(true);
}

function buildTerrainTreeMesh() {
    local paramsResult=procgen.newParams();
    if(!paramsResult.ok) throw "terrain-stamping: tree parameter creation failed";
    local params=paramsResult.value;
    params.setSeed(73);params.setString("style","lowpoly");params.setString("branchAlgorithm","weberPenn");
    params.setString("leafMode","clusters");params.setFloat("leafDensity",0.45);params.setFloat("height",5.5);
    params.setFloat("crownRadius",1.8);params.setInt("branchLevels",2);params.setInt("branchCount",5);
    params.setFloat("clusterSize",0.28);params.setFloat("clusterSeparation",0.58);params.setFloat("clusterLeafScale",0.78);
    params.setInt("clusterPlanes",8);params.setInt("clusterLeaves",20);params.setInt("clusterLimit",72);
    local generated=procgen.generateMesh("mesh.tree",params,gfx);
    if(!generated.ok) throw "terrain-stamping: tree mesh failed";
    retained.append(generated.value);
    sharedTreeMesh=generated.value;
}

function setupTerrainTreeWind(bendFactor) {
    treeWindShader=retain(gfx.newTreeWindShader());treeWindProfile=retain(eve.TreeWindProfile());
    treeWindProfile.setDimensions(3.6,5.5);treeWindProfile.bendFactor=bendFactor;
}

function setupTerrainCamera(treePoints) {
    camera=retain(eve.Camera3D());
    camera.setEye(137.0,96.0,151.0);camera.setTarget(71.0,5.0,32.0);camera.setUp(0.0,1.0,0.0);
    camera.setFov(48.0);camera.setClipPlanes(0.1,500.0);camera.setAmbient(0.3,0.34,0.4);camera.setActive(true);
    pcg_validation.closeView <- function() { camera.setEye(112.0,10.0,73.0);camera.setTarget(108.0,5.0,54.0); };
    pcg_validation.treeView <- function() {
        local x=treePoints.getX(0),y=treePoints.getY(0),z=treePoints.getZ(0);
        camera.setEye(x+11.0,y+7.0,z+11.0);camera.setTarget(x,y+3.0,z);
    };
}

function setupTerrainWind() {
    windState=retain(eve.VegetationWindState());windProfile=retain(eve.VegetationWindProfile());
    requireResult(eve.initializeVegetationWind(windState,1.0,0.0,0.2,1.0),"instant wind initialization");
    pcg_validation.windTime <- function(seconds, enabled) {
        windState.setDirection(1.0,-0.5,0.2);windState.strength=1.0;windState.phase=seconds*0.1;
        windProfile.enabled=enabled;
        requireResult(eve.applyGrassWind(terrainGrass.getShader(),windState,windProfile,seconds),"wind snapshot");
        if(treeWindShader!=null) requireResult(eve.applyTreeWind(treeWindShader,windState,windProfile,treeWindProfile,seconds),"tree wind snapshot");
    };
    pcg_validation.freezeWind <- function(seconds, enabled) { windPaused=true;pcg_validation.windTime(seconds,enabled); };
    pcg_validation.windTime(0.0,true);
    windProfile.maximumDistance=0.0;
    local badWind=eve.applyGrassWind(terrainGrass.getShader(),windState,windProfile,0.0);
    if(badWind.ok) throw "invalid wind accepted";
    windProfile.maximumDistance=100.0;
    local windPcm=retain(eve.Sound().newSoundDataFromFile("wind.wav"));
    windAudioSource=retain(eve.Audio().newSource(windPcm));
    windAudioSource.setLooping(true);windAudioSource.setRelative(true);
    windAudioSource.setAttenuationDistances(1.0,100000.0);windAudioSource.setVolume(0.0);windAudioSource.play();
    windAudioState=retain(eve.VegetationWindAudioState());
    requireResult(eve.advanceVegetationWindAudio(windAudioState,1.0,5.0,1.0,true,true),"wind audio initialize");
    windAudioSource.setVolume(windAudioState.volume);
    if(!windAudioSource.isLooping() || !windAudioSource.isPlaying() || windAudioSource.getVolume()!=0.2)
        throw "wind audio source mismatch";
    print("TERRAIN_WIND_PASS audio="+windAudioSource.getVolume()+" looping=1\n");
}

function validateTerraceRemoval(source) {
    local classes=heightmap(source.getWidth(),source.getHeight());
    local output=heightmap(source.getWidth(),source.getHeight());
    local settings=eve.TerrainTerraceRemovalSettings();
    settings.noiseSeed=73;settings.perlinStrength=0.002;settings.terrainWorkflow=true;
    requireResult(eve.analyzeTerrainTerraces(classes,source,settings),"terrace classification");
    local changed=requireResult(eve.removeTerrainTerraces(output,source,classes,settings),"terrace removal");
    if(changed<=0) throw "terrace removal produced no filtered samples";
    print("TERRAIN_TERRACE_REMOVAL_PASS changed="+changed+"\n");
}

function validateVelocityFlow(source) {
    local output=heightmap(source.getWidth(),source.getHeight());
    requireResult(eve.generateTerrainVelocityFlowMap(output,source,6),"heightmap velocity flow");
    local peak=0.0;
    for(local y=0;y<output.getHeight();++y)for(local x=0;x<output.getWidth();++x) {
        local value=output.height(x,y);if(value>peak)peak=value;
    }
    if(peak<0.999) throw "velocity flow normalization mismatch";
    print("TERRAIN_VELOCITY_FLOW_PASS peak="+peak+"\n");
}

function validateDerivedMaps(source) {
    local curvature=heightmap(source.getWidth(),source.getHeight());
    local aspect=heightmap(source.getWidth(),source.getHeight());
    requireResult(eve.generateTerrainHeightmapCurvature(curvature,source,0),"heightmap average curvature");
    requireResult(eve.generateTerrainHeightmapAspect(aspect,source,0),"heightmap aspect");
    local centerCurvature=curvature.height(32,32),centerAspect=aspect.height(32,32);
    if(centerCurvature<0.0 || centerCurvature>1.0 || centerAspect<0.0 || centerAspect>1.0)
        throw "derived heightmap range mismatch";
    print("TERRAIN_DERIVED_MAP_PASS curvature="+centerCurvature+" aspect="+centerAspect+"\n");
}

function validateNeighborhoodFilters(source) {
    local denoised=heightmap(source.getWidth(),source.getHeight());
    local grown=heightmap(source.getWidth(),source.getHeight());
    local shrunk=heightmap(source.getWidth(),source.getHeight());
    requireResult(eve.filterTerrainHeightmapNeighborhood(denoised,source,1,0),"heightmap denoise");
    requireResult(eve.filterTerrainHeightmapNeighborhood(grown,source,1,1),"heightmap grow edges");
    requireResult(eve.filterTerrainHeightmapNeighborhood(shrunk,source,1,2),"heightmap shrink edges");
    print("TERRAIN_NEIGHBORHOOD_FILTER_PASS denoise="+denoised.height(32,32)+
          " grow="+grown.height(32,32)+" shrink="+shrunk.height(32,32)+"\n");
}

function validateHeightmapFilters(source) {
    local smooth=heightmap(source.getWidth(),source.getHeight());
    local radius=heightmap(source.getWidth(),source.getHeight());
    local convolved=heightmap(source.getWidth(),source.getHeight());
    local kernel=heightmap(3,3);
    for(local y=0;y<3;++y)for(local x=0;x<3;++x)kernel.setHeight(x,y,1.0);
    requireResult(eve.smoothTerrainHeightmap(smooth,source,2),"heightmap smooth");
    requireResult(eve.smoothTerrainHeightmapRadius(radius,source,5),"heightmap smooth radius");
    requireResult(eve.convolveTerrainHeightmap(convolved,source,kernel),"heightmap convolve");
    print("TERRAIN_HEIGHTMAP_FILTER_PASS smooth="+smooth.height(32,32)+
          " radius="+radius.height(32,32)+" convolve="+convolved.height(32,32)+"\n");
}

function validateSlopeAndQuantize(source) {
    local slope=heightmap(source.getWidth(),source.getHeight());
    local quantized=heightmap(source.getWidth(),source.getHeight());
    requireResult(eve.generateTerrainHeightmapSlope(slope,source),"heightmap slope");
    requireResult(eve.quantizeTerrainHeightmap(quantized,source,0.025),"heightmap quantize");
    print("TERRAIN_SLOPE_QUANTIZE_PASS slope="+slope.height(32,32)+
          " quantized="+quantized.height(32,32)+"\n");
}

function validateHeightmapArithmetic(source) {
    local scalar=heightmap(source.getWidth(),source.getHeight());
    local raster=heightmap(source.getWidth(),source.getHeight());
    local blended=heightmap(source.getWidth(),source.getHeight());
    local mask=heightmap(1,1);mask.setHeight(0,0,0.35);
    requireResult(eve.applyTerrainHeightmapScalarArithmetic(scalar,source,0.04,0,true,0.0,1.0),"heightmap scalar add");
    requireResult(eve.applyTerrainHeightmapRasterArithmetic(raster,scalar,source,1,true,0.0,1.0),"heightmap raster subtract");
    requireResult(eve.lerpTerrainHeightmap(blended,source,scalar,mask),"heightmap lerp");
    print("TERRAIN_HEIGHTMAP_ARITHMETIC_PASS scalar="+scalar.height(32,32)+
          " raster="+raster.height(32,32)+" lerp="+blended.height(32,32)+"\n");
}

function validateHeightmapTransforms(source) {
    local normalized=heightmap(source.getWidth(),source.getHeight());
    local powered=heightmap(source.getWidth(),source.getHeight());
    requireResult(eve.transformTerrainHeightmap(normalized,source,1,0.0),"heightmap normalize");
    requireResult(eve.transformTerrainHeightmap(powered,source,2,1.5),"heightmap power");
    print("TERRAIN_HEIGHTMAP_TRANSFORM_PASS normalized="+normalized.height(32,32)+
          " powered="+powered.height(32,32)+"\n");
}

function validateHeightmapCopy(source) {
    local copied=heightmap(33,33);
    local clamped=heightmap(33,33);
    for(local y=0;y<33;++y)for(local x=0;x<33;++x)copied.setHeight(x,y,0.18);
    requireResult(eve.copyTerrainHeightmap(copied,source,2),"heightmap copy greater");
    requireResult(eve.copyTerrainHeightmapClamped(clamped,source,0.14,0.20),"heightmap copy clamped");
    print("TERRAIN_HEIGHTMAP_COPY_PASS conditional="+copied.height(16,16)+
          " clamped="+clamped.height(16,16)+"\n");
}

function validateHeightmapFlip() {
    local source=heightmap(3,2),flipped=heightmap(1,1);
    local value=1.0;
    for(local y=0;y<2;++y)for(local x=0;x<3;++x){source.setHeight(x,y,value);value+=1.0;}
    requireResult(eve.flipTerrainHeightmap(flipped,source),"heightmap flip");
    if(flipped.getWidth()!=2 || flipped.getHeight()!=3 || flipped.height(1,2)!=6.0)
        throw "heightmap transpose mismatch";
    print("TERRAIN_HEIGHTMAP_FLIP_PASS size="+flipped.getWidth()+"x"+flipped.getHeight()+" tail="+flipped.height(1,2)+"\n");
}

function validateHeightmapMeasure(source) {
    local minimum=requireResult(eve.measureTerrainHeightmap(source,0),"heightmap minimum");
    local maximum=requireResult(eve.measureTerrainHeightmap(source,1),"heightmap maximum");
    local average=requireResult(eve.measureTerrainHeightmap(source,3),"heightmap average");
    local baseLevel=requireResult(eve.measureTerrainHeightmap(source,4),"heightmap base level");
    if(minimum>maximum || average<minimum || average>maximum)throw "heightmap statistics mismatch";
    print("TERRAIN_HEIGHTMAP_MEASURE_PASS min="+minimum+" max="+maximum+" avg="+average+" base="+baseLevel+"\n");
}

function validateTerraceQuantize(source) {
    local output=heightmap(source.getWidth(),source.getHeight());
    local starts=heightmap(2,1),curves=heightmap(3,2);
    starts.setHeight(0,0,0.0);starts.setHeight(1,0,0.18);
    curves.setHeight(0,0,0.0);curves.setHeight(1,0,0.2);curves.setHeight(2,0,1.0);
    curves.setHeight(0,1,0.0);curves.setHeight(1,1,0.8);curves.setHeight(2,1,1.0);
    requireResult(eve.quantizeTerrainHeightmapTerraces(output,source,starts,curves),"heightmap terrace curves");
    print("TERRAIN_TERRACE_QUANTIZE_PASS center="+output.height(32,32)+"\n");
}

function validateSlopeQueries(source) {
    local grid=requireResult(eve.measureTerrainHeightmapSlope(source,32.0,32.0,0),"grid slope query");
    local central=requireResult(eve.measureTerrainHeightmapSlope(source,0.5,0.5,1),"central slope query");
    local average=requireResult(eve.measureTerrainHeightmapSlope(source,0.5,0.5,2),"average slope query");
    print("TERRAIN_SLOPE_QUERY_PASS grid="+grid+" central="+central+" average="+average+"\n");
}

function validateHeightmapWrites() {
    local target=heightmap(3,2),row=heightmap(2,1),column=heightmap(3,1);
    row.setHeight(0,0,0.1);row.setHeight(1,0,0.2);
    column.setHeight(0,0,0.3);column.setHeight(1,0,0.4);column.setHeight(2,0,0.5);
    requireResult(eve.fillTerrainHeightmap(target,2.0),"heightmap fill");
    requireResult(eve.setTerrainHeightmapSafe(target,-1,9,0.25),"heightmap safe set");
    requireResult(eve.setTerrainHeightmapRow(target,1,row),"heightmap row");
    requireResult(eve.setTerrainHeightmapColumn(target,0,column),"heightmap column");
    print("TERRAIN_HEIGHTMAP_WRITE_PASS corner="+target.height(0,1)+" row="+target.height(1,1)+" column="+target.height(2,0)+"\n");
    requireResult(eve.resetTerrainHeightmap(target),"heightmap reset");
    if(target.getWidth()!=0 || target.getHeight()!=0)throw "heightmap reset mismatch";
}

function validatePcgRawLoader() {
    local loaded=procgen.loadTerrainBytes("ABCD","raw8");
    local map=retain(requireResult(loaded,"Pcg RAW8 bytes"));
    if(loaded.format!="raw8" || loaded.width!=2 || loaded.height!=2 || map.height(1,0)!=(67.0/255.0))
        throw "Pcg RAW traversal mismatch";
    print("TERRAIN_PCG_RAW_PASS format="+loaded.format+" size="+loaded.width+"x"+loaded.height+
          " sample="+map.height(1,0)+"\n");
}

function validateLegacyHydraulic() {
    local heights=heightmap(3,1),sediment=heightmap(3,1),hardness=heightmap(3,1),rain=heightmap(3,1);
    heights.setHeight(1,0,1.0);rain.setHeight(1,0,1.0);
    local settings=eve.TerrainLegacyHydraulicSettings();settings.sedimentDissolveRate=1.0;
    requireResult(heights.applyLegacyHydraulic(sediment,hardness,rain,settings),"legacy hydraulic erosion");
    if(heights.height(1,0)!=0.6 || sediment.height(0,0)!=0.2 || sediment.height(1,0)!=-0.4)
        throw "legacy hydraulic result mismatch";
    print("TERRAIN_LEGACY_HYDRAULIC_PASS center="+heights.height(1,0)+" sediment="+sediment.height(1,0)+"\n");
}

function validateLegacyThermal() {
    local distributed=heightmap(3,3),steepest=heightmap(3,3),hardness=heightmap(1,1);
    distributed.setHeight(1,1,1.0);steepest.setHeight(1,1,1.0);
    local a=eve.TerrainLegacyDistributedErosionSettings();a.minimumThreshold=0.1;
    requireResult(distributed.applyLegacyDistributedErosion(a),"legacy distributed erosion");
    local b=eve.TerrainLegacySteepestErosionSettings();
    requireResult(steepest.applyLegacySteepestErosion(hardness,b),"legacy steepest erosion");
    if(distributed.height(1,1)!=0.5 || distributed.height(0,1)!=0.125 ||
       steepest.height(1,1)!=0.5 || steepest.height(1,2)!=0.5) throw "legacy thermal mismatch";
    print("TERRAIN_LEGACY_THERMAL_PASS distributed="+distributed.height(0,1)+
          " steepest="+steepest.height(1,2)+"\n");
}

function validateHeightmapAccess() {
    local map=heightmap(2,2);map.setHeight(1,0,1.0);map.setHeight(0,1,2.0);map.setHeight(1,1,3.0);
    local safe=requireResult(eve.sampleTerrainHeightmapSafe(map,-3,8),"safe heightmap sample");
    local normalized=requireResult(eve.sampleTerrainHeightmapNormalized(map,0.25,0.25),"normalized heightmap sample");
    if(safe!=2.0 || normalized!=1.5 || !requireResult(map.hasData(),"heightmap data query") ||
       !requireResult(map.isPowerOfTwo(),"heightmap power-of-two query"))
        throw "heightmap access mismatch";
    print("TERRAIN_HEIGHTMAP_ACCESS_PASS safe="+safe+" normalized="+normalized+" power2=1\n");
}

eve_init = function() {
    validateTerrainImageMaskAdapter();
    gfx.setBackgroundColor(0.055, 0.075, 0.10, 1.0);
    gfx.setDirectionalLight(-0.45, 0.85, 0.35, 1.6, 1.48, 1.25);
    local sourceTerrain = heightmap(65,65);
    local processed = heightmap(65,65);
    local stamp = heightmap(33,33);
    local mask = heightmap(65,65);
    local sediment = heightmap(65,65);
    local curve = heightmap(2,1);
    local reverse = heightmap(2,1);
    local one = heightmap(1,1);
    curve.setHeight(0,0,0.0); curve.setHeight(1,0,1.0);
    reverse.setHeight(0,0,1.0); reverse.setHeight(1,0,0.0);
    one.setHeight(0,0,1.0);
    for (local y=0; y<65; ++y) for (local x=0; x<65; ++x) {
        local h = 0.16 + 0.07*sin(x*0.095)*cos(y*0.075) + 0.018*sin((x+y)*0.3);
        sourceTerrain.setHeight(x,y,h);
        processed.setHeight(x,y,h);
    }
    local legacyFlow=heightmap(65,65),flowSettings=eve.TerrainWaterFlowMapSettings();
    flowSettings.dropletVolume=0.12;flowSettings.absorptionRate=0.04;flowSettings.smoothIterations=1;
    requireResult(eve.generateTerrainWaterFlowMap(legacyFlow,sourceTerrain,flowSettings),"legacy water flow map");
    local flowSum=0.0;for(local fy=0;fy<65;++fy)for(local fx=0;fx<65;++fx)flowSum+=legacyFlow.height(fx,fy);
    if(flowSum<=0.0)throw "empty legacy water flow map";
    print("TERRAIN_WATER_FLOW_MAP_PASS sum="+flowSum+"\n");
    validateVelocityFlow(sourceTerrain);
    validateDerivedMaps(sourceTerrain);
    validateNeighborhoodFilters(sourceTerrain);
    validateHeightmapFilters(sourceTerrain);
    validateSlopeAndQuantize(sourceTerrain);
    validateHeightmapArithmetic(sourceTerrain);
    validateHeightmapTransforms(sourceTerrain);
    validateHeightmapCopy(sourceTerrain);
    validateHeightmapFlip();
    validateHeightmapMeasure(sourceTerrain);
    validateTerraceQuantize(sourceTerrain);
    validateSlopeQueries(sourceTerrain);
    validateHeightmapWrites();
    validatePcgRawLoader();
    validateLegacyHydraulic();
    validateLegacyThermal();
    validateHeightmapAccess();
    validateTerraceRemoval(sourceTerrain);
    for (local y=0; y<33; ++y) for (local x=0; x<33; ++x) {
        local u=(x-16)/16.0, v=(y-16)/16.0;
        local h=exp(-3.5*(u*u+v*v))*(0.8+0.2*cos(u*13.0)*cos(v*10.0));
        stamp.setHeight(x,y,h);
    }
    local distance = eve.TerrainDistanceMaskSettings();
    requireResult(mask.distanceMask(distance,0,reverse,curve),"distance mask");
    local collisionOutput=heightmap(65,65),collisionStack=eve.TerrainCollisionMaskStack();
    requireResult(collisionStack.addLayer(one,0,true,false),"collision radius layer");
    requireResult(collisionStack.apply(collisionOutput,mask,curve,0),"collision mask stack");
    if(collisionStack.getLayerCount()!=1) throw "collision mask layer mismatch";
    print("TERRAIN_COLLISION_MASK_PASS layers="+collisionStack.getLayerCount()+"\n");
    local polygonOutput=heightmap(65,65),polygonBrush=heightmap(1,1),polygon=eve.TerrainPolygonMask();
    requireResult(polygon.addNode(10.0,10.0,0.5,1.0),"polygon node 0");
    requireResult(polygon.addNode(20.0,10.0,0.5,1.0),"polygon node 1");
    requireResult(polygon.addNode(15.0,20.0,0.5,1.0),"polygon node 2");
    requireResult(polygon.rasterize(polygonOutput,polygonBrush,0.0,0.0,1.0,1.0,1),"closed polygon mask");
    if(polygonOutput.height(15,15)!=1.0) throw "polygon flood fill mismatch";
    print("TERRAIN_POLYGON_MASK_PASS nodes="+polygon.getNodeCount()+"\n");
    local biomeMaskCache=eve.TerrainBakedMaskCache(),cachedBiomeMask=heightmap(17,17);
    requireResult(biomeMaskCache.store("terrain-main","biome-guid",polygonOutput),"cache world biome mask");
    requireResult(biomeMaskCache.copyMask("terrain-main","biome-guid",cachedBiomeMask),"copy world biome mask");
    if(requireResult(biomeMaskCache.markDirty("biome-guid"),"dirty world biome mask")!=1) throw "biome dirty mismatch";
    if(biomeMaskCache.copyMask("terrain-main","biome-guid",cachedBiomeMask).ok) throw "dirty biome mask remained readable";
    requireResult(biomeMaskCache.store("terrain-main","biome-guid",polygonOutput),"rebuild world biome mask");
    print("TERRAIN_WORLD_BIOME_MASK_PASS entries="+biomeMaskCache.getEntryCount()+"\n");
    local globalSpawnerMask=heightmap(65,65),globalMaskSettings=eve.TerrainImageMaskSettings();
    requireResult(globalSpawnerMask.applyGlobalSpawnerMask(mask,cachedBiomeMask,curve,globalMaskSettings,0),
        "global spawner mask output");
    print("TERRAIN_GLOBAL_SPAWNER_MASK_PASS\n");
    local noiseMask=heightmap(65,65),noiseSettings=eve.TerrainNoiseMaskSettings();
    noiseSettings.octaves=3.5;noiseSettings.warpIterations=1.5;noiseSettings.warpStrength=0.2;noiseSettings.seed=42;
    requireResult(noiseMask.generateNoiseMask(mask,curve,noiseSettings,0,0),"terrain noise mask");
    print("TERRAIN_NOISE_MASK_PASS types=5\n");
    local smoothMask=heightmap(65,65);
    requireResult(smoothMask.smoothMask(noiseMask,0.0,1.0),"terrain smooth mask");
    print("TERRAIN_SMOOTH_MASK_PASS passes=2\n");
    local settings = eve.TerrainStampSettings();
    settings.setCenter(32.0,32.0);
    settings.setSize(54.0,44.0);
    settings.setRotation(0.38);
    settings.amplitude=0.68;
    settings.baseHeight=0.05;
    local generation=eve.TerrainGenerationSession();
    requireResult(generation.reset(processed),"generation baseline");
    requireResult(generation.stamp(stamp,settings,0,one,one),"record rotated stamp");
    requireResult(generation.undo(),"generation undo");
    requireResult(generation.redo(),"generation redo");
    requireResult(generation.contrast(mask,2.0,1.5),"contrast");
    local terrace = eve.TerrainTerraceSettings();
    terrace.count=30.0; terrace.bevel=0.6; terrace.strength=0.65;
    requireResult(generation.terrace(mask,terrace),"terrace");
    local smooth = eve.TerrainSmoothSettings();
    smooth.radius=0.4; smooth.strength=0.3;
    requireResult(generation.smooth(mask,smooth),"smooth");
    local thermal = eve.TerrainThermalSettings();
    thermal.heightScale=28.0; thermal.reposeSlope=0.7;
    thermal.dt=0.01; thermal.iterations=8;
    requireResult(generation.thermal(thermal),"record thermal erosion");
    requireResult(generation.undo(),"undo thermal erosion");
    requireResult(generation.redo(),"redo thermal erosion");
    if(requireResult(generation.beginReplay(),"generation staged begin")!=1) throw "staged replay not pending";
    if(requireResult(generation.stepReplay(2),"generation staged step")!=1) throw "staged replay ended early";
    if(requireResult(generation.cancelReplay(),"generation staged cancel")!=3) throw "staged replay not cancelled";
    requireResult(generation.beginReplay(),"generation staged restart");
    if(requireResult(generation.stepReplay(100),"generation staged complete")!=2) throw "staged replay incomplete";
    requireResult(generation.copyTerrain(processed),"generation terrain output");
    requireResult(generation.copySediment(sediment),"generation sediment output");
    local generationJson=requireResult(generation.snapshotJson(),"generation snapshot");
    local restoredGeneration=eve.TerrainGenerationSession();
    requireResult(restoredGeneration.restoreJson(generationJson),"generation restore");
    requireResult(restoredGeneration.copyTerrain(processed),"restored generation terrain");
    requireResult(restoredGeneration.copySediment(sediment),"restored generation sediment");
    if(restoredGeneration.getOperationCount()!=generation.getOperationCount()) throw "generation history restore mismatch";
    print("TERRAIN_SESSION_PASS operations="+generation.getOperationCount()+" staged=1 persisted=1\n");
    local tileWest=heightmap(3,3),tileEast=heightmap(3,3),tileStamp=heightmap(1,1),tileOne=heightmap(1,1);
    tileStamp.setHeight(0,0,0.75);tileOne.setHeight(0,0,1.0);
    local multi=eve.TerrainMultiTileWorkspace();
    requireResult(multi.addTile("west",tileWest,0.0,0.0,2.0,2.0,false),"multi-tile west");
    requireResult(multi.addTile("east",tileEast,2.0,0.0,2.0,2.0,false),"multi-tile east");
    local multiStamp=eve.TerrainStampSettings();multiStamp.setCenter(2.0,1.0);multiStamp.setSize(2.0,2.0);
    requireResult(multi.stamp(tileStamp,multiStamp,3,tileOne,tileOne,false),"multi-tile stamp");
    if(multi.getLastAffectedTiles()!=2 || multi.getLastMappingCount()!=2) throw "multi-tile mapping mismatch";
    requireResult(multi.undo(),"multi-tile undo");requireResult(multi.redo(),"multi-tile redo");
    print("TERRAIN_MULTITILE_PASS tiles="+multi.getLastAffectedTiles()+"\n");
    local worldConfig=eve.TerrainWorldCreationSettings();worldConfig.tilesX=2;worldConfig.tilesZ=1;
    worldConfig.tileSize=2.0;worldConfig.tileHeight=8.0;worldConfig.heightmapResolution=3;
    worldConfig.controlTextureResolution=2;worldConfig.detailResolution=2;worldConfig.treeResolution=2;
    worldConfig.objectResolution=2;worldConfig.splatLayers=2;worldConfig.nameSuffix="runtime";
    local world=eve.TerrainWorldWorkspace();requireResult(world.create(worldConfig),"world create");
    local worldBounds=eve.TerrainStampSettings();worldBounds.setCenter(0.0,0.0);worldBounds.setSize(4.0,2.0);
    requireResult(world.stamp(tileStamp,worldBounds,tileOne,tileOne),"world stamp");
    if(world.getTileCount()!=2 || world.getOperationCount()!=1 || world.getTileName(0).value!="Terrain_0_0-runtime") throw "world topology mismatch";
    requireResult(world.undo(),"world undo");requireResult(world.redo(),"world redo");
    requireResult(world.flatten(),"world flatten");requireResult(world.undo(),"world flatten undo");
    local clearWorld=eve.TerrainWorldClearSettings();
    requireResult(world.clearSpawns(clearWorld),"world clear spawns");requireResult(world.undo(),"world clear undo");
    local worldFitness=heightmap(4,2);for(local wz=0;wz<2;++wz) for(local wx=0;wx<4;++wx) worldFitness.setHeight(wx,wz,1.0);
    local worldDetail=eve.TerrainDetailSettings();worldDetail.minimumFitness=0.0;worldDetail.fadeStart=0.0;
    worldDetail.density=2.0;worldDetail.namespaceId=4201;
    local worldProbe=eve.TerrainProbePlacementSettings();worldProbe.name="runtime-reflection";
    worldProbe.spacing=1.0;worldProbe.jitterPercent=0.0;worldProbe.minimumFitness=0.0;
    worldProbe.namespaceId=4202;worldProbe.maxPoints=100;
    local spawnPlan=eve.TerrainSpawnPlan();
    requireResult(spawnPlan.addModifierStamp("runtime-modifier",tileStamp,worldBounds,3,tileOne,tileOne),"spawn plan modifier stamp");
    requireResult(spawnPlan.addSplat("runtime-texture",worldFitness,1,worldBounds),"spawn plan texture");
    requireResult(spawnPlan.addDetail("runtime-detail",worldFitness,worldDetail,worldBounds,0,29),"spawn plan detail");
    requireResult(spawnPlan.addProbes("runtime-probe",worldFitness,worldProbe,worldBounds,0,0),"spawn plan probe");
    local spawnPlanJson=requireResult(spawnPlan.snapshotJson(),"spawn plan snapshot");
    local restoredPlan=eve.TerrainSpawnPlan();requireResult(restoredPlan.restoreJson(spawnPlanJson),"spawn plan restore");
    local biomePreset=eve.TerrainBiomePreset();
    requireResult(biomePreset.addSpawner("runtime-forest",restoredPlan,true,false,true),"biome preset add");
    local biomeJson=requireResult(biomePreset.snapshotJson(),"biome preset snapshot");
    local restoredBiome=eve.TerrainBiomePreset();requireResult(restoredBiome.restoreJson(biomeJson),"biome preset restore");
    if(requireResult(world.beginSpawnBiome(restoredBiome),"world biome staged begin")!=1) throw "world spawn not pending";
    if(requireResult(world.stepSpawn(1),"world biome staged step")!=1) throw "world spawn ended early";
    if(requireResult(world.cancelSpawn(),"world biome staged cancel")!=3) throw "world spawn not cancelled";
    requireResult(world.beginSpawnBiome(restoredBiome),"world biome staged restart");
    if(requireResult(world.stepSpawn(100),"world biome staged complete")!=2) throw "world spawn incomplete";
    local worldProbes=retain(requireResult(procgen.newPointSet(),"world probe output"));
    if(requireResult(world.copyProbes(0,worldProbes),"world probe copy")<=0) throw "world probes missing";
    local publishedProbeCount=requireResult(eve.publishTerrainProbes("pcg-world-probes",worldProbes),"probe publish");
    if(eve.getTerrainProbeBatchCount("pcg-world-probes")!=publishedProbeCount ||
       eve.getTerrainReflectionProbeCount("pcg-world-probes")!=publishedProbeCount ||
       eve.getTerrainLightProbeCount("pcg-world-probes")!=0) throw "probe provider mismatch";
    requireResult(eve.tickTerrainProbeBatches(1,0,64),"probe capture tick");
    if(requireResult(eve.removeTerrainProbeBatch("pcg-world-probes"),"probe remove")!=publishedProbeCount ||
       eve.getTerrainProbeBatchCount("pcg-world-probes")!=0) throw "probe provider removal mismatch";
    requireResult(eve.publishTerrainProbes("pcg-world-probes",worldProbes),"probe republish");
    print("TERRAIN_PROBE_PROVIDER_PASS probes="+publishedProbeCount+" captureFaces=1\n");
    print("TERRAIN_SPAWN_PLAN_PASS rules="+restoredPlan.getRuleCount()+" persisted=1 modifier=1 probe=1\n");
    print("TERRAIN_BIOME_PRESET_PASS spawners="+restoredBiome.getSpawnerCount()+" persisted=1 staged=1\n");
    print("TERRAIN_WORLD_PASS tiles="+world.getTileCount()+" operations="+world.getOperationCount()+"\n");
    local splatSource=eve.TerrainSplatmap(),splatOut=eve.TerrainSplatmap();
    requireResult(splatSource.initialize(2,2,3,0),"splat source");
    local splatPaint=heightmap(4,2);for(local sy=0;sy<2;++sy) for(local sx=0;sx<4;++sx) splatPaint.setHeight(sx,sy,sx<2?0.25:0.75);
    local splatBounds=eve.TerrainStampSettings();splatBounds.setCenter(2.0,1.0);splatBounds.setSize(4.0,2.0);
    local multiSplat=eve.TerrainMultiSplatWorkspace();
    requireResult(multiSplat.addTile("west",splatSource,0.0,0.0,2.0,2.0,false),"multi-splat west");
    requireResult(multiSplat.addTile("east",splatSource,2.0,0.0,2.0,2.0,false),"multi-splat east");
    requireResult(multiSplat.paint(splatPaint,1,splatBounds,false),"multi-splat paint");
    requireResult(multiSplat.copyTile("east",splatOut),"multi-splat copy");
    if(splatOut.sample(1,0,0).value!=0.75 || multiSplat.getLastChangedSamples()!=8) throw "multi-splat mapping mismatch";
    requireResult(multiSplat.undo(),"multi-splat undo");requireResult(multiSplat.redo(),"multi-splat redo");
    print("TERRAIN_MULTISPLAT_PASS tiles="+multiSplat.getTileCount()+"\n");
    local objectWest=retain(requireResult(procgen.newPointSet(),"multi-object west"));
    local objectEast=retain(requireResult(procgen.newPointSet(),"multi-object east"));
    local objectOut=retain(requireResult(procgen.newPointSet(),"multi-object output"));
    local objectHeights=heightmap(2,2);for(local oz=0;oz<2;++oz) for(local ox=0;ox<2;++ox) objectHeights.setHeight(ox,oz,0.2);
    local objectFitness=heightmap(4,2);for(local oz=0;oz<2;++oz) for(local ox=0;ox<4;++ox) objectFitness.setHeight(ox,oz,1.0);
    local objectBounds=eve.TerrainStampSettings();objectBounds.setCenter(146.0,3.0);objectBounds.setSize(4.0,2.0);
    local objectChild=eve.TerrainObjectInstanceSettings();objectChild.asset="pcg:terrain-object/rock";
    local objectRule=eve.TerrainObjectPlacementSettings();objectRule.spacing=1.0;objectRule.spawnDensity=1.0;
    objectRule.jitterPercent=0.0;objectRule.minimumFitness=0.0;objectRule.minimumInstanceFitness=0.0;
    objectRule.prototype="pcg-rocks";objectRule.namespaceId=94;objectRule.seed=73;objectRule.maxPoints=32;
    objectRule.boundsRadius=0.2;requireResult(objectRule.addInstance(objectChild),"multi-object child");
    local multiObject=eve.TerrainMultiObjectWorkspace();
    requireResult(multiObject.addTile("west",objectWest,objectHeights,144.0,2.0,2.0,2.0,2,2,false),"multi-object west");
    requireResult(multiObject.addTile("east",objectEast,objectHeights,146.0,2.0,2.0,2.0,2,2,false),"multi-object east");
    requireResult(multiObject.apply(objectFitness,objectRule,objectBounds,0,false),"multi-object add");
    local objectCount=requireResult(multiObject.copyTile("east",objectOut),"multi-object copy");
    if(objectCount<=0 || multiObject.getLastAffectedTiles()!=2) throw "multi-object transaction mismatch";
    requireResult(multiObject.undo(),"multi-object undo");requireResult(multiObject.redo(),"multi-object redo");
    print("TERRAIN_MULTIOBJECT_PASS points="+objectCount+"\n");
    local detailWest=eve.TerrainDetailLayer(),detailEast=eve.TerrainDetailLayer(),detailOut=eve.TerrainDetailLayer();
    requireResult(detailWest.reset(3,3,1),"multi-detail west");requireResult(detailEast.reset(3,3,1),"multi-detail east");
    requireResult(detailOut.reset(3,3,0),"multi-detail output");
    local detailWindow=heightmap(4,3);for(local dz=0;dz<3;++dz) for(local dx=0;dx<4;++dx) detailWindow.setHeight(dx,dz,1.0);
    local detailRule=eve.TerrainDetailSettings();detailRule.minimumFitness=0.0;detailRule.fadeStart=0.0;detailRule.density=4.0;
    local multiDetail=eve.TerrainMultiDetailWorkspace();
    requireResult(multiDetail.addTile("west",detailWest,0.0,0.0,2.0,2.0,false),"multi-detail add west");
    requireResult(multiDetail.addTile("east",detailEast,2.0,0.0,2.0,2.0,false),"multi-detail add east");
    requireResult(multiDetail.apply(detailWindow,detailRule,multiStamp,0,37,false),"multi-detail apply");
    requireResult(multiDetail.copyTile("east",detailOut),"multi-detail copy");
    if(multiDetail.getLastAffectedTiles()!=2 || detailOut.sample(0,1).value!=4) throw "multi-detail mapping mismatch";
    requireResult(multiDetail.undo(),"multi-detail undo");requireResult(multiDetail.redo(),"multi-detail redo");
    print("TERRAIN_MULTIDETAIL_PASS tiles="+multiDetail.getTileCount()+"\n");
    for (local y=0; y<65; ++y) for (local x=0; x<65; ++x) {
        if (processed.height(x,y)!=sourceTerrain.height(x,y)) ++pcg_validation.changed;
        pcg_validation.sediment += sediment.height(x,y);
    }
    local displaySplat=eve.TerrainSplatmap();requireResult(displaySplat.initialize(32,32,3,0),"display splat");
    local displayPaint=heightmap(32,32);for(local py=0;py<32;++py) for(local px=0;px<32;++px) {
        local ridge=0.15+0.75*abs(sin(px*0.19)*cos(py*0.13));displayPaint.setHeight(px,py,ridge);
    }
    requireResult(displaySplat.paint(displayPaint,1),"display splat vegetation");
    local displayRock=heightmap(32,32);for(local py=0;py<32;++py) for(local px=0;px<32;++px) {
        local dx=(px-16)/16.0,dy=(py-16)/16.0;displayRock.setHeight(px,py,0.45*exp(-3.0*(dx*dx+dy*dy)));
    }
    requireResult(displaySplat.paint(displayRock,2),"display splat rock");
    applyGtsLayerHeightBlend(displaySplat,displayPaint,displayRock);
    local terrainTextureMask=heightmap(32,32);
    requireResult(displaySplat.copyLayer(1,terrainTextureMask),"terrain texture mask source");
    if(terrainTextureMask.height(0,0)!=displaySplat.sample(1,0,0).value) throw "terrain texture mask mismatch";
    print("TERRAIN_TEXTURE_MASK_PASS layer=1\n");
    local splatImage=retain(eve.Image().newEmptyImageData(32,32,"RGBA8"));
    local weatherNormal=retain(eve.Image().newEmptyImageData(32,32,"RGBA8"));
    local weatherHeights=heightmap(32,32);
    for(local wz=0;wz<32;++wz) for(local wx=0;wx<32;++wx) weatherHeights.setHeight(wx,wz,processed.height(wx*2,wz*2)*28.0);
    bakeGtsLayerTextures(gtsHeightSplat,splatImage,weatherNormal,weatherHeights);
    print("TERRAIN_SPLAT_ALBEDO_PASS pixels=1024\n");
    gtsGlobalBlend=heightmap(32,32);
    requireResult(eve.generateGtsGlobalBlendDistance(gtsGlobalBlend,weatherHeights,137.0,96.0,151.0,0.1,1.0,78.0,0.0,0.0,2.0,2.0),"GTS global blend");
    print("TERRAIN_GTS_GLOBAL_BLEND_PASS camera=137,96,151\n");
    applyGtsColorMap(splatImage);
    applyGtsGeological(splatImage,weatherNormal,weatherHeights);
    applyGtsDetail(splatImage,weatherNormal);
    local snowAlbedo=retain(eve.Image().newEmptyImageData(2,2,"RGBA8"));
    snowAlbedo.setPixel(0,0,0.90,0.94,1.0,1.0);snowAlbedo.setPixel(1,0,0.76,0.86,0.96,1.0);
    snowAlbedo.setPixel(0,1,0.82,0.90,0.98,1.0);snowAlbedo.setPixel(1,1,0.94,0.97,1.0,1.0);
    local snowMask=retain(eve.Image().newEmptyImageData(1,1,"RGBA8"));snowMask.setPixel(0,0,1.0,0.5,0.5,1.0);
    local gtsSnow=eve.GtsSnowSurfaceSettings();gtsSnow.enabled=true;gtsSnow.minimumHeight=8.4;
    gtsSnow.blendRange=2.24;gtsSnow.slopeBlend=5.0;gtsSnow.scale=11.0;gtsSnow.colorR=0.92;gtsSnow.colorG=0.96;
    local gtsRain=eve.GtsRainSurfaceSettings();gtsRain.enabled=true;gtsRain.power=0.32;
    gtsRain.minimumHeight=0.0;gtsRain.maximumHeight=6.72;gtsRain.darkness=0.18;
    requireResult(eve.bakeGtsWeatherAlbedoDetailed(splatImage,weatherHeights,snowAlbedo,snowMask,gtsSnow,gtsRain,gtsDetailGreyscale,78.0,0.0,2.0,2.0),"GTS weather albedo");
    print("TERRAIN_GTS_WEATHER_PASS snow=1 rain=1 pixels=1024\n");
    applyGtsMacroVariation(splatImage);
    local weatherMask=retain(eve.Image().newEmptyImageData(32,32,"RGBA32F"));
    for(local pnz=0;pnz<32;++pnz) for(local pnx=0;pnx<32;++pnx) {
        weatherMask.setPixel(pnx,pnz,0.0,1.0,0.0,0.35);
    }
    local snowNormal=retain(eve.Image().newEmptyImageData(1,1,"RGBA32F"));snowNormal.setPixel(0,0,0.5,0.5,1.0,1.0);
    local rainData=retain(eve.Image().newEmptyImageData(2,2,"RGBA32F"));
    rainData.setPixel(0,0,0.17,0.43,0.71,0.91);rainData.setPixel(1,0,0.83,0.29,0.59,0.11);
    rainData.setPixel(0,1,0.37,0.97,0.23,0.67);rainData.setPixel(1,1,0.73,0.07,0.47,0.89);
    local weatherDisplacement=gtsLayerDisplacement,weatherTessellation=gtsLayerTessellation;
    requireResult(eve.bakeGtsWeatherPbr(weatherNormal,weatherMask,weatherDisplacement,weatherTessellation,weatherHeights,snowNormal,snowMask,rainData,gtsSnow,gtsRain,2.0,78.0,0.0,2.0,2.0),"GTS weather PBR");
    print("TERRAIN_GTS_PBR_PASS outputs=4 time=2\n");
    local weatherHeightImage=retain(eve.Image().newEmptyImageData(32,32,"RGBA8"));
    for(local whz=0;whz<32;++whz) for(local whx=0;whx<32;++whx) {
        local value=weatherDisplacement.height(whx,whz);if(value<0.0)value=0.0;if(value>1.0)value=1.0;
        weatherHeightImage.setPixel(whx,whz,value,value,value,1.0);
    }
    local weatherNormalTexture=retain(gfx.newTexture(weatherNormal,false,false));
    local weatherHeightTexture=retain(gfx.newTexture(weatherHeightImage,false,false));
    print("TERRAIN_GTS_MATERIAL_PASS packedNormalMask=1 parallax=1\n");
    showTerrain(sourceTerrain,0.0);
    showTerrain(processed,78.0,splatImage,weatherNormalTexture,weatherHeightTexture);
    local detail=eve.TerrainDetailLayer();
    requireResult(detail.reset(32,32,0),"detail layer");
    local fitness=heightmap(32,32);
    for(local z=0;z<32;++z) for(local x=0;x<32;++x) {
        local h=processed.height(x*2,z*2);
        fitness.setHeight(x,z,h>0.15 && h<0.31 ? 1.0 : 0.0);
    }
    local detailRule=eve.TerrainDetailSettings();detailRule.density=5.0;
    requireResult(detail.apply(fitness,detailRule,0,31),"detail density");
    local points=retain(requireResult(procgen.newPointSet(),"detail points"));
    local placement=eve.TerrainDetailPlacementSettings();
    placement.originX=78.0;placement.width=64.0;placement.depth=64.0;
    placement.heightScale=28.0;placement.minimumScale=0.8;placement.maximumScale=1.2;
    placement.minimumWidth=0.55;placement.maximumWidth=1.4;
    placement.minimumHeight=0.8;placement.maximumHeight=1.8;
    placement.healthyR=0.72;placement.healthyG=0.95;placement.healthyB=0.48;
    placement.dryR=0.62;placement.dryG=0.46;placement.dryB=0.20;
    placement.noiseSpread=0.3;placement.noiseSeed=31;
    placement.asset="pcg:0ba274ba3ae7d3a4482573ef44ef9a30";placement.namespaceId=31;
    local pointCount=requireResult(eve.exportTerrainDetailPoints(points,detail,processed,placement),"detail placement");
    local firstColor=points.getColorR(0),variedColor=false;
    for(local i=0;i<pointCount;++i) {
        local r=points.getColorR(i),g=points.getColorG(i),b=points.getColorB(i);
        if(r<placement.dryR || r>placement.healthyR ||
           g<placement.dryG || g>placement.healthyG ||
           b<placement.dryB || b>placement.healthyB) throw "detail color range mismatch";
        if(r!=firstColor) variedColor=true;
    }
    if(!variedColor) throw "detail color noise did not vary";
    print("TERRAIN_DETAIL_COLOR_PASS\n");
    terrainGrass=retain(gfx.newGrassField());
    requireResult(eve.bakeTerrainGrass(terrainGrass,points,"unused:resource",0.9,1.3),"empty resource group");
    if(terrainGrass.getDenseCount()!=0) throw "unexpected resource group";
    local imageApi=eve.Image();
    local grassImage=retain(imageApi.newImageDataFromFile("assets/PW_Grass_Terrain_01_D.png"));
    local grassNormal=retain(imageApi.newEmptyImageData(grassImage.getWidth(),grassImage.getHeight(),"RGBA8"));
    local grassMask=retain(imageApi.newEmptyImageData(grassImage.getWidth(),grassImage.getHeight(),"RGBA8"));
    for(local gy=0;gy<grassImage.getHeight();++gy) for(local gx=0;gx<grassImage.getWidth();++gx) {
        grassNormal.setPixel(gx,gy,0.5,0.5,1.0,1.0);
        grassMask.setPixel(gx,gy,0.05,0.82,0.22,0.72);
    }
    local foliage=eve.GrassFoliageSettings();
    foliage.baseR=0.86;foliage.baseG=1.0;foliage.baseB=0.78;foliage.alphaCutoff=0.08;
    foliage.normalStrength=1.15;foliage.renderDistance=72.0;foliage.fadeRange=20.0;
    foliage.snowMinimumHeight=13.0;foliage.snowFadeDistance=12.0;foliage.snowProgress=0.72;
    foliage.snowR=0.72;foliage.snowG=0.82;foliage.snowB=0.94;
    requireResult(eve.bakeTerrainGrassFoliage(terrainGrass,points,placement.asset,0.9,1.3,
                                               grassImage,grassNormal,grassMask,foliage),"foliage surface upload");
    local detailOverride=eve.TerrainDetailOverwriteSettings();
    detailOverride.pcgDetailDistance=70.0;detailOverride.pcgFadeoutDistance=18.0;
    detailOverride.unityDetailDistance=110;detailOverride.unityDetailDensity=0.55;
    detailOverride.detailResolutionPerPatch=8;
    local detailQuality=requireResult(terrainGrass.setTerrainDetailOverwrite(detailOverride),"detail overwrite");
    if(detailQuality!=3 || terrainGrass.getTerrainDetailHardDistance()!=110.0 ||
       terrainGrass.getTerrainDetailDensity()!=0.55) throw "detail overwrite mismatch";
    print("TERRAIN_DETAIL_OVERWRITE_PASS quality=High8 hard=110 density=0.55 fade=70+18\n");
    print("TERRAIN_GRASS_FOLIAGE_PASS pbr=1 snow=1 distance=1\n");
    local invalidImage=imageApi.newEmptyImageData(1,1,"RGBA32F");
    local rejectedImage=eve.bakeTerrainGrassImage(terrainGrass,points,placement.asset,0.9,1.3,invalidImage);
    if(rejectedImage.ok || terrainGrass.getDenseCount()!=pointCount) throw "grass image rollback failed";

    if(terrainGrass.getDenseCount()!=pointCount) throw "grass count mismatch";
    local invalid=eve.bakeTerrainGrass(terrainGrass,points,placement.asset,-1.0,1.3);
    if(invalid.ok || terrainGrass.getDenseCount()!=pointCount) throw "grass rollback failed";
    local firstWidth=points.getScaleX(0),firstHeight=points.getScaleY(0);
    points.setScale(0,0.0,firstHeight,0.0);
    local zeroWidth=eve.bakeTerrainGrassImage(terrainGrass,points,placement.asset,0.9,1.3,grassImage);
    if(zeroWidth.ok || terrainGrass.getDenseCount()!=pointCount) throw "zero width rollback failed";
    points.setScale(0,firstWidth,firstHeight,firstWidth);
    print("TERRAIN_GRASS_PASS points="+pointCount+"\n");

    local treePoints=retain(requireResult(procgen.newPointSet(),"tree points"));
    local probePoints=retain(requireResult(procgen.newPointSet(),"probe points"));
    local probeSettings=eve.TerrainProbePlacementSettings();
    probeSettings.name="runtime-reflection";probeSettings.width=256.0;probeSettings.depth=256.0;
    probeSettings.spacing=128.0;probeSettings.jitterPercent=0.0;probeSettings.minimumFitness=0.0;
    probeSettings.heightScale=30.0;probeSettings.seaLevelActive=true;probeSettings.namespaceId=4301;
    local probeCount=requireResult(eve.generateTerrainProbes(probePoints,fitness,processed,probeSettings,0),"probe placement");
    if(probeCount<=0) throw "no probes generated";
    print("TERRAIN_PROBE_PASS points="+probeCount+" types=2\n");
    local treePlacement=eve.TerrainTreePlacementSettings();
    treePlacement.originX=78.0;treePlacement.width=64.0;treePlacement.depth=64.0;
    treePlacement.heightScale=28.0;treePlacement.spacing=14.0;treePlacement.spawnDensity=1.0;
    treePlacement.jitterPercent=0.35;treePlacement.minimumFitness=0.5;treePlacement.failureRate=0.05;
    treePlacement.setScaleMode(3);treePlacement.minimumWidth=0.72;treePlacement.maximumWidth=1.18;
    treePlacement.minimumHeight=0.82;treePlacement.maximumHeight=1.35;
    treePlacement.widthRandomPercentage=0.18;treePlacement.heightRandomPercentage=0.22;
    treePlacement.healthyR=0.42;treePlacement.healthyG=0.72;treePlacement.healthyB=0.25;
    treePlacement.dryR=0.48;treePlacement.dryG=0.34;treePlacement.dryB=0.16;
    treePlacement.bendFactor=0.35;treePlacement.boundsRadius=2.2;
    treePlacement.asset="pcg:terrain-tree/oak";treePlacement.namespaceId=47;treePlacement.seed=73;
    treePlacement.maxPoints=64;
    local multiTreeWest=retain(requireResult(procgen.newPointSet(),"multi-tree west"));
    local multiTreeEast=retain(requireResult(procgen.newPointSet(),"multi-tree east"));
    local multiTreeOut=retain(requireResult(procgen.newPointSet(),"multi-tree output"));
    local multiTreeHeights=heightmap(2,2);for(local tz=0;tz<2;++tz) for(local tx=0;tx<2;++tx) multiTreeHeights.setHeight(tx,tz,0.2);
    local multiTreeFitness=heightmap(4,2);for(local tz=0;tz<2;++tz) for(local tx=0;tx<4;++tx) multiTreeFitness.setHeight(tx,tz,1.0);
    local multiTreeSettings=eve.TerrainTreePlacementSettings();multiTreeSettings.spacing=1.0;multiTreeSettings.spawnDensity=1.0;
    multiTreeSettings.jitterPercent=0.0;multiTreeSettings.failureRate=0.0;multiTreeSettings.minimumFitness=0.0;
    multiTreeSettings.setScaleMode(0);multiTreeSettings.asset="pcg:terrain-tree/oak";multiTreeSettings.namespaceId=88;
    multiTreeSettings.seed=73;multiTreeSettings.maxPoints=32;multiTreeSettings.heightScale=28.0;
    local multiTreeBounds=eve.TerrainStampSettings();multiTreeBounds.setCenter(146.0,3.0);multiTreeBounds.setSize(4.0,2.0);
    local multiTree=eve.TerrainMultiTreeWorkspace();
    requireResult(multiTree.addTile("west",multiTreeWest,multiTreeHeights,144.0,2.0,2.0,2.0,2,2,false),"multi-tree add west");
    requireResult(multiTree.addTile("east",multiTreeEast,multiTreeHeights,146.0,2.0,2.0,2.0,2,2,false),"multi-tree add east");
    requireResult(multiTree.apply(multiTreeFitness,multiTreeSettings,multiTreeBounds,0,false),"multi-tree apply");
    local multiTreeCount=requireResult(multiTree.copyTile("east",multiTreeOut),"multi-tree copy");
    if(multiTreeCount!=4 || multiTree.getLastChangedSamples()!=8) throw "multi-tree transaction mismatch";
    requireResult(multiTree.undo(),"multi-tree undo");requireResult(multiTree.redo(),"multi-tree redo");
    print("TERRAIN_MULTITREE_PASS points="+multiTreeCount+"\n");
    requireResult(eve.exportTerrainTreePoints(treePoints,fitness,processed,treePlacement),"tree placement");
    local treeCount=treePoints.getCount();
    if(treeCount<=0) throw "tree placement produced no instances";

    local runtimeStamp=eve.PcgRuntimeStamper();
    requireResult(runtimeStamp.configure("Pcg\\Stamps\\Runtime",true,true),"runtime stamper configure");
    local runtimeSourceResult=procgen.newHeightmap(3,3);local runtimeTargetResult=procgen.newHeightmap(5,5);
    if(!runtimeSourceResult.ok||!runtimeTargetResult.ok)throw "runtime stamper heightmap creation failed";
    local runtimeSource=runtimeSourceResult.value;local runtimeTarget=runtimeTargetResult.value;
    for(local y=0;y<3;++y)for(local x=0;x<3;++x)runtimeSource.setHeight(x,y,1.0);
    for(local y=0;y<5;++y)for(local x=0;x<5;++x)runtimeTarget.setHeight(x,y,20.0);
    requireResult(runtimeStamp.loadStamp(runtimeSource,"Runtime"),"runtime stamper load");
    requireResult(runtimeStamp.execute(runtimeTarget,0.0,0.0,1.0,1.0),"runtime stamper execute");
    if(runtimeTarget.height(2,2)!=6.0||runtimeStamp.getStatus()!=3)throw "runtime stamper mismatch";

    local spawnProgress=eve.PcgSpawnProgress();
    requireResult(spawnProgress.updateRule("Terrain",10,3,4,1),"spawn progress rule");
    requireResult(spawnProgress.updateRuleFraction(0.5),"spawn progress fraction");
    if(abs(spawnProgress.getProgress()-0.35)>0.0001)throw "spawn progress mismatch";

    local taskQueue=eve.PcgTaskQueue();local taskAdd=taskQueue.add(0.25);
    if(!taskAdd.ok)throw "task add failed";local taskId=taskAdd.value;
    requireResult(taskQueue.tick(0.25),"task tick");
    if(taskQueue.getReadyTaskId()!=taskId)throw "task queue mismatch";
    requireResult(taskQueue.resolveReady(true),"task resolve");
    print("PCG_RUNTIME_ORCHESTRATION_PASS\n");

    setupTerrainTreeWind(treePlacement.bendFactor);
    buildTerrainTreeMesh();
    for(local i=0;i<treeCount;++i) {
        showTree(sharedTreeMesh,treePoints.getX(i),treePoints.getY(i),treePoints.getZ(i),treePoints.getYaw(i),
            treePoints.getScaleX(i),treePoints.getScaleY(i),treePoints.getScaleZ(i),
            treePoints.getColorR(i),treePoints.getColorG(i),treePoints.getColorB(i));
    }
    for(local i=0;i<multiTreeCount;++i) {
        showTree(sharedTreeMesh,multiTreeOut.getX(i),multiTreeOut.getY(i),multiTreeOut.getZ(i),multiTreeOut.getYaw(i),
            multiTreeOut.getScaleX(i),multiTreeOut.getScaleY(i),multiTreeOut.getScaleZ(i),0.35,0.7,0.22);
    }
    print("TERRAIN_TREE_PASS points="+treeCount+"\n");

    setupTerrainCamera(treePoints);
    setupWaterSystem();
    setupTerrainWind();
    pcg_validation.ready=true;
    print("TERRAIN_STAMPING_READY changed="+pcg_validation.changed+"\n");
};

eve_update = function(dt) {
    if(terrainWater!=null) terrainWater.update(dt);
    if(terrainGrass!=null) {
        terrainGrass.update(dt);
        if(!windPaused) {
            windSeconds += dt;
            requireResult(eve.advanceVegetationWind(windState, 1.0, 0.0, 0.2, 1.0, dt), "wind advance");
            requireResult(eve.applyGrassWind(terrainGrass.getShader(), windState, windProfile, windSeconds), "wind upload");
            requireResult(eve.advanceVegetationWindAudio(windAudioState,1.0,5.0,dt,true,true),"wind audio advance");
            windAudioSource.setVolume(windAudioState.volume);
        }
    }
};
eve_render = function() {
    gfx.clear();
    gfx.render3D();
    if(terrainGrass!=null) terrainGrass.draw();
    ++pcg_validation.frames;
};
