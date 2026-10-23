// Independent visuals: weapon attachment, muzzle-to-target discharge, chain hop.
objects <- [];
bolts <- [];
camera <- null;
labelFont <- null;
effectTime <- 0.0;
nextPulse <- 0.0;
paused <- false;
lastSpace <- false;
function checked(result) {
    if (!result.ok) throw "Weapon lightning operation failed";
    return ("value" in result) ? result.value : null;
}
function prop(mesh,x,y,z,sx,sy,sz,r,g,b) {
    local e=eve.Renderable3D();e.setMesh(mesh);e.setPosition(x,y,z);e.setScale(sx,sy,sz);
    e.setTint(r,g,b,1.0);e.setMetallic(0.65);e.setRoughness(0.28);objects.push(e);return e;
}
function bolt(width,jitter,branches,duration,seed,r,g,b) {
    local s=eve.LightningSettings();s.width=width;s.displacement=jitter;s.branches=branches;
    s.duration=duration;s.seed=seed;s.variants=6;
    local fx=checked(weather.prepareLightning(gfx,s));checked(fx.setColor(r,g,b));bolts.push(fx);return fx;
}
bladeFx <- null;
shotFx <- null;
chainFx <- null;
blade <- null;
target <- null;
eve_init = function() {
    // Forward rendering keeps this visual lab independent of scene post effects.
    local render = gfx.getRenderControl();
    render.disable("ao"); render.disable("gi"); render.disable("gbuffer");
    render.disable("clustered"); render.disable("shadow");
    gfx.setBackgroundColor(0.012,0.018,0.035,1.0);
    gfx.setDirectionalLight(-0.4,0.8,0.6,1.1,1.25,1.6);
    camera=eve.Camera3D();camera.setEye(4.5,6.0,14.0);camera.setTarget(0.5,2.1,0.0);
    camera.setClipPlanes(0.1,100.0);camera.setFov(44.0);camera.setActive(true);
    labelFont=gfx.newFont(eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",24)," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:-");
    local cube=gfx.newMeshCube(1.0);local sphere=gfx.newMeshSphere(24,16);
    prop(cube,0.0,-0.2,0.0,18.0,0.3,10.0,0.035,0.05,0.09);
    for(local i=0;i<3;i++)prop(cube,-5.0+i*5.0,0.0,0.0,2.8,0.4,2.8,0.09,0.13,0.21);
    blade=prop(cube,-5.0,2.2,0.0,0.18,3.4,0.12,0.55,0.65,0.8);
    prop(cube,-5.0,0.8,0.0,1.1,0.14,0.25,0.3,0.24,0.12);
    prop(cube,-1.0,1.8,0.0,1.8,0.38,0.5,0.18,0.25,0.36);
    prop(cube,-1.5,1.2,0.0,0.35,0.9,0.45,0.12,0.16,0.22);
    prop(cube,-0.05,1.8,0.0,0.3,0.5,0.65,0.35,0.5,0.65);
    target=prop(sphere,3.5,2.6,0.0,0.6,0.6,0.6,0.12,0.38,0.55);
    prop(sphere,6.5,3.8,0.0,0.45,0.45,0.45,0.4,0.18,0.5);
    bladeFx=bolt(0.12,0.28,5,0.8,17,0.65,0.9,1.0);
    shotFx=bolt(0.20,0.48,4,0.65,42,0.7,0.85,1.0);
    chainFx=bolt(0.13,0.28,3,0.65,83,1.0,0.45,1.4);
    print("WEAPON_LIGHTNING_READY: pooled blade + discharge + chain\n");
};
eve_update = function(dt) {
    local down=keyboard.isDown("Space");if(down && !lastSpace)paused=!paused;lastSpace=down;
    if(paused)return;
    effectTime+=dt;
    local sway=sin(effectTime*1.3)*0.3;
    blade.setPosition(-5.0,2.2+sway,0.0);
    local tx=3.5+sin(effectTime)*0.7,ty=2.6+cos(effectTime*1.2)*0.5;
    target.setPosition(tx,ty,0.0);
    checked(bladeFx.setEndpoints(-5.0,0.7+sway,0.0,-5.0,3.9+sway,0.0));
    checked(shotFx.setEndpoints(-0.1,1.8,0.0,tx,ty,0.0));
    checked(chainFx.setEndpoints(tx,ty,0.0,6.5,3.8,0.0));
    foreach(fx in bolts)checked(fx.update(dt));
    nextPulse-=dt;
    if(nextPulse<=0.0){foreach(fx in bolts)checked(fx.trigger());nextPulse=0.85;}
};
eve_render = function() {
    gfx.clear();gfx.render3D();
    gfx.drawText(labelFont,"WEAPON / LIGHTNING",32.0,24.0,1.0,0.95,0.9,1.0,1.0);
    gfx.drawText(labelFont,"BLADE ENCHANTMENT             MUZZLE DISCHARGE                 CHAIN ARC",100.0,645.0,0.7,0.83,1.0,1.0,0.8);
    gfx.drawText(labelFont,"Moving endpoints | seeded variants | Space: freeze / resume",32.0,680.0,0.5,0.65,0.8,1.0,0.75);
};
