persist materialGalleryTextures=[]
persist materialGalleryIndex=0
local MATERIAL_GALLERY_NAMES=["preview", "realAssetPreview", "realAssetMap16x16Preview", "threeMaterialMap16x16Preview", "worldSpaceSplatPreview", "worldSpaceSandStonePreview", "worldSpaceFourMaterialPreview"];
eve_init=function() {
    gfx.setBackgroundColor(0.06,0.08,0.07,1.0);
    foreach(name in MATERIAL_GALLERY_NAMES) {
        local tex=gfx.newTextureFromFile("previews/"+name+".png");
        if(tex==null) throw "Missing material preview: run dual_grid_material_bake";
        materialGalleryTextures.append(tex);
    }
    print("dual-grid-material-gallery ready: 1-7 select material previews\n");
};
eve_update=function(dt) {
    for(local i=0;i<7;++i) if(key_just_pressed((i+1).tostring())) materialGalleryIndex=i;
};
eve_render=function() {
    gfx.clear();
    local tex=materialGalleryTextures[materialGalleryIndex];
    local scaleX=gfx.getWidth().tofloat()/tex.getWidth(),scaleY=gfx.getHeight().tofloat()/tex.getHeight();
    local scale=scaleX<scaleY?scaleX:scaleY;
    local w=tex.getWidth()*scale,h=tex.getHeight()*scale;
    gfx.drawTexturedRect(tex,(gfx.getWidth()-w)*0.5,(gfx.getHeight()-h)*0.5,w,h,1.0,1.0,1.0,1.0);
};
