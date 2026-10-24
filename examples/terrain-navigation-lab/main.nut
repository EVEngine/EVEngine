// One authoritative height raster drives rendering, A*, flow fields and collision.
local keep = [];
local heights = null, baseCosts = null, world = null, models = null;
local overview = null, closeup = null;
local actors = [], elapsed = 0.0, fixed = 0.0, steps = 0, arrivals = 0;
local captured = false, frames = 0, nextReport = 5.0;
local function smaller(a,b) { return a < b ? a : b; }
local function larger(a,b) { return a > b ? a : b; }
function own(v) { if (v == null) throw "terrain-lab: missing resource"; keep.append(v); return v; }
function value(r) { if (!r.ok) throw "terrain-lab: operation failed"; return own(r.value); }
function raster() { return value(procgen.newHeightmap(65,65)); }
function terrainY(x,z) {
    return heights.sampleBilinear(x, z);
}
function plan(a) {
    local settings = own(eve.TerrainTraversalSettings());
    settings.maxSlopeDegrees = a.limit;
    settings.slopeCost = a.penalty;
    local result = a.costs.traversalCosts(heights, baseCosts, settings);
    if (!result.ok) throw "terrain-lab: traversal bake failed";
    for (local z=0; z<65; ++z) for (local x=0; x<65; ++x)
        a.grid.setCellCost(x,z,a.costs.height(x,z));
    a.path = a.grid.findPath(8,20,56,20);
    if (a.path == null || a.path.getLength() < 2) throw "terrain-lab: route unavailable";
    local field = a.grid.buildFlowField(56,20);
    if (field == null || !field.isReachable(8,20)) throw "terrain-lab: flow field unavailable";
    a.flow = field;
    a.index = 1; a.x = 8.0; a.z = 20.0; a.y = terrainY(a.x,a.z)+0.05;
    a.stalled = 0.0;
    print("terrain-lab route limit=" + a.limit + " nodes=" + a.path.getLength() + "\n");
    // Mark all planned waypoints in the scene.
    foreach (marker in a.markers) marker.setVisible(false);
    a.markers.clear();
    for (local i=0; i<a.path.getLength(); i+=2) {
        local x=a.path.getX(i).tofloat(), z=a.path.getY(i).tofloat();
        local marker=own(eve.Renderable3D());
        marker.setMesh(a.markerMesh);
        marker.setPosition(x,terrainY(x,z)+0.10,z);
        marker.setScale(0.28,0.12,0.28);
        marker.setTint(a.color[0],a.color[1],a.color[2],1.0);
        a.markers.append(marker);
    }
}
function addActor(limit,penalty,color,model) {
    local a={limit=limit,penalty=penalty,color=color,costs=raster(),
        grid=own(map.newPathfinderSize(65,65)),path=null,flow=null,index=1,
        x=8.0,y=0.0,z=20.0,stalled=0.0,parts=[],markers=[],markerMesh=own(gfx.newMeshCube(1.0))};
    a.grid.setDiagonal(false);
    for(local i=0; i<model.getMeshCount(); ++i) {
        local part=own(models.createRenderable(gfx,model,i));
        part.setTint(color[0],color[1],color[2],1.0);
        // The glTF scene transform is imported by Model3D.
        part.setRotation(0.0,0.0,0.0);
        part.setScale(1.6,1.6,1.6);
        a.parts.append(part);
    }
    actors.append(a); plan(a);
}
eve_init = function() {
    gfx.setBackgroundColor(0.055,0.075,0.11,1.0);
    gfx.setDirectionalLight(-0.45,0.9,0.35,1.8,1.65,1.35);
    heights=raster(); baseCosts=raster();
    local collision=[];
    for(local z=0; z<65; ++z) for(local x=0; x<65; ++x) {
        local ridge=larger(0.0,7.0-fabs(x-32.0));
        // A gentle pass at the far end forces the low-slope actor to detour.
        local factor=z>44 ? larger(0.20,1.0-(z-44)*0.10) : 1.0;
        local h=1.8*ridge*factor;
        heights.setHeight(x,z,h); baseCosts.setHeight(x,z,1.0); collision.append(h);
    }
    local layers=own(procgen.analyzeTerrain(heights,64.0,0.12,0.40));
    local shader=own(procgen.createTerrainMaterialShader(gfx));
    for(local cz=0; cz<2; ++cz) for(local cx=0; cx<2; ++cx) {
        local chunk=own(procgen.buildTerrainChunk(heights,layers,cx*32,cz*32,32,32,0,1.0,1.0,0.0));
        local mesh=own(procgen.generateTerrainChunkMesh(chunk,gfx));
        local image=own(procgen.generateTerrainSplatMap(chunk));
        local tex=own(gfx.newTexture(image,false,false));
        local entity=own(eve.Renderable3D());
        entity.setMesh(mesh); entity.setTexture(tex); entity.setShader(shader);
        entity.setPosition(cx*32.0,0.0,cz*32.0); entity.setRoughness(0.92);
    }
    world=own(physics.newWorld3D(0.0,0.0,0.0,false));
    local ground=own(world.newBody("static",0.0,0.0,0.0));
    own(ground.newHeightFieldShape(65,65,1.0,1.0,collision));
    models=own(eve.Model3D());
    local model=own(models.newModelDataFromFile("assets/CesiumMan.glb"));
    addActor(35.0,2.0,[0.15,0.85,1.0],model);
    addActor(75.0,0.0,[1.0,0.65,0.15],model);
    if (actors[0].path.getLength() <= actors[1].path.getLength())
        throw "terrain-lab: low slope actor did not detour";
    overview=own(eve.Camera3D()); overview.setEye(75.0,84.0,87.0);
    overview.setTarget(32.0,1.5,32.0); overview.setUp(0.0,1.0,0.0);
    overview.setFov(52.0); overview.setClipPlanes(0.1,400.0);
    overview.setAmbient(0.35,0.40,0.45); overview.setActive(true);
    closeup=own(eve.Camera3D()); closeup.setUp(0.0,1.0,0.0);
    closeup.setFov(50.0); closeup.setClipPlanes(0.1,250.0);
    closeup.setAmbient(0.35,0.40,0.45);
    ui.beginBuild(); ui.beginWindow("Terrain Navigation Lab","terrain-lab");
    ui.text("CYAN / 35 degrees + slope cost     AMBER / 75 degrees","legend");
    ui.text("Same terrain: rendering + A* + flow fields + capsule collision. Metres, degrees.","help");
    ui.beginRow("policies",16.0);
    ui.slider("Cyan maximum slope",35.0,0.0,85.0,"limit0");
    ui.slider("Cyan slope cost",2.0,0.0,8.0,"cost0");
    ui.slider("Amber maximum slope",75.0,0.0,85.0,"limit1");
    ui.slider("Amber slope cost",0.0,0.0,8.0,"cost1");
    ui.end(); ui.button("Reset routes","reset");
    ui.beginRow("views",8.0);
    ui.viewport("overview",810.0,570.0); ui.viewport("character",390.0,570.0);
    ui.end(); ui.text("Starting","status"); ui.end();
    ui.mountBuildAs("terrain-lab"); ui.select("terrain-lab");
    ui.setHostPos(0.0,0.0,0.0,0.0);
    ui.setHostSize(config.width.tofloat(),config.height.tofloat());
    ui.setHostOverlay(false);
    print("terrain-lab READY\n");
};
eve_update = function(dt) {
    elapsed+=dt; fixed+=smaller(dt,0.10);
    ui.select("terrain-lab");
    local click=ui.consumeClick();
    while(click!="") {
        if(click.find("reset")!=null) {
            foreach(i,a in actors) {
                a.limit=ui.getValue("limit"+i); a.penalty=ui.getValue("cost"+i); plan(a);
            }
            ui.setText("legend","CYAN / "+actors[0].limit+" degrees    AMBER / "+actors[1].limit+" degrees");
        }
        click=ui.consumeClick();
    }
    while(fixed>=1.0/60.0) {
        fixed-=1.0/60.0; ++steps;
        foreach(a in actors) {
            if(a.index>=a.path.getLength()) continue;
            local tx=a.path.getX(a.index).tofloat(), tz=a.path.getY(a.index).tofloat();
            local dx=tx-a.x,dz=tz-a.z,dist=sqrt(dx*dx+dz*dz);
            if(dist<0.18) {
                ++a.index; a.stalled=0.0;
                if(a.index>=a.path.getLength()) { ++arrivals; print("terrain-lab ARRIVED "+a.limit+"\n"); }
                continue;
            }
            local travel=smaller(dist,3.5/60.0);
            world.setMoverSlopeLimit(a.limit);
            world.moveCapsule(a.x,a.y+0.3,a.z,a.x,a.y+1.3,a.z,0.3,
                // A small downward probe maintains ground contact without cancelling uphill intent.
                dx/dist*travel,-0.01,dz/dist*travel);
            local mx=world.getMoverDeltaX(), my=world.getMoverDeltaY(), mz=world.getMoverDeltaZ();
            a.x+=mx; a.y+=my; a.z+=mz;
            if(fabs(mx)+fabs(mz)<0.0001) a.stalled+=1.0/60.0; else a.stalled=0.0;
            if(a.stalled>5.0) throw "terrain-lab: actor stalled";
            foreach(part in a.parts) part.setPosition(a.x,a.y,a.z);
        }
    }
    local a=actors[0];
    closeup.setEye(a.x+7.0,a.y+6.0,a.z+9.0); closeup.setTarget(a.x,a.y+1.0,a.z);
    ui.setText("status","Time "+elapsed.tointeger()+"s | steps "+steps+" | arrivals "+arrivals+
        " | Reset applies edited policies");
    if(elapsed >= nextReport) {
        nextReport += 5.0;
        print("terrain-lab PROGRESS steps="+steps+" arrivals="+arrivals+"\n");
    }
};
eve_render = function() {
    gfx.clear(); ++frames;
    ui.select("terrain-lab");
    local canvas=ui.viewportCanvas("overview");
    if(canvas!=null) gfx.renderScene3DToCanvas(canvas,overview);
    canvas=ui.viewportCanvas("character");
    if(canvas!=null) gfx.renderScene3DToCanvas(canvas,closeup);
    ui.beginFrameAndRender();
    if(!captured && elapsed>2.0 && frames>12) captured=gfx.saveFramePng("overview.png");
};
