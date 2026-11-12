persist waterGarden = null;

function waterBuildGarden() {
    waterGarden=waterMap.newLayer(FIELD_W,FIELD_H,TILE_W,TILE_H);
    configureIsoLayer(waterGarden,1);
    local texture=gfx.newTextureFromFile("assets/garden.png");
    waterGarden.setTileset(texture,1,4,0,0);waterGarden.setTilesetTileSize(96,128);
    for(local gid=1;gid<=4;gid++)
        waterGarden.setTileVisual(gid,(gid-1)*96,0,96,128,8.0,98.0,0.0);
    for(local y=0;y<FIELD_H;y++)for(local x=0;x<FIELD_W;x++) {
        local distance=fabs(y-waterField.channelCenter(x));
        if(distance>3.5 && (x*7+y*11)%9==0)waterGarden.setTile(x,y,(x+y)%3==0?1:2);
        else if(distance>2.5 && distance<3.5 && x%4==0)waterGarden.setTile(x,y,3);
    }
}
