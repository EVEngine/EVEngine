// Target-centred meteor shower. All renderables are pooled at startup.
local settings={targetX=0.0,targetZ=0.0,radius=6.0,count=12,
    interval=0.28,fallTime=1.65,repeatAfter=9.0};
local cam=null,cubeMesh=null,sphereMesh=null;
local meteors=[],boundary=[];
local elapsed=0.0,clock=0.0,previous=false;
// Dedicated visual RNG stream; successive casts advance it, never global rand().
local rainSeed=1979;
const PI=3.14159265;
function clamp(v,a,b) { return v<a?a:(v>b?b:v); }
function rainRandom() {
    rainSeed=(rainSeed*16807)%2147483647;
    return rainSeed.tofloat()/2147483647.0;
}
function object(mesh,x,y,z,sx,sy,sz,r,g,b,unlit=false) {
    local e=eve.Renderable3D(); e.setMesh(mesh);
    e.setPosition(x.tofloat(),y.tofloat(),z.tofloat());
    e.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    e.setTint(r.tofloat(),g.tofloat(),b.tofloat(),1.0);
    e.setReceiveLight(!unlit); e.setRoughness(0.92);
    return e;
}

function pooled(mesh,n,r,g,b) {
    local list=[];
    for(local i=0;i<n;i++) {
        local e=object(mesh,0,0,0,1,1,1,r,g,b,true);
        e.setVisible(false); list.push(e);
    }
    return list;
}
// Snapshot target and choose uniformly distributed points within its disk.
function castRain() {
    elapsed=0.0;
    foreach(i,m in meteors) {
        local a=rainRandom()*2*PI;
        local r=sqrt(rainRandom())*settings.radius;
        m.x=settings.targetX+cos(a)*r;
        m.z=settings.targetZ+sin(a)*r;
        m.delay=i*settings.interval+rainRandom()*0.12;
        m.size=0.45+rainRandom()*0.3;
        m.hit=false;
        m.meteor.setVisible(false);
        foreach(list in [m.trail,m.clouds,m.sparks,m.ring,m.debris])
            foreach(e in list) e.setVisible(false);
        if(r>settings.radius+0.0001) throw "meteor landing outside target radius";
    }
    print("meteor-rain: cast / "+settings.count+" meteors / radius "+settings.radius+"\n");
}
eve_init=function() {
    gfx.setBackgroundColor(0.018,0.027,0.045,1.0);
    cam=eve.Camera3D(); cam.setEye(23.0,20.0,29.0); cam.setTarget(0.0,4.0,0.0);
    cam.setUp(0.0,1.0,0.0); cam.setFov(49.0); cam.setAmbient(0.38,0.43,0.55); cam.setActive(true);
    gfx.setDirectionalLight(-0.5,-1.0,-0.3,0.9,1.0,1.25);
    cubeMesh=gfx.newMeshCube(1.0); sphereMesh=gfx.newMeshSphere(12,8);
    object(cubeMesh,0,-0.25,0,40,0.5,40,0.10,0.13,0.18);
    for(local i=-10;i<=10;i++) {
        object(cubeMesh,i*2,0.009,0,0.025,0.015,40,0.18,0.23,0.29,true);
        object(cubeMesh,0,0.01,i*2,40,0.015,0.025,0.18,0.23,0.29,true);
    }
    boundary=pooled(sphereMesh,96,1.0,0.3,0.04);
    for(local i=0;i<settings.count;i++) {
        meteors.push({x=0.0,z=0.0,delay=0.0,size=0.6,hit=false,
            meteor=object(sphereMesh,0,0,0,1,1,1,0.28,0.10,0.025),
            trail=pooled(sphereMesh,18,1.0,0.5,0.05),
            clouds=pooled(sphereMesh,14,1.0,0.4,0.05),
            sparks=pooled(sphereMesh,24,1.0,0.7,0.1),
            ring=pooled(sphereMesh,32,1.0,0.5,0.08),
            debris=pooled(cubeMesh,10,0.38,0.23,0.13)});
    }
    castRain();
    print("meteor-rain ready: SPACE cast, WASD move target\n");
};
function updateMeteor(m) {
    local age=elapsed-m.delay-settings.fallTime;
    local falling=elapsed>=m.delay&&age<0.0;
    m.meteor.setVisible(falling);
    if(falling) {
        m.meteor.setPosition(m.x+age*4.0,0.65-age*8.0,m.z+age*1.2);
        m.meteor.setScale(m.size,m.size*1.25,m.size);
        m.meteor.setRotation(clock*3,clock*2,clock);
    }
    if(age>=0.0&&!m.hit) {
        m.hit=true;
        print("meteor-rain: impact x="+m.x+" z="+m.z+"\n");
    }
    foreach(i,e in m.trail) {
        local t=-age+i*0.035;
        e.setVisible(elapsed>=m.delay&&t>0.0&&age<0.5);
        if(elapsed<m.delay||t<=0.0||age>=0.5) continue;
        local f=1.0-i.tofloat()/m.trail.len();
        local s=m.size*(0.12+f)*clamp(1.0-age/0.5,0.0,1.0);
        e.setPosition(m.x-t*4.0,0.65+t*8.0,m.z-t*1.2);
        e.setScale(s,s*1.4,s);
        e.setTint(1.0,0.12+f*f*0.78,0.025+f*f*f*0.55,1.0);
    }
    foreach(i,e in m.debris) {
        e.setVisible(age>=0.0&&age<4.0);
        if(age<0.0||age>=4.0) continue;
        local a=i*2.399963,vy=3.0+(i%4)*0.8;
        local landing=(vy+sqrt(vy*vy+19.6*0.5))/9.8;
        local t=age<landing?age:landing;
        local y=0.5+vy*t-4.9*t*t;
        e.setPosition(m.x+cos(a)*3.0*t,y>0.12?y:0.12,m.z+sin(a)*3.0*t);
        e.setRotation(t*(i%5+1),t*2.0,t*3.0);
        local s=(0.12+(i%3)*0.06)*clamp(4.0-age,0.0,1.0);
        e.setScale(s,s,s);
    }
    foreach(i,e in m.clouds) {
        local t=age-(i%7)*0.035;
        local life=3.5+(i%5)*0.25;
        e.setVisible(t>=0.0&&t<life);
        if(t<0.0||t>=life) continue;
        local a=i*2.399963, spread=0.35+(i%6)*0.22;
        local reach=(1.0-exp(-t*3.0))*spread;
        local y=0.65+(i%4)*0.24+t*(0.65+(i%3)*0.17);
        e.setPosition(m.x+cos(a)*reach+t*0.22,y,m.z+sin(a)*reach);
        local s=(0.14+(1-exp(-t*4))*0.65+t*0.22)*(clamp((life-t)*1.5,0.0,1.0));
        e.setScale(s,s*(1.0+(i%3)*0.12),s);
        local heat=clamp(1.0-t/1.15-(i%5)*0.08,0.0,1.0);
        e.setReceiveLight(heat<0.05);
        e.setTint(0.19+heat*0.81,0.21+heat*heat*0.52,0.24*(1-heat)+heat*0.025,1.0);
    }
    foreach(i,e in m.sparks) {
        local t=age, life=0.65+(i%11)*0.12;
        e.setVisible(t>=0.0&&t<life);
        if(t<0.0||t>=life) continue;
        local a=i*2.399963, speed=3.0+(i%13)*0.48;
        local y=0.8+(2.0+(i%7)*0.65)*t-4.9*t*t;
        e.setPosition(m.x+cos(a)*speed*t,y,m.z+sin(a)*speed*t);
        e.setVisible(y>0.04);
        local s=0.025+0.025*(1-t/life); e.setScale(s,s*2.8,s);
        e.setTint(1.0,0.35+0.5*(1-t/life),0.06,1.0);
    }
    foreach(i,e in m.ring) {
        e.setVisible(age>=0.0&&age<0.65);
        if(age<0.0||age>=0.65) continue;
        local a=i*2*PI/m.ring.len(), r=0.3+age*8;
        e.setPosition(m.x+cos(a)*r,0.06,m.z+sin(a)*r);
        local s=0.12*(1-age/0.65); e.setScale(s*2,0.035,s);
    }
}
eve_update=function(dt) {
    dt=clamp(dt,0.0,0.05); clock+=dt; elapsed+=dt;
    if(keyboard.isDown("A")) settings.targetX-=dt*5;
    if(keyboard.isDown("D")) settings.targetX+=dt*5;
    if(keyboard.isDown("W")) settings.targetZ-=dt*5;
    if(keyboard.isDown("S")) settings.targetZ+=dt*5;
    settings.targetX=clamp(settings.targetX,-7.0,7.0);
    settings.targetZ=clamp(settings.targetZ,-7.0,7.0);
    local pressed=keyboard.isDown("SPACE");
    if((pressed&&!previous)||elapsed>settings.repeatAfter) castRain();
    previous=pressed;
    foreach(i,e in boundary) {
        local a=i*2*PI/boundary.len();
        e.setVisible(true);
        e.setPosition(settings.targetX+cos(a)*settings.radius,0.04,settings.targetZ+sin(a)*settings.radius);
        e.setScale(0.065,0.025,0.065);
    }
    local flash=0.0;
    foreach(m in meteors) {
        updateMeteor(m);
        local t=elapsed-m.delay-settings.fallTime;
        if(t>=0.0) flash+=exp(-t*9);
    }
    flash=clamp(flash,0.0,1.0);
    cam.setAmbient(0.38+flash*0.35,0.43+flash*0.14,0.55);
    cam.setEye(23.0+sin(clock*89)*flash*0.12,20.0+cos(clock*71)*flash*0.12,29.0);
};
eve_render=function() { gfx.clear(); gfx.render3D(); };
