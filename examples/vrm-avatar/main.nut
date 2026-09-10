persist avatar = null;
persist camera = null;
persist light = null;
persist elapsed = 0.0;
persist expressionIndex = 0;
persist activeExpressions = ["neutral", "happy", "angry", "sad", "relaxed", "surprised"];
persist autoPose = true;
persist previousSpace = false;
persist previousP = false;
function checked(result) { if(!result.ok) throw result.status.summary; }
eve_init = function() {
    gfx.setBackgroundColor(0.13,0.16,0.21,1.0);
    avatar = eve.Avatar().newVroidAvatar();
    checked(avatar.loadVroidModel("model.vrm"));
    camera = eve.Camera3D(); camera.setEye(0.0,1.25,3.2); camera.setTarget(0.0,0.92,0.0); camera.setUp(0.0,1.0,0.0); camera.setFov(35.0); camera.setAmbient(0.3,0.3,0.3); camera.setActive(true);
    light = eve.Light3D(); light.setType("dir"); light.setDirection(0.3,0.7,1.0); light.setColor(1.0,0.98,0.95,0.8);
    print("VRM_READY version="+avatar.getVroidVersion()+" meshes="+avatar.getVroidMeshCount()+" springs="+avatar.getVroidSpringCount()+" expressions="+avatar.getExpressionCount()+"\n");
};
eve_update = function(dt) {
    elapsed += dt;
    local space=keyboard.isDown("space"), p=keyboard.isDown("p");
    if(space && !previousSpace) { expressionIndex=(expressionIndex+1)%activeExpressions.len(); avatar.setExpression(activeExpressions[expressionIndex]); }
    if(p && !previousP) autoPose=!autoPose;
    previousSpace=space; previousP=p;
    if(autoPose) checked(avatar.setHumanoidBoneRotation("head",0.12*sin(elapsed),0.0,0.035*sin(elapsed*0.7)));
    avatar.setLookAtTarget(0.7*sin(elapsed*0.65),1.5,2.0);
    local phase=(elapsed%4.0)-3.7; if(phase<0) phase=-phase;
    local blink=1.0-phase/0.12; if(blink<0) blink=0.0;
    avatar.setParameter("blink",blink);
    avatar.update(dt); avatar.sync();
};
eve_render = function() { gfx.clear(); gfx.render3D(); };
eve_quit = function() { if(avatar!=null) avatar.release(); };
