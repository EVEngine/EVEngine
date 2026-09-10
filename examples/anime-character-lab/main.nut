// Reproducible static character look-development scene. Assets remain local.
lab <- { data=null, parts=[], anime=[], legacy=null, textures={}, camera=null, ground=null, sun=null,
         frame=0, mode="anime", portrait=true, yaw=0.947, light=0, ready=false, assetMessage="" };

function setView(portrait) {
    lab.portrait=portrait;
    if(lab.ground!=null)lab.ground.setVisible(!portrait);
    local radius=portrait?3.80:6.50;
    local targetY=portrait?2.05:1.48;
    lab.camera.setEye(sin(lab.yaw)*radius,portrait?2.35:1.75,cos(lab.yaw)*radius+0.25);
    lab.camera.setTarget(0.0,targetY,0.25);
}
function setLighting(index) {
    lab.light=index;
    local lights=[[0.65,0.7,0.45],[-0.65,0.5,0.45],[0.1,0.35,-0.9]];
    local l=lights[index];gfx.setDirectionalLight(l[0],l[1],l[2],1.0,1.0,1.0);
    if(lab.sun!=null)lab.sun.setDirection(l[0],l[1],l[2]);
}
function materialVariant(name) {
    if(name=="Skóra")return 1;
    if(name=="Włosy2")return 2;
    if(name=="OCZKI")return 3;
    if(name=="yellow")return 4;
    return 0;
}
function setStyle(mode) {
    lab.mode=mode;
    foreach(i,part in lab.parts) {
        if(part==null)continue;
        local name=lab.data.getMaterialName(lab.data.getMaterialIndex(i));
        part.getMaterial().setShader(mode=="anime"?lab.anime[materialVariant(name)]:lab.legacy);
    }
}
function reloadAnime() {
    dofile("shaders/compiled.nut");
    foreach(sh in lab.anime) {
        local result=gfx.replaceShaderFromSpv(sh,animeVertex,animeFragment);
        if(!result.ok)throw result.status;
    }
}
function loadPart(i) {
    local idx=lab.data.getMaterialIndex(i), name=lab.data.getMaterialName(idx);
    // The source has inverted black outline shells. Screen-space depth outlines
    // replace these shells so line width remains measured in screen pixels.
    if(name=="Black") { lab.parts.append(null); return; }
    local part=model3d.createRenderable(gfx,lab.data,i);
    part.setCastShadow(true);part.setReceiveShadow(true);part.setReceiveLight(true);
    part.setScale(0.005,0.005,0.005);part.setPosition(-1.8,1.5836,0.0);
    local mat=part.getMaterial(), tex=lab.data.getMaterialTexturePath(idx,"diffuse",0);
    if(tex!="") {
        if(!(tex in lab.textures))lab.textures[tex] <- gfx.newTextureFromFile("assets/"+tex);
        mat.setAlbedoTexture(lab.textures[tex]);mat.setTint(1.0,1.0,1.0,1.0);
    }
    mat.setShader(lab.anime[materialVariant(name)]);mat.setShadingModel("custom");
    lab.parts.append(part);
}
eve_init = function() {
    gfx.setBackgroundColor(0.29,0.35,0.46,1.0);
    lab.camera=eve.Camera3D();lab.camera.setUp(0.0,1.0,0.0);
    lab.camera.setFov(35.0);lab.camera.setAmbient(0.65,0.65,0.65);lab.camera.setActive(true);
    lab.camera.setClipPlanes(0.1,30.0);
    lab.sun=eve.Light3D();lab.sun.setType("dir");lab.sun.setColor(1.0,1.0,1.0,1.0);
    lab.sun.setCastShadow(true);lab.sun.setShadowStrength(0.9);
    setView(true);setLighting(0);
    local rc=gfx.getRenderControl();rc.disable("ao");rc.enable("outline");rc.enable("msaa");rc.enable("aa");rc.compile();
    local outline=gfx.getOutline();outline.setColor(0.14,0.105,0.19);
    outline.setWidth(0.65);outline.setNormalThreshold(1.45);outline.setDepthThreshold(0.15);
    outline.setDepthSensitivity(0.03);outline.setSoftness(0.55);
    for(local variant=0;variant<5;++variant) {
        local sh=stylize.newMeshShader(gfx,"anime");
        sh.sendFloat("skin",variant==1?1.0:0.0);
        sh.sendFloat("hair",variant==2?1.0:0.0);
        sh.sendFloat("skinDetail",0.25);
        sh.sendFloat("unlit",variant==3?1.0:0.0);
        sh.sendFloat("surfaceSpecular",variant==4?0.7:0.0);
        if(variant==0) {
            sh.sendFloat("shadowThreshold",0.15);sh.sendFloat("fillGradient",0.13);
            sh.sendFloat("shadowR",0.53);sh.sendFloat("shadowG",0.57);sh.sendFloat("shadowB",0.75);
        }
        lab.anime.append(sh);
    }
    lab.legacy=stylize.newMeshShader(gfx,"cartoon");
    lab.ground=eve.Renderable3D();lab.ground.setMesh(gfx.newMeshCube(1.0));
    lab.ground.setScale(100.0,0.04,100.0);lab.ground.setPosition(0.0,-0.02,0.0);
    lab.ground.setTint(0.33,0.39,0.48,1.0);lab.ground.setCastShadow(false);
    lab.ground.setVisible(!lab.portrait);
    if(!file_exists("assets/witch.obj")) {
        lab.assetMessage="Model assets are not prepared. Run prepare.py as described in README.md, then restart.";
        print("[anime-lab] assets missing: "+lab.assetMessage+"\n");
        return;
    }
    lab.data=model3d.newModelDataFromFile("assets/witch.obj");
    if(lab.data==null)throw "The prepared witch.obj exists but could not be decoded";
};
eve_update = function(dt) {
    lab.frame++;
    if(lab.data==null)return;
    if(lab.parts.len()<lab.data.getMeshCount())loadPart(lab.parts.len());
    else if(!lab.ready) { lab.ready=true;print("[anime-lab] ready; 1 anime / 2 legacy / 3 framing / 4-6 lights / A,D orbit\n"); }
    if(key_just_pressed("1"))setStyle("anime");
    if(key_just_pressed("2"))setStyle("cartoon");
    if(key_just_pressed("3"))setView(!lab.portrait);
    if(key_just_pressed("4"))setLighting(0);
    if(key_just_pressed("5"))setLighting(1);
    if(key_just_pressed("6"))setLighting(2);
    if(key_just_pressed("a")) {lab.yaw-=0.18;setView(lab.portrait);}
    if(key_just_pressed("d")) {lab.yaw+=0.18;setView(lab.portrait);}
};
// render3D owns its color clear and submits depth/normal passes first.
eve_render = function() { gfx.render3D(); };
