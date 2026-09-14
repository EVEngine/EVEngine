persist avatar = null;
persist ready = false;
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
    ready = false;
    gfx.setBackgroundColor(0.13,0.16,0.21,1.0);
    avatar = eve.Avatar().newVroidAvatar();
    local bundled = !file_exists("model.vrm");
    local modelPath = bundled ? "assets/contract.vrm" : "model.vrm";
    checked(avatar.loadVroidModel(modelPath));
    activeExpressions = [];
    for (local i = 0; i < avatar.getExpressionCount(); ++i)
        activeExpressions.append(avatar.getExpressionName(i));
    expressionIndex = 0;
    print("VRM_SOURCE " + modelPath + "\n");
    camera = eve.Camera3D(); camera.setEye(0.0,1.25,3.2); camera.setTarget(0.0,0.92,0.0); camera.setUp(0.0,1.0,0.0); camera.setFov(35.0); camera.setAmbient(0.3,0.3,0.3); camera.setActive(true);
    if (bundled) { camera.setEye(0.0,0.4,2.0); camera.setTarget(0.0,0.3,0.0); }
    light = eve.Light3D(); light.setType("dir"); light.setDirection(0.3,0.7,1.0); light.setColor(1.0,0.98,0.95,0.8);
    ready = true;
    print("VRM_READY version="+avatar.getVroidVersion()+" meshes="+avatar.getVroidMeshCount()+" springs="+avatar.getVroidSpringCount()+" expressions="+avatar.getExpressionCount()+"\n");
};
eve_update = function(dt) {
    if (!ready) return;
    elapsed += dt;
    local space=keyboard.isDown("space"), p=keyboard.isDown("p");
    if(space && !previousSpace && activeExpressions.len() > 0) { expressionIndex=(expressionIndex+1)%activeExpressions.len(); avatar.setExpression(activeExpressions[expressionIndex]); }
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
