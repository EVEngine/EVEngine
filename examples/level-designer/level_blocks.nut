// Hierarchy authoring owns scope/selection/library; SceneEditorSession owns TRS.
// All subtree edits are one validated, reversible scene snapshot transaction.
function levelCopy(value) { return levelJsonDecode(levelJsonEncode(value)); }
function levelIsGroup(id) {
    return id != "" && levelObject(id) != null && !(id in level.pieces) && !(id in level.spawns);
}
function levelIsInside(id, ancestor) {
    local object = levelObject(id);
    while (object != null) {
        if (object.id == ancestor) return true;
        object = levelObject(object.parent);
    }
    return false;
}
function levelSelect(id, additive = false) {
    if (!additive) level.selection.clear();
    local index = level.selection.find(id);
    if (index != null) { if (additive) level.selection.remove(index); }
    else if (id != "") level.selection.push(id);
    level.selected = level.selection.len() ? level.selection.top() : "";
    levelSyncGizmo();
    levelMarkPanelsDirty();
}
function levelScopedPick(id) {
    local object = levelObject(id);
    if (object == null || id == level.scope || !levelIsInside(id, level.scope)) return "";
    while (object.parent != level.scope) object = levelObject(object.parent);
    return object.id;
}
function levelEnterBlock() {
    if (!levelIsGroup(level.selected)) { levelStatus("Select a building block to enter"); return; }
    level.scope = level.selected;
    levelSelect("");
}
function levelLeaveBlock() {
    local object = levelObject(level.scope);
    local previous = level.scope;
    level.scope = object != null && object.parent != "" ? object.parent : "root";
    levelSelect(previous == "root" ? "" : previous);
}
function levelRestoreHierarchy(payload) {
    try { levelValidateHierarchyGeometry(payload.objects); } catch(error) { levelStatus(error);return false; }
    local result = level.session.restoreJson(levelJsonEncode(payload));
    if (!levelChecked(result)) return false;
    scene.updateTransformsAll();
    levelRefreshObjects();
    whiteboxSyncTransforms();
    spawnSyncTransforms();
    levelMarkPanelsDirty();
    return true;
}
function levelIdentityTransform() {
    return {x=0.0,y=0.0,z=0.0,rotationX=0.0,rotationY=0.0,rotationZ=0.0,
            scaleX=1.0,scaleY=1.0,scaleZ=1.0};
}
/** @brief Abstract selected siblings into a block; children keep world poses. */
function levelGroupSelection() {
    local ids = [];
    foreach (id in level.selection) if (id != "root" && levelObject(id) != null) ids.push(id);
    if (ids.len() == 0 && level.selected != "" && level.selected != "root") ids.push(level.selected);
    if (ids.len() == 0) { levelStatus("Select one or more sibling objects (Ctrl + click)"); return; }
    local parent = levelObject(ids[0]).parent;
    local center = [0.0,0.0,0.0];
    foreach (id in ids) {
        local object = levelObject(id);
        if (object.parent != parent) { levelStatus("Group objects at the same hierarchy level"); return; }
        center[0] += object.transform.x; center[1] += object.transform.y; center[2] += object.transform.z;
    }
    foreach (i, value in center) center[i] = value / ids.len();
    local payload = levelJsonDecode(level.session.saveJson());
    local id = levelNextId("block"), t = levelIdentityTransform();
    t.x = center[0]; t.y = center[1]; t.z = center[2];
    foreach (object in payload.objects) if (ids.find(object.id) != null) {
        object.parent = id;
        object.transform.x -= t.x; object.transform.y -= t.y; object.transform.z -= t.z;
    }
    payload.objects.push({id=id,name="Building block " + level.serial,parent=parent,transform=t});
    if (levelRestoreHierarchy(payload)) { levelSelect(id); levelStatus("Block created · Enter to edit its parts"); }
}
/** @brief Decompose a sampled affine basis; reject shear instead of corrupting physics. */
function levelBasisTransform(points) {
    local axes = [], scales = [];
    for (local i=1; i<4; ++i) {
        local v=[];
        for(local k=0;k<3;++k) v.push(points[i][k]-points[0][k]);
        local size=sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
        if(size<0.00001) throw "Degenerate transform";
        scales.push(size);
        foreach(k,value in v) v[k]=value/size;
        axes.push(v);
    }
    for(local i=0;i<3;++i) for(local j=i+1;j<3;++j) {
        local dot=0.0; for(local k=0;k<3;++k) dot+=axes[i][k]*axes[j][k];
        if(fabs(dot)>0.001) throw "Hierarchy shear: use uniform block scaling before physics or unpack";
    }
    local determinant=axes[0][0]*(axes[1][1]*axes[2][2]-axes[1][2]*axes[2][1])-
        axes[1][0]*(axes[0][1]*axes[2][2]-axes[0][2]*axes[2][1])+
        axes[2][0]*(axes[0][1]*axes[1][2]-axes[0][2]*axes[1][1]);
    if(determinant<0.0) throw "Mirrored transforms are unsupported by the physics preview";
    local t=levelIdentityTransform();
    t.x=points[0][0];t.y=points[0][1];t.z=points[0][2];
    t.scaleX=scales[0];t.scaleY=scales[1];t.scaleZ=scales[2];
    t.rotationX=asin(levelClamp(-axes[2][1],-1.0,1.0));
    if(fabs(cos(t.rotationX))>0.0001) {
        t.rotationY=atan2(axes[2][0],axes[2][2]);t.rotationZ=atan2(axes[0][1],axes[1][1]);
    } else { t.rotationY=atan2(-axes[0][2],axes[0][0]);t.rotationZ=0.0; }
    return t;
}
function levelTransformRelativeTo(id, parent) {
    local points=[];
    foreach(p in [[0.0,0.0,0.0],[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]) {
        local world=levelProjectedPoint(id,p[0],p[1],p[2]);
        points.push(parent=="" ? world : scene.worldToLocalAt(levelHost(),parent,world[0],world[1],world[2]));
    }
    return levelBasisTransform(points);
}
function levelUnpackBlock() {
    local group=levelObject(level.selected);
    if(group==null || group.id=="root" || !levelIsGroup(group.id)) return;
    local payload=levelJsonDecode(level.session.saveJson()), objects=[];
    try {
        foreach(object in payload.objects) {
            if(object.id==group.id) continue;
            if(object.parent==group.id) {
                object.transform=levelTransformRelativeTo(object.id,group.parent);object.parent=group.parent;
            }
            objects.push(object);
        }
    } catch(error) { levelStatus(error);return; }
    payload.objects=objects;
    if(levelRestoreHierarchy(payload)) { levelSelect("");levelStatus("Block unpacked · world poses preserved"); }
}
function levelCaptureBlock() {
    if(level.library.len()>=256) { levelStatus("Block library limit reached (256)");return; }
    local root=levelObject(level.selected);
    if(root==null || root.id=="root") { levelStatus("Select a block or piece to capture");return; }
    local record={name=root.name,root=root.id,objects=[],pieces=[],spawns=[]};
    foreach(object in levelJsonDecode(level.session.saveJson()).objects) if(levelIsInside(object.id,root.id)) {
        record.objects.push(object);
        if(object.id in level.pieces) {
            local piece=level.pieces[object.id];
            record.pieces.push({object=object.id,recipe=piece.recipe,values=levelCopy(piece.values),
                motion=piece.motion});
        }
        if(object.id in level.spawns) {
            local spawn=level.spawns[object.id];
            record.spawns.push({object=object.id,yaw=spawn.yaw,character=spawn.character,motionSet=spawn.motionSet});
        }
    }
    level.library.push(record);level.blockIndex=level.library.len()-1;
    levelStatus("Captured " + root.name + " · independent reusable block");levelMarkPanelsDirty();
}
function levelPlaceBlock(index,x,y,z) {
    if(index<0 || index>=level.library.len()) return "";
    local record=levelCopy(level.library[index]), mapping={};
    local payload=levelJsonDecode(level.session.saveJson());
    foreach(object in record.objects) mapping[object.id] <- levelNextId("instance");
    local root=mapping[record.root];
    local position=scene.worldToLocalAt(levelHost(),level.scope,x,y,z);
    foreach(object in record.objects) {
        local source=object.id;object.id=mapping[source];
        if(source==record.root) {
            object.parent=level.scope;object.transform.x=position[0];object.transform.y=position[1];object.transform.z=position[2];
        } else object.parent=mapping[object.parent];
        payload.objects.push(object);
    }
    local stagedPieces={},stagedSpawns={};
    try {
        foreach(entry in record.pieces) {
            local piece={recipe=entry.recipe,values=entry.values,motion=entry.motion,entity=null,half=null,bounds=null};
            stagedPieces[mapping[entry.object]] <- piece;
            local schema=whiteboxSchema(piece.recipe);
            if(schema==null || !whiteboxCreateRenderable(piece,schema)) throw level.status;
            piece.entity.setVisible(false);
        }
        foreach(entry in record.spawns) {
            local spawn=spawnDefault();spawn.yaw=entry.yaw;spawn.character=entry.character;spawn.motionSet=entry.motionSet;
            stagedSpawns[mapping[entry.object]] <- spawn;spawnBuildMarker(spawn,null);
            spawn.entity.setVisible(false);spawn.facing.setVisible(false);
        }
        if(!levelRestoreHierarchy(payload)) throw level.status;
    } catch(error) {
        foreach(id,piece in stagedPieces) levelReleaseRenderable(piece.entity);
        foreach(id,spawn in stagedSpawns) { spawnReleaseProjection(spawn); }
        levelStatus("Block placement rejected: " + error);return "";
    }
    foreach(id,piece in stagedPieces) level.pieces[id] <- piece;
    foreach(id,spawn in stagedSpawns) level.spawns[id] <- spawn;
    whiteboxReconcile();spawnReconcile();
    whiteboxSyncTransforms();spawnSyncTransforms();levelSelect(root);levelStatus("Placed " + record.name);
    return root;
}
function levelDeleteSelection() {
    if(level.selected=="" || level.selected=="root") return;
    local payload=levelJsonDecode(level.session.saveJson()), kept=[];
    foreach(object in payload.objects) if(!levelIsInside(object.id,level.selected)) kept.push(object);
    payload.objects=kept;
    if(levelRestoreHierarchy(payload)) levelSelect("");
}

/** @brief Validate a saved library block without creating scene/runtime objects. */
function levelValidateBlockRecord(record) {
    if(typeof record!="table") throw "Invalid block record";
    foreach(key,value in record) if(["name","root","objects","pieces","spawns"].find(key)==null)
        throw "Unknown block field " + key;
    if(!("name" in record) || typeof record.name!="string" || !("root" in record) ||
        !("objects" in record) || typeof record.objects!="array" || record.objects.len()==0 ||
        !("pieces" in record) || typeof record.pieces!="array" ||
        !("spawns" in record) || typeof record.spawns!="array") throw "Incomplete block record";
    local objects=levelCopy(record.objects), ids={};
    foreach(object in objects) ids[object.id] <- true;
    if(!(record.root in ids)) throw "Missing block root";
    foreach(object in objects) {
        if(object.id==record.root) object.parent="";
        else if(!(object.parent in ids)) throw "Disconnected block object";
    }
    local opened=eve.SceneEditorModule().createSession("level.block.validation");
    if(!opened.ok) throw opened.status.summary;
    local checked=opened.value.restoreJson(levelJsonEncode({schemaId="eve.scene.hierarchy",schemaVersion=1,objects=objects}));
    if(!checked.ok) throw checked.status.summary;
    levelValidateHierarchyGeometry(objects);
    local seen={};
    foreach(piece in record.pieces) {
        if(!(piece.object in ids) || piece.object in seen || typeof piece.values!="table" ||
            !("motion" in piece) || (piece.motion!="static" && piece.motion!="dynamic") || whiteboxSchema(piece.recipe)==null)
            throw "Invalid block piece";
        seen[piece.object] <- true;
    }
    foreach(spawn in record.spawns) {
        if(!(spawn.object in ids) || spawn.object in seen ||
            (spawn.character!="" && levelCharacterNames().find(spawn.character)==null) ||
            (spawn.motionSet!="" && levelMotionSetNames().find(spawn.motionSet)==null)) throw "Invalid block spawn";
        seen[spawn.object] <- true;
    }
}

/** @brief Read-only world projection, including an uncommitted ancestor gizmo. */
function levelHierarchyPoint(objects,id,x,y,z,preview=false) {
    function find(id) { foreach(entry in objects) if(entry.id==id) return entry;return null; }
    local object=find(id);
    while(object!=null) {
        local t=object.transform;
        if(preview && object.id==level.selected && level.gizmo!=null && level.gizmo.isDragging()) {
            t={x=level.gizmo.getPositionX(),y=level.gizmo.getPositionY(),z=level.gizmo.getPositionZ(),
                rotationX=level.gizmo.getRotationX(),rotationY=level.gizmo.getRotationY(),rotationZ=level.gizmo.getRotationZ(),
                scaleX=level.gizmo.getScaleX(),scaleY=level.gizmo.getScaleY(),scaleZ=level.gizmo.getScaleZ()};
        }
        x*=t.scaleX;y*=t.scaleY;z*=t.scaleZ;
        local c=cos(t.rotationZ),s=sin(t.rotationZ),nx=x*c-y*s,ny=x*s+y*c;x=nx;y=ny;
        c=cos(t.rotationX);s=sin(t.rotationX);ny=y*c-z*s;local nz=y*s+z*c;y=ny;z=nz;
        c=cos(t.rotationY);s=sin(t.rotationY);nx=x*c+z*s;nz=-x*s+z*c;
        x=nx+t.x;y+=t.y;z=nz+t.z;object=find(object.parent);
    }
    return [x,y,z];
}
function levelDuplicateSelection() {
    if(level.selected=="" || level.selected=="root") return;
    local before=level.library.len(), origin=scene.localToWorldAt(levelHost(),level.selected,0.0,0.0,0.0);
    levelCaptureBlock();
    if(level.library.len()==before) return;
    local scope=level.scope;level.scope=levelObject(level.selected).parent;
    levelPlaceBlock(before,origin[0]+3.0,origin[1],origin[2]);
    level.scope=scope;
    level.library.pop();level.blockIndex=level.library.len()-1;
}

function levelProjectedPoint(id,x,y,z) {
    return levelHierarchyPoint(level.objects,id,x,y,z,true);
}
function levelValidateHierarchyGeometry(objects) {
    foreach(object in objects) {
        local points=[];
        foreach(p in [[0.0,0.0,0.0],[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]])
            points.push(levelHierarchyPoint(objects,object.id,p[0],p[1],p[2]));
        levelBasisTransform(points);
    }
}
