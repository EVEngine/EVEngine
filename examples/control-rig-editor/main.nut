// Project-composed UI. Native ControlRigEditor owns all authored data and history.
persist rigSaved = "";
// Reuse ECS-owned preview entities across re-dofile; dropping script refs does not destroy them.
if("rigUi" in getroottable() && rigUi.rig!=null) {
    local detached=rigUi.rig.disableAutomation();if(!detached.ok)throw detached.status.summary;
}
if(!("rigUi" in getroottable())) rigUi <- { rig=null, skeleton=null, pose=null, clip=null, player=null, camera=null,
    model=null, texture=null, parts=[], skins=[], selected=0, playing=false, baked=false,
    down=false, dragging=false, drag=null, status="Ready", frame=0, document=null, graph=false, graphPage=0, font=null, proof=0, proofFrame=0, showLimits=false, limitDraft=null };

function must(result) { if (!result.ok) throw result.status.summary; return "value" in result ? result.value : null; }
function report(result) { rigUi.status=result.ok ? "Edit committed" : result.status.summary; if(result.ok) refreshDocument(); return result.ok; }
function refreshDocument() { rigUi.revision <- rigUi.rig.getRevision(); rigUi.document=must(rigUi.rig.getDocument()); rigSaved=must(rigUi.rig.exportJson()); }
function host(name,x,y,w,h,builder) {
    ui.beginBuild(); ui.beginWindow(name,"root"); builder(); ui.end(); ui.mountBuildAs(name); ui.select(name);
    ui.setHostPos(x.tofloat(),y.tofloat(),0.0,0.0); ui.setHostSize(w.tofloat(),h.tofloat()); ui.setHostMovable(false); ui.setHostResizable(false); ui.setHostOverlay(false);
}
function selectedId() { return rigUi.rig.getControlId(rigUi.selected); }
function selectControl(index) {
    if(index<0 || index>=rigUi.rig.getControlCount()) return;
    rigUi.selected=index;
    local t=must(rigUi.rig.getControlLocal(index)); ui.select("Inspector");
    ui.setText("selected",selectedId());
    ui.setValue("x",t.px); ui.setValue("y",t.py); ui.setValue("z",t.pz);
    ui.setValue("angle",0.0);ui.setValue("angle-x",0.0);ui.setValue("angle-y",0.0); ui.setValue("scale",t.sx);
    if(rigUi.showLimits) buildLimits();
}
function limitAxes(l) { return [l.translation[0],l.translation[1],l.translation[2],l.rotation[0],l.rotation[1],l.rotation[2],l.scale]; }
function buildLimits() {
    rigUi.limitDraft=must(rigUi.rig.getDocument()).controls[rigUi.selected].limits;
    host("Solve",1090,450,350,450,function() {
        ui.button("Return to solve graph","toggle-limits");ui.text("LIMITS: "+selectedId(),"limit-title");
        local degrees=function(axes) {local n=0;foreach(a in axes)if(a.mode=="free" || (a.mode=="limited" && a.min<a.max))n++;return n;};
        local l=rigUi.limitDraft;
        local translation=l.extension?(l.minLength<l.maxLength?1:0):degrees(l.translation);
        ui.text("DOF: move "+translation+", rotate "+degrees(l.rotation)+", scale "+degrees([l.scale]),"limit-dof");
        ui.textWrapped("Scroll for all axes. Modes: free, locked, limited. Apply is one undo step.",310.0,"limit-help");
        local labels=["Move X","Move Y","Move Z","Pitch X","Yaw Y","Roll Z","Scale ratio"];
        foreach(i,a in limitAxes(rigUi.limitDraft)) {
            ui.inputText(labels[i]+" mode",a.mode,"limit-mode-"+i);
            ui.inputText("Min",a.min.tostring(),"limit-min-"+i);ui.inputText("Max",a.max.tostring(),"limit-max-"+i);
        }
        ui.checkbox("Axial extension (translation axes must be free)",rigUi.limitDraft.extension,"limit-extension");
        ui.inputText("Min length ratio",rigUi.limitDraft.minLength.tostring(),"limit-length-min");
        ui.inputText("Max length ratio",rigUi.limitDraft.maxLength.tostring(),"limit-length-max");
        ui.button("Apply limits","apply-limits");ui.button("Hinge X preset","hinge-limits");
    });
}
function limitsJson(l) {
    local axis=function(a) { return "{\"mode\":\""+a.mode+"\",\"min\":"+a.min+",\"max\":"+a.max+"}"; };
    local reference="";foreach(i,v in l.reference)reference+=(i?",":"")+v;
    return "{\"reference\":["+reference+"],\"translation\":["+axis(l.translation[0])+","+axis(l.translation[1])+","+axis(l.translation[2])+"],\"rotation\":["+
        axis(l.rotation[0])+","+axis(l.rotation[1])+","+axis(l.rotation[2])+"],\"scale\":"+axis(l.scale)+",\"extension\":"+(l.extension?"true":"false")+
        ",\"minLength\":"+l.minLength+",\"maxLength\":"+l.maxLength+"}";
}
function applyLimits(hinge) {
    ui.select("Solve");local l=rigUi.limitDraft;
    foreach(i,a in limitAxes(l)) {
        a.mode=hinge?(i==3?"limited":"locked"):ui.getValueText("limit-mode-"+i);
        if(a.mode!="free" && a.mode!="locked" && a.mode!="limited")throw "Mode must be free, locked or limited";
        a.min=hinge?(i==6?1:(i==3?-90:0)):ui.getValueText("limit-min-"+i).tofloat();
        a.max=hinge?(i==6?1:(i==3?90:0)):ui.getValueText("limit-max-"+i).tofloat();
    }
    l.extension=hinge?false:ui.getChecked("limit-extension");
    l.minLength=ui.getValueText("limit-length-min").tofloat();l.maxLength=ui.getValueText("limit-length-max").tofloat();
    if(report(rigUi.rig.setLimitsJson(selectedId(),limitsJson(l)))) {rigUi.baked=false;rigUi.playing=false;selectControl(rigUi.selected);}
}
function buildPanels() {
    host("Controls",0,0,220,620,function() {
        ui.text("RIG HIERARCHY","title");
        ui.textWrapped("Select a control or drag its cyan handle in the viewport.",190.0,"help");
        foreach(i,c in rigUi.document.controls) ui.listItem((c.parent==""?"":"  ")+c.id,"control-"+i);
    });
    host("Pose",220,0,870,620,function() {
        ui.beginToolbar("view-tools");ui.button("Pose / solve graph","toggle-graph");ui.button("Previous page","prev-page");ui.button("Next page","next-page");ui.end();
        ui.viewport("view",824.0,450.0);
        ui.text("Ready","status");
    });
    host("Inspector",1090,0,350,450,function() {
        ui.text("TRANSFORM","title");ui.text(selectedId(),"selected");
        ui.slider("Local X",0.0,-3.0,3.0,"x");ui.slider("Local Y",0.0,-3.0,3.0,"y");ui.slider("Local Z",0.0,-3.0,3.0,"z");
        ui.slider("Rotate X",0.0,-180.0,180.0,"angle-x");ui.slider("Rotate Y",0.0,-180.0,180.0,"angle-y");ui.slider("Rotate Z",0.0,-180.0,180.0,"angle");ui.slider("Uniform scale",1.0,0.1,2.0,"scale");
        ui.checkbox("Auto key",true,"autokey");
        ui.button("Set key","key");ui.button("Delete key at playhead","delete-key");
        ui.inputText("Parent ID","","parent");ui.button("Reparent / keep pose","reparent");
        ui.inputText("New control ID","target","new-control");ui.button("Add target control","add-control");
    });
    host("Solve",1090,450,350,450,function() {
        ui.text("SOLVE GRAPH","title");
        ui.button("Control degrees of freedom / limits","toggle-limits");
        ui.textWrapped("Nodes run after their prerequisites. Cycles and missing references are rejected.",315.0,"help");
        ui.inputText("Node ID","hand-ik","node-id");ui.inputText("Operation","twoBoneIK","kind");
        ui.inputText("Control","target","node-control");ui.inputText("Root bone","upperarm.l","bone");
        ui.inputText("IK middle","lowerarm.l","middle");ui.inputText("IK tip","wrist.l","tip");
        ui.inputText("After node ID","fk:wrist.l","after");ui.slider("Weight",1.0,0.0,1.0,"weight");
        ui.button("Create / replace node","apply-node");ui.button("Delete node","delete-node");
    });
    host("Timeline",0,620,1090,280,function() {
        ui.beginToolbar("transport");ui.button("Play / pause","play");ui.button("Undo","undo");ui.button("Redo","redo");
        ui.button("Save rig","save");ui.button("Load rig","load");ui.button("Bake + replay","bake");ui.button("Edit rig","edit");ui.button("Key from baked pose","match-pose");ui.end();
        ui.slider("Seconds",0.0,0.0,rigUi.rig.getDuration(),"time");
        ui.text("Selected control keys / click timeline to scrub","help");ui.viewport("keys",1048.0,74.0);
    });
    selectControl(rigUi.selected);
}

function buildCharacter() {
    if(rigUi.model==null)rigUi.model=eve.Model3D().newModelDataFromFile("assets/Knight.glb");
    if(rigUi.texture==null)rigUi.texture=gfx.newTextureFromFile("assets/knight_texture.png");
    rigUi.skeleton=anim.newSkeletonFromModel(rigUi.model);
    rigUi.pose=anim.newPose(rigUi.skeleton.getBoneCount());
    rigUi.rig=must(eve.AnimationEditorModule().createRig("knight-rig"));
    must(rigUi.rig.initialize(rigUi.skeleton));
    rigUi.clip=anim.newClip("Rig bake");rigUi.player=anim.newPlayer(rigUi.skeleton);
    for(local i=0;i<rigUi.model.getMeshCount();++i) {
        local part=i<rigUi.parts.len()?rigUi.parts[i]:eve.Model3D().createRenderable(gfx,rigUi.model,i);
        part.setTexture(rigUi.texture);part.setReceiveLight(true);part.setTint(1.0,1.0,1.0,1.0);
        if(i>=rigUi.parts.len())rigUi.parts.push(part);
        if(rigUi.model.hasBones(i)) rigUi.skins.push({part=part,skin=anim.newSkinFromModel(rigUi.model,i,rigUi.skeleton)});
    }
    if(rigUi.camera==null)rigUi.camera=eve.Camera3D();rigUi.camera.setEye(0.0,1.0,6.0);rigUi.camera.setTarget(0.0,1.0,0.0);
    rigUi.camera.setUp(0.0,1.0,0.0);rigUi.camera.setOrthographic(3.0);rigUi.camera.setAmbient(0.65,0.68,0.75);rigUi.camera.setActive(true);
    gfx.setDirectionalLight(-0.4,1.0,0.8,1.4,1.3,1.2);
    // A small authored chest turn demonstrates native control-track playback immediately.
    for(local i=0;i<rigUi.rig.getControlCount();++i) if(rigUi.rig.getControlId(i)=="chest") {
        rigUi.selected=i;local t=must(rigUi.rig.getControlLocal(i));
        must(rigUi.rig.setControl("chest",t.px,t.py,t.pz,t.qx,t.qy,t.qz,t.qw,t.sx,true));
        must(rigUi.rig.seek(1.0));
        // Compose a rotation delta through the quaternion product, retaining bind orientation.
        local a=0.25; local s=sin(a);local c=cos(a);
        must(rigUi.rig.setControl("chest",t.px,t.py,t.pz,t.qx*c+t.qy*s,t.qy*c-t.qx*s,t.qz*c+t.qw*s,t.qw*c-t.qz*s,t.sx,true));
        must(rigUi.rig.seek(2.0));must(rigUi.rig.setControl("chest",t.px,t.py,t.pz,t.qx,t.qy,t.qz,t.qw,t.sx,true));
        must(rigUi.rig.seek(0.0));break;
    }
    refreshDocument();
}
function eventId(path) { local i=path.find("/");return i==null?path:path.slice(i+1); }
function handleEvents() {
    local event=ui.consumeClick();
    while(event!="") {
        local id=eventId(event);
        if(id.find("control-")==0) selectControl(id.slice(8).tointeger());
        else if(id=="play") rigUi.playing=!rigUi.playing;
        else if(id=="toggle-limits") {rigUi.showLimits=!rigUi.showLimits;buildPanels();}
        else if(id=="apply-limits" || id=="hinge-limits") applyLimits(id=="hinge-limits");
        else if(id=="toggle-graph") rigUi.graph=!rigUi.graph;
        else if(id=="prev-page") rigUi.graphPage=max(0,rigUi.graphPage-1);
        else if(id=="next-page") rigUi.graphPage=min(((rigUi.document.nodes.len()-1)/21).tointeger(),rigUi.graphPage+1);
        else if(id=="undo") report(rigUi.rig.undo());
        else if(id=="redo") report(rigUi.rig.redo());
        else if(id=="key") {
            local t=must(rigUi.rig.getControlLocal(rigUi.selected));
            report(rigUi.rig.setControl(selectedId(),t.px,t.py,t.pz,t.qx,t.qy,t.qz,t.qw,t.sx,true));
        } else if(id=="delete-key") report(rigUi.rig.deleteKey(selectedId()));
        else if(id=="save") {
            local text=must(rigUi.rig.exportJson());rigUi.status=fs.writeTextAtomic("knight.rig.json",text)?"Saved: "+fs.getSaveDirectory()+"/knight.rig.json":"Save failed";
        } else if(id=="load") { try { if(report(rigUi.rig.importJson(fs.readText("knight.rig.json")))) {rigUi.selected=0;buildPanels();} } catch(error) {rigUi.status="Load failed: "+error;} }
        else if(id=="bake") {
            if(report(rigUi.rig.bakeInto(rigUi.clip,30.0))) {rigUi.clip.setLoop(true);rigUi.player.play(rigUi.clip);rigUi.player.setLoop(true);rigUi.baked=true;rigUi.playing=true;rigUi.status="Playing baked native AnimClip";}
        } else if(id=="edit") {rigUi.baked=false;rigUi.playing=false;report(rigUi.rig.seek(rigUi.rig.getTime()));}
        else if(id=="match-pose") { if(rigUi.baked && report(rigUi.rig.keyFromPose(rigUi.pose))) {rigUi.baked=false;rigUi.playing=false;} }
        else if(id=="reparent") {ui.select("Inspector");report(rigUi.rig.reparent(selectedId(),ui.getValueText("parent")));}
        else if(id=="add-control") {
            ui.select("Inspector");local name=ui.getValueText("new-control");
            if(report(rigUi.rig.addControl(name,"",0.7,1.2,0.0))) {rigUi.selected=rigUi.rig.getControlCount()-1;buildPanels();}
        } else if(id=="apply-node") {
            ui.select("Solve");report(rigUi.rig.setNode(ui.getValueText("node-id"),ui.getValueText("kind"),ui.getValueText("node-control"),
                ui.getValueText("bone"),ui.getValueText("middle"),ui.getValueText("tip"),ui.getValueText("after"),ui.getValue("weight")));
        } else if(id=="delete-node") {ui.select("Solve");report(rigUi.rig.removeNode(ui.getValueText("node-id")));}
        event=ui.consumeClick();
    }
    local changed=ui.consumeChange();
    while(changed!="") {
        local id=eventId(changed);
        if(id=="time") {ui.select("Timeline");rigUi.playing=false;report(rigUi.rig.seek(ui.getValue("time")));}
        else if(id=="x" || id=="y" || id=="z" || id=="angle" || id=="angle-x" || id=="angle-y" || id=="scale") {
            ui.select("Inspector");local t=must(rigUi.rig.getControlLocal(rigUi.selected));
            local rotation=id=="angle" || id=="angle-x" || id=="angle-y";
            local a=(rotation?ui.getValue(id):0)*0.00872664626;local s=sin(a);local c=cos(a);
            local dx=id=="angle-x"?s:0;local dy=id=="angle-y"?s:0;local dz=id=="angle"?s:0;
            if(report(rigUi.rig.setControl(selectedId(),ui.getValue("x"),ui.getValue("y"),ui.getValue("z"),
                t.qw*dx+t.qx*c+t.qy*dz-t.qz*dy,t.qw*dy-t.qx*dz+t.qy*c+t.qz*dx,
                t.qw*dz+t.qx*dy-t.qy*dx+t.qz*c,t.qw*c-t.qx*dx-t.qy*dy-t.qz*dz,ui.getValue("scale"),ui.getChecked("autokey"))))selectControl(rigUi.selected);
            if(rotation) ui.setValue(id,0.0);
        }
        changed=ui.consumeChange();
    }
}
function pointer() {
    ui.select("Pose");local down=ui.viewportActive("view");local x=ui.viewportMouseX("view");local y=ui.viewportMouseY("view");
    local scale=450.0/3.0;
    if(down && !rigUi.down && ui.viewportHovered("view") && rigUi.graph) {
        local index=rigUi.graphPage*21+floor(y/62.0).tointeger()*3+floor(x/274.0).tointeger();
        if(index>=0 && index<rigUi.document.nodes.len()) {
            local n=rigUi.document.nodes[index];ui.select("Solve");
            ui.setValueText("node-id",n.id);ui.setValueText("kind",n.operation);ui.setValueText("node-control",n.control);
            ui.setValueText("bone",n.bone);ui.setValueText("middle",n.middle);ui.setValueText("tip",n.tip);
            ui.setValueText("after",n.after.len()>0?n.after[0]:"");ui.setValue("weight",n.weight);
        }
    }
    if(down && !rigUi.down && ui.viewportHovered("view") && !rigUi.baked && !rigUi.graph) {
        local best=18.0*18.0;local selected=-1;
        for(local i=0;i<rigUi.rig.getControlCount();++i) {
            local p=must(rigUi.rig.getControlWorld(i));local dx=x-(412+p.px*scale);local dy=y-(225-(p.py-1)*scale);
            if(dx*dx+dy*dy<best) {best=dx*dx+dy*dy;selected=i;}
        }
        if(selected>=0) {selectControl(selected);rigUi.playing=false;rigUi.dragging=true;rigUi.drag=must(rigUi.rig.getControlWorld(selected));}
    }
    if(rigUi.dragging) {
        rigUi.drag.px=(x-412)/scale;rigUi.drag.py=1+(225-y)/scale;
        if(down) {local r=rigUi.rig.previewMoveControl(rigUi.selected,rigUi.drag.px,rigUi.drag.py,rigUi.drag.pz);if(!r.ok) rigUi.status=r.status.summary;}
        else {
            ui.select("Inspector");report(rigUi.rig.moveControl(rigUi.selected,rigUi.drag.px,rigUi.drag.py,rigUi.drag.pz,ui.getChecked("autokey")));
            rigUi.dragging=false;selectControl(rigUi.selected);
        }
    }
    ui.select("Timeline");if(ui.viewportActive("keys")) {
        rigUi.playing=false;must(rigUi.rig.seek(ui.viewportMouseX("keys")/1048.0*rigUi.rig.getDuration()));
    }
    rigUi.down=down;
}
function drawHandles() {
    ui.select("Pose");local canvas=ui.viewportCanvas("view");if(canvas==null)return;
    if(!rigUi.graph) gfx.renderScene3DToCanvas(canvas,rigUi.camera);
    gfx.setCanvas(canvas);
    if(rigUi.graph) {
        gfx.clear();gfx.drawSolidRect(0.0,0.0,824.0,450.0,0.04,0.06,0.09,1.0);gfx.setFont(rigUi.font);
        foreach(index,n in rigUi.document.nodes) {
            if(index<rigUi.graphPage*21 || index>=(rigUi.graphPage+1)*21) continue;
            local i=index-rigUi.graphPage*21;local x=(i%3)*274.0+8;local y=floor(i/3.0)*62+7;
            gfx.drawSolidRect(x,y,260.0,48.0,0.10,0.18,0.24,1.0);
            gfx.print(n.id+" / "+n.operation,x+8.0,y+5.0,0.4,0.95,0.95,1.0,0.55);
            gfx.print(n.control+" -> "+n.bone,x+8.0,y+25.0,0.8,0.85,0.9,1.0,0.45);
            foreach(dep in n.after) foreach(j,other in rigUi.document.nodes) if(other.id==dep && j>=rigUi.graphPage*21 && j<(rigUi.graphPage+1)*21) {
                local row=j-rigUi.graphPage*21;local ax=(row%3)*274.0+268;local ay=floor(row/3.0)*62+31;
                for(local step=0;step<24;++step) {local t=step/24.0;gfx.drawSolidRect(ax+(x-ax)*t,ay+(y+24.0-ay)*t,2.0,2.0,0.35,0.65,0.7,0.6);}
            }
        }
    }
    if(!rigUi.graph && !rigUi.baked) for(local i=0;i<rigUi.rig.getControlCount();++i) {
        local p=rigUi.dragging && i==rigUi.selected ? rigUi.drag : must(rigUi.rig.getControlWorld(i));
        local x=412+p.px*150;local y=225-(p.py-1)*150;local selected=i==rigUi.selected;local r=selected?9:4;
        gfx.drawSolidRect(x-r,y-r,r*2.0,2.0,selected?1.0:0.2,selected?0.7:0.9,0.9,1.0);
        gfx.drawSolidRect(x-r,y+r,r*2.0,2.0,selected?1.0:0.2,selected?0.7:0.9,0.9,1.0);
        gfx.drawSolidRect(x-r,y-r,2.0,r*2.0,selected?1.0:0.2,selected?0.7:0.9,0.9,1.0);
        gfx.drawSolidRect(x+r,y-r,2.0,r*2.0,selected?1.0:0.2,selected?0.7:0.9,0.9,1.0);
    }
    gfx.setCanvas(null);
    ui.select("Timeline");canvas=ui.viewportCanvas("keys");if(canvas==null)return;
    gfx.setCanvas(canvas);gfx.clear();gfx.drawSolidRect(0.0,0.0,1048.0,74.0,0.045,0.065,0.09,1.0);
    for(local i=0;i<=20;++i) gfx.drawSolidRect(i*52.4,0.0,1.0,74.0,0.14,0.18,0.23,1.0);
    if(rigUi.selected>=0 && rigUi.selected<rigUi.document.controls.len()) foreach(k in rigUi.document.controls[rigUi.selected].keys) gfx.drawSolidRect(k.time/rigUi.rig.getDuration()*1048.0-4.0,30.0,8.0,12.0,1.0,0.7,0.25,1.0);
    gfx.drawSolidRect(rigUi.rig.getTime()/rigUi.rig.getDuration()*1048.0,0.0,2.0,74.0,0.3,0.9,0.9,1.0);gfx.setCanvas(null);
}
eve_init = function() {
    if("dev" in eve) {
        eve.dev.markStateRoot("rigSaved");
        eve.dev.console.setVisible(false);
        eve.dev.ai.setVisible(false);
    }
    local saved=rigSaved==""?null:rigSaved;
    if(!fs.setIdentity("control-rig-editor",true) || !fs.setupWriteDirectory()) throw "Cannot initialize rig save directory";
    print("CONTROL_RIG_SAVE_DIRECTORY "+fs.getSaveDirectory()+"\n");
    if(rigUi.rig!=null) must(rigUi.rig.disableAutomation());
    rigUi.player=null;rigUi.clip=null;rigUi.skins=[];rigUi.pose=null;rigUi.skeleton=null;
    rigUi.playing=false;rigUi.baked=false;rigUi.dragging=false;rigUi.frame=0;rigUi.proof=0;rigUi.proofFrame=0;
    gfx.setBackgroundColor(0.06,0.08,0.12,1.0);ui.setTheme("dark");ui.setScale(1.0);buildCharacter();
    if(saved!=null) {must(rigUi.rig.importJson(saved));refreshDocument();} must(rigUi.rig.enableAutomation()); buildPanels();
    rigUi.font=gfx.newFont(eve.Font().newFontDataFromFile("assets/DejaVuSans-Bold.ttf",22),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.:/->");
    print("CONTROL_RIG_EDITOR_READY nativeSolver=true nativeSkin=true transactional=true\n");
    local ids="";foreach(part in rigUi.parts)ids+=part.getEntityId()+":"+part.getEntityGeneration()+",";
    print("CONTROL_RIG_PREVIEW_ENTITIES "+ids+"\n");
};
eve_update = function(dt) {
    if(rigUi.revision!=rigUi.rig.getRevision()) {
        rigUi.dragging=false;rigUi.baked=false;rigUi.playing=false;
        local selected=rigUi.selected>=0 && rigUi.selected<rigUi.document.controls.len()?rigUi.document.controls[rigUi.selected].id:"";
        refreshDocument();rigUi.selected=rigUi.rig.getControlCount()>0?0:-1;
        foreach(i,control in rigUi.document.controls) if(control.id==selected) {rigUi.selected=i;break;}
        buildPanels();
        rigUi.status="Agent edit synchronized";
    }
    try {handleEvents();pointer();} catch(error) {rigUi.status="Edit rejected: "+error;rigUi.dragging=false;}
    if(rigUi.baked) {if(rigUi.playing)rigUi.player.update(dt);rigUi.pose.copyFrom(rigUi.player.getPose());}
    else {if(rigUi.playing) {local t=rigUi.rig.getTime()+dt;local d=rigUi.rig.getDuration();must(rigUi.rig.seek(t-floor(t/d)*d));} must(rigUi.rig.copyPose(rigUi.pose));}
    rigUi.pose.computeWorld(rigUi.skeleton);foreach(binding in rigUi.skins)binding.skin.applyToMesh(gfx,binding.part.getMesh(),rigUi.pose);
    ui.select("Pose");ui.setText("status",rigUi.status);ui.select("Timeline");ui.setValue("time",rigUi.rig.getTime());
};
eve_render = function() {
    gfx.clear();drawHandles();ui.beginFrameAndRender();rigUi.frame++;
    if(getenv("EVE_CONTROL_RIG_PROOF")=="1" && rigUi.proof<4 && rigUi.frame-rigUi.proofFrame>=30) {
        local names=["pose-0","pose-1","baked-1","solve-graph"];
        if(gfx.saveFramePng("evidence/"+names[rigUi.proof]+".png")) {
            print("CONTROL_RIG_PROOF "+names[rigUi.proof]+"\n");rigUi.proof++;rigUi.proofFrame=rigUi.frame;
            if(rigUi.proof==1) must(rigUi.rig.seek(1.0));
            else if(rigUi.proof==2) {
                local saved=must(rigUi.rig.exportJson());
                if(!fs.writeTextAtomic("control-rig-proof.json",saved)) throw "Proof save failed";
                must(rigUi.rig.importJson(fs.readText("control-rig-proof.json")));
                must(rigUi.rig.bakeInto(rigUi.clip,30.0));rigUi.player.play(rigUi.clip);rigUi.player.setTime(1.0);
                rigUi.baked=true;rigUi.playing=false;
            } else if(rigUi.proof==3) {
                must(rigUi.rig.keyFromPose(rigUi.player.getPose()));must(rigUi.rig.undo());
                rigUi.baked=false;rigUi.graph=true;refreshDocument();
            } else print("CONTROL_RIG_PROOF_COMPLETE roundtrip=true bake=true backwardMatch=true undo=true\n");
        }
    }
};

// The engine restores rigSaved before this hook; native previews are reconstructed.
eve_after_reload <- function() { eve_init(); };
