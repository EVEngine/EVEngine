// Engine simulation -> RGBA frames -> disk atlas -> ordinary particles.
// Fixed seed-by-formula, fixed dt, fixed orthographic camera; no runtime solver after baking.
persist solver = null
persist surface = null
persist images = null
persist framePixels = null
persist atlas = null
persist atlasTexture = null
persist frameTexture = null
persist font = null
persist emitter = null
persist baked = 0
persist elapsed = 0.0
persist replay = 0.0
persist ready = false
persist peakAlpha = 0.0
persist bakeSeconds = 0.0
const CELL = 128;
const GRID = 6;
const FRAMES = 36;
const FPS = 18.0;
const ATLAS_PATH = "smoke-flipbook-v1.tga";

function checked(result) {
    if (!result.ok) throw result.status.summary;
    return ("value" in result) ? result.value : null;
}
function byteChar(value) { return format("%c", value.tointeger() & 255); }
function saveAtlas() {
    // Uncompressed RGBA TGA: top-left origin, 8 alpha bits, BGRA byte order.
    // writeText is byte-safe (std::string), including embedded zero bytes.
    local side = CELL*GRID;
    local header = "";
    local values = [0,0,2,0,0,0,0,0,0,0,0,0,side&255,side>>8,side&255,side>>8,32,40];
    foreach (value in values) header += byteChar(value);
    local payload = header;
    for (local y=0; y<side; ++y) {
        local row = "";
        for (local x=0; x<side; ++x) {
            row += byteChar(atlas.getPixelB(x,y)*255.0+0.5);
            row += byteChar(atlas.getPixelG(x,y)*255.0+0.5);
            row += byteChar(atlas.getPixelR(x,y)*255.0+0.5);
            row += byteChar(atlas.getPixelA(x,y)*255.0+0.5);
        }
        payload += row;
    }
    if (!fs.writeText(ATLAS_PATH,payload)) throw "Cannot write atlas";
    local metadata = "{\"schema\":\"eve.example.smoke-flipbook\",\"version\":1,\"texture\":\"smoke-flipbook-v1.tga\",\"columns\":6,\"rows\":6,\"frames\":36,\"fps\":18,\"cellSize\":128,\"seed\":37,\"fixedDt\":0.0185185185185}";
    if (!fs.writeText("smoke-flipbook-v1.json",metadata)) throw "Cannot write atlas metadata";
}
function finishBake() {
    saveAtlas();
    local loaded = images.newImageDataFromFile(ATLAS_PATH);
    if (loaded.getWidth()!=CELL*GRID || loaded.getHeight()!=CELL*GRID) throw "Atlas reload size mismatch";
    if (peakAlpha<0.02) throw "Baked smoke is empty";
    atlasTexture = gfx.newTexture(loaded,false,false);
    emitter = particles.newEmitter(16);
    emitter.setTexture(atlasTexture);
    emitter.setBlendMode("alpha");
    emitter.setFlipbook(GRID,GRID,FPS,0.0);
    emitter.setPosition(865.0,365.0);
    emitter.setEmissionRate(0.0);
    emitter.setParticleLifetime(1.99,1.99);
    emitter.setParticleSize(460.0,460.0);
    emitter.setSizes(1.0,1.0);
    emitter.setSpeed(0.0,0.0);
    emitter.setGravity(0.0,0.0);
    emitter.setColorStart(1.0,1.0,1.0,1.0);
    emitter.setColorEnd(1.0,1.0,1.0,1.0);
    emitter.start(); emitter.emit(1);
    solver=null; surface=null; atlas=null; ready=true;
    print("SMOKE_FLIPBOOK_READY frames=36 grid=6x6 fps=18 solver=released save="+fs.getSaveDirectory()+"/"+ATLAS_PATH+"\n");
}
function bakeFrame() {
    local began=clock();
    // Three solver steps per output frame: 54 Hz simulation, 18 Hz sampling.
    for (local i=0;i<3;++i) checked(solver.step(1.0/54.0,3));
    checked(surface.renderGasVolume(solver,4.0));
    checked(surface.copyToImage(framePixels));
    // Artistic envelope avoids a hard birth and ensures no visible wrap at death.
    local t=baked.tofloat()/(FRAMES-1);
    local fade=sin(t*3.14159265); fade=fade*fade;
    for(local y=0;y<CELL;++y) for(local x=0;x<CELL;++x) {
        // Two transparent guard pixels plus a soft eight-pixel border prevent atlas bleed.
        local edge=x<y?x:y;
        if(CELL-1-x<edge) edge=CELL-1-x;
        if(CELL-1-y<edge) edge=CELL-1-y;
        local margin=(edge-1)/8.0;
        if(margin<0.0) margin=0.0;
        if(margin>1.0) margin=1.0;
        local a=framePixels.getPixelA(x,y)*fade*margin;
        if(a>peakAlpha) peakAlpha=a;
        framePixels.setPixel(x,y,framePixels.getPixelR(x,y),framePixels.getPixelG(x,y),framePixels.getPixelB(x,y),a);
    }
    atlas.paste(framePixels,(baked%GRID)*CELL,(baked/GRID)*CELL,0,0,CELL,CELL);
    gfx.updateTextureFromImageData(frameTexture,framePixels);
    gfx.updateTextureFromImageData(atlasTexture,atlas);
    baked++; bakeSeconds+=clock()-began;
    if(baked==FRAMES) finishBake();
}
eve_init = function() {
    gfx.setBackgroundColor(0.035,0.045,0.065,1.0);
    if (!fs.setIdentity("smoke-flipbook",true) || !fs.setupWriteDirectory()) throw "Cannot initialize save directory";
    images=eve.Image();
    framePixels=images.newEmptyImageData(CELL,CELL,"RGBA8");
    atlas=images.newEmptyImageData(CELL*GRID,CELL*GRID,"RGBA8");
    frameTexture=gfx.newTexture(framePixels,false,false);
    atlasTexture=gfx.newTexture(atlas,false,false);
    font=gfx.newFont(eve.Font().newFontDataFromFile("fonts/DejaVuSans-Bold.ttf",22)," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789:/.-=+");
    local definition=checked(fluids.volumeDefaults());
    definition.settings.capacity=256; definition.settings.spacing=0.13;
    definition.settings.minimum=[-3.0,-2.0,-3.0]; definition.settings.maximum=[3.0,6.0,3.0];
    definition.settings.gravity=[0.0,-1.0,0.0]; definition.settings.iterations=2;
    definition.particles=[];
    local prototype=checked(fluids.volumeEmissionDefaults()).description.prototype;
    for(local i=0;i<256;++i) {
        local p=clone prototype; p.material=clone prototype.material;
        local angle=i*2.399963+37.0; local r=0.65*sqrt((i+0.5)/256.0);
        p.position=[cos(angle)*r,0.2+0.45*sin(i*1.37),sin(angle)*r];
        p.velocity=[cos(angle)*(0.3+0.2*sin(i)),0.5+0.4*cos(i*0.7),sin(angle)*0.4];
        p.material.phase=1; p.material.buoyancy=-1.3; p.material.drag=0.4;
        p.material.viscosity=0.05; p.material.cohesion=0.0; p.material.vorticity=2.0;
        p.color=[0.72,0.78,0.86,0.7]; p.life=4.0;
        definition.particles.append(p);
    }
    solver=checked(fluids.newVolumeSimulator(definition));
    surface=fluids.newSurfaceRenderer(CELL,CELL);
    checked(surface.configureProjection(true,3.4));
    surface.setCameraXYZ(0.0,1.3,8.0,0.0,1.3,0.0,45.0);
    print("SMOKE_FLIPBOOK_BAKING particles=256 fixedHz=54\n");
};
eve_update = function(dt) {
    elapsed+=dt;
    if(!ready) { bakeFrame(); return; }
    particles.update(dt); replay+=dt;
    if(replay>=2.3) { replay=0.0; emitter.emit(1); }
};
function label(text,x,y,scale=0.7) { gfx.drawText(font,text,x,y,0.80,0.86,0.92,1.0,scale); }
function checker(x,y,w,h) {
    for(local j=0;j<h;j+=24) for(local i=0;i<w;i+=24) {
        local c=((i/24+j/24)%2)==0?0.09:0.13;
        gfx.drawSolidRect((x+i).tofloat(),(y+j).tofloat(),24.0,24.0,c,c+0.015,c+0.03,1.0);
    }
}
eve_render = function() {
    gfx.clear();
    label("SMOKE / FROM SIMULATION TO SPRITES",36.0,26.0,1.0);
    label("01  GAS SOLVER    /    02  RGBA BAKE    /    03  DISK ATLAS    /    04  PARTICLE PLAYBACK",36.0,69.0,0.65);
    checker(36,142,528,528); checker(612,142,504,528);
    gfx.drawTexturedRect(atlasTexture,36.0,142.0,528.0,528.0,1.0,1.0,1.0,1.0);
    for(local i=0;i<=GRID;++i) {
        gfx.drawSolidRect(36.0+i*88.0,142.0,1.0,528.0,0.22,0.30,0.36,1.0);
        gfx.drawSolidRect(36.0,142.0+i*88.0,528.0,1.0,0.22,0.30,0.36,1.0);
    }
    if(ready) particles.render(gfx);
    else gfx.drawTexturedRect(frameTexture,635.0,135.0,460.0,460.0,1.0,1.0,1.0,1.0);
    label("6 x 6 ATLAS / 128 PX PER CELL",36.0,108.0);
    label(ready?"RELOADED TEXTURE / ALPHA PARTICLE":"LIVE BAKE / FIXED CAMERA",612.0,108.0);
    label(format("FRAMES %02d / 36    BAKE %.2f SEC",baked,bakeSeconds),36.0,695.0);
    label(ready?"SOLVER RELEASED / REPLAY EVERY 2.3 SEC":"BAKING ONE FRAME PER UPDATE",612.0,695.0,0.62);
};
