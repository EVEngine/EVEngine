// Four real terrain meshes: two authored-height stamps and two baked-mesh stamps.
local retained = [];
persist mountainCamera = null;
local forest = [];
local treeMeshes = [];
local treeTexture = null;
local panelStats = [];
local vegetationVisible = true;
local shader = null;

function retain(value) {
    if (value == null) throw "terrain-smooth-mountain: missing resource";
    retained.append(value);
    return value;
}
function requireValue(result) {
    if (!result.ok) throw "terrain-smooth-mountain: operation failed";
    return retain(result.value);
}
function checked(result) {
    if (!result.ok) throw "terrain-smooth-mountain: operation failed";
    return result.value;
}
function raster(width, height) {
    return requireValue(procgen.newHeightmap(width, height));
}
function absolute(x) { return x < 0.0 ? -x : x; }
function prepareTrees() {
    local image=retain(eve.Image().newEmptyImageData(32,4,"RGBA8"));
    for(local y=0;y<4;++y) for(local x=0;x<32;++x) {
        if(x<16) image.setPixel(x,y,0.12,0.07,0.035,1.0);
        else image.setPixel(x,y,0.035,0.12,0.065,1.0);
    }
    treeTexture=retain(gfx.newTexture(image,false,false));
    for(local i=0;i<3;++i) {
        local p=requireValue(procgen.newParams());p.setSeed(20261111+i);
        p.setString("leafMode","canopy");p.setFloat("height",3.0+i*0.2);
        p.setFloat("crownRadius",1.5);p.setFloat("trunkRadius",0.08);
        p.setInt("branchLevels",1);p.setInt("branchCount",5);p.setFloat("leafDensity",0.95);
        p.setInt("radialSegments",6);p.setInt("curveSegments",3);
        treeMeshes.append(requireValue(procgen.generateMesh("mesh.tree",p,gfx)));
    }
}
function plantTerrain(map,ox,oz) {
    local count=0;
    for(local z=2.0;z<30.0;z+=2.4) for(local x=2.0;x<30.0;x+=2.4) {
        // Identical local candidates in both columns; placement is evaluated on the final terrain.
        local px=x+(math.noise2(x*0.71,z*0.51)-0.5)*1.2;
        local pz=z+(math.noise2(x*0.43+17,z*0.83)-0.5)*1.2;
        local y=map.sampleBilinear(px*4.0,pz*4.0);
        local dx=map.sampleBilinear(px*4.0+2,pz*4.0)-map.sampleBilinear(px*4.0-2,pz*4.0);
        local dz=map.sampleBilinear(px*4.0,pz*4.0+2)-map.sampleBilinear(px*4.0,pz*4.0-2);
        local moisture=math.noise2(px*0.23,pz*0.23);
        if(y<1.0 || y>7.8 || dx*dx+dz*dz>0.48 || moisture<0.52 || math.noise2(px*1.7+8,pz*1.7)<0.45) continue;
        local tree=retain(eve.Renderable3D());
        tree.setMesh(treeMeshes[(x*7+z*11).tointeger()%3]);tree.setTexture(treeTexture);
        tree.setPosition(ox+px,y-0.02,oz+pz);
        local size=0.65+0.2*(1.0+math.noise2(px*0.7,pz*0.7));tree.setScale(size,size,size);
        tree.setTint(0.8,0.95,0.8,1.0);tree.setRoughness(0.95);tree.setYaw(px+pz);
        forest.append(tree);++count;
    }
    panelStats.append(count);
    print("MOUNTAIN_FOREST panel="+panelStats.len()+" trees="+count+" maxHeight=7.8 maxSlope2=0.48\n");
}
function setMountainVegetation(visible) { foreach(tree in forest) tree.setVisible(visible); }
function setMountainView(mode) {
    if(mode==1) { mountainCamera.setEye(36.0,38.0,115.0);mountainCamera.setTarget(36.0,6.0,56.0); }
    else if(mode==2) { mountainCamera.setEye(36.0,125.0,80.0);mountainCamera.setTarget(36.0,3.5,36.0); }
    else { mountainCamera.setEye(36.0,83.0,122.0);mountainCamera.setTarget(36.0,3.5,36.0); }
}
function baseTerrain() {
    local map = raster(129, 129);
    for (local z = 0; z < 129; ++z) for (local x = 0; x < 129; ++x) {
        local h = 2.0 + 1.7 * math.noise2(x * 0.0275, z * 0.0275);
        map.setHeight(x, z, h);
    }
    return map;
}
function showTerrain(map, offsetX, offsetZ, smooth) {
    local layers = retain(procgen.analyzeTerrain(map, 200.0, 0.12, 0.4));
    local chunk = retain(procgen.buildTerrainChunk(map, layers, 0, 0, 128, 128, 0, 0.25, 1.0, 0.0));
    local mesh = retain(procgen.generateTerrainChunkMesh(chunk, gfx));
    local image = retain(procgen.generateTerrainSplatMap(chunk));
    local texture = retain(gfx.newTexture(image, false, false));
    local entity = retain(eve.Renderable3D());
    entity.setMesh(mesh);
    entity.setTexture(texture);
    entity.setShader(shader);
    entity.setTint(1.0, 1.0, 1.0, 1.0);
    entity.setRoughness(0.92);
    entity.setPosition(offsetX, 0.0, offsetZ);
    plantTerrain(map, offsetX, offsetZ);
}
function placePair(stamp, coverage, one, amplitude, baseHeight, offsetZ) {
    local pair = [];
    for (local panel = 0; panel < 2; ++panel) {
        local terrain = baseTerrain();
        local settings = retain(eve.TerrainStampSettings());
        settings.setGrid(0.0, 0.0, 0.25, 0.25);
        settings.setCenter(16.0, 16.0);
        settings.setSize(28.0, 28.0);
        settings.setRotation(0.18);
        settings.amplitude = amplitude;
        settings.baseHeight = baseHeight;
        settings.smoothWidth = 4.0;
        settings.edgeFade = 3.0;
        local operation = panel == 0 ? 0 : 6;
        local changed = checked(terrain.applyStamp(stamp, settings, operation, one, coverage));
        if (changed <= 0) throw "terrain-smooth-mountain: stamp changed no samples";
        if(offsetZ==0.0) {
            settings.setCenter(23.0,12.0);settings.setSize(15.0,21.0);settings.amplitude=32.0;
            checked(terrain.applyStamp(stamp,settings,operation,one,coverage));
        }
        pair.append(terrain);
        showTerrain(terrain, panel * 40.0, offsetZ, panel == 1);
    }
    local different=0, maxDelta=0.0;
    for(local z=0;z<129;++z) for(local x=0;x<129;++x) {
        local a=pair[0].height(x,z),b=pair[1].height(x,z);
        if(a!=a || b!=b) throw "mountain comparison: non-finite height";
        local delta=absolute(a-b);
        if(delta>0.0001) ++different;
        if(delta>maxDelta) maxDelta=delta;
        if((x==0 || x==128) && (z==0 || z==128) && delta>0.0001)
            throw "mountain comparison: untouched corner changed";
    }
    if(different==0 || maxDelta<0.02) throw "mountain comparison: blend difference missing";
    print("MOUNTAIN_COMPARE row="+offsetZ+" differentSamples="+different+" maxDelta="+maxDelta+" corners=equal\n");
}
eve_init = function() {
    gfx.setBackgroundColor(0.045, 0.065, 0.09, 1.0);
    gfx.setDirectionalLight(-0.45, 0.9, 0.35, 1.1, 1.05, 0.95);
    shader = requireValue(gfx.loadMeshShaderSpv("shaders/mountain.vert.spv", "shaders/mountain.frag.spv"));
    prepareTrees();
    local one = raster(1, 1);
    one.setHeight(0, 0, 1.0);

    // Author a mountain as a height stamp, then use identical terrain for both operations.
    local authored = raster(129, 129);
    for (local z = 0; z < 129; ++z) for (local x = 0; x < 129; ++x) {
        local dx = (x - 64) * 0.25, dz = (z - 64) * 0.25;
        local warp = 2.1 * math.noise2(x*0.035,z*0.035);
        local a = dx*0.85+dz*0.26+warp, b = dz*0.75-dx*0.33;
        local crest = 1.0-absolute(a)/14.0-absolute(b)/23.0;
        local shoulder = 1.0-absolute(a-5.0)/10.0-absolute(b+5.0)/17.0;
        local ridge = crest > shoulder*0.87 ? crest : shoulder*0.87;
        if(ridge<0.0) ridge=0.0;
        local gullies = 0.94+0.06*math.noise2(x*0.09,z*0.09);
        authored.setHeight(x,z,20.5*ridge*ridge*gullies);

    }
    placePair(authored, one, one, 1.0, 0.0, 40.0);

    // Standalone CPU model source. Replace this MeshBuild with your imported model data.
    local params = requireValue(procgen.newParams());
    params.setSeed(17);
    params.setInt("resolution", 40);
    params.setString("field", "rock");
    local cpuMesh = requireValue(procgen.buildMesh("mesh.marchingcubes", params));
    local builder = retain(eve.TerrainMeshStampBuilder());
    checked(builder.setSource(cpuMesh));
    local baked = raster(129, 129), coverage = raster(129, 129);
    local hits = checked(builder.bake(baked, coverage, -0.5, -0.5, 1.0, 1.0, 0.07));
    if (hits <= 0) throw "terrain-smooth-mountain: model bake has no surface hits";
    placePair(baked, coverage, one, 45.0, -2.0, 0.0);

    mountainCamera = retain(eve.Camera3D());
    mountainCamera.setEye(36.0, 83.0, 122.0);
    mountainCamera.setTarget(36.0, 3.5, 36.0);
    mountainCamera.setUp(0.0, 1.0, 0.0);
    mountainCamera.setFov(45.0);
    mountainCamera.setClipPlanes(0.1, 300.0);
    mountainCamera.setAmbient(0.17, 0.21, 0.25);
    mountainCamera.setActive(true);
    ui.beginBuild();
    ui.beginWindow("Terrain stamp comparison", "root");
    ui.text("RIDGE STUDY / HARD MAX vs SMOOTH RAISE", "title");
    ui.text("LEFT: Raise (max)    RIGHT: SmoothRaise / width 4", "columns");
    ui.text("NEAR: authored height stamp    FAR: baked rock mesh", "rows");
    ui.text("1: overview   2: ridge close-up   3: overhead   V: trees", "controls");
    ui.end();
    ui.mountBuildAs("stamp-legend");
    ui.select("stamp-legend");
    ui.setHostOverlay(true);
    ui.setHostPos(18.0, 16.0, 0.0, 0.0);
    ui.setHostSize(650.0, 160.0);
    print("SMOOTH_MOUNTAIN_READY panels=4 meshSamples=" + hits + "\n");
};
eve_update = function(dt) {
    if(key_just_pressed("1")) setMountainView(0);
    if(key_just_pressed("2")) setMountainView(1);
    if(key_just_pressed("3")) setMountainView(2);
    if(key_just_pressed("v") || key_just_pressed("V")) {
        vegetationVisible=!vegetationVisible;setMountainVegetation(vegetationVisible);
    }
};
eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
