// Initialization-only CPU blue noise. No global random state, file I/O or frame work.
StylizedGrassRandom <- class {
    state=1;
    constructor(seed) {if(seed<0) throw "grass seed must be nonnegative";state=(seed%2147483646)+1;}
    function next() {
        // Schrage decomposition also works with a 32-bit Squirrel integer.
        local value=48271*(state%44488)-3399*(state/44488);
        state=value>0?value:value+2147483647;
        return state/2147483647.0;
    }
};

stylizedGrassScatterDefaults <- function() {
    return {seed=7319,extent=17.8,coverageRadii=[0.1633,0.27,0.36,0.48],
        variationFraction=0.006,variationRadius=2.7,width=0.64,height=0.65};
};

// @cost Bridson generation and packed-array construction once per field.
// heightAt(x,z) and accepts(x,z) are synchronous caller-owned callbacks. No locks.
// Return owns records/banks; native shader builders synchronously copy records.
stylizedGrassBuildField <- function(settings,heightAt,accepts) {
    if(settings.extent<=0 || settings.width<=0 || settings.height<=0 || settings.variationRadius<=0 ||
       settings.variationFraction<0 || settings.variationFraction>0.05 || settings.coverageRadii.len()==0)
        throw "invalid grass scatter settings";
    local records=[],banks=[];
    foreach(bank,radius in settings.coverageRadii) {
        if(radius<=0 || radius<settings.extent/1000.0) throw "grass radius outside bounded scatter range";
        local rng=StylizedGrassRandom(settings.seed+bank);
        local result=gfx.buildStylizedGrassPoints(radius,settings.extent,settings.seed+bank);
        if(!result.ok) throw "grass CPU blue-noise generation failed: "+result.code;
        local points=[];
        for(local i=0;i<result.value.len();i+=2) points.append([result.value[i],result.value[i+1]]);
        local roots=[];
        foreach(p in points) if(accepts(p[0],p[1])) roots.append(p);
        for(local i=roots.len()-1;i>0;--i) {
            local j=floor(rng.next()*(i+1)).tointeger(),p=roots[i];roots[i]=roots[j];roots[j]=p;
        }
        local rare=[],chosen={},target=floor(roots.len()*settings.variationFraction+0.5).tointeger();
        foreach(i,p in roots) {
            if(rare.len()>=target) break;
            local valid=true;
            foreach(q in rare) {local x=p[0]-q[0],z=p[1]-q[1];if(x*x+z*z<settings.variationRadius*settings.variationRadius) {valid=false;break;}}
            if(valid) {chosen[i] <- true;rare.append(p);}
        }
        local first=records.len()/16;
        foreach(i,p in roots) {
            local width=settings.width*(0.8+rng.next()*0.4),height=settings.height*(0.75+rng.next()*0.45);
            local variant=floor(rng.next()*2).tointeger(),accent=(i in chosen)?1.0:0.0;
            records.extend([width,0.0,0.0,0.0,0.0,height,0.0,0.0,
                variant.tofloat(),accent,1.0,0.0,p[0],heightAt(p[0],p[1])-0.025,p[1],1.0]);
        }
        if(roots.len()==0) throw "grass scatter produced no accepted roots";
        banks.append({first=first,count=roots.len(),radius=radius,variations=rare.len()});
    }
    return {records=records,banks=banks};
};
