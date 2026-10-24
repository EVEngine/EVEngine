// Owns preview-only resources and renderables. Model/environment changes stage
// candidates first; authoring documents and their undo history are untouched.
// All methods are owner-thread-only. Host resolvers must not reenter this object.
class MaterialPreviewScene {
    models=null; environments=null; modelCache=null; environmentCache=null;
    modelEntities=null; skyEntities=null; activeModel=null; activeEnvironment=null;
    skyResources=null; visibleSky=true; intensity=0.65; modelSource=null; textureSource=null;
    constructor(settings=null) {
        models=[{id="shaderball",label="Material Ball"},{id="sphere",label="Sphere"},
                {id="cube",label="Cube"},{id="cylinder",label="Cylinder"},{id="torus",label="Torus"}];
        environments=[{id="studio",label="Photo Studio"},{id="daylight",label="Daylight"},
                      {id="sunset",label="Sunset"}];
        modelCache={}; environmentCache={}; modelEntities=[]; skyEntities=[]; skyResources=[];
        if (settings!=null) {
            if ("resolveModel" in settings) modelSource=settings.resolveModel;
            if ("resolveTexture" in settings) textureSource=settings.resolveTexture;
        }
    }
    function indexOf(items,id) {
        foreach (i,item in items) if (item.id==id) return i;
        throw "Unknown preview asset: "+id;
    }
    function labels(items) {
        local text="";
        foreach (i,item in items) text+=(i==0 ? "" : "\n")+item.label;
        return text;
    }
    function registerModel(id,label,provider) {
        if (id=="" || typeof provider!="function") throw "Model requires an id and mesh provider";
        foreach (item in models) if (item.id==id) throw "Preview model already registered: "+id;
        models.append({id=id,label=label,provider=provider});
    }
    function registerEnvironment(id,label,faces) {
        if (id=="" || typeof faces!="array" || faces.len()!=6) throw "Skybox requires an id and six faces (+X,-X,+Y,-Y,+Z,-Z)";
        foreach (face in faces) if (typeof face!="string" || face=="") throw "Skybox faces must be nonempty asset paths";
        foreach (item in environments) if (item.id==id) throw "Environment already registered: "+id;
        environments.append({id=id,label=label,faces=faces.slice(0)});
    }
    function destroyEntities(entities) {
        foreach (entity in entities) {
            local result=eve.destroyRenderable3D(entity.getEntityId(),entity.getEntityGeneration());
            if (!result.ok && result.code!="NotFound") throw result.status.summary;
        }
    }
    function boundsFor(meshes) {
        if (meshes.len()==0) throw "Model has no renderable meshes";
        local bounds=null;
        foreach (mesh in meshes) {
            local result=mesh.getLocalBounds();
            if (!result.ok) throw result.status.summary;
            local b=result.value;
            if (b.len()!=6) throw "Mesh bounds must contain six coordinates";
            if (bounds==null) bounds=b.slice(0);
            else for (local i=0;i<3;++i) {
                if (b[i]<bounds[i]) bounds[i]=b[i];
                if (b[i+3]>bounds[i+3]) bounds[i+3]=b[i+3];
            }
        }
        local size=0.0;
        for (local i=0;i<3;++i) if (bounds[i+3]-bounds[i]>size) size=bounds[i+3]-bounds[i];
        if (size<0.000001) throw "Preview model has zero extent";
        return {bounds=bounds,scale=2.6/size,center=[(bounds[0]+bounds[3])*0.5,
                    (bounds[1]+bounds[4])*0.5,(bounds[2]+bounds[5])*0.5]};
    }
    function importMeshes(path) {
        if (path=="") throw "Enter a model path relative to the project";
        if (modelSource!=null) return modelSource(path);
        if (!ensure_module("model3d")) throw "Model import requires model3d or options.resolveModel";
        local decoded=model3d.loadModelDataWithOptions(path,{triangulate=true,generateNormalsIfMissing=true});
        if (!decoded.ok) throw decoded.status.summary;
        local model=decoded.value, meshes=[], temporary=[];
        try {
            for (local i=0;i<model.getMeshCount();++i) {
                if (model.getVertexCount(i)==0) continue;
                local entity=model3d.createRenderable(gfx,model,i);
                if (entity==null) throw "Could not upload mesh "+i;
                temporary.append(entity); entity.setVisible(false);
                meshes.append(entity.getMesh());
            }
        } catch (error) { destroyEntities(temporary); throw error; }
        destroyEntities(temporary);
        return meshes;
    }
    function modelAsset(id) {
        if (id in modelCache) return modelCache[id];
        local entry=models[indexOf(models,id)], meshes=[];
        if ("provider" in entry) meshes=entry.provider();
        else if (id=="sphere") meshes=[gfx.newMeshSphere(64,32)];
        else if (id=="cube") meshes=[gfx.newMeshCube(1.0)];
        else if (id=="cylinder") meshes=[gfx.newMeshCylinder(64,1,true)];
        else meshes=[MaterialPreviewGeometry.upload(MaterialPreviewGeometry.build(id))];
        local result=boundsFor(meshes);
        result.meshes <- meshes.slice(0);
        modelCache[id] <- result;
        return result;
    }
    function importModel(path) {
        local id="file:"+path;
        if (!(id in modelCache)) {
            local meshes=importMeshes(path);
            local asset=boundsFor(meshes); asset.meshes <- meshes.slice(0);
            registerModel(id,"Imported: "+path,function() { return meshes; });
            modelCache[id] <- asset;
        }
        return id;
    }
    function transformEntities(entities,asset,yaw) {
        local c=asset.center,s=asset.scale,cy=cos(yaw),sy=sin(yaw);
        foreach (entity in entities) {
            entity.setScale(s,s,s); entity.setYaw(yaw);
            entity.setPosition(-s*(cy*c[0]+sy*c[2]),0.10-s*c[1],-s*(-sy*c[0]+cy*c[2]));
        }
    }
    function selectModel(id,material,yaw) {
        local asset=modelAsset(id), candidate=[];
        try {
            foreach (mesh in asset.meshes) {
                local entity=eve.Renderable3D(); candidate.append(entity);
                entity.setVisible(false); entity.setMesh(mesh); entity.setMaterial(material);
            }
            transformEntities(candidate,asset,yaw);
        } catch (error) { destroyEntities(candidate); throw error; }
        local old=modelEntities;
        foreach (entity in old) entity.setVisible(false);
        modelEntities=candidate; activeModel=id;
        foreach (entity in candidate) entity.setVisible(true);
        destroyEntities(old);
    }
    function setYaw(yaw) {
        if (activeModel!=null) transformEntities(modelEntities,modelCache[activeModel],yaw);
    }
    function environmentAsset(id) {
        if (id in environmentCache) return environmentCache[id];
        local entry=environments[indexOf(environments,id)], textures=[];
        if ("faces" in entry) {
            foreach (path in entry.faces) {
                local texture=textureSource==null ? gfx.newTextureFromFile(path) : textureSource(path);
                if (texture==null) throw "Skybox face could not be resolved: "+path;
                textures.append(texture);
            }
        } else {
            local image=eve.Image(), size=96;
            for (local face=0;face<6;++face) {
                local pixels=image.newEmptyImageData(size,size,"RGBA8");
                for (local y=0;y<size;++y) for (local x=0;x<size;++x) {
                    local direction=MaterialPreviewGeometry.faceDirection(face,2.0*(x+0.5)/size-1.0,2.0*(y+0.5)/size-1.0);
                    local color=MaterialPreviewGeometry.skyColor(id,direction);
                    pixels.setPixel(x,y,color[0],color[1],color[2],1.0);
                }
                textures.append(gfx.newTexture(pixels,false,false));
            }
        }
        local probe=gfx.newReflectionProbeCapture();
        probe.configure(0.0,0.1,0.0,32,0.1,100.0);
        for (local face=0;face<6;++face) {
            probe.setSkyFaceColor(face,1.0,1.0,1.0);
            probe.setSkyFaceTexture(face,textures[face]);
        }
        probe.setCaptureMask(0); probe.requestCapture();
        local budget=0;
        while (!probe.isCaptureComplete() && budget<24) { probe.update(6); budget+=1; }
        if (!probe.isCaptureComplete() || !probe.stageCapturedFaces() || !probe.filterAndPublish(32))
            throw "Could not prepare environment reflections: "+id;
        local cube=probe.getActiveCubemap();
        if (cube==null) throw "Environment cubemap is unavailable: "+id;
        local asset={textures=textures,probe=probe,cube=cube};
        environmentCache[id] <- asset;
        return asset;
    }
    function selectEnvironment(id,camera) {
        local asset=environmentAsset(id), candidate=[], resources=[];
        try {
            for (local face=0;face<6;++face) {
                local entity=eve.Renderable3D(); candidate.append(entity); entity.setVisible(false);
                local mesh=MaterialPreviewGeometry.skyFace(face);
                local material=gfx.newMaterial(); resources.append({mesh=mesh,material=material});
                entity.setMesh(mesh); material.setShadingModel("unlit");
                material.setAlbedoTexture(asset.textures[face]); material.setDoubleSided(true);
                material.setDepthWrite(false); material.setReceiveLight(false);
                material.setCastShadow(false); material.setReceiveShadow(false);
                entity.setMaterial(material); entity.setCastShadow(false); entity.setReceiveShadow(false);
                // Offscreen viewports also use capture masks; retain the default mask here.
                // The probe's zero scene mask already excludes all preview geometry.
                entity.setReceiveLight(false); entity.setCastOcclusion(false);
                entity.setPosition(camera.getEyeX(),camera.getEyeY(),camera.getEyeZ());
            }
        } catch (error) { destroyEntities(candidate); throw error; }
        camera.setEnvMap(asset.cube); camera.setEnvIntensity(intensity);
        local old=skyEntities; skyEntities=candidate; activeEnvironment=id;
        foreach (entity in candidate) entity.setVisible(visibleSky);
        destroyEntities(old);
        skyResources=resources;
    }
    function syncSky(camera) {
        foreach (entity in skyEntities)
            entity.setPosition(camera.getEyeX(),camera.getEyeY(),camera.getEyeZ());
    }
    function setSkyVisible(visible) {
        visibleSky=visible;
        foreach (entity in skyEntities) entity.setVisible(visible);
    }
    function setIntensity(value,camera) {
        if (value<0.0 || value>3.0 || value!=value) throw "Environment intensity must be between 0 and 3";
        intensity=value; camera.setEnvIntensity(value);
    }
}
