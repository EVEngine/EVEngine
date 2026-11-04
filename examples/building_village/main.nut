dofile("workshop.nut");
dofile("workshop_checks.nut");
dofile("navigation.nut");
dofile("multilevel.nut");
dofile("collision.nut");
dofile("gravity.nut");
dofile("multilevel_demo.nut");
dofile("multilevel_checks.nut");
dofile("collision_checks.nut");
dofile("view3d.nut");
dofile("navigation_view.nut");
dofile("navigation_checks.nut");
// One authoritative building world per workspace; drawings only project its snapshots.
persist builder = null
persist workshop = null
persist town = null
persist blueprint = null
persist state = null
persist mode = "workshop"
persist palette = 0
persist quarter = 0
persist serial = 1
persist selected = ""
persist history = []
persist redoHistory = []
persist preview = []
persist previewKey = ""
persist previewStatus = ""
persist message = ""
persist lastLeft = false
persist lastRight = false
persist templateParts = {}
persist uiReady = false

function pose(x, y, q = 0) { return { x=x, y=y, level=0, quarterTurns=q, elevation=0.0 }; }
function currentWorld() { return mode == "workshop" ? workshop : town; }
function callHouse(world, request) {
    request.version <- 1;
    local result = eve.buildingHouseRequest(world, request);
    if (!result.ok) { message = "Rejected: " + result.code; if(result.diagnostics.len()>0)message += " / " + result.diagnostics[0].message; return null; }
    return result.value;
}
function refresh() {
    state = callHouse(currentWorld(), {op="snapshot"});
    previewKey = "";viewDirty=true;ghostDirty=true;
    if(navMap!=null)navRebuild(mode=="workshop"?requireHouse(town,{op="snapshot"}):state);
}
function remember(receipt) {
    if (receipt == null) return;
    history.push(receipt); redoHistory.clear(); message="House change committed"; refresh();
}
function newHouseId() {
    while(true) {
        local id="00000000-0000-7000-8000-"+format("%012d",serial);local used=false;
        if(state!=null)foreach(a in state.assemblies)if(a.id==id)used=true;
        if(!used)return id;
        serial++;
    }
}
function persistValue(name, value) {
    local text = callHouse(currentWorld(), {op="encode", value=value});
    if (text == null) return;
    if (fs.writeTextAtomic(name, text) != 1) { message="File save failed"; return; }
    message="Saved " + name + " to the application write directory";
}
function captureHouse(save = true) {
    local members=[];
    for(local i=0;i<workshop.getBuildingCount();i++) members.push(workshop.getBuildingInstanceAt(i));
    local value=callHouse(workshop,{op="capture",blueprintId="00000000-0000-7000-8000-000000000001",members=members,pose=pose(0,0)});
    if(value==null)return;
    blueprint=value;previewKey="";ghostDirty=true;
    if(save)persistValue("saves/house-blueprint.json",blueprint);
}
function chooseMode(next) {
    mode=next;cutaway=mode=="workshop" || mode=="interior";preview=[];refresh();
    message=mode=="workshop"?"Workshop: edit parts, then capture blueprint":(mode=="interior"?"Interior: select a part tool; Roof removes by its footprint":(mode=="visit"?"Walk: click a destination, or use WASD / direction buttons":"Village: place houses or select one to remodel"));
}
function selectedAt(x,y) {
    if(state==null)return "";
    foreach(a in state.assemblies) foreach(m in a.members) foreach(p in state.instances)
        if(p.instanceId==m.record && p.placementKind=="cell" && p.cellX==x && p.cellY==y)return a.id;
    return "";
}
function partAt(parts,x,y) {
    if(palette==6) {
        foreach(p in parts)if(p.buildingId=="village.stairs" && p.level==editLevel) {
            local s=villageStair(p),hw=s.ax==0.0?1.0:2.0,hd=s.ax==0.0?2.0:1.0;
            if(x>=s.x-hw && x<s.x+hw && y>=s.z-hd && y<s.z+hd)return p.instanceId;
        }
        return 0;
    }
    if(palette==4) {
        foreach(p in parts)if(p.buildingId=="village.roof" && p.level==editLevel+1) {
            local turn=((p.rotationDegrees/90.0).tointeger()%2+2)%2;
            if(x>=p.cellX && y>=p.cellY && x<p.cellX+(turn==0?4:3) && y<p.cellY+(turn==0?3:4))return p.instanceId;
        }
        return 0;
    }
    // Furniture is picked first; floor is last so removing an interior item is predictable.
    foreach(kind in ["free","corner","edge","cell"])
        foreach(p in parts) if(p.buildingId!="village.roof" && p.level==editLevel && p.placementKind==kind && p.cellX==x && p.cellY==y)return p.instanceId;
    return 0;
}
function editWorkshop(x,y,remove) {
    if(!remove && palette==6 && editLevel==3){message="Stairs must start below level 3";return;}
    local before=requireHouse(workshop,{op="snapshot"});
    workshop.setActiveLevel(editLevel);
    if(remove) {
        local id=partAt(state.instances,x,y);
        if(id>0 && !workshop.removeBuilding(id))message="Removal rejected: a floor may still support another part";
    } else {
        local id=0;
        if(palette==0)id=workshop.placeAt("village.floor",x,y,0.0);
        if(palette==1)id=workshop.placeEdge("village.wall",x,y,quarter%2==0?"north":"west");
        if(palette==2)id=workshop.placeCorner("village.post",x,y);
        if(palette==3)id=workshop.placeFree("village.chair",x+0.5,y+0.5,0.0,quarter*90.0);
        if(palette==6)id=workshop.placeAt("village.stairs",x,y,quarter*90.0);
        if(palette==5)id=workshop.placeEdge("village.door",x,y,quarter%2==0?"north":"west");
        if(palette==4) {
            workshop.setActiveLevel(editLevel+1);id=workshop.placeAt("village.roof",x,y,quarter*90.0);workshop.setActiveLevel(editLevel);
        }
        if(id<=0)message="Part placement rejected (roof needs a complete floor footprint)";
    }
    workshopRecord(before);refresh();
}
function remodelAt(x,y,remove) {
    if(!remove && palette==6 && editLevel==3){message="Stairs must start below level 3";return;}
    if(selected==""){message="Select a house with right click first";return;}
    local parts=callHouse(town,{op="members",assemblyId=selected});if(parts==null)return;
    if(remove) {
        local id=partAt(parts,x,y);if(id==0)return;
        for(local i=parts.len()-1;i>=0;i--)if(parts[i].instanceId==id)parts.remove(i);
    } else {
        local definition=["village.floor","village.wall","village.post","village.chair","village.roof","village.door","village.stairs"][palette];
        if(!(definition in templateParts)){message="Capture a template containing this part first";return;}
        local p=clone templateParts[definition];p.instanceId=1;p["new"] <- true;
        p.level=palette==4?editLevel+1:editLevel;
        p.cellX=x;p.cellY=y;p.edgeX=x;p.edgeY=y;p.cornerX=x;p.cornerY=y;
        p.worldX=x.tofloat();p.worldY=y.tofloat();p.rotationDegrees=quarter*90.0;
        p.edgeAxis=quarter%2==0?"horizontal":"vertical";
        if(p.placementKind=="free"){p.worldX+=0.5;p.worldY+=0.5;}
        parts.push(p);
    }
    remember(callHouse(town,{op="remodel",assemblyId=selected,members=parts}));
}
function undoStep(redo) {
    if(mode=="workshop"){workshopUndo(redo);return;}
    local source=redo?redoHistory:history;
    if(source.len()==0)return;
    local r=source.top();
    local result=callHouse(town,{op="applyReceipt",expected=redo?r.before:r.after,state=redo?r.after:r.before});
    if(result==null)return;
    source.pop();(redo?history:redoHistory).push(r);message=redo?"Redo completed":"Undo completed";refresh();
}
function requireHouse(world,request) {
    local value=callHouse(world,request);if(value==null)throw message;return value;
}
function runWorkflow() {
    if(town.getBuildingCount()>0){message="Demo requires an empty village. Continue editing these houses or restart the example.";return;}
    captureHouse();
    local loaded=requireHouse(workshop,{op="decode",json=fs.readText("saves/house-blueprint.json")});
    chooseMode("town");
    local first=newHouseId();
    remember(requireHouse(town,{op="place",blueprint=loaded,assemblyId=first,pose=pose(5,5)}));serial++;
    local second=newHouseId();
    remember(requireHouse(town,{op="place",blueprint=loaded,assemblyId=second,pose=pose(17,7,1)}));serial++;
    local beforeOther=requireHouse(town,{op="members",assemblyId=second});
    local beforeOtherText=requireHouse(town,{op="encode",value=beforeOther});
    local parts=requireHouse(town,{op="members",assemblyId=first});
    foreach(p in parts)if(p.placementKind=="free") {p.worldX+=0.2;p.rotationDegrees=45.0;p.properties.paint <- "blue";}
    local remodel=requireHouse(town,{op="remodel",assemblyId=first,members=parts});remember(remodel);
    local afterOther=requireHouse(town,{op="members",assemblyId=second});
    if(requireHouse(town,{op="encode",value=afterOther})!=beforeOtherText)throw "Other house changed during remodel";
    local roofParts=requireHouse(town,{op="members",assemblyId=first});
    local withoutRoof=[],withoutFloor=[],removedFloor=false,roofCount=0;
    foreach(p in roofParts) {
        if(p.buildingId=="village.roof")roofCount++;else withoutRoof.push(p);
        if(!removedFloor && p.buildingId=="village.floor")removedFloor=true;else withoutFloor.push(p);
    }
    if(roofCount!=1)throw "Expected one persistent roof per house";
    local beforeRejected=requireHouse(town,{op="encode",value=requireHouse(town,{op="snapshot"})});
    local rejected=eve.buildingHouseRequest(town,{version=1,op="remodel",assemblyId=first,members=withoutFloor});
    if(rejected.ok)throw "Roof allowed removal of its required floor support";
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="snapshot"})})!=beforeRejected)throw "Rejected roof support edit mutated world";
    local roofRemoval=requireHouse(town,{op="remodel",assemblyId=first,members=withoutRoof});
    foreach(p in requireHouse(town,{op="members",assemblyId=first}))if(p.buildingId=="village.roof")throw "Roof removal did not commit";
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="members",assemblyId=second})})!=beforeOtherText)throw "Roof removal changed another house";
    requireHouse(town,{op="applyReceipt",expected=roofRemoval.after,state=roofRemoval.before});
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="snapshot"})})!=beforeRejected)throw "Roof undo differed";
    local saved=requireHouse(town,{op="snapshot"});
    local savedText=requireHouse(town,{op="encode",value=saved});
    if(fs.writeTextAtomic("saves/village-world.json",savedText)!=1)throw "Village save failed";
    local restored=requireHouse(town,{op="decode",json=fs.readText("saves/village-world.json")});
    requireHouse(town,{op="restore",state=restored});
    requireHouse(town,{op="applyReceipt",expected=remodel.after,state=remodel.before});
    requireHouse(town,{op="applyReceipt",expected=remodel.before,state=remodel.after});
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="snapshot"})})!=savedText)throw "World round-trip differed";
    selected=first;refresh();message="PASS: blueprint file → two houses → independent remodel → world file → undo/redo";
    print("building_village.workflow PASS (roof support, independent removal and restore included)\n");
}
function action(id) {
    if(id=="view"){view3d=!view3d;viewDirty=true;ghostDirty=true;}
    else if(id=="cutaway"){cutaway=!cutaway;viewDirty=true;ghostDirty=true;}
    else if(id=="demo")runWorkflow();
    else if(id=="walk")chooseMode("visit");
    else if(id=="visit"){chooseMode("visit");navVisit(selected,editLevel);}
    else if(id=="outside"){editLevel=0;chooseMode("visit");navWalkPoint(2.125,2.125);}
    else if(id=="door")navToggleDoor();
    else if(id=="resetVisitor")navReset();
    else if(id=="walkDemo")navRunDemo();
    else if(id=="collisionDemo")navCollisionChecks();
    else if(id=="multiDemo")multiRunTest();
    else if(id=="resumeDemo")workshopRunChecks();
    else if(id=="upper")multiVisit(navLevel<3?navLevel+1:3);
    else if(id=="ground")multiVisit(navLevel>0?navLevel-1:0);
    else if(id=="removeStairs")multiChangeStairs();
    else if(id=="dropFloor")multiDropFloor();
    else if(id=="levelUp"){editLevel=(editLevel+1)%4;viewDirty=true;}
    else if(id=="part6")palette=6;
    else if(id=="north")navNudge(0.0,-1.0);
    else if(id=="south")navNudge(0.0,1.0);
    else if(id=="west")navNudge(-1.0,0.0);
    else if(id=="east")navNudge(1.0,0.0);
    else if(id=="workshop")chooseMode("workshop");
    else if(id=="town")chooseMode("town");
    else if(id=="interior")chooseMode("interior");
    else if(id=="selectNext") {
        if(mode=="workshop")chooseMode("town");
        if(state!=null && state.assemblies.len()>0) {
            local next=0;
            foreach(i,a in state.assemblies)if(a.id==selected)next=(i+1)%state.assemblies.len();
            selected=state.assemblies[next].id;message="Selected "+selected;viewDirty=true;
        }
    }
    else if(id=="capture")captureHouse();
    else if(id=="loadBlueprint")workshopLoad();
    else if(id=="rotate"){quarter=(quarter+1)%4;previewKey="";}
    else if(id=="undo")undoStep(false);
    else if(id=="redo")undoStep(true);
    else if(id=="save"){local value=callHouse(town,{op="snapshot"});if(value!=null)persistValue("saves/village-world.json",value);}
    else if(id=="load") {
        local text=fs.readText("saves/village-world.json");
        local value=callHouse(town,{op="decode",json=text});
        if(value!=null && callHouse(town,{op="restore",state=value})!=null){history.clear();redoHistory.clear();selected="";chooseMode("town");}
    } else if(id=="remove" && selected!="")remember(callHouse(town,{op="remove",assemblyId=selected}));
    else if(id=="move" && selected!="") {
        // The move destination is the upper-right reserved area; preview remains an owning value.
        remember(callHouse(town,{op="move",assemblyId=selected,pose=pose(selected==multiDemoId?12:20,selected==multiDemoId?11:12,quarter)}));
    }
    for(local i=0;i<6;i++)if(id=="part"+i)palette=i;
}
eve_init <- function() {
    if(!fs.setIdentity("building-village",false) || !fs.createDirectory("saves"))throw "Could not prepare save directory";
    builder=eve.Building();workshop=builder.newWorld(8,8,1.0);town=builder.newWorld(26,20,1.0);
    workshop.setGridPlane("xz");town.setGridPlane("xz");
    local count=builder.registerBuildingsFromJson(@"[
      {""id"":""village.floor"",""channel"":""floor"",""tags"":[""support""]},
      {""id"":""village.wall"",""placementKind"":""edge"",""channel"":""wall"",""edgeThicknessCells"":0.12},
      {""id"":""village.post"",""placementKind"":""corner"",""channel"":""post""},
      {""id"":""village.chair"",""placementKind"":""free"",""channel"":""furniture"",""rotationMode"":""free"",""freeFootprintWidthCells"":0.6,""freeFootprintHeightCells"":0.4,""blockedByEdgeChannels"":[""wall""]},
      {""id"":""village.roof"",""channel"":""roof"",""footprintW"":4,""footprintH"":3,""structuralRole"":""roof"",""supportMode"":""cell_below"",""supportTag"":""support""},
      {""id"":""village.door"",""placementKind"":""edge"",""channel"":""wall"",""edgeThicknessCells"":0.12},
      {""id"":""village.stairs"",""channel"":""stairs"",""footprintW"":2,""footprintH"":4}
    ]");
    if(count!=7)throw "House definitions failed";
    for(local y=1;y<4;y++)for(local x=1;x<5;x++)if(workshop.placeAt("village.floor",x,y,0.0)<=0)throw "Floor placement failed";
    for(local x=1;x<5;x++){
        if(workshop.placeEdge("village.wall",x,1,"north")<=0)throw "Wall placement failed";
        if(x!=2 && workshop.placeEdge("village.wall",x,3,"south")<=0)throw "Wall placement failed";
    }
    for(local y=1;y<4;y++){
        if(workshop.placeEdge("village.wall",1,y,"west")<=0)throw "Wall placement failed";
        if(workshop.placeEdge("village.wall",4,y,"east")<=0)throw "Wall placement failed";
    }
    if(workshop.placeCorner("village.post",1,1)<=0)throw "Post placement failed";
    if(workshop.placeFree("village.chair",2.5,2.5,0.0,0.0)<=0)throw "Furniture placement failed";
    if(workshop.placeEdge("village.door",2,4,"north")<=0)throw "Door placement failed";
    workshop.setActiveLevel(1);
    local roof=workshop.placeAt("village.roof",1,1,0.0);workshop.setActiveLevel(0);
    if(roof<=0)throw "Roof placement failed";
    workshopInitTools();captureHouse(false);refresh();villageInitView();navInit();
    message="Workshop ready. Load saved blueprint to continue; existing files are unchanged.";
    if("dev" in eve)eve.dev.markStateRoot("navObservation");
    if(!uiReady) {
        ui.setScale(1.0);ui.setTheme("dark");ui.beginBuild();
        ui.beginWindow("House workshop / village","root");
        ui.beginColumn("panel",1.0);

        ui.beginRow("viewControls",8.0);ui.button("3D / Plan","view");ui.button("Cutaway / Full walls","cutaway");ui.end();
        ui.button("Run complete workflow demo","demo");
        ui.button("1. Workshop: build parts","workshop");ui.button("2. Capture + save blueprint","capture");
        ui.beginRow("resumeTools",5.0);ui.button("Load blueprint","loadBlueprint");ui.button("Test resume","resumeDemo");ui.end();
        ui.button("3. Village: place houses","town");ui.button("Select next house","selectNext");ui.button("4. Remodel selected house","interior");
        ui.beginRow("parts",5.0);foreach(i,label in ["Floor","Wall","Post","Furniture"])ui.button(label,"part"+i);ui.end();
        ui.beginRow("roofTools",8.0);ui.button("Roof 4x3","part4");ui.button("Door","part5");ui.button("Rotate 90 degrees","rotate");ui.end();ui.button("Move selected to demo pad","move");ui.button("Remove selected house","remove");
        ui.beginRow("history",8.0);ui.button("Undo","undo");ui.button("Redo","redo");ui.end();
        ui.beginRow("files",8.0);ui.button("Save village","save");ui.button("Load village","load");ui.end();
        ui.beginRow("walking",5.0);ui.button("Walk mode","walk");ui.button("Visit house","visit");ui.button("Outside","outside");ui.end();
        ui.beginRow("doors",5.0);ui.button("Open / close door","door");ui.button("Reset visitor","resetVisitor");ui.end();
        ui.beginRow("walkTests",5.0);ui.button("Test navigation","walkDemo");ui.button("Test collision","collisionDemo");ui.end();
        ui.beginRow("manual",5.0);foreach(dir in ["north","south","west","east"])ui.button(dir,dir);ui.end();
        ui.beginRow("levels",4.0);ui.button("Floor +","levelUp");ui.button("Stairs","part6");ui.button("Go up","upper");ui.button("Go down","ground");ui.end();
        ui.beginRow("multiTests",4.0);ui.button("Test floors","multiDemo");ui.button("Remove stairs","removeStairs");ui.button("Drop floor","dropFloor");ui.end();
        ui.textWrapped("",340.0,"navigation");
        ui.textWrapped("",340.0,"status");ui.end();ui.end();ui.mountBuildAs("village");ui.select("village");
        ui.setHostPos(890.0,16.0,0.0,0.0);ui.setHostSize(375.0,770.0);uiReady=true;
    }
    gfx.setBackgroundColor(0.055,0.07,0.10,1.0);
};
eve_update <- function(dt) {
    local clicked=ui.consumeClick();while(clicked!=""){action(clicked.slice(8));clicked=ui.consumeClick();}
    if(key_just_pressed("r"))action("rotate");if(key_just_pressed("z"))undoStep(false);if(key_just_pressed("y"))undoStep(true);
    villageSyncView();
    local hit=villageMouseCell();local inside=hit!=null;
    local x=inside?hit[0]:0,y=inside?hit[1]:0;
    local left=mouse.isDown(1);local right=mouse.isDown(2);
    if(inside && mode=="town" && blueprint!=null) {
        local key=x+":"+y+":"+quarter+":"+serial;
        if(key!=previewKey){
            previewKey=key;
            local result=eve.buildingHouseRequest(town,{version=1,op="preview",blueprint=blueprint,assemblyId=newHouseId(),pose=pose(x,y,quarter)});
            preview=result.ok?result.value:[];ghostDirty=true;previewStatus=result.ok?"Ready to place":"Placement blocked here";
        }
    }
    if(!inside && preview.len()>0){preview=[];previewKey="";ghostDirty=true;}
    if(inside && left && !lastLeft) {
        if(mode=="workshop")editWorkshop(x,y,false);
        else if(mode=="interior")remodelAt(x,y,false);
        else if(mode=="visit")navWalkPoint(x+0.5,y+0.5);
        else placeHouseAt(x,y);
    }
    if(inside && right && !lastRight) {
        if(mode=="workshop")editWorkshop(x,y,true);
        else if(mode=="interior")remodelAt(x,y,true);
        else {selected=selectedAt(x,y);viewDirty=true;message="Selected "+selected;}
    }
    lastLeft=left;lastRight=right;navUpdate(dt);workshopUpdateCheck();villageSyncView();navDraw3D();navPublish();
    ui.setText("navigation","Visitor: "+navPhase+" / level "+navLevel+" / y "+format("%.2f",navY));
    ui.setText("status","View: "+(view3d?"3D":"Plan")+" / Mode: "+mode+" / edit floor: "+editLevel+" / Tool: "+["Floor","Wall","Post","Furniture","Roof","Door","Stairs"][palette]+"\nLeft: "+(mode=="visit"?"walk":"build")+". Right: select/remove. R/Z/Y: rotate/undo/redo.\n"+message+(mode=="town" && inside?"\nPreview: "+previewStatus:""));
};
function drawParts(parts,cell,alpha) {
    foreach(p in parts) {
        if(p.level!=editLevel)continue;
        local x=24+p.cellX*cell,y=50+p.cellY*cell;
        if(p.buildingId=="village.roof") {
            if(cutaway)continue;
            local turn=((p.rotationDegrees/90.0).tointeger()%2+2)%2;
            local w=(turn==0?4:3)*cell,h=(turn==0?3:4)*cell;
            gfx.drawSolidRect(x,y,w,3.0,0.75,0.30,0.18,alpha);
            gfx.drawSolidRect(x,y+h-3,w,3.0,0.75,0.30,0.18,alpha);
            gfx.drawSolidRect(x,y,3.0,h,0.75,0.30,0.18,alpha);
            gfx.drawSolidRect(x+w-3,y,3.0,h,0.75,0.30,0.18,alpha);
        } else if(p.buildingId=="village.stairs") {
            foreach(step in villageStair(p).boxes)gfx.drawSolidRect(24+(step.x-step.hw)*cell,50+(step.z-step.hd)*cell,step.hw*2.0*cell-1,step.hd*2.0*cell-1,0.62,0.47,0.30,alpha);
        } else if(p.placementKind=="cell")gfx.drawSolidRect(x+2,y+2,cell-4,cell-4,0.22,0.48,0.55,alpha);
        else if(p.buildingId=="village.door" && !villageDoorClosed(p)) {
            gfx.drawSolidRect(24+p.edgeX*cell,50+p.edgeY*cell,p.edgeAxis=="horizontal"?cell:3.0,p.edgeAxis=="vertical"?cell:3.0,0.2,0.9,0.6,alpha);
        }
        else if(p.placementKind=="edge")gfx.drawSolidRect(24+p.edgeX*cell-2,50+p.edgeY*cell-2,p.edgeAxis=="horizontal"?cell+4.0:4.0,p.edgeAxis=="vertical"?cell+4.0:4.0,0.85,0.72,0.46,alpha);
        else if(p.placementKind=="corner")gfx.drawSolidRect(24+p.cornerX*cell-4,50+p.cornerY*cell-4,8.0,8.0,0.95,0.50,0.35,alpha);
        else {
            local blue=("paint" in p.properties) && p.properties.paint=="blue";
            gfx.drawSolidRect(24+p.worldX*cell-cell*0.24,50+p.worldY*cell-cell*0.18,cell*0.48,cell*0.36,blue?0.25:0.55,blue?0.60:0.80,blue?0.95:0.39,alpha);
        }
    }
}
eve_render <- function() {
    gfx.clear();
    if(view3d){gfx.render3D();ui.beginFrameAndRender();return;}
    local cell=mode=="workshop"?80.0:32.0;
    for(local y=0;y<currentWorld().getHeight();y++)for(local x=0;x<currentWorld().getWidth();x++)
        gfx.drawSolidRect(24+x*cell,50+y*cell,cell-1,cell-1,0.095,0.12,0.16,1.0);
    if(state!=null)drawParts(state.instances,cell,1.0);
    if(mode=="town")drawParts(preview,cell,0.35);
    navDrawPlan();ui.beginFrameAndRender();
};
