persist spiderBodyTexture=null
persist spiderAbdomenTexture=null
persist spiderBoneTexture=null
persist spiderFontOwner=null
persist spiderFont=null
function spiderTexture(abdomen) {
    local image=eve.Image();local data=image.newEmptyImageData(96,64,"RGBA8");
    for(local y=0;y<64;++y) for(local x=0;x<96;++x) {
        local u=(x-47.5)/46.0,v=(y-31.5)/30.0;
        local d=u*u+v*v;
        if(d>1.0) {data.setPixel(x,y,0.0,0.0,0.0,0.0);continue;}
        local shade=0.48+0.40*sqrt(1.0-d)-v*0.12;
        local stripe=abdomen && abs(v)<0.15+0.10*cos(u*10.0);
        local seam=!abdomen && abs(u)<0.035;
        data.setPixel(x,y,seam?0.08:(stripe?0.77:0.24)*shade,seam?0.12:(stripe?0.48:0.49)*shade,seam?0.14:(stripe?0.17:0.53)*shade,1.0);
    }
    return gfx.newTexture(data,false,false);
}
// Autonomous IK spider — eve.IK Skeleton2D + Solver2D FABRIK crawl demo.
dofile("spider.nut");

persist spider = null
persist ikMod = null
persist spiderReadyPrinted = false
persist spiderScreenshotSaved = false
persist spiderFrame = 0
persist trail = []
persist showGuides = true
persist paused = false
persist lastSpace = false
persist lastG = false
persist lastR = false

function spiderDrawArena(gfx, w, h) {
    // Floor wash.
    gfx.drawSolidRect(0.0, 0.0, w.tofloat(), h.tofloat(), 0.10, 0.12, 0.11, 1.0);
    // Subtle grid.
    local step = 48.0;
    for (local x = 0.0; x < w; x += step) {
        gfx.drawSolidRect(x, 0.0, 1.0, h.tofloat(), 0.14, 0.17, 0.15, 0.55);
    }
    for (local y = 0.0; y < h; y += step) {
        gfx.drawSolidRect(0.0, y, w.tofloat(), 1.0, 0.14, 0.17, 0.15, 0.55);
    }
    // Stone paving gives scale without pretending to affect the planar IK solver.
    for(local row=0;row<9;++row) for(local col=0;col<14;++col) {
        local x=col*84.0+(row%2)*42.0,y=row*84.0;
        local tone=0.13+((row*7+col*11)%5)*0.008;
        gfx.drawSolidRect(x+3.0,y+3.0,77.0,77.0,tone,tone+0.035,tone+0.025,1.0);
        gfx.drawSolidRect(x+8.0,y+8.0,66.0,1.0,tone+0.045,tone+0.06,tone+0.05,1.0);
    }
    // Border.
    gfx.drawSolidRect(0.0, 0.0, w.tofloat(), 4.0, 0.22, 0.28, 0.24, 1.0);
    gfx.drawSolidRect(0.0, h - 4.0, w.tofloat(), 4.0, 0.22, 0.28, 0.24, 1.0);
    gfx.drawSolidRect(0.0, 0.0, 4.0, h.tofloat(), 0.22, 0.28, 0.24, 1.0);
    gfx.drawSolidRect(w - 4.0, 0.0, 4.0, h.tofloat(), 0.22, 0.28, 0.24, 1.0);
}

function spiderDrawTrail(gfx) {
    for (local i = 0; i < trail.len(); i++) {
        local p = trail[i];
        local a = 0.12 + 0.35 * (i.tofloat() / trail.len().tofloat());
        gfx.drawSolidRect(p.x - 2.0, p.y - 2.0, 4.0, 4.0, 0.35, 0.55, 0.40, a);
    }
}

function spiderPushTrail(sp) {
    trail.push({ x = sp.x, y = sp.y });
    while (trail.len() > 96) trail.remove(0);
}

function spiderRebuild() {
    ikMod = eve.IK();
    math.setRandomSeed(20261105);
    spider = spiderMake(ikMod, config.width * 0.5, config.height * 0.55);
    spiderPickWaypoint(spider, config.width, config.height, 110.0);
    trail = [];
    spiderScreenshotSaved = false;
    spiderFrame = 0;
}

eve_init <- function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.09, 1.0);
    if(spiderBodyTexture==null) {
        spiderBodyTexture=spiderTexture(false);spiderAbdomenTexture=spiderTexture(true);
        local image=eve.Image();local pixel=image.newEmptyImageData(1,1,"RGBA8");pixel.setPixel(0,0,1.0,1.0,1.0,1.0);
        spiderBoneTexture=gfx.newTexture(pixel,false,false);
        spiderFontOwner=eve.Font();local data=spiderFontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf",20);
        spiderFont=gfx.newFont(data," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:-");
    }
    if (spider == null) spiderRebuild();
    if (!spiderReadyPrinted) {
        spiderReadyPrinted = true;
        print("IK_SPIDER_READY legs=8 gait=tetrapod solver=FABRIK solveChain space=pause g=guides r=reseed\n");
    }
};

eve_update <- function(dt) {
    if (spider == null) return;

    local spaceDown = false;
    local gDown = false;
    local rDown = false;
    if (has_module("keyboard")) {
        spaceDown = keyboard.isDown("space");
        gDown = keyboard.isDown("g") || keyboard.isDown("G");
        rDown = keyboard.isDown("r") || keyboard.isDown("R");
    }
    if (spaceDown && !lastSpace) paused = !paused;
    if (gDown && !lastG) showGuides = !showGuides;
    if (rDown && !lastR) spiderRebuild();
    lastSpace = spaceDown;
    lastG = gDown;
    lastR = rDown;

    if (!paused) {
        spiderUpdate(spider, dt, config.width.tofloat(), config.height.tofloat());
        if ((spiderFrame % 3) == 0) spiderPushTrail(spider);
    }
    spiderFrame += 1;

    // Wait until the tetrapod gait has taken several strides so the preview
    // shows planted feet, a body trail, and a non-rest pose.
    if (!spiderScreenshotSaved && spider.age > 2.8 && spider.steps >= 24) {
        if (gfx.saveFramePng("ik-spider.png")) {
            spiderScreenshotSaved = true;
            print("IK_SPIDER_SHOT steps=" + spider.steps + " age=" + spider.age + "\n");
        }
    }
};

eve_render <- function() {
    gfx.clear();
    spiderDrawArena(gfx, config.width, config.height);
    spiderDrawTrail(gfx);
    if (spider != null) {
        if (showGuides) spiderDrawGuides(spider, gfx);
        spiderDraw(spider, gfx);
        gfx.drawSolidRect(18.0,18.0,470.0,78.0,0.025,0.05,0.055,0.96);
        gfx.drawText(spiderFont,"FABRIK / EIGHT LEG LOCOMOTION",30.0,28.0,0.80,0.93,0.90,1.0,0.9);
        gfx.drawText(spiderFont,"Space pause | G foot guides | R reseed",30.0,57.0,0.60,0.76,0.73,1.0,0.7);
        gfx.drawText(spiderFont,"Steps "+spider.steps+" / "+(paused?"PAUSED":"CRAWLING"),30.0,config.height-36.0,0.91,0.72,0.38,1.0,0.8);
    }
};

eve_reload <- function() {
    spiderReadyPrinted = false;
    eve_init();
};
