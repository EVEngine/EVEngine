// Native scene authoring, driven by MCP or the host game.
persist scene_director = null

// ---- internal helpers (roottable scope; reused across reinstalls) ----

function sd_num(v, fallback) {
    if (typeof v == "integer" || typeof v == "float") return v;
    return fallback;
}

function sd_int(v, fallback) {
    if (typeof v == "integer") return v;
    if (typeof v == "float") return v.tointeger();
    return fallback;
}

function sd_arr3(v, def) {
    local out = [def[0], def[1], def[2]];
    if (typeof v == "array") {
        for (local i = 0; i < 3 && i < v.len(); i++) out[i] = sd_num(v[i], def[i]);
    }
    return out;
}

function sd_recipe_from_kind(kind) {
    local key = kind;
    if (key.len() > 5 && key.slice(0, 5) == "mesh.") key = key.slice(5);
    return key;
}

// Build a Mesh for a prop kind. Returns Mesh or null (unknown kind).
function sd_build_mesh(kind, seed, mesh_params) {
    if (!("gfx" in getroottable())) return null;
    local key = sd_recipe_from_kind(kind);
    if (key == "box" || key == "cube") return gfx.newMeshCube(1.0);
    if (key == "sphere" || key == "ball") return gfx.newMeshSphere(24, 12);
    if (key == "cylinder" || key == "pillar" || key == "column" || key == "trunk")
        return gfx.newMeshCylinder(16, 1, true);
    // Procedural recipe: tree / rock / bush / skyscraper / hexplanet /
    // marchingcubes / fence / stonewall / bridge / greatwall / hedge / chevaldefrise
    if ("procgen" in getroottable()) {
        local recipe = "mesh." + key;
        try {
            if (procgen.hasMeshRecipe(recipe)) {
                local paramsResult = procgen.newParams();
                if (!paramsResult.ok) return null;
                local p = paramsResult.value;
                p.setSeed(seed);
                p.setFloat("scale", 1.0);
                if (typeof mesh_params == "table") {
                    foreach (k, v in mesh_params) {
                        local tk = typeof v;
                        if (tk == "integer") p.setInt(k, v);
                        else if (tk == "float") p.setFloat(k, v);
                        else if (tk == "string") p.setString(k, v);
                    }
                }
                local meshResult = procgen.generateMesh(recipe, p, gfx);
                if (!meshResult.ok) return null;
                return meshResult.value;
            }
        } catch (e) {
            return null;
        }
    }
    return null;
}

// Canonical authoring values live in SceneEditorSession; native objects are projections.
function sd_require(r) {
    if (!r.ok) throw ("error" in r)?r.error:r.status.summary;
    return ("value" in r) ? r.value : r;
}
function sd_empty_document() {
    return {schemaId="eve.scene.authoring",schemaVersion=1,
        hierarchy={schemaId="eve.scene.hierarchy",schemaVersion=1,objects=[]},sources={},camera={},lighting={}};
}
function sd_equal(a,b) {
    if(typeof a!=typeof b) return false;
    if(typeof a=="table" || typeof a=="array") {
        if(a.len()!=b.len()) return false;
        foreach(k,v in a) if(!(k in b) || !sd_equal(v,b[k])) return false;
        return true;
    }
    return a==b;
}
if(scene_director==null) scene_director <- {
    session=null,editorService=null,editorModule=null,nativeScene=null,cameraObj=null,visuals={},
    projectedRevision=-1,renderedRevision=-1,frame=0,hostName="ai-authoring-projection",importedHost=null
};
scene_director.install <- function() {
    if (!("eve" in getroottable())) return {ok=false,code="Unsupported",error="native scene authoring modules are unavailable"};
    if(scene_director.session==null) {
        scene_director.editorService=eve.Editor();
        scene_director.editorModule=eve.SceneEditorModule();
        scene_director.nativeScene=eve.Scene();
        scene_director.session=sd_require(scene_director.editorModule.createAuthoringSession("ai-stage"));
    }
    return {ok=true};
};
scene_director.document <- function() {
    sd_require(scene_director.install());return sd_require(scene_director.session.snapshot());
};
scene_director.hierarchy <- function() {
    local d=scene_director.document();return ("hierarchy" in d)?d.hierarchy:d;
};
scene_director.find <- function(id) {
    foreach(p in scene_director.hierarchy().objects) if(p.id==id) return p;return null;
};
scene_director.release_visual <- function(entry) {
    foreach(ent in entry.entities) sd_require(eve.destroyRenderable3D(ent.getEntityId(),ent.getEntityGeneration()));
};
scene_director.prepare <- function(id,source) {
    local geometry={};
    foreach(k in ["kind","seed","mesh_params","path","revision"]) if(k in source) geometry[k] <- source[k];
    if(id in scene_director.visuals && sd_equal(scene_director.visuals[id].geometry,geometry))
        return scene_director.visuals[id];
    local entry={geometry=geometry,entities=[],materials=[],bounds=null};
    try {
        if(source.kind=="asset") {
            local model=eve.Model3D(),data=model.newModelDataFromFile(source.path);
            if(data==null) throw "asset import failed: "+source.path;
            for(local i=0;i<data.getMeshCount();i++) entry.entities.append(model.createRenderable(gfx,data,i));
            if(entry.entities.len()==0) throw "asset has no meshes";
        } else {
            local kind=(source.kind=="ground" || source.kind=="floor")?"box":source.kind;
            local mesh=sd_build_mesh(kind,("seed" in source)?source.seed:0,("mesh_params" in source)?source.mesh_params:{});
            if(mesh==null) throw "unknown source kind: "+source.kind;
            local ent=eve.Renderable3D();ent.setMesh(mesh);entry.entities.append(ent);
        }
        foreach(ent in entry.entities) {
            entry.materials.append({tint=[ent.getTintR(),ent.getTintG(),ent.getTintB()],
                roughness=ent.getRoughness(),metallic=ent.getMetallic()});
            ent.setVisible(false);
            local b=sd_require(ent.getMesh().getLocalBounds());
            if(entry.bounds==null) entry.bounds=clone b;
            else for(local i=0;i<3;i++) {
                if(b[i]<entry.bounds[i]) entry.bounds[i]=b[i];
                if(b[i+3]>entry.bounds[i+3]) entry.bounds[i+3]=b[i+3];
            }
        }
    } catch(e) {scene_director.release_visual(entry);throw e;}
    return entry;
};
scene_director.stage_document <- function(d) {
    local staged={},allocated=[];
    try {
        foreach(item in d.hierarchy.objects) {
            if(item.id.find("@projection/")==0) throw "reserved projection id";
            if(item.id in d.sources) {
                local entry=scene_director.prepare(item.id,d.sources[item.id]);staged[item.id] <- entry;
                if(!(item.id in scene_director.visuals) || scene_director.visuals[item.id]!=entry) allocated.append(entry);
            }
        }
    } catch(e) {foreach(entry in allocated) scene_director.release_visual(entry);throw e;}
    return staged;
};
scene_director.discard_staged <- function(staged) {
    foreach(id,entry in staged) if(!(id in scene_director.visuals) || scene_director.visuals[id]!=entry)
        scene_director.release_visual(entry);
};
scene_director.refresh <- function(prepared=null) {
    sd_require(scene_director.install());
    local revision=scene_director.session.getRevision();
    if(revision==scene_director.projectedRevision) return {ok=true};
    if(scene_director.importedHost!=null) {
        scene_director.nativeScene.updateTransformsAll();scene_director.projectedRevision=revision;return {ok=true};
    }
    local d=scene_director.document(),next={},allocated=[];
    try {
        foreach(item in d.hierarchy.objects) if(item.id in d.sources) {
            local entry=(prepared!=null && item.id in prepared)?prepared[item.id]:scene_director.prepare(item.id,d.sources[item.id]);next[item.id] <- entry;
            if(!(item.id in scene_director.visuals) || scene_director.visuals[item.id]!=entry) allocated.append(entry);
        }
        local native=scene_director.nativeScene;
        native.beginBuild();native.beginNode("@projection/root","");
        local emitProjectionChildren=null;
        emitProjectionChildren=function(parent) {
            foreach(item in d.hierarchy.objects) if(item.parent==parent) {
                if(item.id.find("@projection/")==0) throw "reserved projection id";
                local t=item.transform;
                native.beginNode(item.id,item.name);
                native.setBuildPosition(t.x,t.y,t.z);
                native.setBuildRotation(t.rotationY,t.rotationX,t.rotationZ);
                native.setBuildScale(t.scaleX,t.scaleY,t.scaleZ);
                if(item.id in d.sources && "visible" in d.sources[item.id]) native.setBuildVisible(d.sources[item.id].visible);
                if(item.id in next) for(local i=0;i<next[item.id].entities.len();i++) {
                    native.beginNode("@projection/"+item.id+"/"+i,"");native.end();
                }
                emitProjectionChildren(item.id);native.end();
            }
        };
        emitProjectionChildren("");native.end();
        if(!native.remountBuildAs(scene_director.hostName)) throw "projection mount failed";
        foreach(id,entry in next) {
            local source=d.sources[id],b=entry.bounds;
            if(!native.setNodeBoundsAt(scene_director.hostName,id,b[0],b[1],b[2],b[3],b[4],b[5])) throw "projection bounds failed";
            for(local i=0;i<entry.entities.len();i++) {
                local ent=entry.entities[i];
                local material=entry.materials[i],tint=("tint" in source)?source.tint:material.tint;
                ent.setTint(tint[0].tofloat(),tint[1].tofloat(),tint[2].tofloat(),1.0);
                ent.setRoughness((("roughness" in source)?source.roughness:material.roughness).tofloat());
                ent.setMetallic((("metallic" in source)?source.metallic:material.metallic).tofloat());
                if("cast_shadow" in source) ent.setCastShadow(source.cast_shadow);
                if("receive_shadow" in source) ent.setReceiveShadow(source.receive_shadow);
                if(!native.linkRenderable3DAt(scene_director.hostName,"@projection/"+id+"/"+i,ent)) throw "projection link failed";
            }
        }
        if(d.camera.len()>0) {
            if(scene_director.cameraObj==null) scene_director.cameraObj=eve.Camera3D();
            local cam=scene_director.cameraObj,c=d.camera;
            cam.setEye(c.eye[0].tofloat(),c.eye[1].tofloat(),c.eye[2].tofloat());cam.setTarget(c.target[0].tofloat(),c.target[1].tofloat(),c.target[2].tofloat());
            cam.setUp(c.up[0].tofloat(),c.up[1].tofloat(),c.up[2].tofloat());cam.setFov(c.fov.tofloat());cam.setClipPlanes(c.near.tofloat(),c.far.tofloat());cam.setPerspective();
            cam.setAutoExposure(false,-8.0,8.0);cam.setExposure(c.exposure.tofloat());
            cam.setAmbient(c.ambient[0].tofloat(),c.ambient[1].tofloat(),c.ambient[2].tofloat());cam.setActive(true);
        } else if(scene_director.cameraObj!=null) scene_director.cameraObj.setActive(false);
        local light=d.lighting.len()>0?d.lighting:{background=[0.10,0.13,0.19],direction=[-0.5,-1.0,-0.4],color=[1.35,1.25,1.05]};
        gfx.setBackgroundColor(light.background[0].tofloat(),light.background[1].tofloat(),light.background[2].tofloat(),1.0);
        gfx.setDirectionalLight(light.direction[0].tofloat(),light.direction[1].tofloat(),light.direction[2].tofloat(),light.color[0].tofloat(),light.color[1].tofloat(),light.color[2].tofloat());
        native.updateTransformsAll();
    } catch(e) {
        foreach(entry in allocated) scene_director.release_visual(entry);
        scene_director.projectedRevision=-1;return {ok=false,error=e.tostring(),code="ProjectionFailed"};
    }
    foreach(id,entry in scene_director.visuals) if(!(id in next) || next[id]!=entry) scene_director.release_visual(entry);
    scene_director.visuals=next;scene_director.projectedRevision=revision;return {ok=true,revision=revision};
};
scene_director.command <- function(command,payload) {
    sd_require(scene_director.install());
    local staged=null;
    if(scene_director.importedHost==null) {
        try {
            local candidate=sd_require(scene_director.editorModule.createAuthoringSession("ai-preflight"));
            sd_require(candidate.restoreJson(scene_director.session.saveJson()));
            local planned=candidate.execute(command,payload);if(!planned.ok) return planned;
            staged=scene_director.stage_document(sd_require(candidate.snapshot()));
        } catch(e) {return {ok=false,error=e.tostring(),code="PreparationFailed"};}
    }
    local r=scene_director.session.execute(command,payload);
    if(!r.ok) {if(staged!=null) scene_director.discard_staged(staged);return r;}
    local projected=scene_director.refresh(staged);
    if(!projected.ok) {
        local rollback=scene_director.session.undo();if(!rollback.ok) return {ok=false,error="rollback failed",code="RollbackFailed"};
        local rebuilt=scene_director.refresh();if(!rebuilt.ok) return {ok=false,error=rebuilt.error,code="RollbackFailed"};
        return projected;
    }
    return {ok=true,revision=scene_director.session.getRevision()};
};
scene_director.spawn <- function(p) {
    sd_require(scene_director.install());
    local id=("id" in p)?p.id:("prop_"+scene_director.session.getRevision());
    local source={kind=("kind" in p)?p.kind:"box",seed=7,mesh_params={},
        tint=[1.0,1.0,1.0],roughness=0.75,metallic=0.02,visible=true,cast_shadow=true,receive_shadow=true};
    if(source.kind=="asset") {delete source.tint;delete source.roughness;delete source.metallic;}
    foreach(k in ["seed","mesh_params","path","revision","tint","roughness","metallic","visible","cast_shadow","receive_shadow"])
        if(k in p) source[k] <- p[k];
    local scale=("scale" in p)?clone p.scale:[1.0,1.0,1.0];
    if(source.kind=="ground" || source.kind=="floor") scale[1]=0.06;
    return scene_director.command("scene.authoring.spawn.v1",{object=id,source=source,
        position=("pos" in p)?p.pos:[0.0,0.5,0.0],rotation=("rotation" in p)?p.rotation:[0.0,("yaw_deg" in p)?p.yaw_deg*0.01745329252:0.0,0.0],
        scale=scale,parent=("parent" in p)?p.parent:""});
};
scene_director.move <- function(id,x,y,z) {return scene_director.command("scene.transform.set.v1",{object=id,position=[x,y,z]});};
scene_director.scale <- function(id,x,y,z) {return scene_director.command("scene.transform.set.v1",{object=id,scale=[x,y,z]});};
scene_director.rotate <- function(id,yaw) {return scene_director.command("scene.transform.set.v1",{object=id,rotation=[0.0,yaw*0.01745329252,0.0]});};
scene_director.remove <- function(id) {return scene_director.command("scene.object.delete.v1",{object=id});};
scene_director.set_material <- function(id,p) {
    local d=scene_director.document();
    if(!("sources" in d) || !(id in d.sources)) return {ok=false,error="object has no authored source"};
    local source=clone d.sources[id];
    foreach(k in ["tint","roughness","metallic","visible","cast_shadow","receive_shadow"]) if(k in p) source[k] <- p[k];
    return scene_director.command("scene.authoring.sources.set.v1",{object=id,value=source});
};
scene_director.set_visible <- function(id,v) {return scene_director.set_material(id,{visible=v});};
scene_director.camera <- function(p) {
    local c={eye=[0.0,6.0,12.0],target=[0.0,1.0,0.0],up=[0.0,1.0,0.0],
        fov=55.0,near=0.1,far=1000.0,exposure=1.0,ambient=[0.20,0.22,0.26]};
    foreach(k,v in c) if(k in p) c[k]=p[k];
    return scene_director.command("scene.authoring.camera.set.v1",{value=c});
};
scene_director.set_lighting <- function(p) {
    local bg=[0.52,0.58,0.70],tod=("timeOfDay" in p)?p.timeOfDay:"day";
    if(tod=="dusk") bg=[0.34,0.26,0.38];else if(tod=="dawn") bg=[0.46,0.42,0.52];else if(tod=="night") bg=[0.05,0.06,0.11];
    local intensity=("intensity" in p)?p.intensity:1.0;
    return scene_director.command("scene.authoring.lighting.set.v1",{value={
        background=("background" in p)?p.background:bg,direction=("direction" in p)?p.direction:[-0.5,-1.0,-0.4],
        color=("color" in p)?p.color:[intensity,intensity*0.95,intensity*0.85]}});
};
scene_director.reset <- function() {return scene_director.command("scene.snapshot.restore.v1",sd_empty_document());};
scene_director.bounds <- function(id) {
    if(scene_director.importedHost!=null) {
        if(!scene_director.nativeScene.hasNodeBoundsAt(scene_director.importedHost,id)) throw "node has no local bounds: "+id;
        return scene_director.nativeScene.getNodeBoundsAt(scene_director.importedHost,id);
    }
    sd_require(scene_director.refresh());
    if(!(id in scene_director.visuals)) throw "object has no geometry: "+id;
    return scene_director.visuals[id].bounds;
};
scene_director.inspect <- function(id) {return scene_director.editorModule.inspectAuthoringSnapshot(scene_director.document(),id,scene_director.bounds(id));};
scene_director.info <- function() {
    local d=scene_director.document(),items=[];
    foreach(p in scene_director.hierarchy().objects) {
        local t=p.transform,item={id=p.id,parent=p.parent,pos=[t.x,t.y,t.z],
            scale=[t.scaleX,t.scaleY,t.scaleZ],rotation=[t.rotationX,t.rotationY,t.rotationZ],yaw_deg=t.rotationY*57.2957795131};
        if("sources" in d && p.id in d.sources) {
            item.source <- d.sources[p.id];item.kind <- d.sources[p.id].kind;if("tint" in d.sources[p.id]) item.tint <- d.sources[p.id].tint;
        }
        try {item.geometry <- scene_director.inspect(p.id);} catch(e) {item.geometry <- {ok=false,error=e.tostring()};}
        items.append(item);
    }
    return {ok=true,count=items.len(),props=items,revision=scene_director.session.getRevision(),
        projectedRevision=scene_director.projectedRevision,renderedRevision=scene_director.renderedRevision,
        frame=scene_director.frame,document=d};
};
scene_director.list <- function() {local ids=[];foreach(p in scene_director.hierarchy().objects) ids.append(p.id);return ids;};
scene_director.status <- function() {return {installed=scene_director.session!=null,propCount=scene_director.list().len(),
    hasCamera=scene_director.cameraObj!=null,revision=scene_director.session.getRevision()};};
scene_director.mark_rendered <- function() {scene_director.frame++;scene_director.renderedRevision=scene_director.projectedRevision;};
scene_director.cameras <- function(count) {
    local lo=[1e9,1e9,1e9],hi=[-1e9,-1e9,-1e9],found=false;
    foreach(id in scene_director.list()) {
        local q=scene_director.inspect(id);if(!q.ok) return q;
        for(local i=0;i<3;i++) {
            if(q.value.worldMinimum[i]<lo[i]) lo[i]=q.value.worldMinimum[i];
            if(q.value.worldMaximum[i]>hi[i]) hi[i]=q.value.worldMaximum[i];
        }
        found=true;
    }
    if(!found) {lo=[-4.0,0.0,-4.0];hi=[4.0,2.0,4.0];}
    local cx=(lo[0]+hi[0])*0.5,cy=(lo[1]+hi[1])*0.5,cz=(lo[2]+hi[2])*0.5;
    local r=(hi[0]-lo[0]+hi[1]-lo[1]+hi[2]-lo[2])*0.5;if(r<4.0) r=4.0;
    local out=[],n=sd_int(count,6);if(n<1 || n>64) return {ok=false,error="camera count must be 1..64"};
    for(local i=0;i<n;i++) {
        local angle=6.28318530718*i/n;
        out.append({id="cam_"+i,eye=[cx+cos(angle)*r*1.8,cy+r,cz+sin(angle)*r*1.8],target=[cx,cy,cz],fov=55.0});
    }
    return out;
};
// Build an owning candidate draw request without projecting it into the live SceneHost.
scene_director.visibility <- function(target,p) {
    if(scene_director.importedHost!=null) return {ok=false,code="Unsupported",error="visibility mapping requires an authoring document"};
    if(!("prepareSceneVisibility" in eve)) return {ok=false,code="Unsupported",error="graphics visibility provider unavailable"};
    sd_require(scene_director.refresh());
    local document=scene_director.document(),draws=[],overrides={},candidate=0;
    if(!("camera" in document) || document.camera.len()==0) return {ok=false,error="visibility requires an authored camera"};
    if("models" in p) overrides=p.models;
    if("preview" in p && p.preview) {
        local report=sd_require(scene_director.session.checkRelationPlacement());candidate=report.tick;
        foreach(item in report.previewObjects) {
            foreach(node in document.hierarchy.objects) if(node.parent==item.object)
                return {ok=false,code="Unsupported",error="visibility preview currently requires an adjustable leaf"};
            local t={x=item.position[0],y=item.position[1],z=item.position[2],
                rotationX=item.rotation[0],rotationY=item.rotation[1],rotationZ=item.rotation[2],
                scaleX=item.scale[0],scaleY=item.scale[1],scaleZ=item.scale[2]};
            local flat={schemaId="eve.scene.hierarchy",schemaVersion=1,objects=[{id=item.object,parent="",name=item.object,transform=t}]};
            overrides[item.object] <- sd_require(scene_director.editorModule.inspectAuthoringSnapshot(flat,item.object,scene_director.bounds(item.object))).worldMatrix;
        }
    }
    foreach(node in document.hierarchy.objects) {
        if(!(node.id in scene_director.visuals)) continue;
        local current=node,visible=true;
        while(current!=null) {
            if(current.id in document.sources && "visible" in document.sources[current.id] && !document.sources[current.id].visible) visible=false;
            current=current.parent==""?null:scene_director.find(current.parent);
        }
        if(!visible) continue;
        local model=(node.id in overrides)?overrides[node.id]:sd_require(scene_director.inspect(node.id)).worldMatrix;
        foreach(ent in scene_director.visuals[node.id].entities)
            draws.append({object=node.id,entityId=ent.getEntityId(),generation=ent.getEntityGeneration(),model=model});
    }
    foreach(id,model in overrides) if(!(id in scene_director.visuals)) return {ok=false,error="candidate object not found: "+id};
    local camera=document.camera;
    local width=("width" in p)?p.width:(("height" in p)?(p.height.tofloat()*gfx.getWidth()/gfx.getHeight()).tointeger():256);
    local height=("height" in p)?p.height:(width.tofloat()*gfx.getHeight()/gfx.getWidth()).tointeger();
    local request={schemaId="eve.scene.visibility-request",schemaVersion=2,
        sourceRevision=scene_director.session.getRevision(),candidateGeneration=candidate,
        width=width,height=height,
        camera={eye=camera.eye,target=camera.target,up=camera.up,fov=camera.fov,near=camera.near,far=camera.far},
        draws=draws,targets=("targets" in p)?p.targets:[target]};
    if("prepareRelations" in p && p.prepareRelations) return scene_director.session.prepareRelationVisibility(scene_director.hostName,request);
    local prepared=sd_require(eve.prepareSceneVisibility(request));
    return prepared.measure(gfx);
};
scene_director.modify <- function(action,target,p) {
    if(p==null) p={};sd_require(scene_director.install());
    if("expectedRevision" in p && p.expectedRevision!=scene_director.session.getRevision()) return {ok=false,code="Conflict",error="stale scene revision"};
    if(action=="visibility") return scene_director.visibility(target,p);
    if(action=="geometry_revision") return scene_director.session.geometryRevision(target);
    if(action=="relations") {
        local relations=[];
        foreach(input in p.value) {
            local relation=clone input;
            if("region" in relation) {
                relation.region=clone relation.region;
                if(!("geometryRevision" in relation.region)) {
                    local object=relation.type=="visible_fraction"?relation.subject:relation.reference;
                    relation.region.geometryRevision <- sd_require(scene_director.session.geometryRevision(object));
                }
            }
            relations.append(relation);
        }
        return scene_director.command("scene.authoring.relations.set.v1",{value=relations});
    }
    if(action=="relation_begin") {
        if(scene_director.importedHost!=null) return {ok=false,code="Unsupported",error="relation preview requires an authoring document"};
        local objects=[],found=false;
        foreach(node in scene_director.hierarchy().objects) {
            local b=scene_director.bounds(node.id);
            local hx=(b[3]-b[0])*0.5,hy=(b[4]-b[1])*0.5,hz=(b[5]-b[2])*0.5;
            if(hx<=0 || hy<=0 || hz<=0) return {ok=false,code="Unsupported",error="degenerate collision bounds: "+node.id};
            local item={object=node.id,selected=node.id==target,locked=node.id!=target,
                halfExtents=[hx,hy,hz],colliders=[{shape="box",halfExtents=[hx,hy,hz],
                    localPosition=[(b[0]+b[3])*0.5,(b[1]+b[4])*0.5,(b[2]+b[5])*0.5]}]};
            if(node.id==target) found=true;
            if("colliders" in p && node.id in p.colliders) item.colliders=p.colliders[node.id];
            objects.append(item);
        }
        if(!found) return {ok=false,code="NotFound",error="adjustable object not found"};
        local begun=scene_director.session.beginRelationPlacement({objects=objects,mode=("mode" in p)?p.mode:"place",
            settings=("settings" in p)?p.settings:{freezeRotationX=true,freezeRotationY=true,freezeRotationZ=true}});
        if(!begun.ok) return begun;
        local visual=false;
        foreach(relation in scene_director.document().relations)
            if(relation.type=="visible_fraction" || relation.type=="occludes" || relation.type=="projected_overlap") visual=true;
        if(visual) {
            local prepared=null;
            try {prepared=scene_director.visibility(target,{prepareRelations=true,width=("width" in p)?p.width:256});}
            catch(e) {prepared={ok=false,error=e.tostring(),code="VisibilityPreparationFailed"};}
            if(!prepared.ok) {sd_require(scene_director.session.cancelPhysicsPlacement());return prepared;}
        }
        return begun;
    }
    if(action=="relation_update") {
        if(!("pos" in p) || p.pos.len()!=3) return {ok=false,error="relation_update requires pos triple"};
        local ticks=("ticks" in p)?p.ticks:1;
        if(typeof ticks!="integer" || ticks<1 || ticks>240) return {ok=false,error="ticks must be integer 1..240"};
        for(local i=0;i<ticks;i++) {
            local r=scene_director.session.updatePhysicsPlacement(p.pos[0].tofloat(),p.pos[1].tofloat(),p.pos[2].tofloat(),0.0166666667);
            if(!r.ok) return r;
        }
        return scene_director.session.checkRelationPlacement();
    }
    if(action=="relation_check") return scene_director.session.checkRelationPlacement();
    if(action=="relation_discard") return scene_director.session.cancelPhysicsPlacement();
    if(action=="relation_commit") {
        local r=scene_director.session.commitPhysicsPlacement();if(!r.ok) return r;
        local projected=scene_director.refresh();
        if(!projected.ok) {sd_require(scene_director.session.undo());sd_require(scene_director.refresh());}
        return projected;
    }
    if(action=="reset") return scene_director.reset();
    if(action=="spawn" || action=="place" || action=="add_object") return scene_director.spawn(p);
    if(action=="move" || action=="move_object") {local v=("pos" in p)?p.pos:[p.x,p.y,p.z];return scene_director.move(target,v[0],v[1],v[2]);}
    if(action=="scale") {local v=("scale" in p)?p.scale:[p.sx,p.sy,p.sz];return scene_director.scale(target,v[0],v[1],v[2]);}
    if(action=="rotation" || action=="rotate") {
        if("rotation" in p) return scene_director.command("scene.transform.set.v1",{object=target,rotation=p.rotation});
        return scene_director.rotate(target,("yaw_deg" in p)?p.yaw_deg:p.yaw);
    }
    if(action=="remove" || action=="remove_object") return scene_director.remove(target);
    if(action=="visible" || action=="visibility") return scene_director.set_visible(target,p.visible);
    if(action=="material") return scene_director.set_material(target,p);
    if(action=="lighting" || action=="set_lighting") return scene_director.set_lighting(p);
    if(action=="camera") return scene_director.camera(p);
    if(action=="cameras") return scene_director.cameras(("count" in p)?p.count:6);
    if(action=="info") return scene_director.info();if(action=="list") return scene_director.list();
    if(action=="save") return {ok=true,json=scene_director.session.saveJson(),revision=scene_director.session.getRevision()};
    if(action=="restore") {
        local staged=null;
        if(scene_director.importedHost==null) {
            try {
                local candidate=sd_require(scene_director.editorModule.createAuthoringSession("ai-restore-preflight"));
                local planned=candidate.restoreJson(p.json);if(!planned.ok) return planned;
                staged=scene_director.stage_document(sd_require(candidate.snapshot()));
            } catch(e) {return {ok=false,error=e.tostring(),code="PreparationFailed"};}
        }
        local r=scene_director.session.restoreJson(p.json);if(!r.ok) {if(staged!=null) scene_director.discard_staged(staged);return r;}
        local projected=scene_director.refresh(staged);
        if(!projected.ok) {sd_require(scene_director.session.undo());sd_require(scene_director.refresh());}
        return projected;
    }
    if(action=="undo" || action=="redo") {
        local r=action=="undo"?scene_director.session.undo():scene_director.session.redo();if(!r.ok) return r;
        local projected=scene_director.refresh();
        if(!projected.ok) {
            sd_require(action=="undo"?scene_director.session.redo():scene_director.session.undo());
            sd_require(scene_director.refresh());
        }
        return projected;
    }
    if(action=="command") return scene_director.command(p.command,p.payload);
    if(action=="bind_host") {
        local r=scene_director.editorModule.createLiveSession("ai-import",p.host);if(!r.ok) return r;
        foreach(id,entry in scene_director.visuals) foreach(ent in entry.entities) ent.setVisible(false);
        scene_director.session=r.value;scene_director.importedHost=p.host;scene_director.projectedRevision=-1;return scene_director.refresh();
    }
    if(action=="ground" || action=="align" || action=="screen") {
        local payload=clone p;if("expectedRevision" in payload) delete payload.expectedRevision;payload.object <- target;
        if(action=="screen") {
            local d=scene_director.document();
            if(!("camera" in d) || d.camera.len()==0) return {ok=false,error="screen placement requires an authored camera"};
            payload.eye <- d.camera.eye;payload.target <- d.camera.target;
            payload.up <- d.camera.up;payload.fov <- d.camera.fov;
            payload.aspect <- gfx.getWidth().tofloat()/gfx.getHeight();
        } else {
            payload.bounds <- scene_director.bounds(target);
            if(action=="align") payload.referenceBounds <- scene_director.bounds(payload.reference);
        }
        return scene_director.command("scene.placement."+action+".v1",payload);
    }
    return {ok=false,error="unknown action: "+action};
};
function scene_director_install() {return scene_director.install();}
sd_require(scene_director.install());
