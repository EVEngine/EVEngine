// A small, editable physics courtyard demonstrates the abstraction workflow.
// Every element is authored through the same public tools as a user's scene.
function levelDemoPiece(name,recipe,x,y,z,width,height,depth,motion="static") {
    local id=whiteboxPlace(recipe,x,y,z);
    if(id=="") throw level.status;
    local piece=level.pieces[id];
    foreach(key,value in {width=width,height=height,depth=depth})
        if(key in piece.values) piece.values[key]=value.tostring();
    piece.motion=motion;whiteboxRebuildPiece(id);
    local bounds=piece.bounds;
    local object=levelObject(id);
    levelCommand("scene.object.update.v1",{object=id,name=name,parent=object.parent,
        position=[x,y-bounds[1],z],rotation=[0.0,0.0,0.0],scale=[1.0,1.0,1.0]});
    return id;
}
function levelBuildDemo() {
    levelNewDocument();
    local ground=terrainSnapHeight(0.0,0.0), heightmap=level.terrain.heightmap;
    // A gently feathered clearing retains the surrounding procedural landscape.
    for(local z=0;z<level.terrain.height;++z) for(local x=0;x<level.terrain.width;++x) {
        local wx=level.terrain.originX+x*level.terrain.spacingX,wz=level.terrain.originZ+z*level.terrain.spacingZ;
        local distance=sqrt(wx*wx+wz*wz), weight=levelClamp((32.0-distance)/12.0,0.0,1.0);
        local old=heightmap.height(x,z);
        heightmap.setHeight(x,z,old+(ground/level.terrain.heightScale-old)*weight);
    }
    terrainRefreshSurface();
    local platform=levelDemoPiece("Courtyard foundation","prototype.cube",0.0,ground-0.3,0.0,20.0,0.6,18.0);
    local parts=[platform];
    parts.push(levelDemoPiece("West wall","prototype.cube",-9.0,ground+0.3,0.0,0.6,2.4,16.0));
    parts.push(levelDemoPiece("North wall","prototype.cube",0.0,ground+0.3,-8.0,18.0,2.4,0.6));
    local tower=[];
    tower.push(levelDemoPiece("Tower base","prototype.cube",-6.0,ground+0.3,-5.0,3.4,0.6,3.4));
    foreach(x in [-7.2,-4.8]) foreach(z in [-6.2,-3.8])
        tower.push(levelDemoPiece("Tower pillar","prototype.cube",x,ground+0.9,z,0.5,3.2,0.5));
    tower.push(levelDemoPiece("Tower roof","prototype.cube",-6.0,ground+4.1,-5.0,3.8,0.4,3.8));
    level.selection=tower;level.selected=tower[0];levelGroupSelection();
    local towerId=level.selected;parts.push(towerId);
    levelCommand("scene.object.rename.v1",{object=towerId,name="Pavilion · structural block"});
    levelSelect(towerId);levelCaptureBlock();
    parts.push(levelPlaceBlock(0,6.0,ground+2.2,-5.0));
    parts.push(levelDemoPiece("Walkable ramp","prototype.ramp",0.0,ground+0.3,-4.0,3.0,1.6,5.0));
    level.selection=parts;level.selected=parts[0];levelGroupSelection();
    local courtyard=level.selected;
    levelCommand("scene.object.rename.v1",{object=courtyard,name="Courtyard · scene assembly"});
    for(local i=0;i<3;++i)
        levelDemoPiece("Physics crate " + (i+1),"prototype.cube",-2.0+i*1.5,ground+0.4+i*1.2,2.0,1.0,1.0,1.0,"dynamic");
    spawnPlace(4.0,ground+0.4,2.5,0.6);
    local player=spawnPlace(0.0,ground+0.4,5.0,3.14159265);
    levelCommand("scene.object.rename.v1",{object=player,name="Player start · select then Play"});
    levelSelect(courtyard);whiteboxSyncTransforms();spawnSyncTransforms();
    level.focusX=0.0;level.focusY=ground+1.0;level.focusZ=0.0;
    level.distance=33.0;level.pitch=0.58;level.yaw=0.65;levelUpdateOrbitCamera();
    levelRefreshPanels();levelStatus("Courtyard demo · Enter a block to edit · F5 to play and push crates");
}
