// Run inside the actual engine via capture_settings.nut; failures throw.
function checkInk(condition,message) {
    if(!condition) throw "INK_VERIFY_FAILED: "+message;
    print("INK_VERIFY_PASS: "+message+"\n");
}
function sampleWorld(s,p,channel) {
    local q=sub(p,s.p);
    local x=clamp((dot(q,s.u)/s.uu*s.w).tointeger(),0,s.w-1);
    local y=clamp((dot(q,s.v)/s.vv*s.h).tointeger(),0,s.h-1);
    local read=s.canvas.readPixels();
    if(!read.ok) throw "INK_VERIFY_FAILED: GPU readback failed";
    local pixels=read.value;
    if(channel==3) return pixels.getPixelA(x,y);
    return channel==0 ? pixels.getPixelR(x,y) : pixels.getPixelB(x,y);
}
local badShader=gfx.loadMeshShaderSpv("","assets/source.json");
checkInk(!badShader.ok && inkShader!=null,"malformed shader rejected while current shader remains usable");
local missingShader=gfx.loadMeshShaderSpv("","missing.spv");
checkInk(!missingShader.ok,"missing shader returns a diagnostic");
clearInk();
checkInk(shotCount==0,"clear resets shot count");
local point=[0.0,0.0,3.0];
local replaySeed=seed;
splat(point,[0,1,0],1.2,0);
checkInk(sampleWorld(surfaces[0],point,0)>0.5,"orange written to ground");
local alpha1=sampleWorld(surfaces[0],point,3);
seed=replaySeed; splat(point,[0,1,0],1.2,0);
local alpha2=sampleWorld(surfaces[0],point,3);
checkInk(fabs(alpha2-(1.0-(1.0-alpha1)*(1.0-alpha1)))<0.012,"RGBA coverage follows reference accumulation formula");
splat(point,[0,1,0],1.2,1);
checkInk(sampleWorld(surfaces[0],point,2)>0.5 && sampleWorld(surfaces[0],point,0)<0.5,
    "violet overwrites orange at same world point");
local before=shotCount;
checkInk(!shootScreen(1270.0,10.0,0) && shotCount==before,"sky miss creates no paint");
checkInk(shootScreen(640.0,450.0,0) && shotCount==before+1,"screen ray hits nearest scene surface");
// The cylinder's 2pi/0 seam lies between the first and last side patch.
clearInk();
splat([-1.3,1.4,-1.5],[1,0,0],1.5,0);
checkInk(sampleWorld(surfaces[6],[-1.31,1.4,-1.4],0)>0.5 &&
    sampleWorld(surfaces[21],[-1.31,1.4,-1.6],0)>0.5,"one projector paints both cylinder seam patches");
checkInk(sampleWorld(surfaces[22],[-1.4,2.7,-1.5],3)==0.0,"grazing side projector leaves cylinder cap unpainted");
clearInk();
checkInk(sampleWorld(surfaces[0],point,3)==0.0,"clear removes persisted paint");
showcase();
local savedCount=shotCount;
local savedAlpha=sampleWorld(surfaces[0],[0,0,3],3);
foreach(i,knob in inkKnobs) {
    checkInk(inkShader.getUniformIndex(knob.id)==i,"parameter slot: "+knob.id);
    local original=knob.value;
    ui.setValue(knob.id,knob.min); updateInkParameters();
    checkInk(fabs(knob.value-knob.min)<0.001,"slider update: "+knob.id);
    ui.setValue(knob.id,original); updateInkParameters();
}
checkInk(shotCount==savedCount && sampleWorld(surfaces[0],[0,0,3],3)==savedAlpha,
    "material adjustment preserves paint state");
if("inkCaptureStyle" in getroottable()) {
    foreach(id,value in inkCaptureStyle) ui.setValue(id,value);
    updateInkParameters();
}
dofile("verify_gpu.nut");
print("INK_VERIFY_COMPLETE\n");
