// Packed brush and four geometry layers; all scene distances match the visible solids.
dofile("parameters.nut");
paintLab <- { camera=null, sun=null, objects=[], materials=[], shaders=[], outlines=[], time=0.0, yaw=0.12,
              paused=true, solidErosion=true, sdfShadow=true, ao=true, edges=true, brush=null };
function makeShader(layer,owner,smoothMode,reflection) {
    local loaded=gfx.loadMeshShaderSpv("assets/impasto.vert.spv","assets/impasto.frag.spv");
    if(!loaded.ok)throw "Impasto shader: "+loaded.status;
    local sh=loaded.value;
    foreach(p in impastoParameters){sh.declareFloat(p[0]);sh.sendFloat(p[0],p[1]);}
    sh.sendFloat("packedBrush",1.0);sh.sendFloat("layer",layer.tofloat());
    sh.sendFloat("sdfOwner",owner.tofloat());sh.sendFloat("smoothNormalMode",smoothMode.tofloat());
    sh.sendFloat("occlusionStrength",paintLab.ao?1.0:0.0);
    sh.sendFloat("sdfShadowStrength",paintLab.sdfShadow?1.0:0.0);
    sh.sendFloat("rimErosion",(paintLab.edges && paintLab.solidErosion)?0.95:0.0);sh.sendFloat("reflectionStrength",reflection);
    sh.sendFloat("sdfSphereAX",-0.35);sh.sendFloat("sdfSphereAY",1.05);sh.sendFloat("sdfSphereAZ",0.0);sh.sendFloat("sdfSphereASize",1.05);
    sh.sendFloat("sdfSphereBX",1.85);sh.sendFloat("sdfSphereBY",0.85);sh.sendFloat("sdfSphereBZ",0.2);sh.sendFloat("sdfSphereBSize",0.85);
    sh.sendFloat("sdfBoxX",-2.8);sh.sendFloat("sdfBoxY",1.0);sh.sendFloat("sdfBoxZ",0.0);sh.sendFloat("sdfBoxSize",1.0);
    if(layer==1){sh.sendFloat("shellWidth",0.043);}
    if(layer==2){sh.sendFloat("shellWidth",0.069);sh.sendFloat("shellOffset",-0.030);}
    if(layer==3){sh.sendFloat("shellWidth",0.095);sh.sendFloat("shellOffset",0.040);}
    paintLab.shaders.append(sh);return sh;
}
function addPart(mesh,position,scale,color,layer,owner,smoothMode,reflection) {
    local object=eve.Renderable3D();object.setMesh(mesh);
    object.setPosition(position[0],position[1],position[2]);object.setScale(scale[0],scale[1],scale[2]);
    local material=gfx.newMaterial();material.setShader(makeShader(layer,owner,smoothMode,reflection));
    material.setShadingModel("custom");material.setAlbedoTexture(paintLab.brush);
    material.setTint(color[0],color[1],color[2],1.0);material.setDoubleSided(true);
    object.setMaterial(material);object.setCastShadow(false);
    paintLab.materials.append(material);paintLab.objects.append(object);
    if(layer>0)paintLab.outlines.append(object);
    return object;
}
function addPainted(mesh,position,scale,color,owner,smoothMode) {
    for(local layer=3;layer>=0;--layer)addPart(mesh,position,scale,color,layer,owner,smoothMode,0.88);
}
eve_init = function() {
    if("impastoSolidErosion" in config)paintLab.solidErosion=config.impastoSolidErosion;
    if("impastoAO" in config)paintLab.ao=config.impastoAO;
    if("impastoShadow" in config)paintLab.sdfShadow=config.impastoShadow;
    if("impastoEdges" in config)paintLab.edges=config.impastoEdges;
    if("impastoYaw" in config)paintLab.yaw=config.impastoYaw;
    local rc=gfx.getRenderControl();
    foreach(feature in ["gbuffer","gbufferAlbedo","ao","gi","clusteredDeferred","shadow"])rc.disable(feature);
    rc.compile();gfx.setBackgroundColor(0.025,0.11,0.115,1.0);
    paintLab.camera=eve.Camera3D();paintLab.camera.setUp(0.0,1.0,0.0);
    paintLab.camera.setFov(38.0);paintLab.camera.setClipPlanes(0.1,50.0);paintLab.camera.setActive(true);
    paintLab.camera.setTarget(-0.55,0.95,0.0);
    paintLab.sun=eve.Light3D();paintLab.sun.setType("dir");
    paintLab.sun.setDirection(0.85,0.6,-0.45);paintLab.sun.setColor(1.0,0.96,0.91,1.0);
    gfx.setDirectionalLight(0.85,0.6,-0.45,1.0,0.96,0.91);
    local image=eve.Image();local data=image.newImageDataFromFile("assets/brush-packed.png");
    if(data==null)throw "Missing packed brush texture";
    paintLab.brush=gfx.newTexture(data,true,true);
    addPainted(gfx.newMeshSphere(96,64),[-0.35,1.05,0.0],[1.05,1.05,1.05],[0.16,0.45,0.52],1,0);
    addPainted(gfx.newMeshSphere(96,64),[1.85,0.85,0.2],[0.85,0.85,0.85],[0.55,0.25,0.52],2,0);
    addPainted(gfx.newMeshCube(2.0),[-2.8,1.0,0.0],[1.0,1.0,1.0],[0.23,0.54,0.48],3,1);
    addPart(gfx.newMeshCube(1.0),[0.0,-0.1,0.0],[15.0,0.2,12.0],[0.93,0.72,0.48],0,0,0,0.1);
    foreach(object in paintLab.outlines)object.setVisible(paintLab.edges);
    print("[impasto] ready: packed brush / real analytic SDF AO + shadows / eroded solid / inverted hull + two offset outlines\n");
};
eve_update = function(dt) {
    if(key_just_pressed("space"))paintLab.paused=!paintLab.paused;
    if(!paintLab.paused)paintLab.yaw+=dt*0.08;
    if(key_just_pressed("a"))paintLab.yaw-=0.15;
    if(key_just_pressed("d"))paintLab.yaw+=0.15;
    paintLab.camera.setEye(-0.55+sin(paintLab.yaw)*9.8,3.8,cos(paintLab.yaw)*9.8);
    if(key_just_pressed("o")){
        paintLab.ao=!paintLab.ao;
        foreach(sh in paintLab.shaders){sh.sendFloat("occlusionStrength",paintLab.ao?1.0:0.0);sh.sendFloat("sdfShadowStrength",paintLab.ao?1.0:0.0);}
    }
    if(key_just_pressed("e")){
        paintLab.edges=!paintLab.edges;
        foreach(object in paintLab.outlines)object.setVisible(paintLab.edges);
        foreach(sh in paintLab.shaders)sh.sendFloat("rimErosion",(paintLab.edges && paintLab.solidErosion)?0.95:0.0);
    }
};
eve_render = function() {gfx.render3D();};
