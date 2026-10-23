// Reusable built-in resource. Load once; each instance owns independent uniforms.
unitSurfacePresets <- {
    shield={mode=0.0,colorR=0.035,colorG=0.42,colorB=1.0,scale=2.3,edgeWidth=0.018,emission=3.2},
    hit={mode=1.0,colorR=1.0,colorG=0.17,colorB=0.015,duration=1.4,emission=5.0},
    frost={mode=2.0,colorR=0.35,colorG=0.8,colorB=1.0,material=true,emission=2.0},
    burn={mode=3.0,colorR=1.0,colorG=0.22,colorB=0.025,material=true,emission=3.5},
    poison={mode=4.0,colorR=0.45,colorG=1.0,colorB=0.08,material=true,emission=2.0},
    heal={mode=5.0,colorR=0.1,colorG=1.0,colorB=0.55,emission=3.0},
    shock={mode=6.0,colorR=0.025,colorG=0.16,colorB=1.0,scale=2.0,edgeWidth=0.012,emission=4.0},
    petrify={mode=7.0,colorR=0.48,colorG=0.47,colorB=0.43,blend="alpha",scale=4.0,material=true,emission=0.3,duration=0.75},
    stealth={mode=8.0,colorR=0.12,colorG=0.8,colorB=0.72,opacity=0.35,material=true,emission=1.2},
    phase={mode=9.0,colorR=0.65,colorG=0.18,colorB=1.0,scale=3.0,material=true,emission=2.8},
    cleanse={mode=10.0,colorR=1.0,colorG=0.82,colorB=0.4,emission=3.0},
    curse={mode=11.0,colorR=0.55,colorG=0.08,colorB=0.85,blend="alpha",scale=3.0,emission=2.0},
    berserk={mode=12.0,colorR=1.0,colorG=0.035,colorB=0.02,scale=3.0,emission=3.0},
    holyArmor={mode=13.0,colorR=1.0,colorG=0.65,colorB=0.12,scale=3.0,emission=3.0},
    shadow={material=true,mode=14.0,colorR=0.18,colorG=0.035,colorB=0.3,blend="alpha",scale=3.0,emission=1.0},
    charge={mode=15.0,colorR=0.12,colorG=0.42,colorB=1.0,scale=3.0,emission=3.5},
    overheat={mode=16.0,colorR=1.0,colorG=0.16,colorB=0.02,scale=4.0,material=true,emission=3.0},
    wet={mode=17.0,colorR=0.06,colorG=0.32,colorB=0.55,blend="alpha",scale=3.0,material=true,emission=1.0},
    sand={mode=18.0,colorR=0.72,colorG=0.44,colorB=0.13,blend="alpha",scale=4.0,material=true,emission=0.5},
    metallize={mode=19.0,colorR=0.65,colorG=0.74,colorB=0.86,blend="alpha",scale=4.0,material=true,emission=0.4},
    bark={mode=20.0,colorR=0.38,colorG=0.19,colorB=0.055,scale=3.0,emission=1.0,material=true},
    fracture={mode=21.0,colorR=0.95,colorG=0.05,colorB=0.75,blend="alpha",scale=3.0,material=true,emission=3.0},
    scan={mode=22.0,colorR=0.05,colorG=0.75,colorB=1.0,scale=4.0,emission=3.0},
    teleport={mode=23.0,colorR=0.025,colorG=0.6,colorB=1.0,scale=5.0,duration=2.0,emission=4.0,material=true},
    slowTime={mode=24.0,colorR=0.12,colorG=0.35,colorB=1.0,scale=3.0,emission=2.5},
    lifesteal={mode=25.0,colorR=0.85,colorG=0.015,colorB=0.055,scale=3.0,emission=3.0}
};
unitSurfacePresetNames <- function() {
    local ids=[];
    foreach(id,recipe in unitSurfacePresets) ids.append(id);
    ids.sort(function(a,b) {return unitSurfacePresets[a].mode<=>unitSurfacePresets[b].mode;});
    return ids;
};
// One 128-byte layout, shared by neutral, surface and fragment stages.
unitSurfaceUniforms <- ["time","speed","colorR","colorG","colorB","intensity","opacity",
    "scale","rimPower","coverage","mode","edgeWidth","distortion","seed","progress",
    "originX","originY","originZ","rotationX","rotationY","rotationZ","rotationW","unitScale",
    "hitX","hitY","hitZ","hitRadius","layer","duration","emission","manualProgress","anchored"];
unitSurfaceDefaults <- function() {
    return {time=0.0,speed=1.0,colorR=1.0,colorG=1.0,colorB=1.0,intensity=1.0,opacity=0.8,
        scale=3.0,rimPower=3.0,coverage=0.65,mode=0.0,edgeWidth=0.018,distortion=0.35,
        seed=0.0,progress=0.5,originX=0.0,originY=0.0,originZ=0.0,
        rotationX=0.0,rotationY=0.0,rotationZ=0.0,rotationW=1.0,unitScale=1.0,
        hitX=0.0,hitY=2.05,hitZ=0.43,hitRadius=0.38,layer=0.0,duration=2.0,
        emission=1.0,manualProgress=0.0,anchored=0.0};
};
unitSurfaceChecked <- function(result) {
    if(!result.ok) throw "unit-surface: "+result.status;
    return ("value" in result)?result.value:null;
};
// @cost File decoding and GPU pipeline creation, amortized over the scene lifetime.
unitSurfacePrepareShader <- function(directory,values,layer,blend,depthWrite) {
    local shader=unitSurfaceChecked(gfx.loadMeshShaderSpv(directory+"/surface.vert.spv",directory+"/surface.frag.spv"));
    unitSurfaceChecked(gfx.configureMeshShaderSurface(shader,blend,depthWrite,layer>3.5 && (values.mode==5.0 || values.mode==24.0)));
    unitSurfaceChecked(gfx.configureMeshShaderRaster(shader,"lessEqual",depthWrite?0.0:-1.0,depthWrite?0.0:-1.0,15));
    foreach(k in unitSurfaceUniforms) {shader.declareFloat(k);shader.sendFloat(k,k=="layer"?layer:values[k]);}
    return shader;
};
// @cost One neutral material pipeline; share it across unaffected baseRenderable renderables.
// Graphics owns the shader; retain the script handle until all consumers are gone.
unitSurfacePrepareNeutral <- function(directory) {
    local values=unitSurfaceDefaults();values.mode=-1.0;
    return unitSurfacePrepareShader(directory,values,1.0,"opaque",true);
};
UnitSurfaceEffect <- class {
    shader=null; materialShader=null; fragmentsShader=null; accentShader=null; values=null; stages=null; frame=null; materials=null; overlayBlend="additive";
    // @cost Prepare one overlay plus preset-specific material, impact or fragment pipelines.
    // Main/render thread, no callbacks. Graphics must outlive this object and its renderables.
    constructor(directory,preset,overrides) {
        if(!(preset in unitSurfacePresets)) throw "unit-surface: unknown preset "+preset;
        values=unitSurfaceDefaults();stages=[];materials=[];
        foreach(k,v in unitSurfacePresets[preset]) if(k in values) values[k]=v;
        foreach(k,v in overrides) {validate(k,v);values[k]=v.tofloat();}
        local recipe=unitSurfacePresets[preset];
        overlayBlend=("blend" in recipe)?recipe.blend:"additive";
        shader=unitSurfacePrepareShader(directory,values,0.0,overlayBlend,false);
        stages.append(shader);
        if("material" in recipe && recipe.material) {
            materialShader=unitSurfacePrepareShader(directory,values,1.0,preset=="stealth"?"alpha":"opaque",preset!="stealth");stages.append(materialShader);
        }
        if(preset=="hit") {
            accentShader=unitSurfacePrepareShader(directory,values,3.0,"additive",false);stages.append(accentShader);
        }
        if(preset=="teleport" || preset=="frost" || preset=="wet" || preset=="shadow" || preset=="heal" || preset=="slowTime") {
            fragmentsShader=unitSurfacePrepareShader(directory,values,preset=="teleport"?2.0:4.0,preset=="shadow"?"alpha":"additive",false);stages.append(fragmentsShader);
        }
    }
    function finite(value) {
        return (typeof value=="integer" || typeof value=="float") && value==value && fabs(value)<=1000000.0;
    }
    function validate(name,value) {
        if(!(name in values) || name=="mode" || name=="layer" || name=="anchored" ||
            name.find("origin")==0 || name.find("rotation")==0 || name=="unitScale")
            throw "unit-surface: unknown/edit-protected parameter "+name;
        if(!finite(value)) throw "unit-surface: finite numeric parameter required";
        if(name!="hitX" && name!="hitY" && name!="hitZ" && value<0.0) throw "unit-surface: nonnegative parameter required";
        if((name=="opacity" || name=="coverage" || name=="progress" || name=="manualProgress") && value>1.0)
            throw "unit-surface: expected [0,1]";
        if(name=="manualProgress" && value!=0.0 && value!=1.0) throw "unit-surface: manualProgress is 0 or 1";
        if(name=="edgeWidth" && (value<0.005 || value>0.3)) throw "unit-surface: edgeWidth requires [0.005,0.3]";
        if((name=="scale" || name=="rimPower" || name=="hitRadius" || name=="duration") && value<0.01)
            throw "unit-surface: positive scale/power/radius/duration required";
    }
    function publish(name,value) {
        values[name]=value.tofloat();foreach(stage in stages) stage.sendFloat(name,values[name]);
    }
    // Caller injects simulation time. No wall clock or shared RNG is read.
    function set(name,value) {
        validate(name,value);publish(name,value);
    }
    // Atomic validation before publication. Uniform scale only; q is normalized here.
    // Call with the unit transform, not a child mesh transform. No target is retained.
    function setUnitFrame(x,y,z,qx,qy,qz,qw,scale) {
        local candidate=[x,y,z,qx,qy,qz,qw,scale];
        foreach(v in candidate) if(!finite(v)) throw "unit-surface: invalid unit frame";
        local length=sqrt(qx*qx+qy*qy+qz*qz+qw*qw);
        if(length<0.00001 || scale<0.00001) throw "unit-surface: nonzero quaternion and positive uniform scale required";
        candidate=[x,y,z,qx/length,qy/length,qz/length,qw/length,scale];
        local keys=["originX","originY","originZ","rotationX","rotationY","rotationZ","rotationW","unitScale"];
        for(local i=0;i<8;++i) if(frame==null || frame[i]!=candidate[i]) publish(keys[i],candidate[i]);
        if(frame==null) publish("anchored",1.0);
        frame=candidate;
    }
    function setHit(x,y,z,radius) {
        validate("hitX",x);validate("hitY",y);validate("hitZ",z);validate("hitRadius",radius);
        publish("hitX",x);publish("hitY",y);publish("hitZ",z);publish("hitRadius",radius);
    }
    // The CPU Material declares the render queue. Shader blend state alone does
    // not classify an ECS renderable as transparent. Instance owns these Materials;
    // retain this effect until every renderable returned below is destroyed.
    function material(shaderHandle,blend,transparent,depthWrite,priority) {
        local mat=gfx.newMaterial();mat.setShadingModel("custom");mat.setShader(shaderHandle);
        mat.setSurfaceMode(transparent?"transparent":"opaque");mat.setBlendMode(blend);
        mat.setDepthWrite(depthWrite);mat.setSortPriority(priority);
        mat.setCastShadow(false);mat.setReceiveShadow(false);mat.setCastOcclusion(false);
        materials.append(mat);return mat;
    }
    // @cost One renderable / draw per visible surface layer. Caller owns the returned object.
    function newOverlay(mesh) {
        local overlay=eve.Renderable3D();overlay.setMesh(mesh);
        overlay.setMaterial(material(shader,overlayBlend,true,false,10));
        overlay.setCastShadow(false);overlay.setReceiveShadow(false);return overlay;
    }
    // @cost One renderable / draw. Neutral is borrowed; material/clip presets replace it.
    function newBase(mesh,neutralShader) {
        local baseRenderable=eve.Renderable3D();baseRenderable.setMesh(mesh);
        local transparent=values.mode==8.0;
        baseRenderable.setMaterial(material(materialShader!=null?materialShader:neutralShader,"alpha",transparent,!transparent,0));
        baseRenderable.setCastShadow(false);baseRenderable.setReceiveShadow(false);return baseRenderable;
    }
    // Restore both shader and queue classification together, with no retained target.
    function setBaseEnabled(baseRenderable,neutralShader,enabled) {
        local mat=baseRenderable.getMaterial();
        if(mat==null) throw "unit-surface: expected an attached base material";
        local transparent=enabled && values.mode==8.0;
        mat.setShader(enabled && materialShader!=null?materialShader:neutralShader);
        mat.setSurfaceMode(transparent?"transparent":"opaque");mat.setDepthWrite(!transparent);
    }
    // @cost One draw for a prepared unit-radius XY annulus, placed at the hit point.
    function newImpactRing(mesh) {
        if(accentShader==null) throw "unit-surface: this preset has no impact ring";
        local ring=eve.Renderable3D();ring.setMesh(mesh);
        ring.setMaterial(material(accentShader,"additive",true,false,30));
        ring.setCastShadow(false);ring.setReceiveShadow(false);return ring;
    }
    // @cost One detail draw: shadow uses ribbons, heal/slowTime use three packed rings, others use lattice cubes.
    function newFragments(mesh) {
        if(fragmentsShader==null) throw "unit-surface: this preset has no fragment stage";
        local fragments=eve.Renderable3D();fragments.setMesh(mesh);
        fragments.setMaterial(material(fragmentsShader,values.mode==14.0?"alpha":"additive",true,false,20));
        fragments.setCastShadow(false);fragments.setReceiveShadow(false);return fragments;
    }
};
