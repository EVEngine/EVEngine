// Called after real shader creation; verifies public parameter and isolation contracts.
function verifyUnitSurfaceEffects(effects,units,neutral) {
    function rejects(action) {
        local rejected=false;
        try {action();} catch(error) {rejected=true;}
        if(!rejected) throw "unit-surface contract: expected invalid input rejection";
    }
    if(unitSurfaceUniforms.len()!=32) throw "unit-surface: expected 128-byte parameter block";
    local ids=unitSurfacePresetNames();
    if(ids.len()!=26 || effects.len()!=26) throw "unit-surface contract: incomplete catalogue";
    foreach(i,id in ids) {
        if(effects[i].values.mode!=i.tofloat()) throw "unit-surface contract: unstable preset mode";
        local hasMaterial=("material" in unitSurfacePresets[id]) && unitSurfacePresets[id].material;
        if((effects[i].materialShader!=null)!=hasMaterial) throw "material stage availability mismatch";
        effects[i].set("time",1.25);effects[i].set("seed",17.0);
        effects[i].set("progress",0.5);
        effects[i].set("distortion",0.35);effects[i].set("seed",0.0);
    }
    foreach(i,body in units) foreach(part in body) {
        if(part.overlay.getMaterial().getSurfaceMode()!="transparent") throw "overlay entered opaque queue";
        if(part.overlay.getMaterial().getDepthWrite()) throw "overlay unexpectedly writes depth";
        local expected=i==8?"transparent":"opaque";
        if(part.object.getMaterial().getSurfaceMode()!=expected) throw "base queue mismatch";
        effects[i].setBaseEnabled(part.object,neutral,false);
        if(part.object.getMaterial().getSurfaceMode()!="opaque") throw "neutral queue restore failed";
        effects[i].setBaseEnabled(part.object,neutral,true);
    }
    local shield=effects[0], hit=effects[1];
    shield.set("opacity",0.4);
    if(hit.values.opacity!=0.8) throw "unit-surface contract: shared instance parameters";
    rejects(function(){shield.set("opacity",1.1);});
    if(shield.values.opacity!=0.4) throw "unit-surface contract: failed update mutated state";
    rejects(function(){shield.set("scale",0.0);});
    rejects(function(){shield.set("time",-1.0);});
    rejects(function(){shield.set("speed","fast");});
    rejects(function(){shield.set("unknown",1.0);});
    rejects(function(){shield.set("mode",2.0);});
    rejects(function(){UnitSurfaceEffect("styles/unit-surface","missing",{});});
    rejects(function(){shield.set("edgeWidth",0.0);});
    rejects(function(){shield.set("progress",1.01);});
    rejects(function(){shield.set("seed",-1.0);});
    rejects(function(){shield.setUnitFrame(0.0,0.0,0.0,0.0,0.0,0.0,0.0,1.0);});
    rejects(function(){shield.setUnitFrame(0.0,0.0,0.0,0.0,0.0,0.0,1.0,0.0);});
    if(shield.values.anchored!=0.0) throw "unit frame failure partially published";
    shield.setUnitFrame(1.0,2.0,3.0,0.0,0.0,0.0,2.0,1.0);
    if(shield.values.rotationW!=1.0 || shield.values.originX!=1.0) throw "unit frame normalization failed";
    rejects(function(){hit.setHit(0.0,2.0,0.4,0.0);});
    if(hit.values.hitRadius!=0.38) throw "failed hit update mutated radius";
    shield.setUnitFrame(0.0,0.0,0.0,0.0,0.0,0.0,1.0,1.0);
    rejects(function(){shield.set("manualProgress",0.5);});
    shield.set("opacity",0.8);
    print("UNIT_SURFACE_CONTRACT_PASS parameter validation, failed update preservation, instance isolation\n");
}
