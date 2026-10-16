// All simulation uses seconds and a fixed 120 Hz step with a bounded catch-up.
// Target references are local table identities; dead targets are released next tick.
local disc = null, glow = null, font = null;
local clock = 0.0, accumulator = 0.0, spawnClock = 0.0;
local hero = { x = 640.0, y = 410.0 };
local orbs = [], enemies = [], sparks = [];
local kills = 0, wave = 0, shots = 0, returns = 0;
local manual = 0.0;
function min(a,b) { return a<b ? a : b; }
function max(a,b) { return a>b ? a : b; }
const TAU = 6.2831853;

function dot(x,y,r,red,green,blue,a=1.0,soft=false) {
    gfx.drawTexturedRect(soft ? glow : disc,(x-r).tofloat(),(y-r).tofloat(),r*2.0,r*2.0,red.tofloat(),green.tofloat(),blue.tofloat(),a.tofloat());
}
function ring(x,y,r,red,green,blue,a=1.0) {
    for(local i=0;i<96;i++) {
        local t=i*TAU/96;
        dot(x+cos(t)*r,y+sin(t)*r*0.62,1.2,red,green,blue,a);
    }
}
function burst(x,y) {
    for(local i=0;i<28;i++) {
        local a=i*TAU/28, speed=70+(i*37%180);
        sparks.append({x=x,y=y,vx=cos(a)*speed,vy=sin(a)*speed,life=0.65,total=0.65});
    }
}
function tick(dt) {
    clock+=dt; spawnClock-=dt; manual-=dt;
    local dx=0.0,dy=0.0;
    if(keyboard.isDown("a") || keyboard.isDown("left")) dx-=1;
    if(keyboard.isDown("d") || keyboard.isDown("right")) dx+=1;
    if(keyboard.isDown("w") || keyboard.isDown("up")) dy-=1;
    if(keyboard.isDown("s") || keyboard.isDown("down")) dy+=1;
    if(dx!=0 || dy!=0) {
        manual=5; local n=sqrt(dx*dx+dy*dy);
        hero.x+=dx/n*180*dt; hero.y+=dy/n*180*dt;
        hero.x=clamp(hero.x,230,1050); hero.y=clamp(hero.y,240,620);
    } else if(manual<=0) {
        hero.x=640+sin(clock*0.32)*110; hero.y=410+sin(clock*0.53)*48;
    }
    if(spawnClock<=0) {
        spawnClock=1.5; wave++;
        for(local j=0;j<3;j++) {
            local a=wave*2.39996+j*TAU/3;
            enemies.append({x=640+cos(a)*470,y=420+sin(a)*260,hp=2,claimed=false});
        }
    }
    foreach(e in enemies) {
        if(e.hp<=0) continue;
        local x=hero.x-e.x,y=hero.y-e.y,n=sqrt(x*x+y*y);
        if(n>45) { e.x+=x/n*27*dt; e.y+=y/n*27*dt; }
    }
    foreach(i,o in orbs) {
        o.cool-=dt;
        if(o.target!=null && o.target.hp<=0) { o.target=null; o.state=2; }
        local a=clock*1.35+i*TAU/6;
        local tx=hero.x+cos(a)*92,ty=hero.y-18+sin(a)*53;
        if(o.state==0 && o.cool<=0) {
            local best=null,range=260.0;
            foreach(e in enemies) {
                local x=e.x-hero.x,y=e.y-hero.y,n=sqrt(x*x+y*y);
                if(e.hp>0 && !e.claimed && n<range) {best=e;range=n;}
            }
            if(best!=null) {o.target=best;best.claimed=true;o.state=1;shots++;}
        }
        if(o.state==1) {tx=o.target.x;ty=o.target.y-12;}
        local x=tx-o.x,y=ty-o.y,n=sqrt(x*x+y*y);
        local speed=o.state==1 ? 440.0 : 330.0;
        if(n>1) {local step=min(n,speed*dt);o.x+=x/n*step;o.y+=y/n*step;}
        if(o.state==1 && n<17) {
            local e=o.target;e.hp--;e.claimed=false;
            burst(o.x,o.y);if(e.hp==0) kills++;
            o.target=null;o.state=2;o.cool=0.85;
        } else if(o.state==2 && n<9) {o.state=0;returns++;}
        o.trail.append({x=o.x,y=o.y});
        if(o.trail.len()>22) o.trail.remove(0);
    }
    for(local i=sparks.len()-1;i>=0;i--) {
        local s=sparks[i];s.life-=dt;s.x+=s.vx*dt;s.y+=s.vy*dt;s.vy+=90*dt;
        if(s.life<=0) sparks.remove(i);
    }
    for(local i=enemies.len()-1;i>=0;i--) if(enemies[i].hp<=0) enemies.remove(i);
}
function clamp(v,lo,hi) {return min(hi,max(lo,v));}
eve_init = function() {
    gfx.setBackgroundColor(0.018,0.027,0.045,1.0);
    disc=gfx.loadTexture("assets/disc.png");glow=gfx.loadTexture("assets/glow.png");
    font=gfx.newFont(eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",24),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.-:+");
    gfx.setFont(font);
    for(local i=0;i<6;i++) orbs.append({x=640.0,y=410.0,state=0,target=null,cool=2.0+i*0.2,trail=[]});
    print("EMBER GUARD ready: six autonomous fireballs\n");
};
eve_update = function(dt) {
    accumulator+=min(dt,0.1);
    while(accumulator>=1.0/120) {tick(1.0/120);accumulator-=1.0/120;}
};
eve_render = function() {
    gfx.clear();
    dot(640,430,540,0.06,0.19,0.23,0.65,true);
    for(local y=180;y<740;y+=42) for(local x=110;x<1200;x+=42)
        dot(x,y,1,0.14,0.24,0.28,0.45);
    ring(640,430,460,0.15,0.32,0.36,0.8);
    ring(640,430,440,0.1,0.23,0.28,0.65);
    ring(640,430,290,0.1,0.25,0.29,0.6);
    for(local i=0;i<12;i++) {
        local a=i*TAU/12;
        dot(640+cos(a)*450,430+sin(a)*279,5,0.25,0.65,0.67,0.8);
    }
    ring(hero.x,hero.y,92,0.9,0.35,0.09,0.5);
    dot(hero.x,hero.y,80,0.05,0.7,0.7,0.5,true);
    foreach(e in enemies) {
        dot(e.x,e.y+12,18,0.0,0.0,0.0,0.5);
        dot(e.x,e.y,31,0.4,0.05,0.55,0.65,true);
        dot(e.x,e.y-7,13,0.23,0.14,0.32);
        dot(e.x,e.y-16,9,0.43,0.25,0.52);
        dot(e.x-4,e.y-17,2,1,0.3,0.6);dot(e.x+4,e.y-17,2,1,0.3,0.6);
        gfx.drawSolidRect(e.x-12.0,e.y-34.0,e.hp*12.0,2.0,0.8,0.22,0.5,1.0);
    }
    // A small robed mage: shadow, boots, cloak, face, hood and luminous staff.
    dot(hero.x,hero.y+13,19,0,0,0,0.7);
    gfx.drawSolidRect(hero.x-10.0,hero.y-2.0,7.0,20.0,0.1,0.15,0.19,1.0);
    gfx.drawSolidRect(hero.x+3.0,hero.y-2.0,7.0,20.0,0.1,0.15,0.19,1.0);
    dot(hero.x,hero.y-8,17,0.08,0.38,0.43);
    gfx.drawSolidRect(hero.x-9.0,hero.y-11.0,18.0,21.0,0.1,0.48,0.53,1.0);
    dot(hero.x,hero.y-28,12,0.08,0.23,0.3);
    dot(hero.x,hero.y-26,7,0.86,0.69,0.49);
    gfx.drawSolidRect(hero.x+22.0,hero.y-34.0,3.0,47.0,0.5,0.34,0.2,1.0);
    dot(hero.x+23,hero.y-38,24,0.1,0.9,0.85,0.8,true);
    dot(hero.x+23,hero.y-38,5,0.6,1,0.95);
    foreach(o in orbs) {
        foreach(i,p in o.trail) {
            local f=(i+1.0)/o.trail.len();
            dot(p.x,p.y,3+f*8,1,0.17+f*0.3,0.025,f*0.48);
        }
        dot(o.x,o.y,45,1,0.22,0.02,0.8,true);
        dot(o.x,o.y,11+sin(clock*19)*1.5,1,0.25,0.025);
        dot(o.x,o.y-2,7,1,0.68,0.12);dot(o.x,o.y-3,3.5,1,0.96,0.7);
    }
    foreach(s in sparks) dot(s.x,s.y,2.5,1,0.45+s.life*0.5,0.1,s.life/s.total);
    gfx.print("EVENGINE / EFFECT STUDY 01",56.0,35.0,0.3,0.7,0.71,1.0,0.6);
    gfx.print("EMBER GUARD",54.0,65.0,0.96,0.9,0.77,1.0,1.65);
    gfx.print("Six flames. One guardian.",57.0,117.0,0.47,0.6,0.64,1.0,0.7);
    gfx.print("AUTO SEEK / IMPACT / RETURN",56.0,738.0,0.94,0.55,0.22,1.0,0.65);
    gfx.print("WASD / ARROWS   Move     Release to resume showcase",56.0,766.0,0.42,0.57,0.61,1.0,0.55);
    gfx.print("NEUTRALIZED  "+kills,973.0,45.0,0.95,0.63,0.29,1.0,0.7);
    gfx.print("FLAMES 06 / WAVE "+wave,973.0,74.0,0.43,0.61,0.65,1.0,0.6);
};
