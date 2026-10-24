// Original screenshot-only test recipes, now a continuously running gallery.
persist galleryObject=null
persist galleryCamera=null
persist galleryTexture=null
persist galleryImage=null
persist galleryIndex=0
persist galleryYaw=0.0
persist galleryPaused=false
persist galleryResources=[null,null,null,null,null]
persist galleryStages=[]
persist galleryShowStages=false
persist galleryStageIndex=0
local GALLERY_STAGE_FILES=["step1_blue_noise_points","step2_leaves_on_plane","step3_only_leaves_flat",
    "step4_planes_around_y","step5_spherical_normals","step6_tree"];
local GALLERY_RECIPES=["tree","bush","lsystem","hexplanet","skyscraper"];
function preset_tree(p) {
    p.setSeed(31415);
    p.setString("style", "lowpoly");
    p.setString("branchAlgorithm", "spaceColonization");
    p.setFloat("leafDensity", 0.82);
    p.setFloat("height", 6.2);
    p.setFloat("crownRadius", 2.15);
    p.setInt("branchLevels", 3);
    p.setInt("branchCount", 8);
    p.setInt("attractorCount", 120);
    p.setInt("colonizationIterations", 38);
    p.setFloat("branchInertia", 1.15);
    p.setFloat("tropism", 0.20);
    p.setFloat("droop", 0.08);
    p.setFloat("growthStep", 0.25);
    p.setFloat("maxTurnAngle", 18.);
    p.setInt("maxChildren", 2);
    p.setString("leafMode","canopy");
    p.setFloat("clusterSize",0.30);
    p.setFloat("clusterLeafScale",0.85);
    p.setFloat("clusterSpacing",0.80);
    p.setFloat("clusterSeparation",0.55);
    p.setInt("clusterPlanes",10);
    p.setInt("clusterCaps",2);
    p.setInt("clusterLeaves",28);
    p.setInt("clusterLimit",120);
    p.setFloat("leafSize",0.465);
}

function preset_bush(p) {
    p.setSeed(20260815);
    p.setString("style", "mound");
    p.setString("leafMode", "mixed");
    p.setFloat("height", 1.7);
    p.setFloat("width", 2.6);
    p.setInt("blobs", 12);
    p.setInt("rings", 5);
    p.setInt("radialSegments", 12);
    p.setFloat("leafDensity", 0.8);
    p.setFloat("leafSize", 0.32);
    p.setInt("twigs", 6);
}

function preset_lsystem(p) {
    p.setSeed(20260826);
    p.setInt("iterations", 5);
    p.setString("leafMode", "cards");
    p.setFloat("tropism", 0.05);
    p.setFloat("leafSize", 0.5);
    p.setString("style","tree");
}

function preset_hexplanet(p) {
    p.setFloat("radius", 1.);
    p.setInt("subdivisions", 3);
    p.setFloat("tileInset", 0.12);
}

function preset_skyscraper(p) {
    p.setSeed(99);
    p.setFloat("baseWidth", 10.);
    p.setFloat("baseDepth", 10.);
    p.setInt("tiers", 6);
    p.setFloat("tierHeight", 6.);
    p.setFloat("setback", 0.08);
    p.setInt("windowCols", 6);
    p.setInt("windowRows", 5);
    p.setFloat("windowDepth", 0.06);
    p.setFloat("spireHeight", 5.);
}

function galleryChecked(result,operation) {
    if(!result.ok) throw operation+": "+result.status.summary;
    return result.value;
}
function galleryBuild(index) {
    if(galleryResources[index]==null) {
        local p=galleryChecked(procgen.newParams(),"gallery parameters");
        local presets=[preset_tree,preset_bush,preset_lsystem,preset_hexplanet,preset_skyscraper];
        presets[index](p);
        local mesh=galleryChecked(procgen.generateMesh("mesh."+GALLERY_RECIPES[index],p,gfx),"gallery mesh");
        local palettes=[[[111,70,42],[128,82,47],[64,119,57],[82,145,67]],
                       [[104,67,38],[84,132,60]],
                       [[111,70,42],[128,82,47],[64,119,57],[82,145,67]],
                       [[42,155,181]],
                       [[90,96,104],[120,128,138],[240,236,220],[200,205,215]]];
        local colors=palettes[index];
        local width=index==4?2:colors.len(),height=index==4?2:1;
        galleryImage=eve.Image().newEmptyImageData(width,height,"RGBA8");
        foreach(i,c in colors) galleryImage.setPixel(i%width,i/width,c[0]/255.0,c[1]/255.0,c[2]/255.0,1.0);
        galleryTexture=gfx.newTexture(galleryImage,false,false);
        galleryResources[index]=[mesh,galleryTexture,galleryImage];
    }
    galleryObject.setMesh(galleryResources[index][0]);
    galleryTexture=galleryResources[index][1];
    galleryObject.setTexture(galleryTexture);
    galleryYaw=index==0?-18.0:0.0;
    local eyes=[[9.2,5.1,11.4],[5.2,3.2,5.6],[7.5,5.2,9.5],[2.65,1.55,3.05],[50.0,40.0,60.0]];
    local targets=[3.1,0.9,3.2,0.0,18.0];
    local eye=eyes[index];galleryCamera.setEye(eye[0],eye[1],eye[2]);
    galleryCamera.setTarget(0.0,targets[index],0.0);galleryCamera.setFov(index==4?42.0:36.0);
    if(index>=3) gfx.setDirectionalLight(0.55,0.85,1.1,1.15,0.98,0.88);
    else gfx.setDirectionalLight(-0.55,-1.0,-0.35,1.45,1.32,1.08);
    print("procedural-render-gallery: "+GALLERY_RECIPES[index]+" (1-5 select, Space pause)\n");
}
eve_init=function() {
    gfx.setBackgroundColor(0.075,0.105,0.095,1.0);
    galleryCamera=eve.Camera3D();galleryCamera.setActive(true);
    galleryCamera.setAmbient(0.30,0.34,0.28);
    gfx.setDirectionalLight(-0.55,-1.0,-0.35,1.45,1.32,1.08);
    galleryObject=eve.Renderable3D();galleryObject.setRoughness(0.88);
    galleryObject.setCamera(galleryCamera);galleryObject.setCastShadow(false);
    foreach(name in GALLERY_STAGE_FILES) {
        local texture=gfx.newTextureFromFile("stages/"+name+".png");
        if(texture==null) throw "Missing foliage stage: run foliage_stage_render";
        galleryStages.append(texture);
    }
    galleryBuild(galleryIndex);
};
eve_update=function(dt) {
    if(key_just_pressed("f")) galleryShowStages=!galleryShowStages;
    if(galleryShowStages) {
        for(local i=0;i<6;++i) if(key_just_pressed((i+1).tostring())) galleryStageIndex=i;
        return;
    }
    for(local i=0;i<5;++i) if(key_just_pressed((i+1).tostring()) && galleryIndex!=i) {
        galleryIndex=i;galleryBuild(i);
    }
    if(key_just_pressed("space")) galleryPaused=!galleryPaused;
    if(!galleryPaused) galleryYaw+=dt*7.0;
    galleryObject.setYaw(galleryYaw);
};
eve_render=function() {
    gfx.clear();
    if(!galleryShowStages) { gfx.render3D(); return; }
    local texture=galleryStages[galleryStageIndex];
    local sx=gfx.getWidth().tofloat()/texture.getWidth(),sy=gfx.getHeight().tofloat()/texture.getHeight();
    local scale=sx<sy?sx:sy,w=texture.getWidth()*scale,h=texture.getHeight()*scale;
    gfx.drawTexturedRect(texture,(gfx.getWidth()-w)*0.5,(gfx.getHeight()-h)*0.5,w,h,1.0,1.0,1.0,1.0);
};
