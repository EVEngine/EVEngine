// Building level/rotation/footprint remain authoritative. Stair geometry is shared by
// collision, rendering and portal routing. Levels are 0..3, with the world's 3 m spacing.
const NAV_MAX_LEVEL = 3;
const NAV_HEIGHT = 1.3;
const NAV_STEP_UP = 0.24;
persist editLevel = 0
persist navY = 0.02
persist navVelocityY = 0.0
persist navGrounded = false
persist navLevel = 0
persist navVerticalTravel = 0.0
persist navStairSteps = 0
persist multiDemoId = ""
persist multiTestPhase = ""

function villageStair(p,spacing=3.0) {
    local q=((p.rotationDegrees/90.0).tointeger()%4+4)%4;
    local ax=[0.0,1.0,0.0,-1.0][q],az=[1.0,0.0,-1.0,0.0][q];
    local x=p.cellX+(q%2==0?1.0:2.0),z=p.cellY+(q%2==0?2.0:1.0);
    local bottom=p.level*spacing+0.1;
    local s={x=x,z=z,ax=ax,az=az,level=p.level,boxes=[],low=[x-ax*2.375,z-az*2.375,bottom,p.level],
        high=[x+ax*2.375,z+az*2.375,bottom+spacing,p.level+1]};
    for(local i=0;i<15;i++) {
        local t=-2.0+(i+0.5)*4.0/15.0,h=(i+1)*spacing/15.0;
        s.boxes.push({x=x+ax*t,z=z+az*t,hw=q%2==0?1.0:2.0/15.0,hd=q%2==0?2.0/15.0:1.0,
            angle=0.0,height=h,bottom=bottom});
    }
    return s;
}
function navCheckScope(snapshot) {
    if(snapshot.floorHeight!=3.0)return navFail("UnsupportedHeight","Walking requires 3 m floor spacing");
    foreach(p in snapshot.instances) {
        if(p.elevation!=0.0)return navFail("UnsupportedHeight","Use discrete floor levels, not elevation offsets");
        if(p.level<0 || p.level>(p.buildingId=="village.roof"?NAV_MAX_LEVEL+1:NAV_MAX_LEVEL))return navFail("UnsupportedLevel","Walking supports levels 0..3");
        if(p.buildingId=="village.stairs" && p.level>=NAV_MAX_LEVEL)return navFail("UnsupportedLevel","Stair top exceeds level 3");
    }
    return navSuccess(null);
}
function navBuild(snapshot) {
    local scope=navCheckScope(snapshot);if(!scope.ok)return scope;
    local highest=0,stairs=[],surfaces=[];
    foreach(p in snapshot.instances) {
        if(p.buildingId=="village.floor" && p.level>highest)highest=p.level;
        if(p.buildingId=="village.stairs") {
            stairs.push(villageStair(p));if(p.level+1>highest)highest=p.level+1;
        }
        if(p.buildingId=="village.floor")surfaces.push({x=p.cellX+0.5,z=p.cellY+0.5,hw=0.5,hd=0.5,angle=0.0,height=0.1,bottom=p.level*3.0});
        if(p.buildingId=="village.roof") {
            local q=((p.rotationDegrees/90.0).tointeger()%2+2)%2;
            surfaces.push({x=p.cellX+(q==0?2.0:1.5),z=p.cellY+(q==0?1.5:2.0),hw=q==0?2.0:1.5,hd=q==0?1.5:2.0,
                angle=0.0,height=0.15,bottom=p.level*3.0});
        }
    }
    local layers=[],boxes=[];
    for(local level=0;level<=highest;level++) {
        local built=navBuildLayer(snapshot,level);if(!built.ok)return built;
        layers.push(built.value);foreach(b in built.value.obstacles)boxes.push(b);
    }
    foreach(b in surfaces)boxes.push(b);
    local portalBoxes=clone boxes;
    foreach(s in stairs)foreach(b in s.boxes)boxes.push(b);
    // Top-level ground aliases retain the existing flat-grid query contract. They are
    // immutable projections; the layers array contains no reference back to this object.
    local g=clone layers[0];g.layers <- layers;g.stairs <- stairs;g.collisionBoxes <- boxes;g.portalBoxes <- portalBoxes;
    return navSuccess(g);
}
function navLayerAt(g,level) {return level>=0 && level<g.layers.len()?g.layers[level]:null;}
function navFloorOwner(g,x,z,level) {
    local layer=navLayerAt(g,level);return layer==null?"":navOwner(layer,x,z);
}
function navStairPoints(s,up) {
    local points=[clone s.low];
    for(local i=0;i<15;i++) {
        local t=-2.0+(i+0.5)*4.0/15.0;
        points.push([s.x+s.ax*t,s.z+s.az*t,s.low[2]+(i+1)*0.2,s.level]);
    }
    points.push(clone s.high);
    if(!up)points.reverse();
    return points;
}
function navRouteCost(points) {
    local cost=0.0;
    for(local i=1;i<points.len();i++) {
        local a=points[i-1],b=points[i],dx=b[0]-a[0],dz=b[1]-a[1],dy=b[2]-a[2];
        cost+=sqrt(dx*dx+dz*dz);
        cost+=fabs(dy);
    }
    return cost;
}
function navLayerRoute(g,a,b) {
    if(a[3]!=b[3])return navFail("NoRoute","Different floors require stairs");
    local layer=navLayerAt(g,a[3]);if(layer==null)return navFail("NoRoute","Floor is missing");
    local route=navFind(layer,a[0],a[1],{house="",x=b[0],z=b[1]});
    if(!route.ok)return route;
    foreach(p in route.value){p.push(a[3]*3.0+0.1);p.push(a[3]);}
    return route;
}
function navStairClear(g,s) {
    foreach(p in navStairPoints(s,true))foreach(b in g.portalBoxes) {
        if(p[2]>=b.bottom+b.height-0.01 || p[2]+NAV_HEIGHT<=b.bottom+0.01)continue;
        local dx=p[0]-b.x,dz=p[1]-b.z,c=cos(b.angle),sn=sin(b.angle);
        if(fabs(dx*c+dz*sn)<b.hw+NAV_RADIUS && fabs(-dx*sn+dz*c)<b.hd+NAV_RADIUS)return false;
    }
    return true;
}
function navFindMulti(g,x,z,y,goal) {
    local targetLevel=("level" in goal)?goal.level:0;
    if(goal.house!="") {
        local present=false;foreach(layer in g.layers)if(goal.house in layer.centers)present=true;
        if(!present)return navFail("TargetGone","Target house no longer has any floor");
    }
    local target=navLayerAt(g,targetLevel);
    if(target==null)return navFail("NoRoute","Requested floor is missing");
    local destination=navDestination(target,goal);
    if(!destination.ok)return destination.code=="TargetGone"?navFail("NoRoute","Requested house floor is missing"):destination;
    local level=floor((y+0.02)/3.0).tointeger();
    local start=[x,z,y,level],end=[destination.value[0],destination.value[1],targetLevel*3.0+0.1,targetLevel];
    local startLayer=navLayerAt(g,level),onStair=false;
    foreach(s in g.stairs) {
        local t=(x-s.x)*s.ax+(z-s.z)*s.az,side=(x-s.x)*s.az-(z-s.z)*s.ax;
        if(fabs(side)<0.7 && fabs(t)<2.1 && fabs(y-(s.low[2]+(t+2.0)*0.75))<0.35)onStair=true;
    }
    if(!onStair && startLayer!=null) {
        local ix=floor(x*NAV_SCALE).tointeger(),iz=floor(z*NAV_SCALE).tointeger();
        if(ix>=0 && iz>=0 && ix<g.width && iz<g.height && startLayer.blocked[navIndex(ix,iz,g.width)])
            return navFail("OccupiedStart","Find a physical connector from current position");
    }
    local nodes=[start,end],links={};
    foreach(s in g.stairs) {
        // Only publish a portal when both landings have traversable floor nodes.
        local low=navLayerRoute(g,s.low,s.low),high=navLayerRoute(g,s.high,s.high);
        if(!low.ok || !high.ok || !navStairClear(g,s))continue;
        local i=nodes.len();nodes.push(s.low);nodes.push(s.high);
        links[i] <- {to=i+1,points=navStairPoints(s,true)};
        links[i+1] <- {to=i,points=navStairPoints(s,false)};
    }
    local n=nodes.len(),distance=array(n,1000000.0),done=array(n,false),previous=array(n,-1),segments=array(n,null);
    distance[0]=0.0;
    for(local pass=0;pass<n;pass++) {
        local best=-1;
        for(local i=0;i<n;i++)if(!done[i] && (best<0 || distance[i]<distance[best]))best=i;
        if(best<0 || distance[best]>=1000000.0)break;
        if(best==1)break;done[best]=true;
        for(local next=1;next<n;next++) {
            if(done[next] || next==best)continue;
            local path=null;
            if(best in links && links[best].to==next)path=navSuccess(links[best].points);
            else path=navLayerRoute(g,nodes[best],nodes[next]);
            // A grounded visitor may currently stand on the staircase itself after an edit.
            if(best==0 && !path.ok)foreach(s in g.stairs) {
                local t=(x-s.x)*s.ax+(z-s.z)*s.az,side=(x-s.x)*s.az-(z-s.z)*s.ax;
                if(fabs(side)>0.7 || t< -2.1 || t>2.1 || fabs(y-(s.low[2]+(t+2.0)*0.75))>0.35)continue;
                if(nodes[next]==s.low || nodes[next]==s.high) {
                    local up=nodes[next]==s.high,points=navStairPoints(s,up),remaining=[];
                    foreach(p in points)if(up?(p[0]-x)*s.ax+(p[1]-z)*s.az>0.01:(p[0]-x)*s.ax+(p[1]-z)*s.az< -0.01)remaining.push(p);
                    if(remaining.len()>0)path=navSuccess(remaining);
                }
            }
            if(!path.ok)continue;
            local cost=distance[best]+navRouteCost(path.value)+0.001;
            if(cost<distance[next]){distance[next]=cost;previous[next]=best;segments[next]=path.value;}
        }
    }
    if(previous[1]<0)return navFail("NoRoute","No connected stair route to requested floor");
    local chain=[],cursor=1;
    while(cursor!=0){chain.push(segments[cursor]);cursor=previous[cursor];}
    chain.reverse();local result=[];foreach(segment in chain)foreach(p in segment)result.push(clone p);
    return navSuccess(result);
}
