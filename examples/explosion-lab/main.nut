// Meter-space Box3D debris; pooled procedural fire, smoke and sparks.
local cam=null, world=null, phys=null, cubeMesh=null, sphereMesh=null;
local debris=[], clouds=[], sparks=[], ring=[];
local age=-1.2, clock=0.0, accumulator=0.0, previous=false;
const PI=3.14159265;
function clamp(v,a,b) { return v<a?a:(v>b?b:v); }
function object(mesh,x,y,z,sx,sy,sz,r,g,b,unlit=false) {
    local e=eve.Renderable3D(); e.setMesh(mesh);
    e.setPosition(x.tofloat(),y.tofloat(),z.tofloat());
    e.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    e.setTint(r.tofloat(),g.tofloat(),b.tofloat(),1.0);
    e.setReceiveLight(!unlit); e.setRoughness(0.92);
    return e;
}
function explode() {
    age=0.0;
    foreach(i,d in debris) {
        local a=i*2.399963;
        local radius=0.65+(i%5)*0.32;
        local x=cos(a)*radius, z=sin(a)*radius;
        d.body.setPosition(x,0.4+(i%4)*0.5,z);
        d.body.setRotation(0.0,0.0,0.0,1.0);
        d.body.setLinearVelocity(0.0,0.0,0.0);
        d.body.setAngularVelocity(0.0,0.0,0.0);
        local force=d.body.getMass()*(5.5+(i%6)*0.75);
        d.body.applyLinearImpulse(cos(a)*force,force*(0.8+(i%3)*0.25),sin(a)*force);
        d.body.setAngularVelocity(sin(a)*5.0,cos(a)*7.0,3.0);
    }
    print("explosion-lab: blast / 36 dynamic rigid bodies\n");
}
eve_init=function() {
    gfx.setBackgroundColor(0.018,0.027,0.045,1.0);
    cam=eve.Camera3D(); cam.setEye(13.0,9.0,16.0); cam.setTarget(0.0,2.0,0.0);
    cam.setUp(0.0,1.0,0.0); cam.setFov(49.0); cam.setAmbient(0.38,0.43,0.55); cam.setActive(true);
    gfx.setDirectionalLight(-0.5,-1.0,-0.3,0.9,1.0,1.25);
    cubeMesh=gfx.newMeshCube(1.0); sphereMesh=gfx.newMeshSphere(16,10);
    phys=eve.Physics(); world=phys.newWorld3D(0.0,-9.8,0.0,true);
    local floor=world.newBody("static",0.0,-0.4,0.0);
    floor.newBoxShape(70.0,0.8,70.0,1.0,0.65,0.25);
    object(cubeMesh,0,-0.25,0,36,0.5,36,0.12,0.16,0.21);
    for(local i=-8;i<=8;i++) {
        object(cubeMesh,i*2,0.009,0,0.025,0.015,32,0.22,0.29,0.35,true);
        object(cubeMesh,0,0.01,i*2,32,0.015,0.025,0.22,0.29,0.35,true);
    }
    for(local i=0;i<64;i++) {
        local a=i*2*PI/64;
        local e=object(cubeMesh,cos(a)*3,0.025,sin(a)*3,0.22,0.025,0.06,1,0.48,0.07,true);
        e.setRotation(-a,0.0,0.0);
        local r=object(sphereMesh,0,0,0,1,1,1,1,0.55,0.14,true);
        r.setVisible(false); ring.push(r);
    }
    for(local i=0;i<36;i++) {
        local s=0.24+(i%4)*0.095;
        local body=world.newBody("dynamic",0.0,0.5+i*0.01,0.0);
        body.newBoxShape(s,s,s,2.0,0.55,0.35);
        debris.push({body=body,e=object(cubeMesh,0,0,0,s,s,s,0.38+(i%3)*0.1,0.28,0.19)});
    }
    for(local i=0;i<68;i++) {
        local e=object(sphereMesh,0,0,0,1,1,1,1,0.4,0.05,true);
        e.setVisible(false); clouds.push(e);
    }
    for(local i=0;i<100;i++) {
        local e=object(sphereMesh,0,0,0,0.04,0.04,0.04,1,0.7,0.15,true);
        e.setVisible(false); sparks.push(e);
    }
    print("explosion-lab ready: SPACE detonate; automatic repeat every 8 seconds\n");
};
eve_update=function(dt) {
    dt=clamp(dt,0.0,0.05); clock+=dt; age+=dt;
    local pressed=keyboard.isDown("SPACE");
    if((pressed&&!previous)||age>8.0||(age>=0.0&&age-dt<0.0)) explode();
    previous=pressed;
    accumulator+=dt;
    while(accumulator>=1.0/120.0) { world.update(1.0/120.0); accumulator-=1.0/120.0; }
    foreach(d in debris) {
        d.e.setPosition(d.body.getX(),d.body.getY(),d.body.getZ());
        // Convert Box3D quaternion to the renderable's yaw/pitch/roll.
        local x=d.body.getRotX(),y=d.body.getRotY(),z=d.body.getRotZ(),w=d.body.getRotW();
        d.e.setRotation(atan2(2*(w*y+x*z),1-2*(x*x+y*y)),asin(clamp(2*(w*x-y*z),-1.0,1.0)),atan2(2*(w*z+x*y),1-2*(x*x+z*z)));
    }
    local shake=age>=0.0?0.16*exp(-age*5.0):0.0;
    cam.setEye(13.0+sin(clock*89)*shake,9.0+cos(clock*71)*shake,16.0);
    local flash=age>=0.0?exp(-age*6):0.0;
    cam.setAmbient(0.38+flash*0.6,0.43+flash*0.25,0.55);
    foreach(i,e in clouds) {
        local t=age-(i%7)*0.035;
        local life=3.5+(i%5)*0.25;
        e.setVisible(t>=0.0&&t<life);
        if(t<0.0||t>=life) continue;
        local a=i*2.399963, spread=0.35+(i%6)*0.22;
        local reach=(1.0-exp(-t*3.0))*spread;
        local y=0.65+(i%4)*0.24+t*(0.65+(i%3)*0.17);
        e.setPosition(cos(a)*reach+t*0.22,y,sin(a)*reach);
        local s=(0.14+(1-exp(-t*4))*0.65+t*0.22)*(clamp((life-t)*1.5,0.0,1.0));
        e.setScale(s,s*(1.0+(i%3)*0.12),s);
        local heat=clamp(1.0-t/1.15-(i%5)*0.08,0.0,1.0);
        e.setReceiveLight(heat<0.05);
        e.setTint(0.19+heat*0.81,0.21+heat*heat*0.52,0.24*(1-heat)+heat*0.025,1.0);
    }
    foreach(i,e in sparks) {
        local t=age, life=0.65+(i%11)*0.12;
        e.setVisible(t>=0.0&&t<life);
        if(t<0.0||t>=life) continue;
        local a=i*2.399963, speed=3.0+(i%13)*0.48;
        local y=0.8+(2.0+(i%7)*0.65)*t-4.9*t*t;
        e.setPosition(cos(a)*speed*t,y,sin(a)*speed*t);
        e.setVisible(y>0.04);
        local s=0.025+0.025*(1-t/life); e.setScale(s,s*2.8,s);
        e.setTint(1.0,0.35+0.5*(1-t/life),0.06,1.0);
    }
    foreach(i,e in ring) {
        e.setVisible(age>=0.0&&age<0.65);
        if(age<0.0||age>=0.65) continue;
        local a=i*2*PI/64, r=0.3+age*14;
        e.setPosition(cos(a)*r,0.06,sin(a)*r);
        local s=0.12*(1-age/0.65); e.setScale(s*2,0.035,s);
    }
};
eve_render=function() { gfx.clear(); gfx.render3D(); };
