// GPU render targets own the paint. All patches evaluate the same
// world-space brush volume, including patches on opposite sides of a UV seam.
dofile("scene.nut");
dofile("parameters.nut");
persist surfaces = [];
persist camera = null;
persist brush = null;
persist imageModule = null;
persist shotCount = 0;
persist cooldown = 0.0;
persist verifyFrame = 0;
persist inkShader = null;
persist paintShader = null;
persist brushTexture = null;
persist orbit = 0.62;
persist seed = 1729;
persist previousKeys = {};
persist paintCommands = [];
function pressed(key) {
    local down=keyboard.isDown(key);
    local before=key in previousKeys ? previousKeys[key] : false;
    previousKeys[key] <- down; return down && !before;
}
colors <- [[1.0, 0.22, 0.015], [0.24, 0.035, 0.95]];
function add(a,b) { return [a[0]+b[0],a[1]+b[1],a[2]+b[2]]; }
function sub(a,b) { return [a[0]-b[0],a[1]-b[1],a[2]-b[2]]; }
function mul(a,t) { return [a[0]*t,a[1]*t,a[2]*t]; }
function dot(a,b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
function cross(a,b) { return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]; }
function norm(a) { return mul(a,1.0/sqrt(dot(a,a))); }
function clamp(x,a,b) { return x<a?a:(x>b?b:x); }
function random01() { seed = (seed * 48271) % 2147483647; return seed / 2147483647.0; }
function clearInk() {
    gfx.setShader(null);
    gfx.setBackgroundColor(0.0,0.0,0.0,0.0);
    foreach(s in surfaces) {
        gfx.setCanvas(s.canvas); gfx.clear();
        // A zero-alpha draw commits the pending clear without changing it.
        gfx.drawSolidRect(0.0,0.0,1.0,1.0,0.0,0.0,0.0,0.0);
        gfx.setCanvas(null);
    }
    gfx.setBackgroundColor(0.035,0.045,0.075,1.0);
    shotCount=0; paintCommands.clear();
}
function splat(center, normal, radius, team) {
    // A bounded oriented projector: no indefinite ray projection through walls.
    local tangent=norm(cross(fabs(normal[1])<0.9?[0,1,0]:[0,0,1],normal));
    local bitangent=cross(normal,tangent);
    local angle=random01()*6.283185;
    local tu=add(mul(tangent,cos(angle)),mul(bitangent,sin(angle)));
    local tv=cross(normal,tu);
    local tile=(random01()*16).tointeger();
    local color=colors[team];
    if("inkVerify" in getroottable() && inkVerify)
        paintCommands.push({center=center,normal=normal,radius=radius,tu=tu,tv=tv,tile=tile,color=color});
    foreach(s in surfaces) {
        // A nearly edge-on projector collapses its footprint into a stripe.
        if(dot(s.n,normal)<=0.15) continue;
        local relative=sub(center,s.p);
        // Reject patches outside the projector's bounding sphere.
        if(fabs(dot(relative,s.n))>radius*1.5) continue;
        local cu=dot(relative,s.u)/s.uu, cv=dot(relative,s.v)/s.vv;
        local ru=radius*1.5/sqrt(s.uu), rv=radius*1.5/sqrt(s.vv);
        local x0=clamp(((cu-ru)*s.w).tointeger(),0,s.w-1);
        local x1=clamp(((cu+ru)*s.w).tointeger()+1,0,s.w-1);
        local y0=clamp(((cv-rv)*s.h).tointeger(),0,s.h-1);
        local y1=clamp(((cv+rv)*s.h).tointeger()+1,0,s.h-1);
        // Affine projector coordinates: compute once per patch, not allocating
        // five temporary vectors and crossing script functions for every texel.
        local origin=sub(s.p,center);
        local scale=1.0/(radius*2);
        local bu0=dot(origin,tu)*scale+0.5, bv0=dot(origin,tv)*scale+0.5;
        local dux=dot(s.u,tu)*scale/s.w, duy=dot(s.v,tu)*scale/s.h;
        local dvx=dot(s.u,tv)*scale/s.w, dvy=dot(s.v,tv)*scale/s.h;
        local depth0=dot(origin,normal)/radius;
        local ddx=dot(s.u,normal)/(radius*s.w), ddy=dot(s.v,normal)/(radius*s.h);
        paintShader.sendVec4("brushU",bu0,dux,duy,(tile%4).tofloat());
        paintShader.sendVec4("brushV",bv0,dvx,dvy,(tile/4).tofloat());
        paintShader.sendVec4("depth",depth0,ddx,ddy,0.0);
        paintShader.sendVec4("ink",color[0],color[1],color[2],1.0);
        gfx.setCanvas(s.canvas); gfx.setShader(paintShader);
        gfx.drawTexturedRect(brushTexture,x0.tofloat(),y0.tofloat(),
            (x1-x0+1).tofloat(),(y1-y0+1).tofloat(),1.0,1.0,1.0,1.0);
        // The backend reads push constants at flush, so submit before updating.
        gfx.setCanvas(null); gfx.setShader(null);
    }
    shotCount++;
}
function shootScreen(x,y,team) {
    camera.screenToRay(x.tofloat(),y.tofloat(),gfx.getWidth().tofloat(),gfx.getHeight().tofloat());
    local o=[camera.getScreenRayOriginX(),camera.getScreenRayOriginY(),camera.getScreenRayOriginZ()];
    local d=[camera.getScreenRayDirX(),camera.getScreenRayDirY(),camera.getScreenRayDirZ()];
    local nearest=1000.0, hit=null, point=null;
    foreach(s in surfaces) {
        local denominator=dot(d,s.n);
        if(fabs(denominator)<0.00001) continue;
        local t=dot(sub(s.p,o),s.n)/denominator;
        if(t<=0 || t>=nearest) continue;
        local p=add(o,mul(d,t)), q=sub(p,s.p);
        local u=dot(q,s.u)/s.uu, v=dot(q,s.v)/s.vv;
        if(u<0 || u>1 || v<0 || v>1) continue;
        if(s.disk && (u-0.5)*(u-0.5)+(v-0.5)*(v-0.5)>0.25) continue;
        nearest=t; hit=s; point=p;
    }
    if(hit==null) return false;
    splat(point,hit.n,0.5+random01()*0.22,team);
    return true;
}
function showcase() {
    local started=clock();
    clearInk(); seed=1729;
    // Overlapping shots form connected pools; reference alpha accumulation
    // removes internal brush boundaries instead of layering painted highlights.
    for(local team=0;team<2;team++) {
        for(local i=0;i<30;i++) {
            local t=i/29.0;
            local x=(team==0?-2.4:1.25)+sin(t*7.0+team)*0.9;
            local z=3.5-t*7.0;
            splat([x,0.01,z],[0,1,0],1.0+random01()*0.5,team);
        }
        for(local i=0;i<12;i++) {
            local x=(team==0?-3.4:2.1)+random01()*1.8;
            local y=0.6+random01()*2.0;
            splat([x,y,-5.0],[0,0,1],1.0+random01()*0.35,team);
        }
    }
    for(local i=0;i<12;i++) {
        local z=1.6-i*0.32;
        splat([2.1+sin(i*0.6)*0.35,0.02+(2.0-z)*0.45,z],norm([0,4,1.8]),1.1,0);
    }
    for(local i=0;i<6;i++) {
        splat([-2.1,0.5+i*0.32,-0.55],[0,0,1],1.0,1);
        splat([2.5,1.82,-2.6-i*0.14],[0,1,0],1.0,0);
    }
    print(format("INK_PROFILE gpuPaint=%.3fs\n",clock()-started));
}
function updateCamera() {
    camera.setEye(sin(orbit)*15,8.5,cos(orbit)*15);
    camera.setTarget(0.0,0.4,-0.6);
}
eve_init=function() {
    local started=clock();
    imageModule=eve.Image();
    brush=imageModule.newImageDataFromFile("assets/splats.tga");
    brushTexture=gfx.newTexture(brush,false,false);
    paintShader=gfx.newShaderFromSpvFile("shaders/paint.frag.spv");
    foreach(name in ["brushU","brushV","depth","ink"]) paintShader.declareVec4(name);
    local loaded=gfx.loadMeshShaderSpv("","shaders/ink.frag.spv");
    if(!loaded.ok) throw "Ink shader load failed: "+loaded.status;
    inkShader=loaded.value;
    foreach(knob in inkKnobs) inkShader.declareFloat(knob.id);
    applyInkParameters();
    camera=eve.Camera3D(); updateCamera();
    camera.setUp(0.0,1.0,0.0); camera.setFov(48.0); camera.setAmbient(0.8,0.8,0.85); camera.setActive(true);
    gfx.setBackgroundColor(0.035,0.045,0.075,1.0);
    gfx.setDirectionalLight(-0.4,-1.0,-0.2,2.4,2.35,2.3);
    foreach(i,frame in sceneFrames) {
        local p=frame[0],u=frame[1],v=frame[2];
        local w=clamp((sqrt(dot(u,u))*36).tointeger(),24,512);
        local h=clamp((sqrt(dot(v,v))*36).tointeger(),24,512);
        local canvas=gfx.newCanvas(w,h);
        local texture=canvas.getTexture();
        local data=model3d.newModelDataFromFile("assets/patch-"+i+".obj");
        local renderable=model3d.createRenderable(gfx,data,0);
        renderable.getMaterial().setAlbedoTexture(texture);
        renderable.getMaterial().setShader(inkShader);
        renderable.getMaterial().setRoughness(0.28);
        renderable.getMaterial().setTint(1.0,1.0,1.0,1.0);
        surfaces.push({p=p,u=u,v=v,n=norm(cross(u,v)),uu=dot(u,u),vv=dot(v,v),
            disk=i==22,w=w,h=h,canvas=canvas,texture=texture,model=data,renderable=renderable});
    }
    print(format("INK_PROFILE assets=%.3fs\n",clock()-started));
    showcase();
    ui.beginBuild(); ui.beginWindow("InkArena","root");
    ui.text("INK ARENA","title");
    ui.text("LMB: orange | RMB: violet","help");
    ui.text("A / D: orbit | C: clear | R: showcase","controls");
    foreach(knob in inkKnobs) {
        ui.text(knob.label,knob.id+"-label");
        ui.slider(knob.label,knob.value,knob.min,knob.max,knob.id);
    }
    ui.text("","stats"); ui.end(); ui.mountBuildAs("inkhud");
    ui.select("inkhud"); ui.setHostOverlay(true); ui.setHostPos(18.0,16.0,0.0,0.0); ui.setHostSize(450.0,560.0);
    print("INK_ARENA_READY backend=GPU-raster patches="+surfaces.len()+" shots="+shotCount+"\n");

};
eve_update=function(dt) {
    updateInkParameters();
    verifyFrame++;
    if(verifyFrame==3 && "inkVerify" in getroottable() && inkVerify) dofile("verify.nut");
    if(keyboard.isDown("a")) orbit-=dt*0.65;
    if(keyboard.isDown("d")) orbit+=dt*0.65;
    updateCamera();
    if(pressed("c")) clearInk();
    if(pressed("r")) showcase();
    cooldown-=dt;
    if(cooldown<=0 && !ui.wantCaptureMouse() && (mouse.isDown(1)||mouse.isDown(2))) {
        shootScreen(mouse.getX(),mouse.getY(),mouse.isDown(2)?1:0); cooldown=0.075;
    }
    ui.setText("stats",shotCount+" splats / "+surfaces.len()+" surfaces");
};
eve_render=function() {
    gfx.clear(); gfx.render3D(); ui.beginFrameAndRender();
};
