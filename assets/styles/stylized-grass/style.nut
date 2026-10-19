// Default stylized grass. Load once with dofile(), then retain the style instance.
// Graphics owns native resources; this wrapper retains their script handles.
// All methods run on the render thread, outside callbacks/locks.
stylizedGrassDefaults <- function() {
    return {
        frameSeconds=0.80,
        swayPeriodMultipliers=[1.0,1.25,1.5,1.75],
        darkThreshold=0.14, lightThreshold=0.23,
        darkGreen=[0.028,0.12,0.014],
        green=[0.09,0.29,0.025],
        youngGreen=[0.30,0.53,0.065],
        referenceGround=[0.25,0.43,0.11],
        rareVariations=true,
    };
};

StylizedGrassStyle <- class {
    grass=null; ground=null; card=null; controls=null; options=null;

    function requireValue(result,label) {
        if(!result.ok) {
            local detail=("diagnostics" in result && result.diagnostics.len()>0)?result.diagnostics[0].message:result.code;
            throw "stylized-grass: "+label+" / "+detail;
        }
        return result.value;
    }

    // @cost CPU rasterization and GPU upload, amortized over the field lifetime.
    // records is an in-memory array of column-major affine matrix float values.
    constructor(records,overrides) {
        options=stylizedGrassDefaults();
        foreach(name,value in overrides) if(name in options) options[name]=value;
        if(options.frameSeconds<=0.0 || options.darkThreshold>=options.lightThreshold)
            throw "stylized-grass requires positive frameSeconds and ordered thresholds";
        if(options.swayPeriodMultipliers.len()!=4) throw "four sway period multipliers required";
        foreach(value in options.swayPeriodMultipliers)
            if(value<=0.0) throw "positive sway period multiplier required";
        card=gfx.newMeshFromArrays(
            [-0.5,0.0,0.0,0.5,0.0,0.0,0.5,1.0,0.0,-0.5,1.0,0.0],
            [0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0],
            [0.0,0.0,1.0,0.0,1.0,1.0,0.0,1.0],4,[0,1,2,0,2,3],6);
        grass=requireValue(gfx.buildStylizedGrassShader(records,"grass"),"CPU grass generation");
        requireValue(gfx.configureMeshShaderSurface(grass,"opaque",true,true),"grass surface");
        ground=requireValue(gfx.buildStylizedGrassShader(records,"ground"),"ground palette generation");
        requireValue(gfx.configureMeshShaderSurface(ground,"opaque",false,true),"ground surface");
        requireValue(gfx.configureMeshShaderRaster(ground,"lessEqual",0.0,0.0,15),"ground depth");
        controls=[];
        for(local i=0;i<32;++i) {
            local name="grassStyle"+i;
            controls.append(name);grass.declareFloat(name);ground.declareFloat(name);
        }
        local values=[0.0,options.frameSeconds,options.darkThreshold,options.lightThreshold,
            options.darkGreen[0],options.darkGreen[1],options.darkGreen[2],
            options.green[0],options.green[1],options.green[2],
            options.youngGreen[0],options.youngGreen[1],options.youngGreen[2],options.rareVariations?1.0:0.0,
            384.0,192.0];
        foreach(value in options.swayPeriodMultipliers) values.append(value.tofloat());
        for(local i=0;i<values.len();++i) {
            grass.sendFloat(controls[i],values[i]);ground.sendFloat(controls[i],values[i]);
        }
    }

    // Simulation seconds supplied by the caller; pausing/replay owns the clock.
    function setTime(seconds) { grass.sendFloat(controls[0],seconds.tofloat()); }
    function setRareVariations(enabled) { grass.sendFloat(controls[13],enabled?1.0:0.0); }

    // @cost One full-resolution snapshot and two draws per field per frame.
    // Call after render3D(), before transparent objects. Ground is world-space,
    // matching the root coordinates; model must be the identity matrix.
    function draw(groundMesh,model,first,count) {
        local capture=gfx.captureMesh3DSceneColor();
        if(capture!="captured" && capture!="scheduled")
            throw "stylized-grass requires same-frame SceneColor: "+capture;
        requireValue(gfx.drawMeshShaderInstances(groundMesh,ground,model,0,1),"ground palette draw");
        requireValue(gfx.drawMeshShaderInstances(card,grass,model,first,count),"grass draw");
    }
};
