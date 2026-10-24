// Optional initialization contract check, enabled by config.verifyGrass.
local settings=stylizedGrassScatterDefaults();
local rngProbe=StylizedGrassRandom(7319);
for(local i=0;i<10000;++i) {local value=rngProbe.next();if(value<0.0 || value>=1.0) throw "grass RNG out of range: "+value+" state="+rngProbe.state;}
settings.extent=1.5;settings.coverageRadii=[0.25];settings.variationRadius=0.7;
settings.variationFraction=0.05;
local flat=function(x,z) {return 0.0;},all=function(x,z) {return true;};
local a=stylizedGrassBuildField(settings,flat,all),b=stylizedGrassBuildField(settings,flat,all);
if(a.records.len()!=b.records.len()) throw "grass scatter changed record count";
for(local i=0;i<a.records.len();++i) if(a.records[i]!=b.records[i]) throw "grass scatter not deterministic";
local variants={},frames=a.records.len()/16,rare=[];
for(local i=0;i<frames;++i) {
    local offset=i*16,x=a.records[offset+12],z=a.records[offset+14];
    variants[a.records[offset+8].tointeger()] <- true;
    if(fabs(x)>1.5 || fabs(z)>1.5 || a.records[offset]<=0 || a.records[offset+5]<=0)
        throw "invalid grass root bounds/dimensions";
    if(a.records[offset+9]>0.5) rare.append([x,z]);
    for(local j=0;j<i;++j) {
        local dx=x-a.records[j*16+12],dz=z-a.records[j*16+14];
        if(dx*dx+dz*dz<0.25*0.25-0.000001) throw "grass blue-noise minimum distance violated";
    }
}
foreach(i,p in rare) for(local j=0;j<i;++j) {
    local dx=p[0]-rare[j][0],dz=p[1]-rare[j][1];
    if(dx*dx+dz*dz<0.7*0.7-0.000001) throw "rare blue-noise spacing violated";
}
if(!(0 in variants) || !(1 in variants) || rare.len()==0 || frames<60) throw "missing grass variants/rare roots";
local invalid=gfx.buildStylizedGrassShader([1.0],"grass");
if(invalid.ok) throw "invalid instance array accepted";
local badPass=gfx.buildStylizedGrassShader(a.records,"invalid");
if(badPass.ok) throw "unknown grass shader pass accepted";
print("GRASS_CPU_CONTRACT_PASS deterministic=true blueNoise=true rareBlueNoise=true invalidInputsRejected=true\n");
