// Main-thread presentation. Each circle/mountain is one quad, with masks loaded once.
class SkiRenderer {
    fontOwner = null;
    font = null;
    circle = null;
    triangle = null;
    white = null;
    drawCalls = 0;

    constructor() {
        fontOwner = eve.Font();
        font = gfx.newFont(fontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf",24),
            " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/:+.-");
        circle = gfx.newTextureFromFile("assets/disc.png");
        triangle = gfx.newTextureFromFile("assets/peak.png");
        white = gfx.newTextureFromFile("assets/white.png");
        if (circle == null || triangle == null || white == null) throw "ski-adventure: missing geometry masks";
        gfx.setFont(font);
    }
    function rect(x,y,w,h,r,g,b,a=1.0) {
        if (w<=0 || h<=0 || x+w<0 || x>1280 || y+h<0 || y>720) return;
        gfx.drawSolidRect(x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),r.tofloat(),g.tofloat(),b.tofloat(),a.tofloat());
        drawCalls += 1;
    }
    function disc(x,y,radius,r,g,b,a=1.0) {
        if (x+radius<0 || x-radius>1280 || y+radius<0 || y-radius>720) return;
        gfx.drawTexturedRect(circle,(x-radius).tofloat(),(y-radius).tofloat(),radius*2.0,radius*2.0,
            r.tofloat(),g.tofloat(),b.tofloat(),a.tofloat());
        drawCalls += 1;
    }
    function peak(x,y,w,h,r,g,b) {
        if (x+w<0 || x-w>1280) return;
        gfx.drawTexturedRect(triangle,(x-w).tofloat(),y.tofloat(),w*2.0,h.tofloat(),r.tofloat(),g.tofloat(),b.tofloat(),1.0);
        drawCalls += 1;
    }
    function line(x1,y1,x2,y2,width,r,g,b,a=1.0) {
        local dx=x2-x1,dy=y2-y1;
        gfx.drawTexturedRectRotated(white,(x1+x2)*0.5,(y1+y2)*0.5,sqrt(dx*dx+dy*dy),width.tofloat(),
            atan2(dy,dx)*180.0/SKI_PI,r.tofloat(),g.tofloat(),b.tofloat(),a.tofloat());
        drawCalls += 1;
    }
    function label(text,x,y,scale=1.0,r=0.13,g=0.24,b=0.34) {
        gfx.drawText(font,text,x.tofloat(),y.tofloat(),r,g,b,1.0,scale);
        drawCalls += 1;
    }
    function skierLine(px,py,x1,y1,x2,y2,angle,width,r,g,b) {
        local c=cos(angle),s=sin(angle);
        line(px+x1*c-y1*s,py+x1*s+y1*c,px+x2*c-y2*s,py+x2*s+y2*c,width,r,g,b);
    }
    function drawSkier(game) {
        local py=game.y-game.cameraY-18.0;
        local angle=game.poseAngle;
        local c=cos(angle),s=sin(angle),crouch=game.controls.tuck&&game.grounded?8.0:0.0;
        disc(310,game.course.height(game.x)-game.cameraY+5,18.0,0.48,0.66,0.76,0.3);
        if (game.invincible>0.0) disc(310,py,35.0,1.0,0.7,0.24,0.18);
        local parts=[[-8,6,8,0.17,0.24,0.32],[3,0,9,0.17,0.24,0.32],
            [0,-13+crouch,13,0.95,0.3,0.2],[4,-29+crouch,10,1.0,0.8,0.62],
            [5,-35+crouch,10,0.14,0.28,0.37],[-20,-17+crouch,7,1.0,0.63,0.19],[-31,-20+crouch,6,1.0,0.63,0.19]];
        foreach (p in parts) disc(310+p[0]*c-p[1]*s,py+p[0]*s+p[1]*c,p[2].tofloat(),p[3],p[4],p[5]);
        skierLine(310,py,-38,23,36,23,angle,4,0.13,0.25,0.32);
        skierLine(310,py,36,23,43,17,angle,4,0.13,0.25,0.32);
        skierLine(310,py,-30,29,43,29,angle,3,0.93,0.39,0.19);
        skierLine(310,py,3,-9+crouch,20,-3+crouch,angle,6,0.94,0.3,0.2);
        skierLine(310,py,20,-3+crouch,30,26,angle,2,0.28,0.41,0.46);
    }
    function draw(game) {
        drawCalls=0;
        local course=game.course,offset=game.x-310.0;
        local palettes=[[0.67,0.82,0.9],[0.51,0.73,0.81],[0.29,0.40,0.58]],sky=palettes[game.stage];
        gfx.clear(); rect(0,0,1280,720,sky[0],sky[1],sky[2]);
        disc(1030,160,48.0,1.0,game.stage==2?0.72:0.94,0.76);
        for (local layer=0;layer<3;++layer) for (local i=-1;i<6;++i) {
            local px=i*330.0-(game.x*(0.04+layer*0.04)%330.0),py=170.0+layer*65.0+(i%2)*45.0;
            peak(px,py,245.0,400.0,sky[0]-0.19+layer*0.10,sky[1]-0.17+layer*0.09,sky[2]-0.10+layer*0.06);
            peak(px,py,52.0,85.0,0.92,0.96,0.98);
        }
        for (local sx=0.0;sx<1280.0;sx+=5.0) {
            local py=course.height(offset+sx)-game.cameraY;
            rect(sx,skiMax(0.0,py),5.0,720.0-skiMax(0.0,py),0.94,0.98,1.0);
            rect(sx,py,5.0,3.0,0.75,0.87,0.93);
        }
        foreach (trail in game.trails) {
            local px=trail.x-offset,py=trail.y-game.cameraY+4.0;
            line(px-7,py-7*sin(trail.angle),px+7,py+7*sin(trail.angle),2,0.7,0.83,0.9);
        }
        foreach (item in game.obstacles) {
            local px=item.x-offset;
            if (item.collected || px < -100 || px>1380) continue;
            local py=course.height(item.x)-game.cameraY;
            if (item.kind=="ramp") {
                local lip=item.x+20.0-offset,top=course.height(item.x+20.0)-game.cameraY;
                line(lip,top,lip,top-44,3,0.28,0.42,0.47); peak(lip+14,top-44,14.0,18.0,1.0,0.48,0.2);
            } else if (item.hit) {
                disc(px-12,py-4,10.0,0.4,0.5,0.55); disc(px+10,py-6,8.0,0.4,0.5,0.55);
            } else if (item.kind=="tree") {
                rect(px-5,py-32,10,35,0.36,0.31,0.29);
                peak(px,py-124,38.0,68.0,0.12,0.35,0.37); peak(px,py-90,48.0,65.0,0.16,0.43,0.43);
                peak(px,py-124,18.0,32.0,0.94,0.98,1.0);
            } else if (item.kind=="rock") {
                disc(px,py-15,29.0,0.36,0.46,0.53); rect(px-21,py-38,38,9,0.9,0.96,0.99);
            } else { disc(px,py-item.height,10.0,1.0,0.72,0.19); disc(px,py-item.height,5.0,1.0,0.9,0.48); }
        }
        local finishX=course.finish-offset;
        if (finishX<1400 && finishX>-100) {
            local py=course.height(course.finish)-game.cameraY;
            line(finishX,py,finishX,py-170,8,0.14,0.3,0.38); rect(finishX-105,py-170,105,40,0.94,0.36,0.2);
            label("FINISH",finishX-99,py-165,0.7,1.0,1.0,1.0);
        }
        foreach (p in game.particles) disc(p.x-offset,p.y-game.cameraY,3.5,1.0,p.gold?0.78:1.0,p.gold?0.24:1.0,p.life/0.65);
        drawSkier(game);
        // The cloud front reaches the player's world X at exactly zero gap.
        local front=310.0-game.avalanche;
        for (local i=0;i<8;++i) disc(front-90-i*44.0,course.baseHeight(game.x-game.avalanche)-game.cameraY-25+(i%3)*45.0,
            90.0,0.8,0.89,0.94,0.94);
        for (local i=0;i<45;++i) {
            local px=(i*179.0-game.x*0.28)%1280.0; if (px<0) px+=1280.0;
            rect(px,(i*97.0+game.elapsed*28.0)%720.0,3,3,1.0,1.0,1.0,0.65);
        }
        drawHud(game);
    }
    function drawHud(game) {
        local meters=(skiMax(0.0,game.x)/game.course.pixelsPerMeter).tointeger();
        rect(28,24,1224,87,0.96,0.99,1.0,0.93);
        label("WHITE HORIZON",48,36,1.0); label("SKI ADVENTURE",49,73,0.55);
        label(format("%05d M",meters),380,42,1.3);
        label("COINS "+game.coins+"   FLIPS "+game.flips,640,43,0.85);
        label("DESCENT "+skiMax(0,((game.y-game.course.baseHeight(0))/30).tointeger())+" M",640,78,0.55);
        label("AVALANCHE GAP",1000,37,0.55); rect(1000,66,220,10,0.81,0.87,0.9);
        rect(1000,66,skiMax(0.0,game.avalanche/920.0)*220.0,10,0.96,0.44,0.26);
        rect(28,116,1224,4,0.8,0.88,0.92); rect(28,116,skiMin(1.0,game.x/game.course.finish)*1224.0,4,0.96,0.51,0.23);
        label(game.course.stages[game.stage],38,132,0.6); label(format("%03d KM/H",(game.speed*0.12).tointeger()),1070,132,0.7);
        rect(20,663,1240,38,0.96,0.99,1.0,0.88);
        label("SPACE Jump / Hold Flip    DOWN Tuck    LEFT Brake    P Pause    R Restart",38,674,0.64);
        if (game.messageTime>0 && game.phase=="riding") label(game.message,355,155,0.8);
        if (game.phase=="riding") return;
        local finished=game.phase=="won"||game.phase=="lost";
        rect(245,215,790,320,0.97,0.99,1.0,0.96);
        local heading=game.phase=="won"?"YOU MADE IT":(game.phase=="lost"?"CAUGHT BY THE SNOW":
            (game.phase=="paused"?"TAKE A BREATH":"WHITE HORIZON"));
        label(heading,290,245,1.45);
        if (finished) {
            label("Score "+game.score()+" / Best "+game.bestScore,295,305,0.9);
            label("Coins "+game.coins+" / Flips "+game.flips+" / Crashes "+game.crashes,295,349,0.75);
            local medals=(game.phase=="won"?1:0)+(game.coins>=40?1:0)+(game.flips>=3?1:0);
            label("TRAIL MEDALS "+medals+" / 3",295,397,0.8);
        } else {
            label("Ride the slope. Let gravity do the work.",295,305,0.85);
            label("Release SPACE to align your skis before landing.",295,351,0.7);
            label("GOALS  Finish / 40 coins / 3 backflips",295,397,0.7);
        }
        label(game.phase=="paused"?"PRESS P TO RESUME":"PRESS SPACE TO RIDE",295,470,1.0,0.87,0.29,0.13);
    }
}
