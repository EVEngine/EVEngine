// NEON R: generated road network + Vehicle wheel mobility + Box2D collision.
const PI = 3.141592653589793;
const RADIUS = 48.0;
const ROAD_SCALE = 3.0;
const STEP = 0.016666666666667;
local vehicles = eve.Vehicle();
local models = eve.Model3D();
local physicsOwner = eve.Physics();
local camera = null;
local world = null;
local car = null;
local carParts = [];
local sceneParts = [];
local materials = [];
local modelData = null;
local cubeMesh = null;
local fontOwner = null;
local hudFont = null;
local previousKeys = {};
local accumulator = 0.0;
local nextCheckpoint = 1;
local lapStart = 0.0;
local cameraX = -14.0;
local cameraZ = -58.0;
local overview = false;
local assisted = false;
local agentThrottle = 0.0;
local agentSteer = 0.0;
local agentBrake = 0.0;
local countdown = 3.0;
local gateParts = [];
local checkpointMaterial = null;
race <- { tick=0, x=0.0, z=-48.0, heading=0.0, speed=0.0, lap=0,
          checkpoint=1, time=0.0, lastLap=0.0, bestLap=0.0, offTrack=false, ready=false };

function required(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return "value" in result ? result.value : null;
}
function clampf(v,lo,hi) { return v<lo?lo:(v>hi?hi:v); }
function pressed(key) {
    local down=keyboard.isDown(key), old=key in previousKeys?previousKeys[key]:false;
    previousKeys[key] <- down; return down&&!old;
}
function material(r,g,b,metal=0.0,unlit=false) {
    local m=gfx.newMaterial();m.setTint(r,g,b,1.0);m.setMetallic(metal);m.setRoughness(0.65);
    if(unlit) {m.setShadingModel("unlit");m.setReceiveLight(false);}
    materials.append(m);return m;
}
function box(x,y,z,sx,sy,sz,m,yaw=0.0) {
    local e=eve.Renderable3D();e.setMesh(cubeMesh);e.setMaterial(m);
    e.setPosition(x,y,z);e.setScale(sx,sy,sz);e.setRotation(yaw,0.0,0.0);
    sceneParts.append(e);return e;
}
function collisionBox(x,z,w,d,angle=0.0) {
    local b=world.newBody("static",x,z);b.setAngle(angle);
    b.newRectangleFixture(w,d,1.0,0.3,0.05);
}
function buildRoad() {
    local p=required(procgen.newParams(),"road parameters");
    required(p.setSeed(17),"road seed");required(p.setString("scene","roundabout"),"road setString");required(p.setFloat("span",80.0),"road setFloat");
    required(p.setInt("lanes",2),"road setInt");required(p.setInt("pathSegments",24),"road setInt");required(p.setBool("navigation",false),"road setBool");
    required(p.setBool("placements",false),"road setBool");required(p.setBool("piers",false),"road setBool");required(p.setBool("markings",true),"road setBool");
    local cpu=required(procgen.buildMesh("mesh.roadNetwork",p),"road bake");
    for(local i=0;i<cpu.getGroupCount();++i) {
        local group=cpu.copyGroup(i);if(group==null||group.empty())continue;
        local name=cpu.getGroupName(i);
        local m=name=="asphalt"?material(0.10,0.13,0.17):
            (name=="markingYellow"?material(1.0,0.68,0.05,0.0,true):
            (name=="marking"?material(0.85,0.92,0.95,0.0,true):material(0.26,0.32,0.38)));
        local e=eve.Renderable3D();e.setMesh(required(procgen.uploadMesh(group,gfx),"road upload"));
        e.setMaterial(m);e.setScale(ROAD_SCALE,ROAD_SCALE,ROAD_SCALE);e.setReceiveShadow(true);
        sceneParts.append(e);
    }
    print("NEON_RACING_ROAD groups="+cpu.getGroupCount()+" vertices="+cpu.getVertexCount()+"\n");
}
function checkpointPosition(index) {
    local a=-PI*0.5+index*PI/6.0;return {x=cos(a)*RADIUS,z=sin(a)*RADIUS,a=a};
}
function moveCheckpoint() {
    local p=checkpointPosition(nextCheckpoint%12);
    for(local i=0;i<2;++i) {
        local offset=i==0?-7.0:7.0;
        gateParts[i].setPosition(p.x+cos(p.a)*offset,1.6,p.z+sin(p.a)*offset);
    }
}
function buildScenery() {
    local ground=material(0.045,0.09,0.105),dark=material(0.025,0.04,0.065,0.3);
    local cyan=material(0.02,0.9,1.0,0.1,true),orange=material(1.0,0.25,0.035,0.0,true);
    local white=material(0.9,0.94,0.98),red=material(0.65,0.05,0.07);
    box(0.0,-0.42,0.0,320.0,0.6,320.0,ground);
    // Closed ring boundaries block the generator's four approach branches.
    for(local i=0;i<96;++i) {
        local a=i*2.0*PI/96.0;
        foreach(r in [37.0,59.0]) {
            local x=cos(a)*r,z=sin(a)*r,len=r*2.0*PI/96.0+0.15;
            box(x,0.65,z,0.45,1.1,len,i%4<2?dark:cyan,-a);
            collisionBox(x,z,0.65,len,a);
        }
    }
    // Start gantry and checkerboard, aligned across the road.
    for(local z=-56.0;z<=-40.0;z+=2.0)for(local x=-1.5;x<=1.5;x+=1.0)
        box(x,0.04,z,1.0,0.03,2.0,((z.tointeger()+56)/2+x.tointeger()+2)%2==0?white:dark);
    box(0.0,3.0,-59.5,0.8,6.0,0.8,dark);box(0.0,3.0,-36.5,0.8,6.0,0.8,dark);
    box(0.0,6.0,-48.0,0.8,0.8,24.0,dark);box(-0.45,6.0,-48.0,0.12,0.28,22.0,cyan);
    for(local i=0;i<5;++i)box(-0.5,5.4,-52.0+i*2.0,0.18,0.35,0.8,orange);
    checkpointMaterial=cyan;
    gateParts.append(box(0.0,0.0,0.0,0.35,3.0,0.35,cyan));
    gateParts.append(box(0.0,0.0,0.0,0.35,3.0,0.35,cyan));moveCheckpoint();
    // Infield monument and distant low-poly skyline.
    box(0.0,4.0,0.0,4.0,8.0,4.0,dark,PI*0.25);
    box(0.0,8.3,0.0,5.5,0.4,5.5,cyan,PI*0.25);
    for(local i=0;i<44;++i) {
        local a=i*2.0*PI/44.0,r=85.0+(i%3)*9.0,h=5.0+(i*7%17);
        box(cos(a)*r,h*0.5,sin(a)*r,5.0,h,7.0,dark,-a);
        box(cos(a)*r,h+0.15,sin(a)*r,5.2,0.2,7.2,i%3==0?orange:cyan,-a);
    }
}
function relocateCar(x,z,heading) {
    vehicles.clearOrders(car);
    vehicles.setInput(car,0.0,0.0,1.0,false);
    // Flush motion through Vehicle's brake law before replacing its physics link.
    vehicles.update(2.1);
    vehicles.detachPhysics(car);
    vehicles.setPosition(car,x,z);vehicles.setHeading(car,heading);
    if(!vehicles.attachPhysics2D(car,world))throw "Vehicle recovery attachment failed";
}
function resetRace() {
    relocateCar(0.0,-48.0,0.0);setDrive(0.0,0.0,0.0);
    vehicles.setInput(car,0.0,0.0,1.0,false);
    race.lap=0;race.checkpoint=1;race.time=0.0;race.lastLap=0.0;
    race.x=0.0;race.z=-48.0;race.speed=0.0;race.heading=0.0;
    nextCheckpoint=1;lapStart=0.0;countdown=3.0;accumulator=0.0;
    cameraX=-14.0;cameraZ=-58.0;moveCheckpoint();
}
function setDrive(throttle,steer,brake) {
    agentThrottle=clampf(throttle,-1.0,1.0);agentSteer=clampf(steer,-1.0,1.0);agentBrake=clampf(brake,0.0,1.0);
}
function toggleAssist() { assisted=!assisted;setDrive(0.0,0.0,0.0); }
function toggleCamera() {overview=!overview;}
function respawn() {
    local p=checkpointPosition(nextCheckpoint-1);
    relocateCar(p.x,p.z,(p.a+PI*0.5)*180.0/PI);setDrive(0.0,0.0,0.0);
}
function simulationStep() {
    local throttle=agentThrottle,steer=agentSteer,brake=agentBrake;
    if(keyboard.isDown("w")||keyboard.isDown("up"))throttle=1.0;
    if(keyboard.isDown("s")||keyboard.isDown("down"))throttle=-0.4;
    if(keyboard.isDown("a")||keyboard.isDown("left"))steer=-1.0;
    if(keyboard.isDown("d")||keyboard.isDown("right"))steer=1.0;
    if(keyboard.isDown("space"))brake=1.0;
    if(assisted) {
        // Steering assistance still supplies normal Vehicle inputs, no scripted position integration.
        local a=atan2(race.z,race.x),target=a+PI*0.5+(sqrt(race.x*race.x+race.z*race.z)-RADIUS)*0.07;
        local error=target-race.heading*PI/180.0;
        while(error>PI)error-=2.0*PI;while(error< -PI)error+=2.0*PI;
        steer=clampf(error*3.5,-1.0,1.0);throttle=0.47;brake=0.0;
    }
    countdown-=STEP;if(countdown>0.0){throttle=0.0;steer=0.0;brake=1.0;}
    local radius=sqrt(race.x*race.x+race.z*race.z);race.offTrack=radius<39.0||radius>57.0;
    if(race.offTrack&&abs(race.speed)>12.0)brake=1.0;
    vehicles.setInput(car,throttle,steer,brake,false);
    vehicles.update(STEP);world.update(STEP);vehicles.clearEvents();
    race.x=vehicles.getX(car);race.z=vehicles.getY(car);race.heading=vehicles.getHeading(car);race.speed=vehicles.getSpeed(car);
    race.tick+=1;if(countdown<=0.0)race.time+=STEP;
    local p=checkpointPosition(nextCheckpoint%12),dx=race.x-p.x,dz=race.z-p.z;
    if(countdown<=0.0&&dx*dx+dz*dz<36.0) {
        nextCheckpoint+=1;
        if(nextCheckpoint%12==1) {
            race.lap+=1;race.lastLap=race.time-lapStart;lapStart=race.time;
            if(race.bestLap<=0.0||race.lastLap<race.bestLap)race.bestLap=race.lastLap;
            print("NEON_RACING_LAP lap="+race.lap+" seconds="+race.lastLap+"\n");
        }
        race.checkpoint=nextCheckpoint%12;moveCheckpoint();
    }
}
eve_init=function() {
    cubeMesh=gfx.newMeshCube(1.0);world=physicsOwner.newWorld(0.0,0.0,true);world.setMeter(1.0);
    local defs=@"[{""id"":""neon.r"",""category"":""car"",""mobility"":""wheel"",""maxSpeed"":32,""accel"":16,""turnRate"":100,""radius"":0.95,""maxHealth"":100}]";
    if(vehicles.registerVehiclesFromJson(defs)!=1)throw "NEON R definition registration failed";
    car=vehicles.newVehicle("neon.r",0.0,-48.0,0.0,"cyan");
    if(!vehicles.attachPhysics2D(car,world))throw "NEON R physics attachment failed";
    buildRoad();buildScenery();
    modelData=models.newModelDataFromFile("assets/neon-r.glb");
    if(modelData==null||modelData.getMeshCount()==0)throw "NEON R mesh missing";
    for(local i=0;i<modelData.getMeshCount();++i) {
        local e=models.createRenderable(gfx,modelData,i);e.setCastShadow(true);carParts.append(e);
    }
    camera=eve.Camera3D();camera.setUp(0.0,1.0,0.0);camera.setFov(52.0);camera.setClipPlanes(0.2,350.0);
    camera.setAmbient(0.44,0.52,0.62);camera.setActive(true);
    gfx.setDirectionalLight(-0.4,-1.0,-0.3,1.8,1.7,1.5);gfx.setBackgroundColor(0.025,0.055,0.09,1.0);
    fontOwner=eve.Font();local data=fontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf",24);
    hudFont=gfx.newFont(data," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:.-+()");gfx.setFont(hudFont);
    resetRace();race.ready=true;print("NEON_RACING_READY meshes="+carParts.len()+" physics="+vehicles.getPhysicsSpace(car)+"\n");
};
eve_update=function(dt) {
    if(pressed("r"))resetRace();if(pressed("c"))toggleCamera();if(pressed("p"))toggleAssist();if(pressed("e"))respawn();
    accumulator+=clampf(dt,0.0,0.25);while(accumulator>=STEP){simulationStep();accumulator-=STEP;}
    local h=race.heading*PI/180.0;
    foreach(e in carParts){e.setPosition(race.x,0.08,race.z);e.setRotation(-h,0.0,0.0);}
    local blend=1.0-exp(-dt*5.0);
    cameraX+=(race.x-cos(h)*11.5-cameraX)*blend;cameraZ+=(race.z-sin(h)*11.5-cameraZ)*blend;
    if(overview){camera.setEye(0.0,125.0,90.0);camera.setTarget(0.0,0.0,0.0);}
    else {camera.setEye(cameraX,5.5,cameraZ);camera.setTarget(race.x+cos(h)*6.0,0.8,race.z+sin(h)*6.0);}
};
function label(s,x,y,r=0.82,g=0.92,b=1.0) {gfx.drawText(hudFont,s,x,y,r,g,b,1.0,1.0);}
eve_render=function() {
    gfx.clear();gfx.render3D();
    gfx.drawSolidRect(20.0,18.0,370.0,120.0,0.02,0.035,0.06,0.9);
    label("NEON R / CIRCUIT RUN",36.0,30.0,0.1,0.92,1.0);
    label(format("%03d KM/H    LAP %d",(abs(race.speed)*3.6).tointeger(),race.lap+1),36.0,65.0);
    label(format("TIME %0.1f  BEST %0.1f",race.time-lapStart,race.bestLap),36.0,100.0);
    label("WASD Drive | SPACE Brake | E Recover | R Restart",25.0,641.0);
    label("C Camera | P Driving assist",25.0,679.0,0.12,0.85,0.96);
    label(format("CHECKPOINT %02d / 12",race.checkpoint==0?12:race.checkpoint),875.0,30.0);
    if(countdown>0.0)label(format("READY %d",ceil(countdown).tointeger()),540.0,270.0,1.0,0.7,0.2);
    if(assisted)label("ASSIST ON",1060.0,675.0,0.1,1.0,0.7);
    if(race.offTrack)label("OFF TRACK - SLOW DOWN",480.0,570.0,1.0,0.4,0.1);
};
