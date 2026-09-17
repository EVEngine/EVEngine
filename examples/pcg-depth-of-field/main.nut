persist dofCamera=null
persist dofObjects=[]
persist dofFocusState=null
persist dofFocusOutput=null
function dofObject(mesh,x,y,z,s,r,g,b){local o=eve.Renderable3D();o.setMesh(mesh);o.setPosition(x,y,z);o.setScale(s,s,s);o.setTint(r,g,b,1.0);o.setRoughness(0.62);dofObjects.push(o);return o;}
eve_init=function(){
 gfx.setBackgroundColor(0.025,0.04,0.07,1.0);dofCamera=eve.Camera3D();dofCamera.setEye(0.0,3.5,12.0);dofCamera.setTarget(0.0,1.0,-14.0);dofCamera.setClipPlanes(0.1,80.0);dofCamera.setAmbient(0.3,0.32,0.38);
 local cube=gfx.newMeshCube(1.0);local ground=eve.Renderable3D();ground.setMesh(cube);ground.setPosition(0.0,-1.3,-18.0);ground.setScale(28.0,0.5,48.0);ground.setTint(0.12,0.16,0.14,1.0);dofObjects.push(ground);dofObject(cube,-3.2,1.0,-7.0,2.5,0.18,0.82,0.42);dofObject(cube,0.0,1.0,-8.0,2.5,0.2,0.68,0.95);dofObject(cube,3.2,1.0,-9.0,2.5,0.9,0.72,0.18);
 for(local i=-4;i<=4;i+=2)dofObject(cube,i.tofloat(),2.0,-38.0,2.2,0.92,0.16,0.2);
 local rc=gfx.getRenderControl();rc.enable("gbuffer");rc.compile();
 local settings=eve.DepthOfFieldFocusSettings();settings.tracking=0;settings.maximumDistance=100.0;settings.response=3.5;local input=eve.DepthOfFieldFocusInput();input.hasRayHit=true;input.rayHitDistance=20.0;input.deltaSeconds=1.0;dofFocusState=eve.DepthOfFieldFocusState();dofFocusOutput=eve.DepthOfFieldFocusOutput();local result=eve.evaluateDepthOfFieldFocus(dofFocusState,dofFocusOutput,settings,input);if(!result.ok)throw "pcg-dof focus failed";
 dofCamera.setDepthOfField(dofFocusOutput.focusDistance,5.0,12.0);
 print("PCG_DEPTH_OF_FIELD_READY focus="+dofFocusOutput.focusDistance+" aperture="+dofFocusOutput.aperture+" gpuComposite=active\n");
};
eve_update=function(dt){};
eve_render=function(){gfx.clear();gfx.render3D();};
