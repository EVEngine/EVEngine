dofile(config.assetSource+"/catalog.nut");

// TileLayer owns terrain GIDs; PlacementWorld lazily reads their semantics.
// These references follow the original example lifecycle. Texture handles are
// transient and reconstructed on reload rather than serialized.
persist bld = null;
persist mapMod = null;
persist layer = null;
persist world = null;
persist session = null;
persist fx = null;
persist cursorLayer = null;
persist buildingLayer = null;
persist buildingPreview = null;
persist uiBuilt = false;
persist paintMode = true;
persist brushIndex = 0;
persist materialVariant = 0;
persist paletteIndex = 0;
persist seed = 7;
persist prevMouse = {left=false,right=false};
persist notice = "选择地表后拖动铺设；B 切换建筑放置。";
persist gridVisible = false;

const MAP_W = 14;
const MAP_H = 12;
const TILE_W = 64.0;
const TILE_H = 46.0;
const ORIGIN_X = 435.0;
const ORIGIN_Y = 96.0;
brushNames <- ["草地", "石板路", "鹅卵石", "沙地", "浅水", "深水"];
brushFiles <- [[11,12,13],[2,3,37],[69,72,73],[7,8,10],[108,14],[114,116]];
palette <- ["house","dock","tower"];
atlas <- null;
cursorTexture <- null;
buildingTexture <- null;

function loadArt() {
    buildingTexture=gfx.newTextureFromFile("art/buildings.png");
    if(buildingTexture==null) throw "Missing generated building atlas";
    gfx.setTextureSampler(buildingTexture,"linear","none",1.0,0.0);
    atlas = gfx.newTextureFromFile(config.assetSource+"/c14-atlas.png");
    cursorTexture = gfx.newTextureFromFile(config.assetSource+"/cursor.png");
    if (atlas==null || cursorTexture==null) throw "Run building-tilemap/prepare_assets.py first";
    gfx.setTextureSampler(atlas,"linear","none",1.0,0.0);
    gfx.setTextureSampler(cursorTexture,"linear","none",1.0,0.0);
}
function newIsoLayer() {
    local result=mapMod.newLayer(MAP_W,MAP_H,TILE_W,TILE_H);
    result.applyConfig(@"{""orientation"":""isometric"",""tilewidth"":64,""tileheight"":46}");
    result.setOrigin(ORIGIN_X,ORIGIN_Y);
    return result;
}
function configureBuildingLayer(target) {
    target.setTileset(buildingTexture,1,1,0,0);
    target.setTilesetTileSize(384,160);
    // Image foot matches the front footprint vertex. House extends over 2x2;
    // tower over 1x1. Painter depth follows the same front vertex.
    target.setTileVisual(1,0,0,136,140,68.0,48.0,46.0);
    target.setTileVisual(2,144,0,72,102,36.0,56.0,0.0);
    target.setTileVisual(3,224,0,96,69,32.0,0.0,23.0);
    target.setTileVisual(4,224,80,96,69,64.0,0.0,23.0);
}
function buildingGid(id,rotation) {
    if(id=="house") return 1;
    if(id=="tower") return 2;
    return (rotation/90).tointeger()%2==0?3:4;
}
function syncBuildingArt() {
    // Render cache only: placement, removal and restore are owned by the world.
    buildingLayer.fill(0);
    for(local i=0;i<world.getBuildingCount();++i) {
        local id=world.getBuildingInstanceAt(i);
        buildingLayer.setTile(world.getBuildingCellX(id),world.getBuildingCellY(id),
            buildingGid(world.getBuildingId(id),world.getBuildingRotation(id)));
    }
}
function configureTerrainArt() {
    configureBuildingLayer(buildingLayer);buildingLayer.setLayer(1);
    configureBuildingLayer(buildingPreview);buildingPreview.setLayer(2);
    layer.setTileset(atlas,1,10,0,0);
    layer.setTilesetTileSize(64,46);
    foreach(file,gid in c14Gids) {
        // Tile projection is the top vertex, not the image's left edge.
        layer.setTileVisual(gid,((gid-1)%10)*64,((gid-1)/10).tointeger()*46,64,46,32.0,0.0,0.0);
    }
    foreach(file,gids in c14Variants) foreach(gid in gids)
        layer.setTileVisual(gid,((gid-1)%10)*64,((gid-1)/10).tointeger()*46,64,46,32.0,0.0,0.0);
    cursorLayer.setTileset(cursorTexture,1,1,0,0);
    cursorLayer.setTilesetTileSize(64,46);
    cursorLayer.setTileVisual(1,0,0,64,46,32.0,0.0,0.0);
    cursorLayer.setLayer(20);
}
function variant(kind,x,y) {
    local file=brushFiles[kind][materialVariant%brushFiles[kind].len()];
    local choices=c14Variants[file];
    return choices[(x*31+y*17+x*y*3+seed)%choices.len()];
}
function registerBuildings() {
    // Generated cottage and tower sprites are projected by the presentation layers.
    bld.registerBuildingsFromJson(@"[
      {""id"":""house"",""displayName"":""石基木屋"",""footprintW"":2,""footprintH"":2,
       ""requireTerrain"":[1],""rotationMode"":""none"",
       ""visual2d"":{""colorR"":""0.72"",""colorG"":""0.48"",""colorB"":""0.28""}},
      {""id"":""dock"",""displayName"":""木码头"",""footprintW"":2,""footprintH"":1,
       ""requireTerrain"":[2],""rotationMode"":""cardinal"",
       ""visual2d"":{""colorR"":""0.45"",""colorG"":""0.60"",""colorB"":""0.68""}},
      {""id"":""tower"",""displayName"":""石质哨塔"",""footprintW"":1,""footprintH"":1,
       ""requireTerrain"":[1],""rotationMode"":""none"",
       ""visual2d"":{""colorR"":""0.64"",""colorG"":""0.55"",""colorB"":""0.42""}}
    ]");
}
function resetMap() {
    // Detach the old world before destroying it, then bind the new one.
    if (session!=null) { session.destroy(); session=null; }
    if (world!=null) { fx.detach(world); world.destroy(); world=null; }
    for(local y=0;y<MAP_H;++y) for(local x=0;x<MAP_W;++x) {
        local kind=0;
        if (x==4 || y==5) kind=1;
        if (x>=9 && y>=6) kind=(x>=11 && y>=8)?5:4;
        else if ((x==8 && y>=6) || (y==5 && x>=9)) kind=3;
        else if (x>=1 && x<=3 && y>=1 && y<=3) kind=2;
        layer.setTile(x,y,variant(kind,x,y));
    }
    world=bld.newWorld(MAP_W,MAP_H,TILE_W);
    world.setId("c14-isometric");
    world.bindTileLayer(layer);
    foreach(file,gid in c14Gids) {
        local water=file==14 || file==108 || file==114 || file==116;
        world.setTerrainGid(gid,water?2:1);
    }
    foreach(file,gids in c14Variants) foreach(gid in gids)
        world.setTerrainGid(gid,(file==14 || file==108 || file==114 || file==116)?2:1);
    session=bld.newSession();
    session.startPlacement(world,palette[paletteIndex]);
    foreach(entry in [["house",2,2],["tower",6,3]]) {
        session.startPlacement(world,entry[0]);
        session.updateFromWorld(world,ORIGIN_X+(entry[1]-entry[2])*TILE_W*0.5,
            ORIGIN_Y+(entry[1]+entry[2]+1)*TILE_H*0.5);
        if(!session.isValid() || session.execute()<=0) throw "Building scene seed failed";
    }
    session.startPlacement(world,palette[paletteIndex]);
    fx.attach(world);
    fx.setGridVisible(world,gridVisible);
    bld.clearChangeEvents();
    cursorLayer.fill(0);
    buildingLayer.fill(0);buildingPreview.fill(0);
}
function setBrush(index) { brushIndex=index; materialVariant=0; paintMode=true; notice="地表 / "+brushNames[index]+"：左键拖动铺设"; }
function selectBuilding(index) {
    paletteIndex=index; paintMode=false;
    session.startPlacement(world,palette[index]);
    notice="建筑 / "+bld.getBuildingDisplayName(palette[index]);
}
function inside(x,y) { return x>=0 && y>=0 && x<MAP_W && y<MAP_H; }
function paintCell(x,y) {
    if (!inside(x,y)) return;
    if (world.getOccupant(x,y)>0) { notice="先拆除建筑，再修改它下面的地形。"; return; }
    layer.setTile(x,y,variant(brushIndex,x,y));
}
function refreshHud() {
    ui.select("hud");
    ui.setText("stats",(paintMode?"地表 / "+brushNames[brushIndex]:"建筑 / "+bld.getBuildingDisplayName(palette[paletteIndex]))+
        " / 样式 "+(materialVariant+1)+"\n建筑数 "+world.getBuildingCount()+" · 样式种子 "+seed);
    ui.setText("status",notice);
}
eve_init = function() {
    gfx.setBackgroundColor(0.055,0.075,0.085,1.0);
    loadArt();
    if (bld==null) bld=eve.Building();
    if (mapMod==null) mapMod=eve.Map();
    if (fx==null) fx=eve.BuildingFx();
    if (layer==null) layer=newIsoLayer();
    if (cursorLayer==null) cursorLayer=newIsoLayer();
    if (buildingLayer==null) buildingLayer=newIsoLayer();
    if (buildingPreview==null) buildingPreview=newIsoLayer();
    configureTerrainArt(); registerBuildings();
    if (world==null) resetMap();
    if (!uiBuilt) {
        ui.beginBuild(); ui.beginWindow("C14 / 等距地表工坊","root");
        ui.textWrapped("",288.0,"stats");
        ui.separator("terrain-line"); ui.text("地表画笔 · 1—6","terrain-title");
        for(local i=0;i<brushNames.len();i+=2) {
            ui.beginRow("brush-row-"+i,12.0);
            ui.button((i+1)+"  "+brushNames[i],"brush-"+i);
            ui.button((i+2)+"  "+brushNames[i+1],"brush-"+(i+1));ui.end();
        }
        ui.button("切换同类材质 V","variant");
        ui.separator("building-line"); ui.text("建筑放置","building-title");
        ui.button("房屋 2×2","house");ui.button("码头 2×1 · 水域","dock");ui.button("塔楼 1×1","tower");
        ui.button("切换网格 T","grid"); ui.button("重置地图 N","reset");
        ui.textWrapped("左键拖动铺地 / 单击建造\n右键拆除 · R 旋转 · B 建造\n重置会恢复示范建筑。\n木屋与哨塔使用生成的透明贴图。",288.0,"help");
        ui.textWrapped("",288.0,"status");ui.end();ui.mountBuildAs("hud");ui.select("hud");
        ui.setHostPos(920.0,20.0,0.0,0.0);ui.setHostSize(340.0,760.0);
        ui.setHostMovable(false);ui.setHostResizable(false);uiBuilt=true;
    }
    refreshHud(); print("BUILDING_TILEMAP_READY\n");
};
eve_reload <- function() { loadArt(); configureTerrainArt(); registerBuildings(); };
eve_update = function(dt) {
    local click=ui.consumeClick();
    while(click!="") {
        for(local i=0;i<6;++i) if(click=="hud/brush-"+i) setBrush(i);
        if(click=="hud/variant") {materialVariant=(materialVariant+1)%brushFiles[brushIndex].len();paintMode=true;}
        if(click=="hud/house") selectBuilding(0);
        if(click=="hud/dock") selectBuilding(1);
        if(click=="hud/tower") selectBuilding(2);
        if(click=="hud/grid") { gridVisible=!gridVisible; fx.setGridVisible(world,gridVisible); }
        if(click=="hud/reset") { ++seed;resetMap(); }
        click=ui.consumeClick();
    }
    for(local i=0;i<6;++i) if(key_just_pressed((i+1).tostring())) setBrush(i);
    if(key_just_pressed("v")) {materialVariant=(materialVariant+1)%brushFiles[brushIndex].len();paintMode=true;}
    if(key_just_pressed("b")) selectBuilding(paletteIndex);
    if(key_just_pressed("r")) session.rotateBy(90.0);
    if(key_just_pressed("t")) { gridVisible=!gridVisible;fx.setGridVisible(world,gridVisible); }
    if(key_just_pressed("n")) { ++seed;resetMap(); }
    local mx=mouse.getX(),my=mouse.getY();
    local x=layer.worldToTileX(mx,my),y=layer.worldToTileY(mx,my);
    local active=inside(x,y) && mx<900;
    local left=mouse.isDown(1),right=mouse.isDown(2);
    session.updateFromWorld(world,mx,my);
    if(active && left) {
        if(paintMode) paintCell(x,y);
        else if(!prevMouse.left) {
            local id=session.execute();notice=id>0?"建造完成 #"+id:"无法建造："+session.getReason();
        }
    }
    if(active && right && !prevMouse.right) {
        session.setMode("remove");local id=session.execute();session.setMode("place");
        notice=id>0?"已拆除 #"+id:"此格没有建筑";
    }
    prevMouse.left=left;prevMouse.right=right;
    syncBuildingArt();buildingPreview.fill(0);cursorLayer.fill(0);
    if(active) {
        local w=paintMode?1:bld.getBuildingFootprintW(palette[paletteIndex]);
        local h=paintMode?1:bld.getBuildingFootprintH(palette[paletteIndex]);
        if(!paintMode && (session.getRotationDeg()/90).tointeger()%2!=0) {local swap=w;w=h;h=swap;}
        for(local dy=0;dy<h;++dy) for(local dx=0;dx<w;++dx)
            if(inside(x+dx,y+dy)) cursorLayer.setTile(x+dx,y+dy,1);
        local ok=paintMode?world.getOccupant(x,y)==0:session.isValid();
        if(!paintMode) {
            buildingPreview.setTile(x,y,buildingGid(palette[paletteIndex],session.getRotationDeg()));
            buildingPreview.setTint(ok?0.8:1.0,ok?1.0:0.35,ok?0.8:0.35,0.65);
        }
        cursorLayer.setTint(ok?0.7:1.0,ok?0.95:0.25,ok?0.55:0.2,1.0);
    }
    refreshHud();
};
eve_render = function() {
    gfx.clear();mapMod.render(gfx);
    if(gridVisible) fx.drawGrid2D(world,gfx);
    ui.beginFrameAndRender();
};
