// Example composition: building owns geometry; Map owns A*; this script owns one transient visitor.
// Rebuild only at the snapshot/commit boundary. No world reads or path queries in movement ticks.
const NAV_SCALE = 4;
const NAV_RADIUS = 0.16;
const NAV_STEP = 0.0166666666667;
persist navMap = null
persist navGraph = null
persist navGoal = null
persist navRoute = []
persist navNext = 0
persist navX = 2.125
persist navZ = 2.125
persist navYaw = 0.0
persist navDistance = 0.0
persist navClock = 0.0
persist navInside = ""
persist navPhase = "Ready"
persist navRevision = 0
persist navDemoTarget = ""
persist navBody = []
persist navTrail = []
persist navObservation = {}

function navFail(code, text) { return {ok=false,code=code,message=text,value=null}; }
function navSuccess(value) { return {ok=true,code="Ok",message="",value=value}; }
function villageDoorClosed(p) { return ("closed" in p.properties) && p.properties.closed=="true"; }
function navIndex(x,z,w) { return z*w+x; }
function navMark(g,x,z) {
    local i=navIndex(x,z,g.width);
    if(!g.blocked[i]){g.blocked[i]=true;g.finder.setBlocked(x,z,true);}
}
function navBlockBox(g,x,z,hw,hd,angle,height=2.9,solid=true) {
    if(solid)g.obstacles.push({x=x,z=z,hw=hw,hd=hd,angle=angle,height=height,bottom=g.level*3.0+0.1});
    local c=cos(angle),s=sin(angle);
    // Inflate by visitor radius plus the projected half-cell square. Adjacent
    // walkable cells then admit their entire shared segment, including at corners.
    local pad=NAV_RADIUS+0.125*(fabs(c)+fabs(s));
    local extent=fabs(hw)+fabs(hd)+pad*2.0;
    local x0=floor((x-extent)*NAV_SCALE).tointeger(),x1=ceil((x+extent)*NAV_SCALE).tointeger();
    local z0=floor((z-extent)*NAV_SCALE).tointeger(),z1=ceil((z+extent)*NAV_SCALE).tointeger();
    if(x0<0)x0=0;if(z0<0)z0=0;if(x1>=g.width)x1=g.width-1;if(z1>=g.height)z1=g.height-1;
    for(local iz=z0;iz<=z1;iz++)for(local ix=x0;ix<=x1;ix++) {
        local dx=(ix+0.5)/NAV_SCALE-x,dz=(iz+0.5)/NAV_SCALE-z;
        if(fabs(dx*c+dz*s)<=hw+pad && fabs(-dx*s+dz*c)<=hd+pad)navMark(g,ix,iz);
    }
}
function navBuildLayer(snapshot,level) {
    if(snapshot.width!=26 || snapshot.height!=20 || snapshot.originX!=0 || snapshot.originY!=0 || snapshot.cellSize!=1.0 || snapshot.plane!="xz")
        return navFail("UnsupportedGrid","Walking supports this example's 26x20 village at origin zero");
    local w=snapshot.width*NAV_SCALE,h=snapshot.height*NAV_SCALE;
    local finder=navMap.newPathfinderSize(w,h);
    if(finder==null)return navFail("Unavailable","Could not create navigation grid");
    finder.setTopology("ortho4");finder.setDiagonal(false);
    local g={finder=finder,width=w,height=h,blocked=array(w*h,false),floorOwners=array(26*20,""),centers={},doors={},obstacles=[],level=level,floors=array(26*20,false)};
    local owners={};foreach(a in snapshot.assemblies)foreach(m in a.members)owners[m.record] <- a.id;
    for(local z=0;z<h;z++)for(local x=0;x<w;x++)
        if(x==0 || z==0 || x==w-1 || z==h-1)navMark(g,x,z);
    foreach(p in snapshot.instances) {
        if(p.level!=level)continue;
        if(p.elevation!=0.0)return navFail("UnsupportedHeight","Walking supports flat ground-floor parts only");
        if(p.buildingId=="village.floor") {
            g.floors[p.cellY*26+p.cellX]=true;
            if(p.instanceId in owners) {
                local id=owners[p.instanceId];g.floorOwners[p.cellY*26+p.cellX]=id;
                if(!(id in g.centers))g.centers[id] <- {x=0.0,z=0.0,count=0};
                g.centers[id].x+=p.cellX+0.5;g.centers[id].z+=p.cellY+0.5;g.centers[id].count++;
            }
        } else if(p.buildingId=="village.stairs") {
            local stair=villageStair(p);
            navBlockBox(g,stair.x,stair.z,stair.ax==0.0?1.0:2.0,stair.ax==0.0?2.0:1.0,0.0,3.0,false);
        } else if(p.placementKind=="edge") {
            local horizontal=p.edgeAxis=="horizontal";
            if(p.buildingId=="village.door") {
                if(p.instanceId in owners)g.doors[owners[p.instanceId]] <- true;
                if(!villageDoorClosed(p)) {
                    foreach(side in [-1.0,1.0])navBlockBox(g,p.edgeX+(horizontal?0.5+side*0.46:0.0),p.edgeY+(horizontal?0.0:0.5+side*0.46),horizontal?0.04:0.06,horizontal?0.06:0.04,0.0);
                    continue;
                }
            }
            navBlockBox(g,p.edgeX+(horizontal?0.5:0.0),p.edgeY+(horizontal?0.0:0.5),horizontal?0.5:0.06,horizontal?0.06:0.5,0.0);
        } else if(p.placementKind=="corner")navBlockBox(g,p.cornerX,p.cornerY,0.09,0.09,0.0,3.0);
        else if(p.placementKind=="free")navBlockBox(g,p.worldX,p.worldY,p.freeHalfWidth,p.freeHalfHeight,p.rotationDegrees*0.01745329252,0.95);
    }
    if(level>0)for(local z=0;z<h;z++)for(local x=0;x<w;x++) {
        local wx=(x+0.5)/NAV_SCALE,wz=(z+0.5)/NAV_SCALE,clear=true;
        foreach(dx in [-0.285,0.285])foreach(dz in [-0.285,0.285]) {
            local cx=floor(wx+dx).tointeger(),cz=floor(wz+dz).tointeger();
            if(cx<0 || cz<0 || cx>=26 || cz>=20 || !g.floors[cz*26+cx])clear=false;
        }
        if(!clear)navMark(g,x,z);
    }
    foreach(id,c in g.centers){c.x/=c.count;c.z/=c.count;}
    return navSuccess(g);
}
function navOwner(g,x,z) {
    if(g==null || x<0 || z<0 || x>=26 || z>=20)return "";
    return g.floorOwners[z.tointeger()*26+x.tointeger()];
}
function navDestination(g,goal) {
    if(goal.house=="")return navSuccess([goal.x,goal.z]);
    if(!(goal.house in g.centers))return navFail("TargetGone","Target house no longer has a floor");
    local center=g.centers[goal.house],best=null,bestScore=1000000.0;
    for(local z=0;z<g.height;z++)for(local x=0;x<g.width;x++) {
        local wx=(x+0.5)/NAV_SCALE,wz=(z+0.5)/NAV_SCALE;
        if(g.blocked[navIndex(x,z,g.width)] || navOwner(g,wx,wz)!=goal.house)continue;
        local score=(wx-center.x)*(wx-center.x)+(wz-center.z)*(wz-center.z);
        if(score<bestScore){bestScore=score;best=[wx,wz];}
    }
    return best==null?navFail("BlockedGoal","House interior has no clearance"):navSuccess(best);
}
function navFind(g,x,z,goal) {
    local destination=navDestination(g,goal);if(!destination.ok)return destination;
    local sx=floor(x*NAV_SCALE).tointeger(),sz=floor(z*NAV_SCALE).tointeger();
    local gx=floor(destination.value[0]*NAV_SCALE).tointeger(),gz=floor(destination.value[1]*NAV_SCALE).tointeger();
    if(sx<0 || sz<0 || sx>=g.width || sz>=g.height || g.blocked[navIndex(sx,sz,g.width)])
        return navFail("OccupiedStart","Visitor is obstructed; undo the edit or reset visitor");
    if(gx<0 || gz<0 || gx>=g.width || gz>=g.height || g.blocked[navIndex(gx,gz,g.width)])
        return navFail("BlockedGoal","Destination is blocked");
    local path=g.finder.findPath(sx,sz,gx,gz);
    if(path==null || path.getLength()==0)return navFail("NoRoute","No route; open a door or clear the passage");
    local points=[];
    for(local i=0;i<path.getLength();i++)points.push([(path.getX(i)+0.5)/NAV_SCALE,(path.getY(i)+0.5)/NAV_SCALE]);
    return navSuccess(points);
}
function navPlan() {
    navRoute=[];navNext=0;navClock=0.0;navManualTicks=0;
    if(navGoal==null || navGraph==null)return;
    if(!navGrounded){navPhase="Waiting to land";return;}
    local result=navFindMulti(navGraph,navX,navZ,navY,navGoal);
    if(!result.ok && result.code=="OccupiedStart" && navCollision!=null)result=navConnectStart(navGraph,navX,navZ,navGoal);
    if(!result.ok) {
        navPhase=result.code+": "+result.message;
        if(result.code=="TargetGone"){navGoal=null;navDemoTarget="";}
        return;
    }
    navRoute=result.value;navPhase="Walking";
}
function navRebuild(snapshot) {
    if(navMap==null)return;
    local result=navBuild(snapshot);
    navRevision++;
    if(!result.ok){navGraph=null;navRoute=[];navCollisionClear();navPhase=result.message;navDemoTarget="";return;}
    local collision=navCollisionBuild(result.value);
    if(!collision.ok){navGraph=null;navRoute=[];navCollisionClear();navPhase=collision.code+": "+collision.message;return;}
    // Publish both projections at one synchronous boundary; release old world afterwards.
    local old=navCollision;navCollision=collision.value;navGraph=result.value;
    navCollisionBodies=navGraph.collisionBoxes.len()+5;navGrounded=false;
    if(old!=null)old.destroy();
    navPlan();
    local inside=navOwner(navGraph,navX,navZ);
    if(inside!=navInside){navInside=inside;viewDirty=true;}
}
function navInit() { navMap=eve.Map();navRebuild(requireHouse(town,{op="snapshot"})); }
function navVisit(id,level=0) {
    navDemoTarget="";
    if(id==""){navPhase="Select a house first";return;}
    navGoal={house=id,x=0.0,z=0.0,level=level};navPlan();
}
function navWalkPoint(x,z) { navDemoTarget="";navGoal={house="",x=x,z=z,level=editLevel};navPlan(); }
function navReset() {
    multiTestPhase="";
    navGoal=null;navRoute=[];navDemoTarget="";navManualTicks=0;
    if(navGraph==null || !navGraph.finder.isWalkable(8,8)){navPhase="Spawn is blocked";return;}
    navX=2.125;navZ=2.125;navY=0.02;navVelocityY=0.0;navGrounded=false;navLevel=0;navClock=0.0;navPhase="Ready";
    navInside=navOwner(navGraph,navX,navZ);viewDirty=true;
}
function navToggleDoor() {
    if(selected==""){message="Select a house first";return;}
    local members=callHouse(town,{op="members",assemblyId=selected});if(members==null)return;
    local found=false,closed=false;
    foreach(p in members)if(p.buildingId=="village.door"){if(!found)closed=!villageDoorClosed(p);found=true;p.properties.closed <- closed?"true":"false";}
    if(!found){message="This house has no door member";return;}
    remember(callHouse(town,{op="remodel",assemblyId=selected,members=members}));
    message=closed?"Door closed; route rechecked":"Door open; route rechecked";
}

function navPublish() {
    // Read-only observation projection, never used as input to simulation.
    navObservation={x=navX,y=navY,z=navZ,level=navLevel,grounded=navGrounded,velocityY=navVelocityY,verticalTravel=navVerticalTravel,stairSteps=navStairSteps,multiTest=multiTestPhase,inside=navInside,phase=navPhase,revision=navRevision,
        moving=navRoute.len()>0 || navManualTicks>0 || navManualActive || !navGrounded,contacts=navContacts,collisionBodies=navCollisionBodies,remaining=navRoute.len()-navNext,selected=selected,
        target=navGoal==null?"":navGoal.house,mode=mode,workshopTest=workshopTest,workshopParts=workshop.getBuildingCount(),blueprintParts=blueprint==null?0:blueprint.parts.len()};
    if(navObservation.remaining<0)navObservation.remaining=0;
}
