// Real Physics World3D queries against disposable projections, independent of A* avoidance.
function navCollisionCheckSnapshot(snapshot,id,closed) {
    local graph=navRequire(navBuild(snapshot));
    local world=navRequire(navCollisionBuild(graph));
    try {
        local ids={};foreach(a in snapshot.assemblies)if(a.id==id)foreach(m in a.members)ids[m.record] <- true;
        local doorCount=0,wallCount=0;
        foreach(p in snapshot.instances) {
            if(!(p.instanceId in ids) || p.level!=0 || p.placementKind!="edge")continue;
            local horizontal=p.edgeAxis=="horizontal";
            local x=p.edgeX+(horizontal?0.5:0.0),z=p.edgeY+(horizontal?0.0:0.5);
            local dx=horizontal?0.0:1.4,dz=horizontal?1.4:0.0;
            local moved=navRequire(navResolve(world,x-dx*0.5,z-dz*0.5,dx,dz));
            local distance=sqrt(moved.x*moved.x+moved.z*moved.z);
            if(p.buildingId=="village.door") {
                doorCount++;
                if(closed && (!moved.constrained || distance>0.65))throw "Closed door did not stop capsule sweep";
                if(!closed && (fabs(moved.x-dx)+fabs(moved.z-dz)>0.001))throw "Open doorway blocked capsule";
            } else {
                wallCount++;
                if(!navOverlapping(world,x,z))throw "Edited wall penetration was not detected";
                if(!moved.constrained || distance>0.65)throw "Wall did not stop capsule sweep";
            }
        }
        if(doorCount==0 || wallCount==0)throw "Collision fixture requires walls and door";
        // A large displacement must not tunnel through the village boundary.
        local border=navRequire(navResolve(world,2.125,2.125,-20.0,0.0));
        if(!border.constrained || 2.125+border.x<0.2799)throw "Capsule boundary result: x="+(2.125+border.x)+" constrained="+border.constrained;
        // Box3D permits 0.005 m linear slop; compare with an additional 0.0001 m float tolerance.
        local slide=navRequire(navResolve(world,2.125,2.125,-4.0,1.0));
        if(!slide.constrained || 2.125+slide.x<0.2799 || fabs(slide.z-1.0)>0.001)throw "Boundary sliding failed";
        foreach(p in snapshot.instances)if((p.instanceId in ids) && p.placementKind=="free" && p.level==0) {
            local angle=p.rotationDegrees*0.01745329252,c=cos(angle),s=sin(angle);
            local hit=navRequire(navResolve(world,p.worldX-c*0.8,p.worldY-s*0.8,c*1.6,s*1.6));
            if(!hit.constrained || sqrt(hit.x*hit.x+hit.z*hit.z)>0.7)throw "Rotated furniture did not block capsule";
        }
        world.destroy();
    } catch(error) {world.destroy();throw error;}
}
function navCollisionChecks() {
    if(town.getBuildingCount()==0)runWorkflow();
    chooseMode("visit");
    if(state.assemblies.len()==0)throw "Build a fixture first";
    selected=state.assemblies[0].id;
    local original=requireHouse(town,{op="snapshot"});
    local originalText=requireHouse(town,{op="encode",value=original});
    foreach(p in requireHouse(town,{op="members",assemblyId=selected}))if(p.buildingId=="village.door" && villageDoorClosed(p)) {
        message="Open fixture doors before collision test";return;
    }
    local failed=navCollisionBuild({obstacles=[{x=0.0,z=0.0,hw=-1.0,hd=1.0,angle=0.0,height=2.9}]});
    if(failed.ok){failed.value.destroy();throw "Invalid collider candidate unexpectedly succeeded";}
    if(failed.code!="PhysicsUnavailable")throw "Invalid collider failure lost diagnostics";
    navCollisionCheckSnapshot(original,selected,false);
    for(local q=0;q<4;q++) {
        local receipt=requireHouse(town,{op="move",assemblyId=selected,pose=pose(20,12,q)});
        local snapshot=requireHouse(town,{op="snapshot"});
        requireHouse(town,{op="applyReceipt",expected=receipt.after,state=receipt.before});
        navCollisionCheckSnapshot(snapshot,selected,false);
    }
    local members=requireHouse(town,{op="members",assemblyId=selected});
    foreach(p in members)if(p.buildingId=="village.door")p.properties.closed <- "true";
    local receipt=requireHouse(town,{op="remodel",assemblyId=selected,members=members});
    local closed=requireHouse(town,{op="snapshot"});
    requireHouse(town,{op="applyReceipt",expected=receipt.after,state=receipt.before});
    navCollisionCheckSnapshot(closed,selected,true);
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="snapshot"})})!=originalText)throw "Collision checks changed building world";
    refresh();navReset();navVisit(selected);navDemoTarget=selected;
    message="Collision checks PASS: walls, open/closed doors, four rotations, boundary sweep";
    print("building_village.collision checks PASS; walking with capsule started\n");
}
