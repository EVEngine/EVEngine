dofile("styles/unit-surface/style.nut");
dofile("mannequin.nut");
local owned=[], units=[], effects=[], bases=[], camera=null, ground=null, fragmentObjects=[], accentObjects=[], neutral=null;
local seconds=0.0, paused=false, enabled=true, strength=1.0, moving=true, page=0, overview=false;
local solo=false, soloIndex=0, hud=true;
local presets=unitSurfacePresetNames();
function keep(x) {owned.append(x);return x;}
function prop(mesh,x,y,z,sx,sy,sz,r,g,b) {
    local o=keep(eve.Renderable3D());o.setMesh(mesh);
    o.setPosition(x,y,z);o.setScale(sx,sy,sz);o.setTint(r,g,b,1.0);
    return o;
}
function setView() {
    if(solo) {camera.setEye(3.4,2.9,8.2);camera.setTarget(0.0,1.97,0.0);camera.setFov(27.0);}
    else if(overview) {camera.setFov(43.0);camera.setEye(14.0,18.0,28.0);camera.setTarget(0.0,1.0,0.0);}
    else {camera.setFov(43.0);camera.setEye(8.8,7.5,13.0);camera.setTarget(0.0,1.0,0.0);}
}
function positionUnits() {
    if(ground!=null) ground.setVisible(!solo);
    foreach(i,body in units) {
        local slot=overview?i:i%6;
        local x=solo?0.0:overview?(slot%7-3)*3.3:(slot%3-1)*3.5;
        local z=solo?0.0:overview?(slot/7-1.5)*4.0:(slot<3?-2.0:2.0);
        local visible=solo?(i==soloIndex):(overview || i/6==page);
        local shift=moving && !solo?0.12*sin(seconds*0.8+i):0.0;
        effects[i].setUnitFrame(x,0.0,z+shift,0.0,0.0,0.0,1.0,1.0);
        if(fragmentObjects[i]!=null) {fragmentObjects[i].setVisible(visible && enabled);fragmentObjects[i].setPosition(x,0.0,z+shift);}
        if(accentObjects[i]!=null) {accentObjects[i].setVisible(visible && enabled);accentObjects[i].setPosition(x,0.0,z+shift);}
        foreach(p in body) {
            p.object.setVisible(visible);p.overlay.setVisible(visible && enabled);
            p.object.setPosition(x+p.x,p.y,z+p.z+shift);
            p.overlay.setPosition(x+p.x,p.y,z+p.z+shift);
        }
        bases[i].setVisible(visible && !solo);bases[i].setPosition(x,0.0,z);
    }
}
eve_init <- function() {
    local fontData=keep(eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",20));
    gfx.setFont(keep(gfx.newFont(fontData," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:-+")));
    gfx.setBackgroundColor(0.003,0.006,0.012,1.0);
    gfx.setDirectionalLight(-0.45,0.75,0.9,1.15,1.05,0.92);
    camera=keep(eve.Camera3D());camera.setUp(0.0,1.0,0.0);camera.setFov(43.0);
    camera.setClipPlanes(0.1,100.0);camera.setAmbient(0.10,0.14,0.2);camera.setExposure(0.0);camera.setBloom(0.30,0.8);camera.setActive(true);setView();
    local sphere=keep(gfx.newMeshSphere(32,24)), cube=keep(gfx.newMeshCube(1.0));
    ground=prop(cube,0.0,-0.16,0.0,30.0,0.2,22.0,0.025,0.035,0.055);
    local mannequin=armoredUnitMeshes();neutral=unitSurfacePrepareNeutral("styles/unit-surface");
    foreach(i,preset in presets) {
        local c=unitSurfacePresets[preset];
        bases.append(prop(cube,0.0,0.0,0.0,2.3,0.12,2.3,c.colorR*0.2,c.colorG*0.2,c.colorB*0.2));
        local effect=UnitSurfaceEffect("styles/unit-surface",preset,{});
        effects.append(effect);
        local baseRenderable=keep(effect.newBase(mannequin.surface,neutral));
        local overlay=keep(effect.newOverlay(mannequin.surface));
        local body=[{mesh=mannequin.surface,x=0.0,y=0.0,z=0.0,object=baseRenderable,overlay=overlay}];
        fragmentObjects.append(effect.fragmentsShader!=null?keep(effect.newFragments(preset=="shadow"?mannequin.wisps:((preset=="heal" || preset=="slowTime")?mannequin.orbits:mannequin.fragments))):null);
        accentObjects.append(preset=="hit"?keep(effect.newImpactRing(mannequin.impact)):null);
        units.append(body);
    }
    dofile("verify.nut");verifyUnitSurfaceEffects(effects,units,neutral);
    positionUnits();listenControls();
    print("UNIT_SURFACE_READY presets="+effects.len()+" pages=5 reference=concept-26.png\n");
};
function listenControls() {
    asyncOnEvent("keypressed").then(function(raw) {
        local key=raw.tolower();
        if(key=="space") paused=!paused;
        if(key=="o") {enabled=!enabled;foreach(i,body in units) foreach(p in body) effects[i].setBaseEnabled(p.object,neutral,enabled);}
        if(key=="r") seconds=0.0;
        if(key=="m") moving=!moving;
        if(key=="n" && paused) seconds+=0.25;
        if(key=="b" && paused) seconds+=1.0/60.0;
        if(key=="v") {solo=!solo;setView();}
        if(key=="h") hud=!hud;
        if(key=="right") {
            if(solo) {soloIndex=(soloIndex+1)%presets.len();seconds=0.0;}
            else page=(page+1)%5;
        }
        if(key=="left") {
            if(solo) {soloIndex=(soloIndex+presets.len()-1)%presets.len();seconds=0.0;}
            else page=(page+4)%5;
        }
        if(key=="g") {solo=false;overview=!overview;setView();}
        for(local i=0;i<5;++i) if(key==(i+1).tostring()) {page=i;overview=false;solo=false;setView();}
        if(key=="up") strength+=0.15;
        if(key=="down") strength=strength>0.15?strength-0.15:0.0;
        listenControls();
    });
}
eve_update <- function(dt) {
    if(!paused) seconds+=dt;
    foreach(effect in effects) {effect.set("time",seconds);effect.set("intensity",strength);}
    positionUnits();
};
eve_render <- function() {
    gfx.clear();gfx.render3D();
    if(!hud) return;
    gfx.print("UNIT SURFACE / 26 REFERENCE-DRIVEN SHADERS",24.0,20.0,0.8,0.9,1.0,1.0,1.0);
    local label=solo?("Solo "+(soloIndex+1)+" / "+presets[soloIndex]):overview?"Overview 7 x 4 / reference order":"Page "+(page+1)+" / Back 1-3 / Front 4-6";
    gfx.print(label,24.0,48.0,0.8,0.9,1.0,1.0,1.0);
    if(!overview && !solo) for(local j=0;j<6;++j) {
        local index=page*6+j;if(index>=presets.len())break;
        gfx.print((j+1)+": "+(index+1)+" "+presets[index],24.0+j%3*400.0,80.0+(j/3)*27.0,0.7,0.8,0.9,1.0,1.0);
    }
    gfx.print("V solo | Left/Right next | H HUD | 1-5 pages | G gallery | Space pause | N step | O overlay",24.0,752.0,0.8,0.9,1.0,1.0,1.0);
};
