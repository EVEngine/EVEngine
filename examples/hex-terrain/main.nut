persist hexWorld = null
persist hexCamera = null
persist hexYaw = 0.72
persist hexPitch = 0.62
persist hexSeed = 20260825
persist hexTime = 0.0

function readText(path) {
    local f=file(path,"r"); if(f==null) return null;
    local s=f.read(); f.close(); return s;
}

function rebuildWorld() {
    local p=procgen.newParams();
    p.setInt("width",38); p.setInt("height",28); p.setInt("seed",hexSeed);
    p.setFloat("radius",0.62); p.setFloat("seaLevel",0.43);
    p.setFloat("heightScale",3.8); p.setInt("riverCount",6);
    p.setBool("decorations",true); p.setFloat("vegetationDensity",1.15);
    local mesh=procgen.generateMesh("mesh.hexterrain",p,gfx);
    if(mesh==null){ print("hex terrain: "+procgen.lastError()+"\n"); return; }
    local shader=gfx.newMeshShaderVF(readText("shaders/hex_terrain.vert"),
                                     readText("shaders/hex_terrain.frag"));
    hexWorld=eve.Renderable3D(); hexWorld.setMesh(mesh); hexWorld.setShader(shader);
    hexWorld.setTint(1.0,1.0,1.0,1.0); hexWorld.setCastShadow(true);
}

eve_init=function(){
    rebuildWorld();
    hexCamera=eve.Camera3D(); hexCamera.setFov(45.0); hexCamera.setAmbient(0.36,0.40,0.46);
    hexCamera.setActive(true);
    gfx.setBackgroundColor(0.075,0.11,0.17,1.0);
    gfx.setDirectionalLight(-0.55,0.78,0.32,1.55,1.38,1.12);
};

eve_update=function(dt){
    hexTime+=dt;
    gfx.setCloudShadows(0.22,5.5,hexTime,0.35,0.38,0.56,0.65);
    if(key_just_pressed("r")||key_just_pressed("R")){hexSeed=procgen.randomSeed();rebuildWorld();}
    hexYaw+=dt*0.045;
    local cx=17.0,cz=15.0,dist=25.5;
    hexCamera.setEye(cx+dist*cos(hexPitch)*sin(hexYaw),dist*sin(hexPitch)+7.0,
                     cz+dist*cos(hexPitch)*cos(hexYaw));
    hexCamera.setTarget(cx,0.8,cz);
};

eve_render=function(){gfx.clear();gfx.render3D();};
