// Example-local presentation. PlacementWorld remains the sole terrain/occupancy owner.
// GPU textures are reconstructed on reload, never serialized as persistent state.
townArt <- {};
persist waterTime = 0.0;
persist showGrid = false;

function loadTownArt() {
    if (townArt.len() > 0) return;
    local names = ["house", "stall", "dock", "crate", "barrel", "pixel"];
    for (local i=0; i<8; ++i) names.push("grass"+i);
    for (local i=0; i<6; ++i) names.push("road"+i);
    foreach (name in names) {
        local texture = gfx.newTextureFromFile("assets/"+name+".png");
        if (texture == null) throw "Missing town art: run prepare_assets.py first";
        gfx.setTextureSampler(texture, "nearest", "none", 1.0, 0.0);
        townArt[name] <- texture;
    }
}

function tileStyle(x,y,n) { return ((x*7381+y*1933+x*y*17) % n); }
function townImage(name,x,y,w,h,r=1.0,g=1.0,b=1.0,a=1.0) {
    gfx.drawTexturedRect(townArt[name],x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),r,g,b,a);
}
function isWater(x,y) {
    return x>=0 && y>=0 && x<gridW && y<gridH && world.getTerrain(x,y)==TERRAIN_WATER;
}
function drawTown() {
    local placed = {};
    for (local y=0; y<gridH; ++y) for (local x=0; x<gridW; ++x) {
        local px=mapOriginX+x*cellPx, py=mapOriginY+y*cellPx;
        if (isWater(x,y)) {
            local shade=tileStyle(x,y,3)*0.015;
            townImage("pixel",px,py,cellPx,cellPx,0.09+shade,0.30+shade,0.36+shade,1.0);
            // Small drifting ripples; animation uses the supplied frame dt.
            local drift=sin(waterTime*1.4+x*1.7+y)*2.0;
            townImage("pixel",px+6.0+drift,py+10.0,9.0,1.0,0.29,0.52,0.52,0.7);
            townImage("pixel",px+15.0-drift,py+21.0,5.0,1.0,0.25,0.46,0.49,0.6);
            if (!isWater(x,y-1)) townImage("pixel",px,py,cellPx,3.0,0.53,0.55,0.30,1.0);
            if (!isWater(x,y+1)) townImage("pixel",px,py+cellPx-3,cellPx,3.0,0.42,0.46,0.24,1.0);
            if (!isWater(x-1,y)) townImage("pixel",px,py,3.0,cellPx,0.48,0.51,0.28,1.0);
            if (!isWater(x+1,y)) townImage("pixel",px+cellPx-3,py,3.0,cellPx,0.42,0.46,0.24,1.0);
        } else townImage("grass"+tileStyle(x,y,8),px,py,cellPx,cellPx);
        local occ=world.getOccupant(x,y);
        if (occ>0) {
            local id=world.getBuildingId(occ);
            if (id=="road") townImage("road"+tileStyle(x,y,6),px,py,cellPx,cellPx);
            else if (id=="barn.l") townImage("crate",px,py,cellPx,cellPx);
            else {
                if (!(occ in placed)) placed[occ] <- {id=id,x=x,y=y,maxX=x,maxY=y};
                placed[occ].maxX=x>placed[occ].maxX ? x : placed[occ].maxX;
                placed[occ].maxY=y>placed[occ].maxY ? y : placed[occ].maxY;
            }
        }
        if (showGrid) {
            townImage("pixel",px,py,cellPx,1.0,0.1,0.16,0.12,0.28);
            townImage("pixel",px,py,1.0,cellPx,0.1,0.16,0.12,0.28);
        }
    }
    foreach (p in placed) {
        local w=(p.maxX-p.x+1)*cellPx,h=(p.maxY-p.y+1)*cellPx;
        local name=p.id=="house.wood" ? "house" : p.id;
        townImage(name,mapOriginX+p.x*cellPx,mapOriginY+p.y*cellPx,w,h);
    }
    if (mouseOnMap() && (ghost.getCellX()!=hiddenGhostCellX || ghost.getCellY()!=hiddenGhostCellY)) {
        local cells=[];
        footprintCells(ghost.getBuildingId(),ghost.getCellX(),ghost.getCellY(),ghost.getRotationDeg(),cells);
        local ok=ghost.isValid() && canAfford(ghost.getBuildingId())=="";
        foreach(c in cells) if(c[0]>=0 && c[1]>=0 && c[0]<gridW && c[1]<gridH) {
            local px=mapOriginX+c[0]*cellPx,py=mapOriginY+c[1]*cellPx;
            // Textured tint shares the same submission path as terrain and buildings.
            townImage("grass0",px+2,py+2,cellPx-4,cellPx-4,ok?0.5:1.0,ok?1.0:0.25,0.3,0.65);
        }
    }
}
