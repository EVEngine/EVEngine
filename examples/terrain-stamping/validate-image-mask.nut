// Full-host contract check: real ImageData, owned procgen handles and adapter binding.
function validateTerrainImageMaskAdapter() {
    local image=eve.Image();
    local input=heightmap(2,2), target=heightmap(2,2), curve=heightmap(2,1);
    curve.setHeight(0,0,0.0); curve.setHeight(1,0,1.0);
    for(local z=0;z<2;++z) for(local x=0;x<2;++x) input.setHeight(x,z,1.0);
    local settings=eve.TerrainImageMaskSettings();
    local pixels=image.newEmptyImageData(2,2,"RGBA32F");
    pixels.setPixel(0,0,0.25,0.0,0.0,1.0);
    pixels.setPixel(1,0,0.375,0.0,0.0,1.0);
    pixels.setPixel(0,1,0.625,0.0,0.0,1.0);
    pixels.setPixel(1,1,0.75,0.0,0.0,1.0);
    requireResult(eve.generateTerrainImageMaskFromImage(target,input,pixels,curve,settings,2,0),"image mask float");
    assert(target.height(0,0)==0.0 && target.height(1,0)==0.25);
    assert(target.height(0,1)==0.75 && target.height(1,1)==1.0);
    local bytes=image.newEmptyImageData(1,1,"RGBA8");
    bytes.setPixel(0,0,0.5,0.25,0.75,1.0);
    requireResult(eve.generateTerrainImageMaskFromImage(target,input,bytes,curve,settings,2,0),"image mask rgba8");
    assert(abs(target.height(0,0)-(2.0*128.0/255.0-0.5))<0.000001);
    local before=target.height(0,0);
    settings.scaleX=0.0;
    assert(!eve.generateTerrainImageMaskFromImage(target,input,bytes,curve,settings,2,0).ok);
    assert(target.height(0,0)==before);
    settings.scaleX=1.0;
    pixels.setPixel(0,0,1e40,0.0,0.0,1.0);
    assert(!eve.generateTerrainImageMaskFromImage(target,input,pixels,curve,settings,2,0).ok);
    assert(target.height(0,0)==before);
    print("TERRAIN_IMAGE_ADAPTER_PASS\n");
}
