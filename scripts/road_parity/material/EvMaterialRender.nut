dofile("mesh_data.nut");
persist parityMesh=null;
persist parityCamera=null;
persist parityMaterial=null;
persist paritySourceMesh=null;
persist roadShader=null;
persist roadAlbedo=null;
persist roadNormal=null;
function eve_init() {
    local rc=gfx.getRenderControl(); rc.disable("gbuffer");rc.disable("gpuDriven");rc.compile();
    local tone=gfx.setSceneToneMapping("none");if(!tone.ok)throw "road tone mapping rejected";
    local loaded=gfx.loadMeshShaderSpv("","road-asphalt.frag.spv");if(!loaded.ok)throw "road shader rejected";roadShader=loaded.value;
    local surface=gfx.configureMeshShaderSurface(roadShader,"opaque",true,false);if(!surface.ok)throw "road raster state rejected";
    roadAlbedo=gfx.newTextureFromFileRepeated("road-color.jpg",false,false);
    roadNormal=gfx.newTextureFromFileRepeated("road-normal.jpg",false,false);
    if(roadAlbedo==null || roadNormal==null)throw "road material textures missing";
    paritySourceMesh=gfx.newMeshFromArrays(parity_data.positions,parity_data.normals,parity_data.uvs,
        parity_data.positions.len()/3,parity_data.indices,parity_data.indices.len());
    if(paritySourceMesh==null)throw "road mesh upload failed";
    parityMaterial=gfx.newMaterial();parityMaterial.setShader(roadShader);
    parityMaterial.setAlbedoTexture(roadAlbedo);parityMaterial.setNormalTexture(roadNormal);
    parityMaterial.setTint(1.0,1.0,1.0,1.0);parityMaterial.setRoughness(0.88);parityMaterial.setMetallic(0.0);
    parityMesh=eve.Renderable3D();parityMesh.setMesh(paritySourceMesh);parityMesh.setMaterial(parityMaterial);
    parityMesh.setCastShadow(false);parityMesh.setReceiveShadow(false);
    parityCamera=eve.Camera3D();
    parityCamera.setEye(parity_view.eye[0].tofloat(),parity_view.eye[1].tofloat(),parity_view.eye[2].tofloat());
    parityCamera.setTarget(parity_view.target[0].tofloat(),parity_view.target[1].tofloat(),parity_view.target[2].tofloat());
    parityCamera.setUp(parity_view.up[0].tofloat(),parity_view.up[1].tofloat(),parity_view.up[2].tofloat());
    parityCamera.setFov(parity_view.fov.tofloat());parityCamera.setClipPlanes(0.1,250.0);
    parityCamera.setAutoExposure(false,-8.0,8.0);parityCamera.setExposure(0.0);parityCamera.setAmbient(0.08,0.08,0.08);parityCamera.setActive(true);
    gfx.setDirectionalLight(-0.4,1.0,0.7,1.0,1.0,1.0);
    gfx.setBackgroundColor(0.0732389559,0.0732389559,0.0732389559,1.0);
    print("ROAD_MATERIAL_READY common-v1 raw-UNORM manual-albedo-decode exposure0 toneNone world8m\n");
}
function eve_update(dt){}
function eve_render(){gfx.clear();gfx.render3D();}
