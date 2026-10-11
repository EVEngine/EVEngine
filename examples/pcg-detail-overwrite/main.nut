local retained=[];
local field=null;
local camera=null;

function keep(value) { retained.append(value); return value; }
function need(result, label) {
    if(!result.ok) throw "pcg-detail-overwrite: "+label+" failed";
    return result.value;
}

eve_init <- function() {
    gfx.setBackgroundColor(0.035,0.055,0.075,1.0);
    gfx.setDirectionalLight(-0.45,0.85,0.35,1.6,1.48,1.25);
    camera=keep(eve.Camera3D());
    camera.setEye(0.0,5.2,11.0);camera.setTarget(0.0,1.0,0.0);camera.setUp(0.0,1.0,0.0);
    camera.setFov(48.0);camera.setClipPlanes(0.1,100.0);camera.setAmbient(0.42,0.48,0.55);camera.setActive(true);

    local points=keep(need(procgen.newPointSet(),"point set"));
    for(local z=-8;z<=8;++z) for(local x=-12;x<=12;++x) {
        local px=x*0.38+(z%2)*0.17,pz=z*0.42;
        local i=points.add(px,0.0,pz);
        local h=0.75+((x*x+z*z)%7)*0.055;
        local w=0.7+((x+z+40)%5)*0.09;
        points.setScale(i,w,h,w);points.setStringAttribute(i,"asset","pcg:detail");
        points.setColor(i,0.48+((x+20)%4)*0.08,0.72+((z+20)%3)*0.07,0.28,1.0);
    }
    points.assignPointIds("4242");

    local imageApi=eve.Image();
    local albedo=keep(imageApi.newEmptyImageData(32,32,"RGBA8"));
    local normal=keep(imageApi.newEmptyImageData(albedo.getWidth(),albedo.getHeight(),"RGBA8"));
    local mask=keep(imageApi.newEmptyImageData(albedo.getWidth(),albedo.getHeight(),"RGBA8"));
    for(local y=0;y<albedo.getHeight();++y) for(local x=0;x<albedo.getWidth();++x) {
        local nx=(x-15.5)/15.5,ny=y/31.0;
        local blade=abs(nx)<(0.08+0.34*ny);
        albedo.setPixel(x,y,0.34+0.2*ny,0.72+0.2*ny,0.18,blade?1.0:0.0);
        normal.setPixel(x,y,0.5,0.5,1.0,1.0);mask.setPixel(x,y,0.05,0.82,0.22,0.72);
    }
    field=keep(gfx.newGrassField());
    local foliage=eve.GrassFoliageSettings();
    foliage.baseR=0.9;foliage.baseG=1.0;foliage.baseB=0.82;foliage.alphaCutoff=0.08;
    foliage.normalStrength=1.0;foliage.renderDistance=70.0;foliage.fadeRange=18.0;
    foliage.hardRenderDistance=110.0;foliage.density=1.0;
    foliage.snowMinimumHeight=100.0;foliage.snowFadeDistance=20.0;foliage.snowProgress=0.0;
    need(eve.bakeTerrainGrassFoliage(field,points,"pcg:detail",0.8,1.25,albedo,normal,mask,foliage),"foliage upload");
    local settings=eve.TerrainDetailOverwriteSettings();
    settings.pcgDetailDistance=70.0;settings.pcgFadeoutDistance=18.0;
    settings.unityDetailDistance=110;settings.unityDetailDensity=0.55;settings.detailResolutionPerPatch=8;
    local quality=need(field.setTerrainDetailOverwrite(settings),"detail overwrite");
    if(quality!=3 || field.getTerrainDetailHardDistance()!=110.0 || field.getTerrainDetailDensity()!=0.55)
        throw "detail overwrite contract mismatch";
    print("PCG_DETAIL_OVERWRITE_READY quality=High8 hard=110 density=0.55 fade=70+18 points="+points.getCount()+"\n");
};

eve_update <- function(dt) { if(field!=null) field.update(dt); };
eve_render <- function() { gfx.clear();gfx.render3D();if(field!=null) field.draw(); };
