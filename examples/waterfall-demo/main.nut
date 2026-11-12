// Waterfall demo — flowing-water sheet shader.
// A vertical waterfall plane with downward flow streaks, turbulence and
// foam at the top lip + bottom splash pool. Press F to dump the current
// frame to /tmp/waterfall_demo.png.

persist fall = null
persist fallEnt = null
persist fallCam = null
persist fallTime = 0.0
persist fallSaved = false
persist fallContext = []

function fallProp(x,y,z,sx,sy,sz,r,g,b,rock=false) {
    local o=eve.Renderable3D();o.setMesh(rock?gfx.newMeshSphere(9,6):gfx.newMeshCube(1.0));
    o.setPosition(x,y,z);o.setScale(sx,sy,sz);o.setTint(r,g,b,1.0);o.setRoughness(rock?0.92:0.18);
    fallContext.append(o);return o;
}
eve_init = function() {
    gfx.setBackgroundColor(0.03, 0.05, 0.08, 1.0);
    gfx.setDirectionalLight(-0.4, 1.0, 0.6, 1.0, 0.95, 0.85);

    if (fallCam == null) {
        fallCam = eve.Camera3D();
        fallCam.setEye(16.0, 9.0, 28.0);
        fallCam.setTarget(0.0, 0.0, 2.0);
        fallCam.setUp(0.0, 1.0, 0.0);
        fallCam.setFov(52.0);
        fallCam.setAmbient(0.17, 0.23, 0.28);
        fallCam.setActive(true);
    }

    if (fall == null) {
        fall = gfx.newWaterfall();
        fall.createSheet(8.0, 14.0, 24, 36);
        fall.setFlowSpeed(1.5);
        fall.setTurbulence(0.75);
        fall.setStreakCount(5);
        fall.setStreakScale(6.0);
        fall.setTopFoam(0.07);
        fall.setBottomFoam(0.14);
        fall.setFoamAmount(0.9);
        fall.setWaterColor(0.06, 0.24, 0.32);
        fall.setReflectionIntensity(0.5);
        fall.setSunIntensity(0.35);

        fallEnt = eve.Renderable3D();
        fallEnt.setMesh(fall.getMesh());
        fallEnt.setShader(fall.getShader());
        fallEnt.setPosition(0.0, 1.5, 0.0);
        fallProp(0.0,-5.7,4.0,22.0,0.25,15.0,0.018,0.10,0.14);
        for(local side=-1;side<=1;side+=2) for(local i=0;i<4;++i) {
            fallProp(side*(5.2+i*1.7),0.0-i*0.6,-1.6-i*0.3,4.6,10.0-i*1.3,4.2,0.16+i*0.02,0.19+i*0.02,0.20,true);
            fallProp(side*(5.0+i*1.7),7.0-i*1.2,-1.2,3.0,0.4,3.5,0.045,0.12,0.055,true);
        }
        for(local i=0;i<15;++i) {
            local x=-3.5+(i%5)*1.75,z=0.6+(i/5)*0.65;
            fallProp(x,-5.43,z,1.1,0.12,0.5,0.65,0.78,0.77,true);
        }

    }
};

eve_update = function(dt) {
    fallTime += dt;
    if (fall != null) fall.update(dt);
    // 手动保存当前帧（头部注释约定）。
    if (keyboard.isDown("F") || keyboard.isDown("f")) {
        if (gfx.saveFramePng("/tmp/waterfall_demo.png"))
            print("waterfall frame saved: /tmp/waterfall_demo.png\n");
    }
};

eve_render = function() {
    // saveFramePng reads the frame presented at the end of the previous tick,
    // so wait a couple of seconds for the swapchain to have a presented image.
    if (!fallSaved && fallTime > 1.5) {
        if (gfx.saveFramePng("/tmp/waterfall_demo.png")) {
            print("waterfall frame saved: /tmp/waterfall_demo.png\n");
        }
        fallSaved = true;
    }
    gfx.clear();
    gfx.render3D();
};
