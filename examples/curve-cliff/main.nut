import { cliffNeed, cliffDefaults, cliffValidate, cliffPlan, cliffPalette, cliffBacking } from "./generator.nut";

persist cliffDoc = null
persist cliffParts = []
persist cliffMeshes = []
persist cliffWall = null
persist cliffCamera = null
persist cliffPolyline = null
persist cliffDrag = -1
persist cliffMouseDown = false
persist cliffDirty = false
persist cliffCooldown = 0.0
persist cliffAngle = 0.65
persist cliffDistance = 32.0
persist cliffFrame = 0
persist cliffStatus = "Ready"
persist cliffUndo = []
persist cliffRedo = []
persist cliffSelected = 0
persist cliffGround = null

function cliffCopy(d) { return cliffNeed(eve.json.decode(cliffNeed(eve.json.encode(d)))); }
function cliffRemember() {
    cliffUndo.append(cliffCopy(cliffDoc));
    if (cliffUndo.len()>32) cliffUndo.remove(0);
    cliffRedo=[];
}
function cliffChanged() { cliffDirty=true; cliffCooldown=0.12; }

function cliffRebuild(candidate) {
    // Prepare CPU placement and GPU backing before publishing; failure keeps the previous preview.
    local plan=cliffPlan(candidate);
    local backing=cliffBacking(plan.path, candidate);
    local line=cliffNeed(plan.path.polylineResult(128,true,32));
    while (cliffParts.len()<plan.placements.len()) {
        local part=eve.Renderable3D();
        part.setVisible(false);
        cliffParts.append(part);
    }
    if (cliffWall==null) cliffWall=eve.Renderable3D();
    cliffWall.setMesh(backing);
    cliffWall.setTint(0.38,0.34,0.29,1.0);
    cliffWall.setRoughness(0.95);
    foreach (i, part in cliffParts) {
        part.setVisible(i<plan.placements.len());
        if (i>=plan.placements.len()) continue;
        local p=plan.placements[i];
        part.setMesh(cliffMeshes[p.mesh]);
        part.setPosition(p.x,p.y,p.z);
        part.setScale(p.sx,p.sy,p.sz);
        part.setRotation(p.yaw,p.pitch,p.roll);
        local shade=p.shade;
        part.setTint(0.48*shade,0.40*shade,0.34*shade,1.0);
        part.setRoughness(0.90);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
    }
    cliffPolyline=line;
    cliffDoc=candidate;
    if (cliffSelected>=cliffDoc.points.len()) cliffSelected=cliffDoc.points.len()-1;
    cliffStatus=format("%d rocks / %.1f m / seed %d",plan.placements.len(),plan.length,candidate.seed);
    cliffDirty=false;
}

function cliffPanel() {
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("CURVE CLIFF", "root");
    ui.text("ROCK WALL AUTHORING", "heading");
    ui.text("Drag points on the plan at left.","help");
    ui.text("Click empty space to add points.","help2");
    ui.slider("Height",cliffDoc.height,1.0,12.0,"height");
    ui.setItemSize(170.0,0.0);
    ui.slider("Spacing",cliffDoc.spacing,0.7,3.0,"spacing");
    ui.setItemSize(170.0,0.0);
    ui.slider("Depth",cliffDoc.depth,0.6,3.0,"depth");
    ui.setItemSize(170.0,0.0);
    ui.slider("Random",cliffDoc.variation,0.0,0.7,"variation");
    ui.setItemSize(170.0,0.0);
    ui.beginRow("seedActions",8.0);
    ui.button("New seed","seed");
    ui.button(cliffDoc.closed ? "Open curve" : "Close curve","closed");
    ui.end();
    ui.beginRow("pointActions",8.0);
    ui.button("Remove point","remove");
    ui.button("Reset curve","reset");
    ui.end();
    ui.beginRow("history",8.0);
    ui.button("Undo","undo"); ui.button("Redo","redo");
    ui.end();
    ui.beginRow("files",8.0);
    ui.button("Save","save"); ui.button("Load","load");
    ui.end();
    ui.slider("Orbit",cliffAngle,-3.14,3.14,"orbit"); ui.setItemSize(170.0,0.0);
    ui.slider("Zoom",cliffDistance,22.0,48.0,"zoom"); ui.setItemSize(170.0,0.0);
    ui.text("32 x 20 m / up to 16 points", "limits");
    ui.text(cliffStatus,"status");
    ui.end();
    ui.mountBuildAs("cliff");
    ui.select("cliff");
    ui.setHostOverlay(true);
    ui.setHostPos(930.0,24.0,0.0,0.0);
    ui.setHostSize(326.0,740.0);
    ui.beginBuild();
    ui.beginWindow("CURVE CONTROL", "root");
    ui.text("Selected point " + (cliffSelected+1) + " / " + cliffDoc.points.len(),"selected");
    ui.beginRow("select",8.0);
    ui.button("Next point","next"); ui.button("Insert point","insert");
    ui.end();
    ui.beginRow("nudge",8.0);
    ui.button("X -","left"); ui.button("X +","right");
    ui.button("Z -","back"); ui.button("Z +","front");
    ui.end();
    ui.text("Move 0.5 m / or drag on the plan", "hint");
    ui.end();
    ui.mountBuildAs("curve"); ui.select("curve");
    ui.setHostOverlay(true); ui.setHostPos(24.0,24.0,0.0,0.0); ui.setHostSize(370.0,205.0);
}

function cliffFiles() {
    local files=eve.Filesystem();
    if (!files.setIdentity("curve-cliff",true) || !files.setupWriteDirectory()) throw "save directory unavailable";
    return files;
}

function cliffAction(id) {
    if (id=="curve/next") {
        cliffSelected=(cliffSelected+1)%cliffDoc.points.len(); cliffPanel();
    } else if (id=="curve/insert") {
        if (cliffDoc.points.len()>=16) throw "maximum 16 points";
        cliffRemember();
        local a=cliffDoc.points[cliffSelected];
        local next=(cliffSelected+1)%cliffDoc.points.len();
        if (!cliffDoc.closed && next==0) { next=cliffSelected; cliffSelected-=1; a=cliffDoc.points[cliffSelected]; }
        local b=cliffDoc.points[next];
        cliffDoc.points.insert(cliffSelected+1,[(a[0]+b[0])*0.5,(a[1]+b[1])*0.5]);
        cliffSelected+=1; cliffChanged(); cliffPanel();
    } else if (id=="curve/left" || id=="curve/right" || id=="curve/back" || id=="curve/front") {
        cliffRemember();
        local p=cliffDoc.points[cliffSelected];
        local axis=(id=="curve/left" || id=="curve/right") ? 0 : 1;
        local delta=(id=="curve/left" || id=="curve/back") ? -0.5 : 0.5;
        local limit=axis==0 ? 16.0 : 10.0;
        local v=p[axis]+delta; p[axis]=v < -limit ? -limit : (v>limit ? limit : v);
        cliffChanged();
    } else if (id=="cliff/save") {
        cliffValidate(cliffDoc);
        if (!cliffFiles().writeTextAtomic("curve-cliff-v1.json",cliffNeed(eve.json.encode(cliffDoc))))
            throw "could not save document";
        cliffStatus="Saved curve-cliff-v1.json";
    } else if (id=="cliff/load") {
        local candidate=cliffNeed(eve.json.decode(cliffFiles().readText("curve-cliff-v1.json")));
        cliffValidate(candidate);
        local previous=cliffCopy(cliffDoc);
        cliffRebuild(candidate);
        cliffUndo.append(previous); cliffRedo=[];
        cliffPanel();
    } else if (id=="cliff/rotate") {
        cliffAngle+=0.4;
    } else if (id=="cliff/undo" || id=="cliff/redo") {
        local source=id=="cliff/undo" ? cliffUndo : cliffRedo;
        local target=id=="cliff/undo" ? cliffRedo : cliffUndo;
        if (source.len()>0) {
            local previous=cliffCopy(cliffDoc);
            cliffRebuild(cliffCopy(source.top()));
            source.pop(); target.append(previous);
            cliffPanel();
        }
    } else {
        cliffRemember();
        if (id=="cliff/seed") cliffDoc.seed=(cliffDoc.seed*251+13849)%1000000+1;
        else if (id=="cliff/closed") cliffDoc.closed=!cliffDoc.closed;
        else if (id=="cliff/remove" && cliffDoc.points.len()>3) {
            cliffDoc.points.remove(cliffSelected); cliffSelected=0;
        }
        else if (id=="cliff/reset") cliffDoc=cliffDefaults();
        cliffChanged();
        cliffPanel();
    }
}

function eve_init() {
    if (cliffDoc==null) cliffDoc=cliffDefaults();
    if (cliffMeshes.len()==0) cliffMeshes=cliffPalette();
    if (cliffGround==null) {
        cliffGround=eve.Renderable3D();
        local groundMesh=gfx.newMeshCube(1.0);
        if (groundMesh==null) throw "ground mesh unavailable";
        cliffGround.setMesh(groundMesh);
        cliffGround.setScale(42.0,0.3,30.0);
        cliffGround.setPosition(0.0,-0.5,0.0);
        cliffGround.setTint(0.16,0.25,0.17,1.0);
        cliffGround.setRoughness(1.0);
        cliffGround.setReceiveShadow(true);
    }
    if (cliffCamera==null) cliffCamera=eve.Camera3D();
    cliffCamera.setUp(0.0,1.0,0.0);
    cliffCamera.setFov(47.0);
    cliffCamera.setAmbient(0.22,0.25,0.29);
    cliffCamera.setActive(true);
    gfx.setDirectionalLight(-0.6,-0.8,-0.3,1.2,1.10,0.95);
    gfx.setBackgroundColor(0.12,0.16,0.20,1.0);
    cliffRebuild(cliffCopy(cliffDoc));
    cliffPanel();
    print("CURVE_CLIFF_READY " + cliffStatus + "\n");
}

function eve_update(dt) {
    local down=mouse.isDown(1), mx=mouse.getX(), my=mouse.getY();
    if (down && !cliffMouseDown && mx>=30 && mx<=414 && my>=480 && my<=720) {
        cliffRemember();
        local x=(mx-222)/12.0, z=(my-600)/12.0;
        cliffDrag=-1;
        foreach (i,p in cliffDoc.points) {
            local dx=p[0]-x,dz=p[1]-z;
            if (dx*dx+dz*dz<0.7) { cliffDrag=i; cliffSelected=i; break; }
        }
        if (cliffDrag<0 && cliffDoc.points.len()<16) {
            cliffDoc.points.append([x,z]); cliffDrag=cliffDoc.points.len()-1;
        }
    }
    if (down && cliffDrag>=0) {
        local x=(mx-222)/12.0,z=(my-600)/12.0;
        x=x < -16 ? -16 : (x>16 ? 16 : x);
        z=z < -10 ? -10 : (z>10 ? 10 : z);
        if (cliffDoc.points[cliffDrag][0]!=x || cliffDoc.points[cliffDrag][1]!=z) {
            cliffDoc.points[cliffDrag]=[x,z]; cliffChanged();
        }
    }
    if (!down) cliffDrag=-1;
    cliffMouseDown=down;
    local clicked=ui.consumeClick();
    while (clicked!="") {
        try { cliffAction(clicked); } catch (error) { cliffStatus="Action rejected: "+error; }
        clicked=ui.consumeClick();
    }
    local changed=ui.consumeChange();
    while (changed!="") {
        ui.select("cliff");
        if (changed=="cliff/orbit") cliffAngle=ui.getValue("orbit");
        if (changed=="cliff/zoom") cliffDistance=ui.getValue("zoom");
        foreach (key in ["height","spacing","depth","variation"]) {
            if (changed=="cliff/"+key) {
                if (!cliffDirty) cliffRemember();
                cliffDoc[key]=ui.getValue(key); cliffChanged();
            }
        }
        changed=ui.consumeChange();
    }
    cliffCooldown-=dt;
    if (cliffDirty && cliffCooldown<=0 && cliffDrag<0) {
        try { cliffRebuild(cliffCopy(cliffDoc)); }
        catch (error) { cliffStatus="Preview unchanged: "+error; cliffDirty=false; }
    }
    cliffCamera.setEye(sin(cliffAngle)*cliffDistance,cliffDistance*0.4375,cos(cliffAngle)*cliffDistance);
    cliffCamera.setTarget(4.5,2.0,0.0);
    ui.select("cliff"); ui.setText("status",cliffStatus);
    ui.select("curve"); ui.setText("selected","Selected point "+(cliffSelected+1)+" / "+cliffDoc.points.len());
    cliffFrame+=1;
}

function eve_render() {
    gfx.clear(); gfx.render3D();
    gfx.drawSolidRect(24.0,474.0,396.0,252.0,0.055,0.075,0.095,0.96);
    for (local i=0;i<=8;++i) gfx.drawSolidRect(30.0+i*48.0,480.0,1.0,240.0,0.18,0.23,0.28,1.0);
    for (local i=0;i<=5;++i) gfx.drawSolidRect(30.0,480.0+i*48.0,384.0,1.0,0.18,0.23,0.28,1.0);
    if (cliffPolyline!=null) {
        for (local i=0;i<cliffPolyline.getCount();++i) {
            local p=cliffNeed(cliffPolyline.getPointResult(i));
            gfx.drawSolidRect(220+p.getX()*12,598+p.getZ()*12,4.0,4.0,0.43,0.73,0.80,1.0);
        }
    }
    foreach (i,p in cliffDoc.points)
        gfx.drawSolidRect(217+p[0]*12,595+p[1]*12,10.0,10.0,0.94,i==cliffSelected ? 0.95 : 0.46,0.30,1.0);
    ui.beginFrameAndRender();
}
