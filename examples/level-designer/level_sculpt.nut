// Sculpt strokes own a bounded history, separate from scene-command history.
// The native terrain codec is shared by saves, stroke undo, and load staging.
function terrainEncodeSurface(terrain) {
    local encoded=level.heightmapTargets.encodeDocument(terrain.heightmap,terrain.spacingX,terrain.spacingZ);
    if(!encoded.ok) throw encoded.status.summary;
    return encoded.value;
}
function terrainRefreshSurface() {
    local terrain=level.terrain;
    if(terrain==null) return;
    if(!level.heightmapTargets.updateSmoothMesh(terrain.mesh,gfx,terrain.heightmap,terrain.spacingX,terrain.heightScale))
        throw "Terrain mesh update failed";
    local bounds=terrainSampleBounds(terrain.heightmap);
    terrain.minSample=bounds.minimum;terrain.maxSample=bounds.maximum;
    level.terrainDirty=false;
}
function terrainBeginStroke(x,z) {
    if(level.mode!="edit" || level.terrain==null) return;
    level.strokeBefore=terrainEncodeSurface(level.terrain);
    level.brushTarget=terrainSnapHeight(x,z);
}
function terrainSculptAt(x,z,dt) {
    local terrain=level.terrain;
    if(level.mode!="edit" || terrain==null || !terrainInside(terrain,x,z)) return 0;
    local hm=terrain.heightmap, cx=(x-terrain.originX)/terrain.spacingX, cz=(z-terrain.originZ)/terrain.spacingZ;
    local radius=level.brushRadius, changes=[], strength=level.brushStrength*levelClamp(dt,0.0,0.05);
    local rx=radius/terrain.spacingX, rz=radius/terrain.spacingZ;
    local minX=levelClamp(floor(cx-rx).tointeger(),0,terrain.width-1).tointeger();
    local maxX=levelClamp(ceil(cx+rx).tointeger(),0,terrain.width-1).tointeger();
    local minZ=levelClamp(floor(cz-rz).tointeger(),0,terrain.height-1).tointeger();
    local maxZ=levelClamp(ceil(cz+rz).tointeger(),0,terrain.height-1).tointeger();
    for(local iz=minZ;iz<=maxZ;++iz) for(local ix=minX;ix<=maxX;++ix) {
        local dx=(ix-cx)*terrain.spacingX,dz=(iz-cz)*terrain.spacingZ;
        local distance=sqrt(dx*dx+dz*dz)/radius;
        if(distance>=1.0) continue;
        local weight=(1.0-distance*distance);weight*=weight;
        local before=hm.height(ix,iz), after=before;
        if(level.brushMode=="raise" || level.brushMode=="lower")
            after+=(level.brushMode=="raise" ? 1.0 : -1.0)*strength*weight/terrain.heightScale;
        else {
            local target=level.brushTarget/terrain.heightScale;
            if(level.brushMode=="smooth") {
                target=0.0;local count=0;
                for(local j=-1;j<=1;++j) for(local i=-1;i<=1;++i) {
                    local xx=ix+i,zz=iz+j;
                    if(xx>=0 && xx<terrain.width && zz>=0 && zz<terrain.height) { target+=hm.height(xx,zz);++count; }
                }
                target/=count;
            }
            after+=(target-before)*levelClamp(strength*5.0*weight,0.0,1.0);
        }
        if(fabs(after-before)>0.0000001) changes.push([ix,iz,after]);
    }
    // Read all neighbours before publishing so smoothing is iteration-order independent.
    foreach(change in changes) hm.setHeight(change[0],change[1],change[2]);
    if(changes.len()) level.terrainDirty=true;
    return changes.len();
}
function terrainEndStroke() {
    if(level.strokeBefore==null) return;
    local after=terrainEncodeSurface(level.terrain);
    if(after!=level.strokeBefore) {
        level.terrainUndo.push(level.strokeBefore);level.terrainRedo.clear();
        if(level.terrainUndo.len()>32) level.terrainUndo.remove(0);
    }
    level.strokeBefore=null;
    if(level.terrainDirty) terrainRefreshSurface();
    levelStatus("Terrain stroke · Undo available in Terrain tool");
}
function terrainHistory(redo) {
    terrainEndStroke();
    local source=redo ? level.terrainRedo : level.terrainUndo;
    local destination=redo ? level.terrainUndo : level.terrainRedo;
    if(source.len()==0) { levelStatus("No terrain strokes to " + (redo ? "redo" : "undo"));return; }
    local current=terrainEncodeSurface(level.terrain);
    local decoded=level.heightmapTargets.decodeDocument(source.top(),level.terrain.heightmap);
    if(!levelChecked(decoded)) return;
    source.pop();destination.push(current);terrainRefreshSurface();
    levelStatus(redo ? "Terrain stroke redone" : "Terrain stroke undone");
}
function terrainSculptPointer(dt) {
    if(level.tool!="terrain" || level.mode!="edit") return;
    local hit=terrainPickAtMouse();
    if(mouse.isDown(1) && hit!=null && !mouse.isDown(2) && !mouse.isDown(3)) {
        if(level.strokeBefore==null) terrainBeginStroke(hit.x,hit.z);
        terrainSculptAt(hit.x,hit.z,dt);
        level.brushClock+=dt;
        if(level.terrainDirty && level.brushClock>=1.0/30.0) { terrainRefreshSurface();level.brushClock=0.0; }
    } else if(level.strokeBefore!=null) terrainEndStroke();
}
