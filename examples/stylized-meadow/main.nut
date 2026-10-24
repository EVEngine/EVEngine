local owned = [];
dofile("styles/stylized-grass/style.nut");
dofile("styles/stylized-grass/scatter.nut");
local meadowBanks=null;
local grassStyle = null, camera = null, groundMesh = null;
local grassOptions=stylizedGrassDefaults();
foreach(name,value in config) if(name in grassOptions) grassOptions[name]=value;
local bankIndex = config.coverageBank;
local accents = grassOptions.rareVariations;
local lastPlus=false, lastMinus=false, lastA=false;
local identity=[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0];
local frameCount = 0;
local seconds = 0.0, paused = !config.animate, lastSpace = false, view = config.meadowView, lastV = false;
function keep(v) { owned.append(v); return v; }
function heightAt(x,z) {
    return 0.7 + 0.7*sin(x*0.15+z*0.12) + 0.45*cos(z*0.24) + 0.25*sin(x*0.3-z*0.1);
}
function pathAt(z) { return 1.8*sin(z*0.18) + 0.6; }
// Spatial hash is independent of global RNG and frame rate.
function noise(i) { local h=sin(i*127.1+311.7)*43758.5453; return h-floor(h); }
function prop(mesh,x,y,z,sx,sy,sz,r,g,b) {
    local o=keep(eve.Renderable3D()); o.setMesh(mesh);
    o.setPosition(x.tofloat(),y.tofloat(),z.tofloat());
    o.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    o.setTint(r.tofloat(),g.tofloat(),b.tofloat(),1.0);o.setRoughness(1.0);o.setReceiveShadow(true);o.setCastShadow(true);
    return o;
}
function setView() {
    if(view==0) { camera.setEye(10.5,7.4,16.5);camera.setTarget(-0.5,1.1,-2.5); }
    else if(view==1) { camera.setEye(-5.0,2.8,8.5);camera.setTarget(-2.0,1.2,-4.0); }
    else { camera.setEye(0.0,17.0,22.0);camera.setTarget(0.0,0.0,-1.0); }
}
eve_init <- function() {
    if(config.verifyGrass) dofile("verify_scatter.nut");

    gfx.setMsaaSamples(1);
    gfx.setShadowSchemeDirectionalEnabled(config.shadows);
    gfx.setBackgroundColor(0.56,0.78,0.84,1.0);
    gfx.setDirectionalLight(-0.5,0.85,0.3,1.0,0.94,0.76);
    local sun=keep(eve.Light3D());sun.setType("dir");sun.setDirection(-0.5,0.85,0.3);
    sun.setColor(1.0,0.94,0.76,1.4);sun.setCastShadow(true);sun.setShadowMethod("csm");
    sun.setShadowStrength(1.0);sun.setShadowBias(0.0004);
    local renderControl=gfx.getRenderControl();renderControl.enable("forward");
    renderControl.enable("depthTest");renderControl.enable("shadow");
    camera=keep(eve.Camera3D());camera.setUp(0.0,1.0,0.0);camera.setFov(48.0);
    camera.setClipPlanes(0.1,180.0);camera.setAmbient(0.22,0.28,0.23);camera.setActive(true);setView();
    local positions=[], normals=[], uvs=[], indices=[];
    for(local z=0;z<97;++z) for(local x=0;x<97;++x) {
        local wx=x*0.375-18.0,wz=z*0.375-18.0;
        positions.extend([wx,heightAt(wx,wz),wz]);
        local dx=(heightAt(wx+0.02,wz)-heightAt(wx-0.02,wz))/0.04;
        local dz=(heightAt(wx,wz+0.02)-heightAt(wx,wz-0.02))/0.04;
        local length=sqrt(dx*dx+1.0+dz*dz);
        normals.extend([-dx/length,1.0/length,-dz/length]);uvs.extend([x/96.0,z/96.0]);
        if(x<96 && z<96) {local a=z*97+x;indices.extend([a,a+97,a+1,a+1,a+97,a+98]);}
    }
    local ground=keep(gfx.newMeshFromArrays(positions,normals,uvs,97*97,indices,indices.len()));
    // Fixed reference material: palette edits never change the light measurement.
    local reference=grassOptions.referenceGround;
    prop(ground,0.0,0.0,0.0,1.0,1.0,1.0,reference[0],reference[1],reference[2]);
    groundMesh=ground;
    local sphere=keep(gfx.newMeshSphere(16,10));
    local cube=keep(gfx.newMeshCube(1.0));
    // Overlapping low stones form an intentionally irregular winding trail.
    for(local i=0;i<115;++i) {
        local z=-17.5+i*0.31,x=pathAt(z);
        prop(sphere,x,heightAt(x,z)+0.015,z,1.22,0.055,0.46,0.67,0.54,0.32);
    }
    // A few quiet landmarks frame the grass without hiding the close foreground.
    local trees=[[-9.0,-6.0,1.0],[8.0,-10.0,1.25],[-13.0,0.0,0.8]];
    foreach(t in trees) {
        local x=t[0],z=t[1],s=t[2],y=heightAt(x,z);
        prop(cube,x,y+1.5*s,z,0.42*s,3.0*s,0.42*s,0.31,0.23,0.12);
        for(local k=0;k<5;++k) {
            local a=k*2.4;
            prop(sphere,x+cos(a)*s,y+(3.1+noise(k)*0.65)*s,z+sin(a)*0.8*s,
                1.8*s,1.45*s,1.65*s,0.32+noise(k)*0.08,0.52+noise(k)*0.12,0.14);
        }
    }
    for(local i=0;i<12;++i) {
        local z=-9.0+i*1.15,x=-4.5+sin(i*1.7)*1.7;
        if(i%3==0) prop(sphere,x,heightAt(x,z)+0.16,z,0.6,0.42,0.5,0.47,0.52,0.42);
    }
    // Pale, flattened cloud clusters sit behind the meadow.
    for(local i=0;i<4;++i) for(local k=0;k<3;++k)
        prop(sphere,-20.0+i*13.0+k*1.8,12.5+sin(i*2.0)+k*0.25,-36.0,
            3.2,0.9+k*0.1,1.3,0.88,0.94,0.89);
    local scatter=stylizedGrassScatterDefaults();
    if("grassScatter" in config)
        foreach(name,value in config.grassScatter) if(name in scatter) scatter[name]=value;
    local field=stylizedGrassBuildField(scatter,heightAt,function(x,z) {
        return fabs(x-pathAt(z))>=1.08+0.12*sin(z*3);
    });
    meadowBanks=field.banks;
    if(bankIndex<0 || bankIndex>=meadowBanks.len()) throw "coverageBank outside available banks";
    grassStyle=StylizedGrassStyle(field.records,grassOptions);
    print("STYLIZED_MEADOW_READY instanced=true roots="+meadowBanks[bankIndex].count+
        " radius="+meadowBanks[bankIndex].radius+" rare="+meadowBanks[bankIndex].variations+" cpuMask=2x4 assetFiles=0\n");
};

eve_update <- function(dt) {
    frameCount++; if(frameCount==120) print("MEADOW_FRAME_120 seconds="+seconds+" view="+view+"\n");
    local space=keyboard.isDown("space"),v=keyboard.isDown("v");
    if(space && !lastSpace) paused=!paused;
    if(v && !lastV) {view=(view+1)%3;setView();}
    lastSpace=space;lastV=v;
    local plus=keyboard.isDown("up"),minus=keyboard.isDown("down"),a=keyboard.isDown("a");
    if(plus && !lastPlus) bankIndex=bankIndex>0?bankIndex-1:0;
    if(minus && !lastMinus) bankIndex=bankIndex<meadowBanks.len()-1?bankIndex+1:bankIndex;
    if(a && !lastA) {accents=!accents;grassStyle.setRareVariations(accents);}
    lastPlus=plus;lastMinus=minus;lastA=a;
    if(!paused) seconds+=dt;
    grassStyle.setTime(seconds);
};
eve_render <- function() {
    gfx.clear();gfx.render3D();
    local bank=meadowBanks[bankIndex];
    grassStyle.draw(groundMesh,identity,bank.first,bank.count);
};
