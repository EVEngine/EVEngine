// Ordinary blueprint/assembly operations: the demonstration is saved and edited exactly
// like user-built houses. It does not maintain a second scene or teleport the character.
function multiRecipe() {
    local world=builder.newWorld(8,8,1.0);world.setGridPlane("xz");
    for(local level=0;level<3;level++) {
        world.setActiveLevel(level);
        for(local z=1;z<=6;z++)for(local x=1;x<=6;x++) {
            if(level>0 && x>=(level==1?2:5) && x<=(level==1?3:6) && z>=2 && z<=5)continue;
            if(world.placeAt("village.floor",x,z,0.0)<=0)throw "Multilevel floor failed";
        }
        for(local i=1;i<=6;i++) {
            if(world.placeEdge("village.wall",i,1,"north")<=0)throw "Multilevel wall failed";
            if(level!=0 || i!=4)if(world.placeEdge("village.wall",i,7,"north")<=0)throw "Multilevel wall failed";
            if(world.placeEdge("village.wall",1,i,"west")<=0)throw "Multilevel wall failed";
            if(world.placeEdge("village.wall",7,i,"west")<=0)throw "Multilevel wall failed";
        }
    }
    world.setActiveLevel(1);
    if(world.placeAt("village.stairs",5,2,0.0)<=0)throw "Upper stair placement failed";
    world.setActiveLevel(0);
    if(world.placeEdge("village.door",4,7,"north")<=0)throw "Multilevel door failed";
    if(world.placeAt("village.stairs",2,2,0.0)<=0)throw "Stair placement failed";
    if(world.placeFree("village.chair",5.5,3.5,0.0,45.0)<=0)throw "Multilevel furniture failed";
    local members=[];for(local i=0;i<world.getBuildingCount();i++)members.push(world.getBuildingInstanceAt(i));
    local recipe=requireHouse(world,{op="capture",blueprintId="00000000-0000-7000-8000-000000000099",members=members,pose=pose(0,0)});
    return recipe;
}
function multiCreate(recipe=null) {
    multiTestPhase="";
    if(recipe==null)recipe=multiRecipe();
    chooseMode("town");local id=newHouseId();
    local receipt=callHouse(town,{op="place",blueprint=recipe,assemblyId=id,pose=pose(12,11)});
    if(receipt==null)return;
    remember(receipt);serial++;selected=id;multiDemoId=id;editLevel=0;
    blueprint=recipe;persistValue("saves/house-blueprint.json",recipe);
    chooseMode("visit");navReset();message="Three-floor house built. Visit upper levels or use WASD to climb stairs.";
}
function multiVisit(level) {editLevel=level;chooseMode("visit");navVisit(selected,level);}
function multiChangeStairs() {
    if(selected=="")return;
    local members=requireHouse(town,{op="members",assemblyId=selected}),removed=false;
    for(local i=members.len()-1;i>=0;i--)if(members[i].buildingId=="village.stairs"){members.remove(i);removed=true;}
    if(!removed){message="No stairs to remove; Undo restores a removed staircase";return;}
    remember(callHouse(town,{op="remodel",assemblyId=selected,members=members}));
}
function multiDropFloor() {
    if(selected=="" || navLevel==0){message="Stand on an upper floor first";return;}
    local members=requireHouse(town,{op="members",assemblyId=selected}),removed=false;
    // Remove every tile intersecting the capsule, including seams; keep other levels intact.
    for(local i=members.len()-1;i>=0;i--) {
        local p=members[i];if(p.buildingId!="village.floor" || p.level!=navLevel)continue;
        if(navX+NAV_RADIUS>p.cellX && navX-NAV_RADIUS<p.cellX+1 && navZ+NAV_RADIUS>p.cellY && navZ-NAV_RADIUS<p.cellY+1){members.remove(i);removed=true;}
    }
    if(removed){navGoal=null;navRoute=[];remember(callHouse(town,{op="remodel",assemblyId=selected,members=members}));}
}
function multiRunTest() {
    if(multiDemoId==""){local recipe=multiRecipe();multiCheckTopology(recipe);multiCreate(recipe);}
    if(multiDemoId=="")return;
    selected=multiDemoId;navReset();multiTestPhase="ascending";multiVisit(2);
    message="Testing gravity and ascent; visitor will then return outside";
}
function multiUpdateTest() {
    if(multiTestPhase=="" || multiTestPhase=="PASS")return;
    if(multiTestPhase=="ascending" && navGrounded && navRoute.len()==0 && navLevel==2 && navInside==multiDemoId) {
        if(navY<6.08 || navStairSteps<20)throw "Ascent did not use physical stair steps";
        print("building_village.multilevel ascent PASS\n");multiTestPhase="descending";editLevel=0;
        navGoal={house="",x=2.125,z=2.125,level=0};navPlan();
    } else if(multiTestPhase=="descending" && navGrounded && navRoute.len()==0 && navInside=="" && fabs(navX-2.125)<0.1 && fabs(navZ-2.125)<0.1) {
        if(fabs(navY)>0.02)throw "Visitor did not return to ground";
        multiTestPhase="PASS";message="PASS: gravity, physical staircase ascent and descent";
        print("building_village.multilevel PASS\n");
    }
}
