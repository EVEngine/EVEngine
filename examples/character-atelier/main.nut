dofile("profile.nut");
dofile("character.nut");
persist atelier = {rig=null,defaults=null,snapshot=null,camera=null,lights=[],ground=null,
    mode="Face",selected="chin",status="Ready",symmetry=true,points=true,playing=false,
    yaw=0.0,distance=0.82,focus=1.455,view="portrait",width=800.0,height=800.0,
    previousLeft=false,previousRight=false,mouseX=0.0,mouseY=0.0,gesture=null,
    preview=null,time=0.0,motionTime=0.0,autoFit=true,projected=[],uiReady=false,revision=0};
function acChecked(r) {if(!r.ok) throw r.status.summary;return "value" in r?r.value:null;}
function acMin(a,b) {return a<b?a:b;}
function acMax(a,b) {return a>b?a:b;}
function acClamp(x,a,b) {return acMin(b,acMax(a,x));}
function acFace(id) {foreach(c in atelierRigData.controls) if(c.id==id) return c;throw "Unknown facial point";}
function acCamera() {
    atelier.camera.setEye(sin(atelier.yaw)*atelier.distance,atelier.focus,cos(atelier.yaw)*atelier.distance);
    atelier.camera.setTarget(0.0,atelier.focus,0.0);atelier.camera.setUp(0.0,1.0,0.0);
}
function acView(name) {
    atelier.view=name;atelier.autoFit=true;atelier.focus=name=="portrait"?1.455:0.84;
    atelier.distance=name=="portrait"?0.82:3.4;acCamera();
}
function acButton(icon,label,id,selected=false) {
    ui.iconButton(icon,label,id);ui.setItemSelected(selected);
}
function acProperty(label,key,lo,hi) {
    ui.text(label,"label-"+key);ui.slider(" ",atelier.snapshot.parameters[key],lo,hi,"p-"+key);
    ui.setItemTooltip("Bone-driven adjustment; values are saved with the character.");
}
function acInspector() {
    ui.beginPanelFrame("Inspector","inspector");
    ui.badge("BONE RIG / YSA","rig-badge");
    if(atelier.mode=="Face") {
        local c=acFace(atelier.selected),prefix="face_"+c.id+"_";
        ui.sectionHeader(c.label,"selection-title");
        ui.text(c.bone,"bone-name");
        ui.beginCard("transform-card");
        acProperty("X / left-right (m)",prefix+"x",-0.012,0.012);
        acProperty("Y / up-down (m)",prefix+"y",-0.012,0.012);
        acProperty("Z / depth (m)",prefix+"z",-0.012,0.012);
        acProperty("Width",prefix+"sx",0.82,1.18);acProperty("Height",prefix+"sy",0.82,1.18);
        ui.end();
        acButton("refresh","Reset selected point","reset-point");
        ui.textWrapped("Pick a facial landmark and drag to shape. Hold Shift while dragging to adjust depth. Mirrored edits affect the paired joint.",280.0,"point-help");
    } else if(atelier.mode=="Pose") {
        ui.sectionHeader("Common poses","pose-title");
        foreach(p in atelierPoses) acButton(p[2],p[1],"pose-"+p[0],atelier.snapshot.selections.pose==p[0]);
        ui.separator("pose-line");acButton(atelier.playing?"pause":"play",atelier.playing?"Pause motion":"Play motion","play");
        ui.textWrapped("Pose presets use the actual skeleton. Playback adds an idle breath or waving motion. Pause for precise shaping.",280.0,"pose-help");
    } else {
        ui.sectionHeader(atelier.mode+" properties","properties-title");
        foreach(f in atelierFields) if(f[2]==atelier.mode) acProperty((atelier.selected==f[0]?"> ":"")+f[1],f[0],f[3],f[4]);
        if(atelier.mode=="Body") ui.textWrapped("Select a body landmark to jump to its proportion control. Clothes share the same bone deformation.",280.0,"body-help");
    }
    ui.spacer("inspector-fill",1.0);
    ui.sectionHeader("Character","asset-title");
    ui.textWrapped("pixiv / downloaded female model\n15 added facial skin joints\nOriginal hair and outfit",280.0,"asset-credit");
    ui.end();
}
function acPalette() {
    ui.beginPanelFrame("Character tools","tools-panel");
    ui.beginToolbox("tools",44.0,5);
    foreach(m in [["Face","user"],["Body","users"],["Expression","eye"],["Pose","cubes"],["Surface","paint-brush"]]) {
        acButton(m[1],"","mode-"+m[0],atelier.mode==m[0]);ui.setItemTooltip(m[0]);
    }
    ui.end();ui.sectionHeader(atelier.mode,"mode-title");
    if(atelier.mode=="Face") {
        foreach(c in atelierRigData.controls) acButton("pointer",c.label,"point-"+c.id,c.id==atelier.selected);
    } else if(atelier.mode=="Body") {
        foreach(f in atelierFields) if(f[2]=="Body") acButton("move",f[1],"body-"+f[0],atelier.selected==f[0]);
    } else if(atelier.mode=="Pose") {
        foreach(p in atelierPoses) acButton(p[2],p[1],"pose-"+p[0],atelier.snapshot.selections.pose==p[0]);
    } else {
        ui.textWrapped(atelier.mode=="Expression"?"Authored facial expressions combine with the added facial joints.":"YSA cartoon material. Adjust the light threshold, shadow softness and rim highlight.",205.0,"mode-help");
    }
    ui.spacer("palette-fill",1.0);ui.beginCard("reference-card");ui.badge("ORIGINAL MESH","original");
    ui.textWrapped("Geometry and textures by pixiv. Facial skin binding adapted in EVEngine.",205.0,"reference");ui.end();ui.end();
}
function acMount() {
    ui.beginBuild();ui.beginWindow("Character Atelier","root");ui.beginAppFrame("atelier-frame");
    ui.beginToolbar("main-toolbar");ui.icon("user","logo");ui.text("CHARACTER ATELIER","brand");
    acButton("save","Save","save");acButton("folder-open","Load","load");
    acButton("undo","","undo");ui.setItemTooltip("Undo appearance / committed landmark drag");
    acButton("redo","","redo");acButton("refresh","Reset","reset");
    ui.spacer("toolbar-fill",1.0);ui.badge("YSA CARTOON","renderer");ui.end();ui.setItemAlignSelf("stretch");
    ui.beginSplitPane("row",0.17,"left-split");ui.beginDockArea("left","left-dock");acPalette();ui.end();
    ui.beginSplitPane("row",0.73,"right-split");ui.beginDockArea("center","center-dock");
    ui.beginPanelFrame("Character preview","preview-panel");
    ui.beginToolbar("viewport-tools");acButton("camera","Portrait","portrait",atelier.view=="portrait");
    acButton("user","Full body","body",atelier.view=="body");acButton("home","Front","front");
    acButton("grid","Points","points",atelier.points);acButton("users","Mirror","symmetry",atelier.symmetry);
    ui.end();ui.viewport("stage",0.0,0.0);ui.setItemFlexGrow(1.0);
    ui.end();ui.end();ui.beginDockArea("right","right-dock");acInspector();ui.end();ui.end();ui.end();ui.setItemFlexGrow(1.0);ui.setItemAlignSelf("stretch");
    ui.beginStatusBar("statusbar");ui.text(atelier.status,"status");ui.spacer("status-fill",1.0);
    ui.text("LMB: select / shape   RMB: orbit   Wheel: zoom   Shift: depth","hint");ui.end();ui.setItemAlignSelf("stretch");
    ui.end();ui.end();ui.mountBuildAs("atelier");ui.setHostPos(0.0,0.0,0.0,0.0);
    ui.setHostSize(1560.0,1000.0);ui.setHostMovable(false);ui.setHostResizable(false);ui.setHostOverlay(true);
    atelier.uiReady=true;
}
function acRefresh(rebuild=false) {
    atelier.snapshot=acChecked(atelier.rig.snapshot());atelier.revision++;atelier.autoFit=true;
    arExpressions(atelier.snapshot.parameters);
    foreach(key in ["lightMidpoint","lightSmoothness","rimStrength"])
        atelierCharacter.shader.sendFloat(key,atelier.snapshot.parameters[key]);
    if(!atelier.uiReady || rebuild) acMount();
    else {
        ui.select("atelier");ui.setText("status",atelier.status);
        foreach(f in atelierFields) if(f[2]==atelier.mode) ui.setValue("p-"+f[0],atelier.snapshot.parameters[f[0]]);
        if(atelier.mode=="Face") foreach(axis in ["x","y","z","sx","sy"]) {
            local k="face_"+atelier.selected+"_"+axis;ui.setValue("p-"+k,atelier.snapshot.parameters[k]);
        }
    }
}
function acEdit(patch) {acChecked(atelier.rig.apply(patch));acRefresh();}
function acMirror(values,id,axis,value) {
    local c=acFace(id),prefix="face_"+id+"_";values[prefix+axis] <- value;
    if(atelier.symmetry && c.mirror!="") values["face_"+c.mirror+"_"+axis] <- axis=="x"?-value:value;
}
function acSaveDirectory() {
    local fs=eve.Filesystem();
    if(fs.getIdentity()=="" && !fs.setIdentity("character-atelier",false)) {atelier.status="Cannot initialize save folder";return null;}
    if(!fs.setupWriteDirectory()) {atelier.status="Save folder is not writable";return null;}return fs;
}
function acAction(id) {
    if(id.find("mode-")==0) {
        atelier.mode=id.slice(5);atelier.playing=false;
        if(atelier.mode=="Face") {atelier.selected="chin";acView("portrait");}
        else if(atelier.mode=="Body" || atelier.mode=="Pose") {atelier.selected="height";acView("body");}
        acMount();
    } else if(id.find("point-")==0) {atelier.selected=id.slice(6);acMount();}
    else if(id.find("body-")==0) {atelier.selected=id.slice(5);atelier.status="Selected proportion: "+atelier.selected;acRefresh(true);}
    else if(id.find("pose-")==0) {atelier.motionTime=0.0;acChecked(atelier.rig.apply({selections={pose=id.slice(5)}}));acRefresh(true);}
    else if(id=="play") {atelier.playing=!atelier.playing;acMount();}
    else if(id=="points") {atelier.points=!atelier.points;acMount();}
    else if(id=="symmetry") {atelier.symmetry=!atelier.symmetry;acMount();}
    else if(id=="portrait" || id=="body") {acView(id);acMount();}
    else if(id=="front") {atelier.yaw=0.0;acCamera();}
    else if(id=="undo" && atelier.rig.canUndo()) {acChecked(atelier.rig.undo());acRefresh(true);}
    else if(id=="redo" && atelier.rig.canRedo()) {acChecked(atelier.rig.redo());acRefresh(true);}
    else if(id=="reset") {atelier.motionTime=0.0;acChecked(atelier.rig.restore(atelier.defaults));atelier.preview=null;acRefresh(true);}
    else if(id=="reset-point") {
        local patch={};foreach(axis in ["x","y","z","sx","sy"]) acMirror(patch,atelier.selected,axis,axis.len()>1?1.0:0.0);
        acEdit({parameters=patch});
    } else if(id=="save") {
        local fs=acSaveDirectory();if(fs!=null) atelier.status=fs.writeTextAtomic("appearance.json",acChecked(atelier.rig.snapshotJson()))==1?"Saved appearance.json":"Cannot save appearance.json";
        acRefresh();
    } else if(id=="load") {
        local fs=acSaveDirectory();if(fs!=null) {
            if(!file_exists("appearance.json")) atelier.status="No saved appearance yet";
            else {local r=atelier.rig.restoreJson(fs.readText("appearance.json"));atelier.status=r.ok?"Loaded appearance.json":r.status.summary;}
        }acRefresh(true);
    }
}
// Projection uses the exact orbit camera, canvas aspect and FOV; no fixed screen coordinates.
function acProject(point) {
    local s=sin(atelier.yaw),c=cos(atelier.yaw),depth=atelier.distance-s*point[0]-c*point[2];
    if(depth<=0.03) return null;
    local factor=atelier.height/(2.0*tan(15.0*3.14159265/180.0)*depth);
    return {x=atelier.width*0.5+(c*point[0]-s*point[2])*factor,
        y=atelier.height*0.5-(point[1]-atelier.focus)*factor,pixelsPerMeter=factor};
}
function acLandmarks() {
    atelier.projected=[];
    if(!atelier.points) return;
    if(atelier.mode=="Face") {
        foreach(c in atelierRigData.controls) {
            local world=arWorld(c.bone),screen=acProject(world);
            if(screen!=null) atelier.projected.append({id=c.id,world=world,screen=screen});
        }
    } else if(atelier.mode=="Body") {
        foreach(pair in [["head","head"],["shoulders","leftShoulder"],["shoulders","rightShoulder"],
            ["waist","spine"],["hips","hips"],["legs","leftLowerLeg"],["legs","rightLowerLeg"],
            ["arms","leftLowerArm"],["arms","rightLowerArm"]]) {
            local world=arWorld(atelierRigData.humanoid[pair[1]]),screen=acProject(world);
            if(screen!=null) atelier.projected.append({id=pair[0],world=world,screen=screen});
        }
    }
}
function acBeginPoint(id,x,y) {
    atelier.selected=id;atelier.playing=false;
    local point=null;foreach(p in atelier.projected) if(p.id==id) point=p;
    if(point==null) return;
    atelier.gesture={id=id,mode=atelier.mode,x=x,y=y,factor=point.screen.pixelsPerMeter,
        before=clone atelier.snapshot.parameters};atelier.preview=clone atelier.snapshot.parameters;
}
function acDragPoint(x,y,depth) {
    if(atelier.gesture==null) return;
    local g=atelier.gesture,prefix="face_"+g.id+"_",patch={};
    local scale=atelier.snapshot.parameters.height*atelier.snapshot.parameters.head;
    if(g.mode=="Body") {
        local delta=(g.id=="waist" || g.id=="hips" || g.id=="shoulders")?(x-g.x)/g.factor:-(y-g.y)/g.factor;
        local lo=0.9,hi=1.1;foreach(f in atelierFields) if(f[0]==g.id) {lo=f[3];hi=f[4];}
        patch[g.id] <- acClamp(g.before[g.id]+delta*2.0,lo,hi);
    } else if(depth) acMirror(patch,g.id,"z",acClamp(g.before[prefix+"z"]+(x-g.x)/g.factor/scale,-0.012,0.012));
    else {
        acMirror(patch,g.id,"x",acClamp(g.before[prefix+"x"]+(x-g.x)*cos(atelier.yaw)/g.factor/scale,-0.012,0.012));
        acMirror(patch,g.id,"y",acClamp(g.before[prefix+"y"]-(y-g.y)/g.factor/scale,-0.012,0.012));
    }
    atelier.preview=clone g.before;foreach(k,v in patch) atelier.preview[k]=v;
    ui.select("atelier");foreach(k,v in patch) ui.setValue("p-"+k,v);
}
function acEndPoint(cancel=false) {
    if(atelier.gesture==null) return;
    if(!cancel) acChecked(atelier.rig.apply({parameters=atelier.preview}));
    atelier.gesture=null;atelier.preview=null;atelier.status=cancel?"Landmark edit cancelled":"Landmark edit committed";acRefresh(true);
}
function acPointer() {
    ui.select("atelier");local hovered=ui.viewportHovered("stage"),left=ui.viewportActive("stage"),right=mouse.isDown(2);
    local x=ui.viewportMouseX("stage"),y=ui.viewportMouseY("stage");
    if(hovered && left && !atelier.previousLeft) {
        local nearest=null,best=225.0;
        foreach(p in atelier.projected) {local dx=x-p.screen.x,dy=y-p.screen.y,d=dx*dx+dy*dy;if(d<best) {best=d;nearest=p;}}
        if(nearest!=null) acBeginPoint(nearest.id,x,y);
    }
    if(atelier.gesture!=null) {
        if(keyboard.isDown("escape")) acEndPoint(true);
        else if(left) acDragPoint(x,y,keyboard.isDown("lshift") || keyboard.isDown("rshift"));
        else acEndPoint();
    }
    if(hovered && right && atelier.previousRight && atelier.gesture==null) {
        atelier.yaw+=(x-atelier.mouseX)*0.008;acCamera();
    }
    if(hovered) {
        local wheel=ui.viewportWheel("stage");if(wheel!=0.0) {atelier.distance=acClamp(atelier.distance-wheel*0.08,0.55,4.5);acCamera();}
    }
    atelier.previousLeft=left;atelier.previousRight=right;atelier.mouseX=x;atelier.mouseY=y;
}
function acDrawPoints(canvas) {
    gfx.setCanvas(canvas);
    foreach(p in atelier.projected) {
        local s=p.screen,selected=p.id==atelier.selected;
        gfx.drawSolidRect(s.x-5.0,s.y-5.0,10.0,10.0,0.02,0.04,0.07,0.95);
        gfx.drawSolidRect(s.x-3.0,s.y-3.0,6.0,6.0,selected?1.0:0.25,selected?0.75:0.85,selected?0.24:1.0,1.0);
        if(selected) {
            gfx.drawSolidRect(s.x+7.0,s.y-1.0,25.0,2.0,0.95,0.35,0.34,1.0);
            gfx.drawSolidRect(s.x-1.0,s.y-32.0,2.0,25.0,0.35,0.9,0.55,1.0);
        }
    }
    gfx.setCanvas(null);
}
eve_init=function() {
    if("dev" in eve) {eve.dev.ai.setVisible(false);eve.dev.console.setVisible(false);}
    local module=eve.Avatar();atelier.rig=eve.AvatarAppearance();acChecked(atelier.rig.configure(atelierDefinition()));
    atelier.defaults=acChecked(atelier.rig.snapshot());atelier.snapshot=atelier.defaults;
    if(model3d==null) model3d=eve.Model3D();if(anim==null) anim=eve.Animation();
    arInit();atelier.camera=eve.Camera3D();atelier.camera.setFov(30.0);
    atelier.camera.setAmbient(0.55,0.55,0.58);atelier.camera.setActive(true);acCamera();
    gfx.setBackgroundColor(0.06,0.075,0.10,1.0);gfx.setDirectionalLight(-0.4,0.8,0.9,0.96,0.96,0.96);
    local key=eve.Light3D();key.setType("dir");key.setDirection(-0.4,0.8,0.9);key.setColor(1.0,1.0,1.0,0.96);atelier.lights.append(key);
    atelier.ground=eve.Renderable3D();atelier.ground.setMesh(gfx.newMeshCylinder(64,1,true));
    atelier.ground.setPosition(0.0,-0.035,0.0);atelier.ground.setScale(0.65,0.035,0.65);atelier.ground.setTint(0.12,0.16,0.22,1.0);
    ui.setTheme("dark");acRefresh(true);
    print("CHARACTER_ATELIER_READY editor=AppFrame landmarks=15 renderer=ysa\n");
};
eve_update=function(dt) {
    atelier.time+=dt;if(atelier.playing) atelier.motionTime+=dt;ui.select("atelier");local click=ui.consumeClick();
    while(click!="") {local slash=click.find("/");acAction(slash==null?click:click.slice(slash+1));ui.select("atelier");click=ui.consumeClick();}
    local change=ui.consumeChange();
    while(change!="") {
        local slash=change.find("/"),id=slash==null?change:change.slice(slash+1);
        if(id.find("p-")==0) {
            local k=id.slice(2),v=ui.getValue(id),patch={};
            if(k.find("face_")==0) {local split=k.find("_",5);acMirror(patch,k.slice(5,split),k.slice(split+1),v);}
            else patch[k] <- v;
            acEdit({parameters=patch});
        }ui.select("atelier");change=ui.consumeChange();
    }
    arPose(atelier.preview==null?atelier.snapshot.parameters:atelier.preview,atelier.snapshot.selections.pose,atelier.motionTime);
    if(atelier.autoFit && atelier.view=="body") {
        local maxY=1.0;
        for(local i=0;i<atelierCharacter.skeleton.getBoneCount();i++)
            maxY=acMax(maxY,atelierCharacter.pose.getWorldPositionY(i)+atelierCharacter.offsetY);
        maxY+=0.16*atelier.snapshot.parameters.height;
        atelier.focus=maxY*0.5;atelier.distance=maxY/(2.0*tan(15.0*3.14159265/180.0))*1.12;acCamera();atelier.autoFit=false;
    }
    acLandmarks();acPointer();
};
eve_render=function() {
    gfx.clear();ui.select("atelier");local canvas=ui.viewportCanvas("stage");
    if(canvas!=null) {
        atelier.width=canvas.getWidth().tofloat();atelier.height=canvas.getHeight().tofloat();
        gfx.renderScene3DToCanvas(canvas,atelier.camera);acLandmarks();acDrawPoints(canvas);
    }ui.beginFrameAndRender();
};
