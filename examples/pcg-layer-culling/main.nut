persist pcgCullCamera = null
persist pcgCullObjects = []
function pcgCullObject(mesh,x,y,z,sx,sy,sz,r,g,b,layer){local o=eve.Renderable3D();o.setMesh(mesh);o.setPosition(x,y,z);o.setScale(sx,sy,sz);o.setTint(r,g,b,1.0);o.setRoughness(0.72);o.setLayer(layer);o.setCastShadow(true);pcgCullObjects.push(o);return o;}
eve_init=function(){
 gfx.setBackgroundColor(0.025,0.045,0.075,1.0);pcgCullCamera=eve.Camera3D();pcgCullCamera.setEye(0.0,5.0,14.0);pcgCullCamera.setTarget(0.0,0.8,-12.0);pcgCullCamera.setClipPlanes(0.1,120.0);pcgCullCamera.setAmbient(0.24,0.28,0.36);pcgCullCamera.setLayerCullDistance(8,32.0);pcgCullCamera.setShadowLayerCullDistance(8,26.0);
 local cube=gfx.newMeshCube(1.0);pcgCullObject(cube,0.0,-1.0,-14.0,22.0,0.5,42.0,0.12,0.18,0.13,0);pcgCullObject(cube,-4.0,1.0,-7.0,2.4,4.0,2.4,0.18,0.68,0.38,8);pcgCullObject(cube,0.0,1.0,-13.0,2.4,4.0,2.4,0.18,0.48,0.82,8);pcgCullObject(cube,4.0,1.0,-16.0,2.4,4.0,2.4,0.82,0.48,0.16,8);pcgCullObject(cube,0.0,4.0,-70.0,8.0,10.0,8.0,1.0,0.05,0.05,8);print("PCG_LAYER_CULLING_READY layer=8 cameraDistance=32 shadowDistance=26 farTower=culled\n");
};
eve_update=function(dt){};eve_render=function(){gfx.clear();gfx.render3D();};
