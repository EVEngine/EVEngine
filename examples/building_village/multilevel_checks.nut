// Topology tests use real rotated assemblies and native snapshots; they do not mutate the
// live village. Actual gravity and locomotion are exercised by multiRunTest afterwards.
function multiCheckTopology(recipe) {
    local world=builder.newWorld(26,20,1.0);world.setGridPlane("xz");
    local id="00000000-0000-7000-8000-000000000098";
    for(local q=0;q<4;q++) {
        local placed=requireHouse(world,{op="place",blueprint=recipe,assemblyId=id,pose=pose(12,11,q)});
        local original=requireHouse(world,{op="snapshot"});
        local graph=navRequire(navBuild(original));
        local route=navRequire(navFindMulti(graph,2.125,2.125,0.0,{house=id,x=0.0,z=0.0,level=2}));
        local upper=false;foreach(p in route)if(p[2]>6.0)upper=true;
        if(!upper)throw "Rotated house route omitted upper floor";
        local members=requireHouse(world,{op="members",assemblyId=id});
        for(local i=members.len()-1;i>=0;i--)if(members[i].buildingId=="village.stairs")members.remove(i);
        requireHouse(world,{op="remodel",assemblyId=id,members=members});
        local removed=navRequire(navBuild(requireHouse(world,{op="snapshot"})));
        local rejected=navFindMulti(removed,2.125,2.125,0.0,{house=id,x=0.0,z=0.0,level=2});
        if(rejected.ok || rejected.code!="NoRoute")throw "Missing stairs retained upper-floor route";
        local text=requireHouse(world,{op="encode",value=original});
        requireHouse(world,{op="restore",state=requireHouse(world,{op="decode",json=text})});
        local restored=navRequire(navBuild(requireHouse(world,{op="snapshot"})));
        navRequire(navFindMulti(restored,2.125,2.125,0.0,{house=id,x=0.0,z=0.0,level=2}));
        // A low ceiling intersecting the upper stair run must invalidate the portal.
        local stair=restored.stairs[0];
        restored.portalBoxes.push({x=stair.x,z=stair.z,hw=3.0,hd=3.0,angle=0.0,bottom=2.5,height=0.1});
        local ceiling=navFindMulti(restored,2.125,2.125,0.0,{house=id,x=0.0,z=0.0,level=2});
        if(ceiling.ok || ceiling.code!="NoRoute")throw "Low ceiling retained stair route";
        requireHouse(world,{op="remove",assemblyId=id});
    }
    print("building_village.multilevel topology PASS (four rotations, missing stairs, ceiling, restore)\n");
}
