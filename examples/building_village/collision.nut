// Disposable static physics projection. Building snapshots own geometry; the script owns
// the capsule position. No Body3D/Shape3D proxy survives candidate construction.
persist navCollision = null
persist navCollisionBodies = 0
persist navContacts = 0
persist navManualX = 0.0
persist navManualZ = 0.0
persist navManualTicks = 0
persist navManualActive = false

function navCollisionBox(world,x,z,hw,hd,angle,height,bottom=0.1) {
    local body=world.newBody("static",x.tofloat(),bottom+height*0.5,z.tofloat());
    if(body==null)throw "Physics body creation failed";
    // Building footprint angles rotate +X toward +Z; physics quaternion yaw is opposite.
    body.setRotation(0.0,sin(-angle*0.5),0.0,cos(angle*0.5));
    local shape=body.newBoxShape(hw*2.0,height,hd*2.0,1.0,0.0,0.0);
    if(shape==null)throw "Physics shape creation failed";
}
function navCollisionBuild(g) {
    local world=null;
    try {
        world=physics.newWorld3D(0.0,0.0,0.0,true);
        if(world==null)return navFail("PhysicsUnavailable","Cannot create collision world");
        local boxes=("collisionBoxes" in g)?g.collisionBoxes:g.obstacles;
        foreach(b in boxes)navCollisionBox(world,b.x,b.z,b.hw,b.hd,b.angle,b.height,("bottom" in b)?b.bottom:0.1);
        navCollisionBox(world,13.0,10.0,13.0,10.0,0.0,0.5,-0.5);
        world.setMoverUp(0.0,1.0,0.0);world.setMoverSlopeLimit(45.0);
        // Keep manual motion within the same inset boundary as the navigation grid.
        navCollisionBox(world,0.0,10.0,0.125,10.0,0.0,12.0);
        navCollisionBox(world,26.0,10.0,0.125,10.0,0.0,12.0);
        navCollisionBox(world,13.0,0.0,13.0,0.125,0.0,12.0);
        navCollisionBox(world,13.0,20.0,13.0,0.125,0.0,12.0);
        return navSuccess(world);
    } catch(error) {
        if(world!=null)world.destroy();
        return navFail("PhysicsUnavailable",error.tostring());
    }
}
function navCollisionClear() {
    if(navCollision!=null){navCollision.destroy();navCollision=null;}
    navCollisionBodies=0;navManualTicks=0;
}
function navResolve(world,x,z,dx,dz) {
    // Planar controller: fixed capsule clearance above the 0.1 m floor surface.
    // There is no gravity/step climbing in this example. Render floor offsets are cosmetic.
    local constrained=world.moveCapsule(x,0.1+NAV_RADIUS,z,x,1.3-NAV_RADIUS,z,NAV_RADIUS,dx,0.0,dz);
    local rx=world.getMoverDeltaX(),ry=world.getMoverDeltaY(),rz=world.getMoverDeltaZ();
    if(fabs(ry)>0.001 || sqrt(rx*rx+rz*rz)>sqrt(dx*dx+dz*dz)+0.006)return navFail("OccupiedStart","Collider overlaps visitor; undo or reset visitor");
    return navSuccess({x=rx,z=rz,constrained=constrained});
}
function navOverlapping(world,x,z,y=0.1) {
    // Ignore the solver's 5 mm resting slop, but reject newly authored penetration.
    return world.queryCapsule(x,y+NAV_RADIUS,z,x,y+NAV_HEIGHT-NAV_RADIUS,z,NAV_RADIUS-0.006)>0;
}
function navConnectStart(g,x,z,goal) {
    if(navOverlapping(navCollision,x,z,navY))return navFail("OccupiedStart","Visitor overlaps an edited collider; undo or reset");
    local layer=navLayerAt(g,floor((navY+0.02)/3.0).tointeger());if(layer==null)return navFail("NoRoute","Current floor is missing");
    local candidates=[],sx=floor(x*NAV_SCALE).tointeger(),sz=floor(z*NAV_SCALE).tointeger();
    for(local iz=sz-3;iz<=sz+3;iz++)for(local ix=sx-3;ix<=sx+3;ix++) {
        if(ix<0 || iz<0 || ix>=g.width || iz>=g.height || layer.blocked[navIndex(ix,iz,g.width)])continue;
        local px=(ix+0.5)/NAV_SCALE,pz=(iz+0.5)/NAV_SCALE;
        candidates.push({x=px,z=pz,score=(px-x)*(px-x)+(pz-z)*(pz-z)});
    }
    candidates.sort(function(a,b){return a.score<b.score?-1:(a.score>b.score?1:0);});
    foreach(p in candidates) {
        local swept=navSweep(navCollision,x,navY,z,p.x-x,0.0,p.z-z);
        if(fabs(swept.x-(p.x-x))+fabs(swept.z-(p.z-z))>0.001)continue;
        local route=navFindMulti(g,p.x,p.z,navY,goal);
        if(route.ok)return route; // First node is reached through the checked physical connector.
    }
    return navFail("OccupiedStart","No clear connection to navigation grid; move away from obstacle or reset");
}
function navNudge(x,z) {
    chooseMode("visit");navGoal=null;navRoute=[];navDemoTarget="";
    multiTestPhase="";navManualX=x;navManualZ=z;navManualTicks=30;navClock=0.0;
    navPhase="Manual: capsule collision";
}
function navManualInput() {
    if(mode!="visit" || ui.wantCaptureKeyboard())return [0.0,0.0];
    local x=0.0,z=0.0;
    if(keyboard.isDown("a"))x-=1.0;if(keyboard.isDown("d"))x+=1.0;
    if(keyboard.isDown("w"))z-=1.0;if(keyboard.isDown("s"))z+=1.0;
    local length=sqrt(x*x+z*z);
    if(length>0.0){x/=length;z/=length;}
    return [x,z];
}
