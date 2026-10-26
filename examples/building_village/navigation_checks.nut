// User-invoked acceptance path. Exercise actual building transactions and the same A* adapter.
function navRequire(result) { if(!result.ok)throw result.code+": "+result.message;return result.value; }
function navCheckPath(g,points) {
    if(points.len()==0)throw "Expected nonempty navigation path";
    foreach(i,p in points) {
        local x=floor(p[0]*NAV_SCALE).tointeger(),z=floor(p[1]*NAV_SCALE).tointeger();
        if(g.blocked[navIndex(x,z,g.width)])throw "Path crosses a blocked cell";
        if(i>0 && fabs(p[0]-points[i-1][0])+fabs(p[1]-points[i-1][1])>0.250001)throw "Path cuts a grid corner";
    }
}
function navRunDemo() {
    if(town.getBuildingCount()==0)runWorkflow();
    chooseMode("visit");
    if(state.assemblies.len()==0){navPhase="Build a house first";return;}
    selected=state.assemblies[0].id;navReset();
    if(navPhase!="Ready"){message=navPhase;return;}
    local original=requireHouse(town,{op="snapshot"});
    local originalText=requireHouse(town,{op="encode",value=original});
    local doors=0;
    foreach(p in requireHouse(town,{op="members",assemblyId=selected}))if(p.buildingId=="village.door")doors++;
    if(doors==0){navPhase="This saved house has no door; use a new blueprint";return;}
    // The demo is intended for the bundled open-door house. Do not rewrite user intent.
    foreach(p in requireHouse(town,{op="members",assemblyId=selected}))if(p.buildingId=="village.door" && villageDoorClosed(p)) {
        navPhase="Open this house's doors before running the test";return;
    }
    local goal={house=selected,x=0.0,z=0.0};
    navCheckPath(navGraph,navRequire(navFind(navGraph,navX,navZ,goal)));
    foreach(p in original.instances)if(p.placementKind=="free" && p.level==0) {
        local blocked=navFind(navGraph,navX,navZ,{house="",x=p.worldX,z=p.worldY});
        if(blocked.ok || blocked.code!="BlockedGoal")throw "Furniture did not block visitor clearance";
    }
    // Rotate/move the whole house through the real transaction API. Each temporary
    // edit is undone before checking the owning snapshot, including assertion failures.
    for(local q=0;q<4;q++) {
        local moved=requireHouse(town,{op="move",assemblyId=selected,pose=pose(20,12,q)});
        local movedSnapshot=requireHouse(town,{op="snapshot"});
        requireHouse(town,{op="applyReceipt",expected=moved.after,state=moved.before});
        local graph=navRequire(navBuild(movedSnapshot));
        navCheckPath(graph,navRequire(navFind(graph,navX,navZ,goal)));
    }
    local removed=requireHouse(town,{op="remove",assemblyId=selected});
    local removedSnapshot=requireHouse(town,{op="snapshot"});
    requireHouse(town,{op="applyReceipt",expected=removed.after,state=removed.before});
    local gone=navFind(navRequire(navBuild(removedSnapshot)),navX,navZ,goal);
    if(gone.ok || gone.code!="TargetGone")throw "Removed house retained a navigation target";
    // Close doors through the canonical remodel operation, rebuild, and prove rejection.
    local members=requireHouse(town,{op="members",assemblyId=selected});
    foreach(p in members)if(p.buildingId=="village.door")p.properties.closed <- "true";
    local closed=requireHouse(town,{op="remodel",assemblyId=selected,members=members});
    local closedSnapshot=requireHouse(town,{op="snapshot"});
    // Restore before running derived checks so assertion failures do not leave a test edit.
    requireHouse(town,{op="applyReceipt",expected=closed.after,state=closed.before});
    local closedGraph=navRequire(navBuild(closedSnapshot));
    local rejected=navFind(closedGraph,navX,navZ,goal);
    if(rejected.ok || rejected.code!="NoRoute")throw "Closed doorway did not reject entry";
    // Persisted door state must rebuild the same blocked route after a real world restore.
    requireHouse(town,{op="restore",state=closedSnapshot});
    local restoredSnapshot=requireHouse(town,{op="snapshot"});
    requireHouse(town,{op="restore",state=original});
    local restoredGraph=navRequire(navBuild(restoredSnapshot));
    local restoredPath=navFind(restoredGraph,navX,navZ,goal);
    if(restoredPath.ok || restoredPath.code!="NoRoute")throw "Closed door changed after restore";
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="snapshot"})})!=originalText)throw "Navigation test changed world";
    refresh();navVisit(selected);
    if(navPhase!="Walking" && navPhase!="Waiting to land")throw "Open door did not restore route";
    navDemoTarget=selected;message="Door closure/restore checks passed; visitor is walking inside";
    print("building_village.navigation checks PASS; walking started\n");
}
