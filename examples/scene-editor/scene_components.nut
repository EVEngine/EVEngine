// Project-owned presentation. Replace this UI, camera or primitive projection
// without changing SceneEditorSession or any domain operation.
persist sceneDemo = {
    session=null, camera=null, gizmo=null, selected="", selection=[], objects=[], shapes=[], axes=[],
    revision=-1, serial=0, down=false, status="Select an object in the world or hierarchy",
    mounted=false, frame=0, yaw=0.63, pitch=0.48, distance=15.0, lastX=0.0,lastY=0.0,orbit=false,
    physicsFrame=null, placementKind="", gizmoMode="move"
};
const SCENE_HOST = "scene-components";
const SCENE_SAVE = "scene-components.json";
const SCENE_COLLIDER_CACHE = "scene-components-colliders.json";
dofile(sceneToolsFull ? "scene_visuals.nut" : "../scene-editor/scene_visuals.nut");

function sceneChecked(result) {
    if (!result.ok) { sceneDemo.status = result.status.summary; return false; }
    return true;
}
function sceneObject(id) {
    foreach (object in sceneDemo.objects) if (object.id == id) return object;
    return null;
}
function scenePhysicsObject(id) {
    if (sceneDemo.physicsFrame == null) return null;
    foreach (object in sceneDemo.physicsFrame.objects) if (object.object == id) return object;
    return null;
}
function isSceneSelected(id) { foreach (value in sceneDemo.selection) if (value==id) return true; return false; }
function scenePhysicsPoint(object,x,y,z) {
    x*=object.scale[0];y*=object.scale[1];z*=object.scale[2];
    local a=object.rotation[2],nx=x*cos(a)-y*sin(a),ny=x*sin(a)+y*cos(a);x=nx;y=ny;
    a=object.rotation[0];ny=y*cos(a)-z*sin(a);local nz=y*sin(a)+z*cos(a);y=ny;z=nz;
    a=object.rotation[1];nx=x*cos(a)+z*sin(a);nz=-x*sin(a)+z*cos(a);
    return [nx+object.position[0],ny+object.position[1],nz+object.position[2]];
}
function startScenePhysicsPlacement(mode) {
    if (sceneDemo.selected=="" || sceneDemo.session.isPhysicsPlacementActive()) return false;
    local objects=[];
    foreach (entry in sceneDemo.objects) if (entry.id!="root")
        objects.push({object=entry.id,selected=isSceneSelected(entry.id),
            shape=entry.id=="ground" ? "box" : "auto",halfExtents=[0.5,0.5,0.5],
            colliderResourceKey=entry.id=="ground" ? "" : "demo-cube-v1",layerBits=1,tags=["placeable"]});
    local started=sceneDemo.session.beginPhysicsPlacement({mode=mode,primary=sceneDemo.selected,objects=objects});
    if (!sceneChecked(started)) return false;
    sceneDemo.placementKind=mode; sceneDemo.physicsFrame=null;
    sceneDemo.status=mode=="drop" ? "Dropping selection..." : "Physics placement";
    return true;
}
function sceneCommand(command, payload) {
    if (sceneChecked(sceneDemo.session.execute(command, payload))) {
        sceneDemo.status = command;
    }
    refreshSceneComponents();
}
function selectSceneObject(id, additive=false) {
    if (!additive) sceneDemo.selection.clear();
    local found=-1; for(local i=0;i<sceneDemo.selection.len();i++) if(sceneDemo.selection[i]==id) found=i;
    if (id!="" && found<0) sceneDemo.selection.push(id);
    else if (additive && found>=0) sceneDemo.selection.remove(found);
    sceneDemo.selected=sceneDemo.selection.len()>0 ? sceneDemo.selection[sceneDemo.selection.len()-1] : "";
    refreshSceneComponents();
}
function clearSceneShapes(shapes) {
    foreach (shape in shapes) sceneChecked(shape.remove());
    shapes.clear();
}
function sceneParentPoint(x, y, z) {
    local object = sceneObject(sceneDemo.selected);
    if (object == null || object.parent == "") return [x,y,z];
    return scene.localToWorldAt(SCENE_HOST, object.parent, x,y,z);
}
function scenePreviewPoint(x,y,z) {
    local g=sceneDemo.gizmo;
    x*=g.getScaleX();y*=g.getScaleY();z*=g.getScaleZ();
    local a=g.getRotationZ(), nx=x*cos(a)-y*sin(a), ny=x*sin(a)+y*cos(a); x=nx;y=ny;
    a=g.getRotationX();ny=y*cos(a)-z*sin(a);local nz=y*sin(a)+z*cos(a);y=ny;z=nz;
    a=g.getRotationY();nx=x*cos(a)+z*sin(a);nz=-x*sin(a)+z*cos(a);x=nx;z=nz;
    return sceneParentPoint(x+g.getPositionX(),y+g.getPositionY(),z+g.getPositionZ());
}
function drawSceneComponents() {
    clearSceneShapes(sceneDemo.shapes);
    scene.updateTransforms();
    local slot=0;
    foreach (object in sceneDemo.objects) {
        if (object.id == "root" || object.id == "ground") continue;
        local center = scene.localToWorldAt(SCENE_HOST, object.id, 0.0,0.0,0.0);
        local dragging = isSceneSelected(object.id) && sceneDemo.gizmo.isDragging();
        local physicsObject = scenePhysicsObject(object.id);
        if (physicsObject != null) center=physicsObject.position;
        else if (dragging) center=scenePreviewPoint(0.0,0.0,0.0);
        local points = [center[0],center[1],center[2]];
        foreach (axis in [[0.5,0.0,0.0],[0.0,0.5,0.0],[0.0,0.0,0.5]]) {
            local end = physicsObject!=null ? scenePhysicsPoint(physicsObject,axis[0],axis[1],axis[2]) : dragging ? scenePreviewPoint(axis[0],axis[1],axis[2]) :
                scene.localToWorldAt(SCENE_HOST, object.id, axis[0],axis[1],axis[2]);
            points.extend([end[0]-center[0],end[1]-center[1],end[2]-center[2]]);
        }
        local selected = isSceneSelected(object.id);
        sceneLitCube(points,selected,slot++);
        // Bounds are a derived picking projection; they are not authored TRS.
        scene.setNodeBoundsAt(SCENE_HOST, object.id, -0.5,-0.5,-0.5,0.5,0.5,0.5);
    }
    for(local i=slot;i<sceneDemo.visuals.len();i++) sceneDemo.visuals[i].entity.setVisible(false);
    drawSceneAxes();
}
function drawSceneAxes() {
    clearSceneShapes(sceneDemo.axes);
    if (sceneDemo.selected == "") return;
    local gizmo = sceneDemo.gizmo;
    for (local i=0; i<gizmo.getPartCount(); ++i) {
        local ox=gizmo.getPartOriginX(i), oy=gizmo.getPartOriginY(i), oz=gizmo.getPartOriginZ(i);
        local length=gizmo.getPartLength(i);
        local a=sceneParentPoint(ox,oy,oz);
        local b=sceneParentPoint(ox+gizmo.getPartDirX(i)*length,
            oy+gizmo.getPartDirY(i)*length,oz+gizmo.getPartDirZ(i)*length);
        local kind=gizmo.getPartKind(i), line=null;
        local r=gizmo.getPartColorR(i), green=gizmo.getPartColorG(i), blue=gizmo.getPartColorB(i);
        if (kind=="axis") {
            if(gizmo.getMode()=="translate")
                line=gfx.newPrimitiveArrow3D(a[0],a[1],a[2],b[0],b[1],b[2],length*0.22,length*0.08,r,green,blue,1.0,3.0);
            else {
                line=gfx.newPrimitiveLine3D(a[0],a[1],a[2],b[0],b[1],b[2],r,green,blue,1.0,3.0);
                sceneHandleBox([ox+gizmo.getPartDirX(i)*length,oy+gizmo.getPartDirY(i)*length,oz+gizmo.getPartDirZ(i)*length],length*0.065,r,green,blue);
            }
        } else if(kind=="center") {
            sceneHandleBox([ox,oy,oz],gizmo.getPartRadius(i)*0.75,r,green,blue);continue;
        } else if(kind=="plane") {
            local mask=gizmo.getPartAxis(i), points=[];
            local first=mask.slice(0,1)=="x" ? 0 : 1, second=mask.slice(1)=="y" ? 1 : 2;
            local lo=gizmo.getSize()*0.18,hi=gizmo.getPartLength(i);
            foreach(corner in [[lo,lo],[hi,lo],[hi,hi],[lo,hi]]) {
                local p=[ox,oy,oz];
                local u=[gizmo.getPartDirX(first),gizmo.getPartDirY(first),gizmo.getPartDirZ(first)];
                local v=[gizmo.getPartDirX(second),gizmo.getPartDirY(second),gizmo.getPartDirZ(second)];
                for(local k=0;k<3;k++)p[k]+=u[k]*corner[0]+v[k]*corner[1];
                points.extend(sceneParentPoint(p[0],p[1],p[2]));
            }
            local u=[],v=[],center=[];
            for(local k=0;k<3;k++) {
                u.push((points[3+k]-points[k])*0.5);v.push((points[9+k]-points[k])*0.5);
                center.push(points[k]+u[k]+v[k]);
            }
            local n=sceneCross(u,v),norm=sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
            center.extend(u);center.extend(v);foreach(value in n)center.push(value/norm*0.004);
            line=gfx.newPrimitiveObb3D(center,r,green,blue,0.55,1.0);
        }
        else if (kind=="ring") {
            local normal=[gizmo.getPartDirX(i),gizmo.getPartDirY(i),gizmo.getPartDirZ(i)];
            local u=fabs(normal[1])<0.9 ? [-normal[2],0.0,normal[0]] : [0.0,normal[2],-normal[1]];
            local norm=sqrt(u[0]*u[0]+u[1]*u[1]+u[2]*u[2]);for(local k=0;k<3;k++)u[k]/=norm;
            local v=[normal[1]*u[2]-normal[2]*u[1],normal[2]*u[0]-normal[0]*u[2],normal[0]*u[1]-normal[1]*u[0]];
            local points=[], radius=gizmo.getPartRadius(i);
            for(local k=0;k<48;k++) {
                local angle=k*6.28318530718/48.0;
                local p=sceneParentPoint(ox+radius*(u[0]*cos(angle)+v[0]*sin(angle)),
                    oy+radius*(u[1]*cos(angle)+v[1]*sin(angle)),oz+radius*(u[2]*cos(angle)+v[2]*sin(angle)));
                points.extend(p);
            }
            line=gfx.newPrimitivePolyline3D(points,true,r,green,blue,1.0,3.0);
        } else continue;
        sceneKeepHandle(line,kind!="ring");
    }
}
function mountScenePanels() {
    ui.beginBuild();
    ui.beginWindow(sceneToolsFull ? "Scene components" : "Build in game", "root");
    ui.textWrapped(sceneDemo.status,320.0,"status");
    ui.beginRow("main-actions",6.0);
    ui.button("Place cube", "create");
    ui.button("Undo", "undo"); ui.button("Redo", "redo");
    ui.end();
    if (sceneToolsFull) {
        ui.beginRow("file-actions",6.0);
        ui.button("Save", "save"); ui.button("Load", "load"); ui.button("Resync", "resync");
        ui.end();
        ui.beginRow("gizmo-modes",6.0);
        ui.button("Move","mode:translate");ui.button("Rotate","mode:rotate");ui.button("Scale","mode:scale");
        ui.end();
        ui.separator("hierarchy-sep");
        ui.text("Live hierarchy", "hierarchy-title");
        function addChildren(parent, indent) {
            foreach (object in sceneDemo.objects) if (object.parent == parent) {
                if (object.id=="ground") continue;
                    ui.button(indent + (isSceneSelected(object.id) ? "> " : "") + object.name,
                    "select:" + object.id);
                addChildren(object.id, indent + "  ");
            }
        }
        addChildren("", "");
        local object=sceneObject(sceneDemo.selected);
        if (object != null) {
            ui.separator("properties-sep");
            ui.inputText("Name", object.name, "name");
            ui.inputText("Parent id", object.parent, "parent");
            foreach (fields in [["x","y","z"],["rotationX","rotationY","rotationZ"],["scaleX","scaleY","scaleZ"]]) {
                ui.beginRow("fields-"+fields[0],6.0);
                foreach (field in fields) {
                    ui.beginColumn("field-"+field,2.0);ui.text(field,"label-"+field);
                    ui.inputText("##"+field,object.transform[field].tostring(),field);ui.setItemSize(90.0,0.0);ui.end();
                    ui.setItemSize(98.0,0.0);
                }
                ui.end();
            }
            ui.beginRow("object-actions",6.0);
            ui.button("Apply properties", "apply"); ui.button("Delete leaf", "delete");
            ui.end();
            ui.beginRow("physics-actions",6.0);
            ui.button("Drop", "physics:drop"); ui.button("Surface", "physics:surface");
            ui.button("Point", "physics:point");
            ui.end();
            ui.beginRow("physics-finish-actions",6.0);
            ui.button("Commit preview", "physics:commit"); ui.button("Cancel preview", "physics:cancel");
            ui.end();
        }
    }
    ui.textWrapped(sceneToolsFull ? "Move uses collision preview. Drop settles onto the ground, Cancel discards it. Axes: one axis. Squares: two axes. RMB orbits. Rotation: radians." : "Click to select. Arrows move one axis; squares move two. RMB orbits.", 320.0, "help");
    ui.end(); ui.mountBuildAs("scene-tools"); ui.select("scene-tools");
    ui.setHostPos(16.0,16.0,0.0,0.0); ui.setHostSize(365.0, sceneToolsFull ? config.height-32.0 : 260.0);
    ui.setHostOverlay(true);
    sceneDemo.mounted=true;
}
function refreshSceneComponents() {
    local snapshot=sceneDemo.session.snapshot();
    if (!sceneChecked(snapshot)) return;
    sceneDemo.objects=snapshot.value.objects;
    local object=sceneObject(sceneDemo.selected);
    if (object == null) { sceneDemo.selected=""; sceneDemo.selection.clear(); }
    else {
        local t=object.transform;
        local cx=0.0,cy=0.0,cz=0.0,count=0;
        foreach(entry in sceneDemo.objects) if(isSceneSelected(entry.id)) {
            cx+=entry.transform.x;cy+=entry.transform.y;cz+=entry.transform.z;count++;
        }
        sceneDemo.gizmo.setPosition(cx/count,cy/count,cz/count);
        sceneDemo.gizmo.setRotationEuler(t.rotationX,t.rotationY,t.rotationZ);
        sceneDemo.gizmo.setScale(t.scaleX,t.scaleY,t.scaleZ);
    }
    sceneDemo.revision=sceneDemo.session.getRevision();
    drawSceneComponents(); mountScenePanels();
}
function createSceneCube() {
    do { sceneDemo.serial++; } while (sceneObject("cube-"+sceneDemo.serial)!=null);
    local id="cube-"+sceneDemo.serial;
    sceneCommand("scene.object.create.v1", {object=id,name="Cube "+sceneDemo.serial,
        parent=sceneObject("root")!=null ? "root" : "",position=[(sceneDemo.serial%5)*2.0-4.0,0.5,0.0]});
    sceneChecked(cacheSceneCubeCollider(id));
    selectSceneObject(id);
}
function cacheSceneCubeCollider(id) {
    return sceneDemo.session.cachePhysicsPlacementCompound(id,"demo-cube-v1",[
        {shape="box",source="generated",halfExtents=[0.25,0.5,0.5],localPosition=[-0.25,0.0,0.0]},
        {shape="box",source="generated",halfExtents=[0.25,0.5,0.5],localPosition=[0.25,0.0,0.0]}
    ]);
}
function handleSceneUi() {
    ui.select("scene-tools");
    local click=ui.consumeClick();
    if (click=="") return;
    local id=click.slice("scene-tools/".len());
    handleSceneAction(id);
}
function handleSceneAction(id) {
    if (id=="create") createSceneCube();
    else if (id.find("mode:")==0) { sceneDemo.gizmoMode=id.slice(5);sceneDemo.gizmo.setMode(sceneDemo.gizmoMode);drawSceneAxes(); }
    else if (id=="undo" || id=="redo") {
        sceneChecked(id=="undo" ? sceneDemo.session.undo() : sceneDemo.session.redo());
        refreshSceneComponents();
    } else if (id=="save") {
        local fs=eve.Filesystem();
        if (fs.getIdentity()=="" && !fs.setIdentity("scene-components",false)) {
            sceneDemo.status="Save identity setup failed";ui.setText("status",sceneDemo.status);return;
        }
        sceneDemo.status=(fs.setupWriteDirectory() && fs.writeTextAtomic(SCENE_SAVE,sceneDemo.session.saveJson()) &&
            fs.writeTextAtomic(SCENE_COLLIDER_CACHE,sceneDemo.session.savePhysicsPlacementColliderCacheJson())) ?
            "Saved scene and collider cache" : "Save failed";
    } else if (id=="load") {
        local fs=eve.Filesystem();
        sceneChecked(sceneDemo.session.restoreJson(fs.readText(SCENE_SAVE)));
        local cache=fs.readText(SCENE_COLLIDER_CACHE);
        if (cache!="") sceneChecked(sceneDemo.session.restorePhysicsPlacementColliderCacheJson(cache));
        refreshSceneComponents();
    } else if (id=="resync") {
        local created=eve.SceneEditorModule().createLiveSession("demo.scene",SCENE_HOST);
        if (sceneChecked(created)) {
            sceneDemo.session=created.value;
            foreach(entry in sceneDemo.objects) if(entry.id.find("cube-")==0) sceneChecked(cacheSceneCubeCollider(entry.id));
        }
        refreshSceneComponents();
    } else if (id.find("select:")==0) selectSceneObject(id.slice(7),
        keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl"));
    else if (id=="physics:drop") startScenePhysicsPlacement("drop");
    else if (id=="physics:point") {
        if (startScenePhysicsPlacement("point")) sceneDemo.status="Point mode: aim with the mouse, then Commit";
    }
    else if (id=="physics:surface") {
        if (startScenePhysicsPlacement("drag")) {
            sceneDemo.placementKind="surface"; sceneDemo.status="Click a surface to preview alignment";
        }
    }
    else if (id=="physics:commit") {
        if (sceneDemo.session.isPhysicsPlacementActive()) {
            sceneChecked(sceneDemo.session.commitPhysicsPlacement());
            sceneDemo.physicsFrame=null; sceneDemo.placementKind=""; refreshSceneComponents();
        }
    }
    else if (id=="physics:cancel") {
        if (sceneDemo.session.isPhysicsPlacementActive()) sceneChecked(sceneDemo.session.cancelPhysicsPlacement());
        sceneDemo.physicsFrame=null; sceneDemo.placementKind=""; drawSceneComponents();
    }
    else if (id=="delete") sceneCommand("scene.object.delete.v1",{object=sceneDemo.selected});
    else if (id=="apply") {
        // Capture fields before a successful command remounts this custom panel.
        local values={};
        try {
            foreach (field in ["x","y","z","rotationX","rotationY","rotationZ","scaleX","scaleY","scaleZ"])
                values[field] <- ui.getValueText(field).tofloat();
        } catch (error) { sceneDemo.status="TRS fields must be numbers"; ui.setText("status",sceneDemo.status); return; }
        local name=ui.getValueText("name"), parent=ui.getValueText("parent");
        sceneCommand("scene.object.update.v1",{object=sceneDemo.selected,name=name,parent=parent,
            position=[values.x,values.y,values.z],rotation=[values.rotationX,values.rotationY,values.rotationZ],
            scale=[values.scaleX,values.scaleY,values.scaleZ]});
    }
    ui.select("scene-tools"); ui.setText("status",sceneDemo.status);
}
function scenePointer() {
    local down=mouse.isDown(1);
    local g=sceneDemo.gizmo;
    sceneDemo.camera.screenToRay(mouse.getX(),mouse.getY(),config.width.tofloat(),config.height.tofloat());
    local ox=sceneDemo.camera.getScreenRayOriginX(), oy=sceneDemo.camera.getScreenRayOriginY(), oz=sceneDemo.camera.getScreenRayOriginZ();
    local dx=sceneDemo.camera.getScreenRayDirX(), dy=sceneDemo.camera.getScreenRayDirY(), dz=sceneDemo.camera.getScreenRayDirZ();
    local object=sceneObject(sceneDemo.selected);
    local a=[ox,oy,oz], b=[ox+dx,oy+dy,oz+dz];
    if (object!=null && object.parent!="") {
        a=scene.worldToLocalAt(SCENE_HOST,object.parent,a[0],a[1],a[2]);
        b=scene.worldToLocalAt(SCENE_HOST,object.parent,b[0],b[1],b[2]);
    }
    local lx=b[0]-a[0],ly=b[1]-a[1],lz=b[2]-a[2];
    local length=sqrt(lx*lx+ly*ly+lz*lz); lx/=length;ly/=length;lz/=length;
    if (down && !sceneDemo.down && !ui.wantCaptureMouse()) {
        if (sceneDemo.placementKind=="surface" && sceneDemo.session.isPhysicsPlacementActive()) {
            local aligned=sceneDemo.session.alignPhysicsPlacementToSurface(
                [ox,oy,oz],[ox+dx*1000.0,oy+dy*1000.0,oz+dz*1000.0],0.02,1.0/60.0);
            if (sceneChecked(aligned)) {
                sceneDemo.physicsFrame=aligned.value; sceneDemo.status="Surface preview ready; Commit or Cancel";
                drawSceneComponents();
            }
        } else {
            local axis=object==null ? "" : g.pick(a[0],a[1],a[2],lx,ly,lz);
            if (axis!="") {
                g.beginDrag(axis,a[0],a[1],a[2],lx,ly,lz);
                local mode=sceneDemo.gizmoMode=="rotate" ? "rotate" : "place";
                if (!startScenePhysicsPlacement(mode)) g.endDrag();
            }
            else selectSceneObject(scene.pickRayAt(SCENE_HOST,ox,oy,oz,dx,dy,dz),
                keyboard.isDown("lctrl") || keyboard.isDown("rctrl") || keyboard.isDown("ctrl"));
        }
    }
    if (down && g.isDragging()) {
        g.updateDrag(a[0],a[1],a[2],lx,ly,lz);
        if (sceneDemo.session.isPhysicsPlacementActive()) {
            local preview=sceneDemo.session.updatePhysicsPlacementTransform(
                [g.getPositionX(),g.getPositionY(),g.getPositionZ()],
                [g.getRotationX(),g.getRotationY(),g.getRotationZ()],
                [g.getScaleX(),g.getScaleY(),g.getScaleZ()],1.0/60.0);
            if (sceneChecked(preview)) {
                sceneDemo.physicsFrame=preview.value;
                sceneDemo.status=preview.value.colliding ? "Physics placement: contact" : "Physics placement";
            }
        }
        drawSceneComponents();
    }
    if (!down && g.isDragging()) {
        local position=[g.getPositionX(),g.getPositionY(),g.getPositionZ()];
        local rotation=[g.getRotationX(),g.getRotationY(),g.getRotationZ()];
        local scale=[g.getScaleX(),g.getScaleY(),g.getScaleZ()];
        g.endDrag();
        if (sceneDemo.session.isPhysicsPlacementActive()) {
            sceneChecked(sceneDemo.session.commitPhysicsPlacement());
            sceneDemo.physicsFrame=null; sceneDemo.placementKind="";
            refreshSceneComponents();
        } else sceneCommand("scene.transform.set.v1",{object=sceneDemo.selected,position=position,rotation=rotation,scale=scale});
    }
    sceneDemo.down=down;
}
eve_init=function() {
    if (sceneToolsFull && eve.Filesystem().getIdentity()=="" &&
        !eve.Filesystem().setIdentity("scene-components",false)) throw "Save identity setup failed";
    scene.beginBuild(); scene.beginNode("root","World"); scene.end(); scene.mountBuildAs(SCENE_HOST);
    local created=eve.SceneEditorModule().createLiveSession("demo.scene",SCENE_HOST);
    if (!sceneChecked(created)) throw sceneDemo.status;
    sceneDemo.session=created.value;
    sceneChecked(sceneDemo.session.execute("scene.object.create.v1",{
        object="ground",name="Ground",parent="root",position=[0.0,-0.1,0.0],scale=[18.0,0.2,18.0]}));
    if (!sceneToolsFull) sceneChecked(sceneDemo.session.restrictCommands(
        ["scene.object.create.v1","scene.transform.set.v1"]));
    sceneDemo.gizmo=eve.Editor().newGizmo(); sceneDemo.gizmo.setSize(1.8);
    sceneDemo.gizmo.setSnapTranslate(0.25,0.25,0.25);
    sceneDemo.camera=eve.Camera3D(); sceneDemo.camera.setEye(8.0,6.0,11.0);
    sceneDemo.camera.setTarget(0.0,0.0,0.0); sceneDemo.camera.setActive(true);
    sceneDemo.camera.setFov(50.0);
    sceneSetupLighting();
    gfx.setBackgroundColor(0.045,0.055,0.075,1.0); ui.setTheme("dark");
    refreshSceneComponents();
    createSceneCube(); createSceneCube();
};
eve_update=function(dt) {
    local orbit=mouse.isDown(3) && !ui.wantCaptureMouse();
    if (orbit && sceneDemo.orbit) {
        sceneDemo.yaw+=(mouse.getX()-sceneDemo.lastX)*0.008;
        sceneDemo.pitch+=(mouse.getY()-sceneDemo.lastY)*0.006;
        if (sceneDemo.pitch<0.1) sceneDemo.pitch=0.1;
        if (sceneDemo.pitch>1.4) sceneDemo.pitch=1.4;
        sceneDemo.camera.setEye(sceneDemo.distance*cos(sceneDemo.pitch)*sin(sceneDemo.yaw),
            sceneDemo.distance*sin(sceneDemo.pitch),sceneDemo.distance*cos(sceneDemo.pitch)*cos(sceneDemo.yaw));
    }
    sceneDemo.orbit=orbit;sceneDemo.lastX=mouse.getX();sceneDemo.lastY=mouse.getY();
    if (!sceneDemo.gizmo.isDragging()) handleSceneUi();
    if (sceneDemo.placementKind=="drop" && sceneDemo.session.isPhysicsPlacementActive()) {
        local preview=sceneDemo.session.updatePhysicsPlacement(0.0,0.0,0.0,dt>1.0/30.0 ? 1.0/30.0 : dt);
        if (sceneChecked(preview)) {
            sceneDemo.physicsFrame=preview.value; drawSceneComponents();
            if (preview.value.settled) {
                sceneChecked(sceneDemo.session.commitPhysicsPlacement());
                sceneDemo.physicsFrame=null; sceneDemo.placementKind=""; refreshSceneComponents();
            }
        }
    }
    if (sceneDemo.placementKind=="point" && sceneDemo.session.isPhysicsPlacementActive() && !ui.wantCaptureMouse()) {
        sceneDemo.camera.screenToRay(mouse.getX(),mouse.getY(),config.width.tofloat(),config.height.tofloat());
        local ox=sceneDemo.camera.getScreenRayOriginX(),oy=sceneDemo.camera.getScreenRayOriginY(),oz=sceneDemo.camera.getScreenRayOriginZ();
        local preview=sceneDemo.session.updatePhysicsPlacement(
            ox+sceneDemo.camera.getScreenRayDirX()*20.0,
            oy+sceneDemo.camera.getScreenRayDirY()*20.0,
            oz+sceneDemo.camera.getScreenRayDirZ()*20.0,dt>1.0/30.0 ? 1.0/30.0 : dt);
        if (sceneChecked(preview)) { sceneDemo.physicsFrame=preview.value; drawSceneComponents(); }
    }
    scenePointer();
};
eve_render=function() {
    gfx.clear(); gfx.render3D(); ui.beginFrameAndRender();
};
eve_reload <- function() { refreshSceneComponents(); };
