// Main-thread authoring operations. Building remains authoritative; history owns snapshots.
// One reusable staging world belongs to builder, just like workshop/town; no per-load worlds.
persist workshopHistory = []
persist workshopRedo = []
persist workshopStage = null
persist workshopEmpty = null

function workshopInitTools() {
    templateParts={};
    foreach(p in requireHouse(workshop,{op="snapshot"}).instances)templateParts[p.buildingId] <- p;
    workshopStage=builder.newWorld(8,8,1.0);workshopStage.setGridPlane("xz");
    workshopEmpty=requireHouse(workshopStage,{op="snapshot"});
    if(workshopStage.placeAt("village.stairs",1,1,0.0)<=0)throw "Stair tool initialization failed";
    foreach(p in requireHouse(workshopStage,{op="snapshot"}).instances)templateParts[p.buildingId] <- p;
    requireHouse(workshopStage,{op="restore",state=workshopEmpty});
}
function workshopRecord(before) {
    local after=requireHouse(workshop,{op="snapshot"});
    if(requireHouse(workshop,{op="encode",value=before})==requireHouse(workshop,{op="encode",value=after}))return;
    workshopHistory.push({before=before,after=after});workshopRedo.clear();
}
function workshopUndo(redo) {
    local source=redo?workshopRedo:workshopHistory;
    if(source.len()==0){message="No workshop change to "+(redo?"redo":"undo");return;}
    local r=source.top();
    if(callHouse(workshop,{op="applyReceipt",expected=redo?r.before:r.after,state=redo?r.after:r.before})==null)return;
    source.pop();(redo?workshopHistory:workshopRedo).push(r);refresh();
    message=redo?"Workshop redo completed":"Workshop undo completed";
}
// Imports preserve the canonical schema. Only assembly membership is detached to expose
// editable loose parts; the native restore revalidates all instances and structural support.
function workshopPrepare(recipe) {
    local reset=eve.buildingHouseRequest(workshopStage,{version=1,op="restore",state=workshopEmpty});
    if(!reset.ok)return navFail(reset.code,"Could not reset blueprint staging world");
    local placed=eve.buildingHouseRequest(workshopStage,{version=1,op="place",blueprint=recipe,
        assemblyId="00000000-0000-7000-8000-000000000097",pose=pose(0,0)});
    if(!placed.ok)return navFail(placed.code,"Blueprint does not fit the 8x8 workshop or violates building rules");
    local snapshot=requireHouse(workshopStage,{op="snapshot"});snapshot.assemblies=[];
    local scope=navCheckScope(snapshot);if(!scope.ok)return scope;
    snapshot.worldId=workshopEmpty.worldId;snapshot.activeLevel=0;
    return navSuccess(snapshot);
}
function workshopLoadText(text) {
    local decoded=eve.buildingHouseRequest(workshop,{version=1,op="decode",json=text});
    if(!decoded.ok)return navFail(decoded.code,"Blueprint JSON could not be read");
    local checked=eve.buildingHouseRequest(workshop,{version=1,op="validateBlueprint",blueprint=decoded.value});
    if(!checked.ok)return navFail(checked.code,"Blueprint schema or version is invalid");
    local prepared=workshopPrepare(checked.value);if(!prepared.ok)return prepared;
    local before=requireHouse(workshop,{op="snapshot"});prepared.value.worldId=before.worldId;
    local restored=eve.buildingHouseRequest(workshop,{version=1,op="restore",state=prepared.value});
    if(!restored.ok)return navFail(restored.code,"Blueprint restore rejected; workshop unchanged");
    workshopRecord(before);blueprint=checked.value;editLevel=0;previewKey="";ghostDirty=true;
    chooseMode("workshop");return navSuccess(null);
}
function workshopLoad() {
    local result=workshopLoadText(fs.readText("saves/house-blueprint.json"));
    message=result.ok?"Blueprint loaded into workshop. Edit, then Capture + save to publish.":result.code+": "+result.message;
}
function placeHouseAt(x,y) {
    if(blueprint==null){message="Capture or load a blueprint first";return;}
    local id=newHouseId();
    local receipt=callHouse(town,{op="place",blueprint=blueprint,assemblyId=id,pose=pose(x,y,quarter)});
    if(receipt!=null){selected=id;serial++;remember(receipt);}
}
