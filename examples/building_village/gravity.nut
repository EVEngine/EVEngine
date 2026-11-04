// Fixed-step kinematic character. Only resolved physical displacement changes navX/Y/Z.
// Stair stepping uses three swept moves (up, across, down), never a position teleport.
persist navStepTrace = ""

function navSweep(world,x,y,z,dx,dy,dz) {
    local constrained=world.moveCapsule(x,y+NAV_RADIUS,z,x,y+NAV_HEIGHT-NAV_RADIUS,z,NAV_RADIUS,dx,dy,dz);
    return {x=world.getMoverDeltaX(),y=world.getMoverDeltaY(),z=world.getMoverDeltaZ(),
        grounded=world.isMoverGrounded(),constrained=constrained};
}
function navLand(world,x,y,z,distance) {
    local hit=world.castCapsule(x,y+NAV_RADIUS,z,x,y+NAV_HEIGHT-NAV_RADIUS,z,NAV_RADIUS,0.0,-distance,0.0);
    // The normal on a capsule/tread edge is rounded even though the tread is horizontal.
    // Stop at first contact rather than sliding off that edge during the landing sweep.
    if(hit<0 || world.getShapeCastNormalY()<0.1)return {x=0.0,y=0.0,z=0.0,grounded=false};
    return {x=0.0,y=-distance*world.getShapeCastFraction(),z=0.0,grounded=true};
}
function navPhysicalStep(dx,dz) {
    if(navOverlapping(navCollision,navX,navZ,navY))return navFail("OccupiedStart","Visitor overlaps an edited collider; undo or reset");
    navVelocityY-=9.8*NAV_STEP;if(navVelocityY< -20.0)navVelocityY=-20.0;
    local dy=navVelocityY*NAV_STEP;
    local moved=navSweep(navCollision,navX,navY,navZ,dx,dy,dz);
    local desired=dx*dx+dz*dz;
    local progress=moved.x*dx+moved.z*dz;
    if(navGrounded && desired>0.000001 && progress<desired-0.00001) {
        local up=navSweep(navCollision,navX,navY,navZ,0.0,NAV_STEP_UP,0.0);
        if(up.y>NAV_STEP_UP-0.001) {
            local across=navSweep(navCollision,navX+up.x,navY+up.y,navZ+up.z,dx,0.0,dz);
            local down=navLand(navCollision,navX+up.x+across.x,navY+up.y+across.y,navZ+up.z+across.z,NAV_STEP_UP+0.06);
            local rise=up.y+across.y+down.y;
            navStepTrace="up="+up.y+" across="+across.x+","+across.z+" down="+down.y+" ground="+down.grounded+" rise="+rise;
            if(down.grounded && rise<=NAV_STEP_UP+0.001 && rise>=-0.061 && across.x*dx+across.z*dz>progress+0.00001) {
                moved={x=up.x+across.x+down.x,y=rise,z=up.z+across.z+down.z,grounded=true,constrained=true};
                navStairSteps++;
            }
        }
    }
    // Stay attached while descending a normal step, but let larger drops fall under gravity.
    if(navGrounded && !moved.grounded && dy<=0.0) {
        local down=navLand(navCollision,navX+moved.x,navY+moved.y,navZ+moved.z,NAV_STEP_UP);
        if(down.grounded){moved.x+=down.x;moved.y+=down.y;moved.z+=down.z;moved.grounded=true;}
    }
    navX+=moved.x;navY+=moved.y;navZ+=moved.z;
    navDistance+=sqrt(moved.x*moved.x+moved.z*moved.z);navVerticalTravel+=fabs(moved.y);
    navGrounded=moved.grounded;if(navGrounded && navVelocityY<0.0)navVelocityY=0.0;
    if(moved.constrained)navContacts++;
    return navSuccess(moved);
}
function navUpdate(dt) {
    navManualActive=false;
    if(mode=="workshop" || navGraph==null || navCollision==null)return;
    local input=navManualInput(),manual=input[0]!=0.0 || input[1]!=0.0;
    navManualActive=manual;
    if(manual){navGoal=null;navRoute=[];navDemoTarget="";navManualTicks=0;multiTestPhase="";}
    if(dt<0.0)return;if(dt>0.1)dt=0.1;navClock+=dt;
    while(navClock>=NAV_STEP) {
        navClock-=NAV_STEP;
        local dx=0.0,dz=0.0,point=null,wasGrounded=navGrounded;
        if(manual || navManualTicks>0) {
            dx=(manual?input[0]:navManualX)*2.0*NAV_STEP;dz=(manual?input[1]:navManualZ)*2.0*NAV_STEP;
            if(!manual)navManualTicks--;
        } else if(navNext<navRoute.len()) {
            point=navRoute[navNext];local vx=point[0]-navX,vz=point[1]-navZ,length=sqrt(vx*vx+vz*vz);
            if(length>0.00001){local distance=length<2.0*NAV_STEP?length:2.0*NAV_STEP;dx=vx/length*distance;dz=vz/length*distance;}
        }
        local moved=navPhysicalStep(dx,dz);
        if(!moved.ok){navRoute=[];navManualTicks=0;navVelocityY=0.0;navPhase=moved.code+": "+moved.message;break;}
        if(dx*dx+dz*dz>0.000001)navYaw=atan2(dx,dz);
        if(manual || navManualTicks>0)navPhase=moved.value.constrained?"Manual: grounded/collision":"Manual: walking/falling";
        if(point!=null) {
            local horizontal=sqrt((point[0]-navX)*(point[0]-navX)+(point[1]-navZ)*(point[1]-navZ));
            if(horizontal<0.035 && fabs(navY-point[2])<0.26 && navGrounded)navNext++;
            else if(navGrounded && dx*dx+dz*dz>0.000001 && moved.value.x*moved.value.x+moved.value.z*moved.value.z<0.0000001) {
                navRoute=[];navPhase="Collision blocked route; clear the passage";print("Stair move blocked: "+navStepTrace+"\n");
            }
        }
        if(!navGrounded && navRoute.len()==0)navPhase="Falling";
        if(!wasGrounded && navGrounded && navRoute.len()==0) {
            if(navGoal!=null)navPlan();
            else navPhase="Grounded on level "+floor((navY+0.02)/3.0).tointeger();
        }
        if(navRoute.len()>0 && navNext>=navRoute.len()) {
            navRoute=[];navNext=0;navPhase="Arrived on level "+floor((navY+0.02)/3.0).tointeger();
            if(navDemoTarget!="") {
                if(navFloorOwner(navGraph,navX,navZ,0)!=navDemoTarget)throw "Walking arrived in wrong house";
                navDemoTarget="";navPhase="PASS: entered through door";print("building_village.navigation PASS\n");
            }
        }
    }
    local nextLevel=floor((navY+0.02)/3.0).tointeger();if(nextLevel<0)nextLevel=0;
    local inside=navFloorOwner(navGraph,navX,navZ,nextLevel);
    if(inside=="" && navY>0.2)inside=navFloorOwner(navGraph,navX,navZ,0);
    if(inside!=navInside || nextLevel!=navLevel){navInside=inside;navLevel=nextLevel;viewDirty=true;}
    multiUpdateTest();
}
