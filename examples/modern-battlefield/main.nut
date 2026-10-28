import { initAssets, shape, createBattlefieldModel, poseAsset } from "./assets.nut"

local camera=null, assets=[], guns=[], aircraft=[], movers=[];
local bombs=[], bombIndex=0;
local shots=[], impacts=[], clock=0.0, shotIndex=0, impactIndex=0;
local paused=false, orbit=false, lastSpace=false, lastP=false, lastC=false;
local nextBarrage=0.7, barrageIndex=0, nextReport=8.0;
local fixedStep=1.0/60.0, accumulator=0.0;
const PI=3.14159265;
function add(kind,x,z,y=0.0) {
    local a=createBattlefieldModel(kind,x,y,z); assets.push(a); poseAsset(a); return a;
}
function impact(x,z,strength=1.0) {
    local p=impacts[impactIndex]; impactIndex=(impactIndex+1)%impacts.len();
    p.x=x; p.z=z; p.age=0.0; p.strength=strength;
}
function fire(a) {
    poseAsset(a);
    local yaw=a.yaw+a.turret, el=a.elevation;
    local dx=sin(yaw)*cos(el), dy=sin(el), dz=cos(yaw)*cos(el);
    local p=shots[shotIndex]; shotIndex=(shotIndex+1)%shots.len();
    p.x=a.muzzle.x; p.y=a.muzzle.y; p.z=a.muzzle.z;
    // Deliberately compressed visual ballistics, not a real weapon simulation.
    local speed=a.kind=="machine-gun"?25.0:(a.kind=="howitzer"?33.0:(a.kind=="heavy-howitzer"?31.0:24.0));
    p.power=a.kind=="machine-gun"?0.16:1.0;
    p.vx=dx*speed; p.vy=dy*speed; p.vz=dz*speed; p.age=0.0;
    a.recoil=0.28; a.flashAge=0.0;
    a.flash.setPosition(p.x,p.y,p.z); a.flash.setVisible(true);
    if(a.kind=="howitzer"||a.kind=="heavy-howitzer") print("battery fire t="+clock+" "+a.kind+"\n");
    if(!a.logged) { print("fire verified: "+a.kind+"\n"); a.logged=true; }
}
eve_init=function() {
    initAssets();
    // This simple material library uses the explicit ForwardPlus lighting path.
    if(!gfx.getRenderControl().setLightingMode("forwardPlus"))
        throw "modern-battlefield requires ForwardPlus lighting";
    gfx.setBackgroundColor(0.32,0.39,0.42,1.0);
    camera=eve.Camera3D(); camera.setEye(46.0,40.0,59.0); camera.setTarget(0.0,3.0,0.0);
    camera.setUp(0.0,1.0,0.0); camera.setFov(51.0); camera.setClipPlanes(0.1,350.0);
    camera.setAmbient(0.34,0.38,0.41); camera.setActive(true);
    gfx.setDirectionalLight(-0.6,-1.0,0.25,0.96,0.88,0.72);
    // Ground, muddy service road, trenches and churned earth. All deterministic.
    shape(0,-0.35,0,110,0.6,110,[0.28,0.29,0.19]);
    shape(0,-0.025,0,9,0.04,98,[0.22,0.21,0.17]);
    foreach(x in [-2.7,2.7]) shape(x,0.005,0,0.45,0.03,98,[0.16,0.16,0.13]);
    for(local i=0;i<95;i++) {
        local x=sin(i*73.13)*47,z=cos(i*19.7)*44;
        shape(x,-0.02,z,0.6+(i%5)*0.4,0.14,0.8+(i%7)*0.4,[0.22,0.23,0.16],true);
    }
    foreach(x in [-18,18]) {
        shape(x,0.015,-3,1.7,0.04,26,[0.12,0.13,0.09]);
        shape(x-1.1,0.22,-3,0.8,0.4,27,[0.34,0.31,0.21]);
    }
    // Defensive perimeter and supplies: every requested prop appears in context.
    for(local i=0;i<7;i++) {
        local x=-30+i*10;
        if(x==0) continue; // A real breach leaves the advancing convoy a clear corridor.
        add("hedgehog",x,21);
        add("barbed-wire",x,25);
        add("concrete-post",x,18);
    }
    foreach(x in [-25,-10,12,27]) add("sandbags",x,10);
    for(local i=0;i<6;i++) add("wooden-crate",-29+(i%3)*1.4,-10-(i/3)*1.2);
    for(local i=0;i<4;i++) add("ammunition-box",20+i*1.4,-9);
    add("medical-box",-27,-7); add("medical-box",-25.5,-7);
    add("bunker",-25,-1); add("bunker",25,-1);
    add("machine-gun-position",-12,7); add("machine-gun-position",12,7);
    add("artillery-position",-13,-15); add("artillery-position",15,-17);
    local types=["machine-gun","anti-tank-gun","mortar","howitzer","heavy-howitzer",
        "tank","armored-car","self-propelled-artillery","self-propelled-mortar","tank-destroyer"];
    local positions=[[-12,7],[12,7],[-22,-6],[-13,-15],[15,-17],[-3,-4],[3,-13],[-9,-27],[8,-25],[21,-11]];
    foreach(i,kind in types) {
        local a=add(kind,positions[i][0],positions[i][1]);
        a.restElevation <- a.elevation;
        a.nextFire <- i==3?0.8:(i==4?3.8:1.4+i*0.61);
        a.reload <- i==0?0.3:(i==3||i==4?6.0:4.8+(i%3)*0.7); a.flashAge <- 10.0; a.logged <- false;
        a.flash <- shape(0,0,0,0.45,0.45,0.9,[1.0,0.66,0.12],true,true);
        a.flash.setVisible(false); guns.push(a);
        if(i>=5) {
            local e=shape(0,0,0,1,1,1,[0.37,0.33,0.23],true); e.setVisible(false);
            movers.push({a=a,home=a.z,lane=a.x,phase=i.tofloat(),speed=i==5?1.15:(i==6?1.05:0.65),dust=e});
        }
    }
    local airTypes=["helicopter","attack-aircraft","bomber","medium-fighter"];
    foreach(i,kind in airTypes) {
        local a=add(kind,-20+i*12,-12+i*6,12+i*3.5);
        a.bank <- 0.0;
        // Long straight passes; semicircle turn centers lie outside the battlefield.
        aircraft.push({a=a,offset=28.0+i*22.0,alt=11+i*3.1,
            speed=i==0?5.5:(i==1?19.0:(i==2?15.0:24.0)),
            length=i==0?126.0:180.0,radius=i==0?24.0:(i==2?38.0:30.0),
            heading=i==0?0.12:(i==1?0.32:(i==2?-0.18:-0.42)),
            centerZ=i==0?16.0:24.0,turning=false});
    }
    // Five additional attack aircraft share one route and maintain a V formation.
    foreach(i,slot in [[0,0],[-14,12],[14,12],[-28,24],[28,24]]) {
        local a=add("attack-aircraft",-100,20,19.0); a.bank <- 0.0;
        aircraft.push({a=a,offset=95.0,alt=19.0+slot[1]*0.035,speed=28.0,
            length=360.0,radius=85.0,heading=0.0,centerZ=105.0,turning=false,
            formation=i,lateral=slot[0].tofloat(),trail=slot[1].tofloat(),
            released=0,nextDrop=0.0,pass=-1});
    }
    for(local i=0;i<24;i++) {
        local e=shape(0,0,0,0.22,0.22,0.75,[0.13,0.15,0.12],true); e.setVisible(false);
        bombs.push({e=e,x=0.0,y=0.0,z=0.0,vx=0.0,vz=0.0,vy=0.0,active=false});
    }
    for(local i=0;i<48;i++) {
        local e=shape(0,0,0,0.09,0.09,0.5,[1.0,0.68,0.14],true,true); e.setVisible(false);
        shots.push({e=e,x=0.0,y=0.0,z=0.0,vx=0.0,vy=0.0,vz=0.0,power=1.0,age=99.0});
    }
    // Scorched craters, rubble and distant ridgelines anchor the battle in terrain.
    for(local i=0;i<18;i++) {
        local x=sin(i*7.17)*32,z=27+cos(i*4.3)*10;
        shape(x,0.01,z,2.3+(i%3),0.1,1.8+(i%4)*0.3,[0.12,0.13,0.10],true);
        for(local j=0;j<3;j++) shape(x+cos(j*2.1)*1.6,0.12,z+sin(j*2.1)*1.4,0.35,0.25,0.5,[0.34,0.32,0.25]);
    }
    for(local i=0;i<15;i++) {
        local x=-62+i*9;
        shape(x,1.2+(i%4)*0.5,-53,15,5+(i%4)*2,13,[0.25,0.29,0.24],true);
    }
    for(local i=0;i<28;i++) {
        local clouds=[];
        for(local j=0;j<9;j++) {
            local e=shape(0,0,0,1,1,1,[0.3,0.28,0.22],true); e.setVisible(false); clouds.push(e);
        }
        impacts.push({x=0.0,z=0.0,age=99.0,strength=1.0,clouds=clouds});
    }
    impact(-19,29); impact(17,32); impact(4,36);
    print("modern-battlefield ready: 24 original asset types / 10 articulated weapons / 9 aircraft (five-ship strike formation)\n");
    print("SPACE: volley; P: pause simulation; C: orbit camera; 1: overview; 2: gun line; 3: air assets\n");
};
function simulate(dt) {
    clock+=dt;
    // Advance, halt to engage, then withdraw in reverse along the same clear lane.
    // A 100-second continuous triangle route avoids teleporting or hull snapping.
    foreach(m in movers) {
        local t=clock%100.0, travel=t<40?t:(t<50?40.0:(t<90?90.0-t:0.0));
        m.a.z=m.home+travel*m.speed;
        local moving=t<40||(t>=50&&t<90);
        m.dust.setVisible(moving);
        m.dust.setPosition(m.a.x+sin(clock*2+m.phase)*0.35,0.25,m.a.z-3.4);
        local puff=0.7+0.25*sin(clock*4+m.phase);
        m.dust.setScale(puff*1.6,puff*0.5,puff*2.0);
    }
    // Scripted incoming shells land beyond the forward defensive belt.
    if(clock>=nextBarrage) {
        local n=barrageIndex++;
        impact(sin(n*2.3999)*29,28+cos(n*1.7)*8);
        nextBarrage=clock+1.15;
    }
    if(clock>=nextReport) {
        print("battle timeline t="+clock+" tank-z="+movers[0].a.z+" battery-A="+guns[3].nextFire+" battery-B="+guns[4].nextFire+" rotor="+aircraft[0].a.rotor+"\n");
        foreach(f in aircraft) print("flight "+f.a.kind+" x="+f.a.x+" z="+f.a.z+" yaw="+f.a.yaw+" bank="+f.a.bank+" turn="+f.turning+"\n");
        nextReport+=8.0;
    }
    foreach(a in guns) {
        a.turret=sin(clock*0.23+a.x*0.05)*0.38;
        a.elevation=a.restElevation+sin(clock*0.35+a.z)*0.09;
        a.recoil*=exp(-dt*12); a.flashAge+=dt;
        a.flash.setVisible(a.flashAge<0.09);
        if(clock>=a.nextFire) {
            fire(a); a.nextFire+=a.reload;
        }
    }
    foreach(f in aircraft) {
        local length=f.length, radius=f.radius;
        local distance=(clock*f.speed+f.offset)%(2*length+2*PI*radius);
        local u=0.0,v=0.0,du=0.0,dv=0.0;
        f.turning=false;
        if(distance<length) {
            u=-length*0.5+distance; v=-radius; du=1.0;
        } else if(distance<length+PI*radius) {
            local angle=-PI*0.5+(distance-length)/radius;
            u=length*0.5+cos(angle)*radius; v=sin(angle)*radius;
            du=-sin(angle); dv=cos(angle); f.turning=true;
        } else if(distance<2*length+PI*radius) {
            u=length*0.5-(distance-length-PI*radius); v=radius; du=-1.0;
        } else {
            local angle=PI*0.5+(distance-2*length-PI*radius)/radius;
            u=-length*0.5+cos(angle)*radius; v=sin(angle)*radius;
            du=-sin(angle); dv=cos(angle); f.turning=true;
        }
        if("formation" in f) {
            local lap=((clock*f.speed+f.offset)/(2*length+2*PI*radius)).tointeger();
            if(lap!=f.pass) { f.pass=lap; f.released=0; }
            // Wingmen remain behind and beside the leader, including during turns.
            u+=-du*f.trail-dv*f.lateral; v+=-dv*f.trail+du*f.lateral;
        }
        local c=cos(f.heading),s=sin(f.heading);
        f.a.x=u*c-v*s; f.a.z=u*s+v*c+f.centerZ; f.a.y=f.alt;
        // Nose follows the velocity tangent. Roll is separate from all rotor joints.
        f.a.yaw=atan2(du*c-dv*s,du*s+dv*c);
        local bank=f.turning?atan2(f.speed*f.speed,9.8*radius):0.0;
        if(bank>0.55) bank=0.55;
        f.a.bank+=(bank-f.a.bank)*(1-exp(-dt*3.0));
        f.a.rotor=clock*29;
        if(("formation" in f)&&!f.turning&&du>0&&f.a.x>=-38&&f.a.x<35&&f.released<3&&clock>=f.nextDrop) {
            local b=bombs[bombIndex]; bombIndex=(bombIndex+1)%bombs.len();
            b.x=f.a.x; b.y=f.a.y-0.8; b.z=f.a.z; b.vx=f.speed; b.vz=0.0; b.vy=-1.0;
            b.active=true; b.e.setVisible(true); b.e.setPosition(b.x,b.y,b.z);
            f.released++; f.nextDrop=clock+0.22;
            print("formation bomb t="+clock+" aircraft="+f.formation+" round="+f.released+"\n");
        }
    }
    foreach(a in assets) if(a.armed||a.air) poseAsset(a);
    foreach(p in shots) {
        if(p.age>8.0) { p.e.setVisible(false); continue; }
        p.age+=dt; p.vy-=9.8*dt;
        p.x+=p.vx*dt; p.y+=p.vy*dt; p.z+=p.vz*dt;
        if(p.y<=0.05) { impact(p.x,p.z,p.power); p.age=99.0; p.e.setVisible(false); continue; }
        p.e.setVisible(true); p.e.setPosition(p.x,p.y,p.z);
        p.e.setRotation(atan2(p.vx,p.vz),-atan2(p.vy,sqrt(p.vx*p.vx+p.vz*p.vz)),0.0);
    }
    foreach(b in bombs) {
        if(!b.active) continue;
        b.vy-=9.8*dt; b.x+=b.vx*dt; b.z+=b.vz*dt; b.y+=b.vy*dt;
        if(b.y<=0.08) {
            impact(b.x,b.z,1.35); b.active=false; b.e.setVisible(false);
            print("bomb impact t="+clock+" x="+b.x+" z="+b.z+"\n");
        } else {
            b.e.setPosition(b.x,b.y,b.z);
            b.e.setRotation(PI*0.5,-atan2(b.vy,b.vx),0.0);
        }
    }
    foreach(p in impacts) {
        p.age+=dt;
        foreach(j,e in p.clouds) {
            local t=p.age, power=sqrt(p.strength), life=5.5*power;
            e.setVisible(t<life);
            if(t>=life) continue;
            local ang=j*2.3999, r=sqrt(t)*(0.45+(j%3)*0.52)*power;
            e.setPosition(p.x+cos(ang)*r,0.25+t*(0.9+(j%4)*0.28)*power,p.z+sin(ang)*r);
            local s=(0.45+t*0.75)*(1-t/life)*power; e.setScale(s*1.5,s*1.7,s*1.5);
            local heat=t<0.55?1-t/0.55:0.0;
            e.setReceiveLight(heat==0.0); e.setTint(0.26+heat*0.74,0.24+heat*0.12,0.18*(1-heat),1.0);
        }
    }
}
eve_update=function(dt) {
    local s=keyboard.isDown("SPACE"), p=keyboard.isDown("P"), c=keyboard.isDown("C");
    if(p&&!lastP) paused=!paused;
    if(c&&!lastC) orbit=!orbit;
    if(s&&!lastSpace) foreach(a in guns) fire(a);
    lastSpace=s; lastP=p; lastC=c;
    if(!paused) {
        // Fixed simulation step; bounded catch-up prevents a stalled frame spiral.
        accumulator+=dt>0.1?0.1:dt;
        while(accumulator>=fixedStep) { simulate(fixedStep); accumulator-=fixedStep; }
    }
    if(orbit) { camera.setEye(sin(clock*0.07)*61,37.0,cos(clock*0.07)*61); camera.setTarget(0,2,0); }
    if(keyboard.isDown("1")) { orbit=false; camera.setEye(46,40,59); camera.setTarget(0,3,0); }
    if(keyboard.isDown("2")) { orbit=false; camera.setEye(29,16,26); camera.setTarget(0,1,-6); }
    if(keyboard.isDown("3")) { orbit=false; camera.setEye(39,26,37); camera.setTarget(0,13,-4); }
};
eve_render=function() { gfx.clear(); gfx.render3D(); };
