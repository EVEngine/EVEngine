// Explicit demonstration: exercises the same commands as the UI, then walks normally.
persist workshopTest = ""
function workshopCheckUnchanged(before,recipeText) {
    if(requireHouse(workshop,{op="encode",value=requireHouse(workshop,{op="snapshot"})})!=before)
        throw "Failed blueprint load changed workshop";
    if(requireHouse(workshop,{op="encode",value=blueprint})!=recipeText)throw "Failed load changed active recipe";
}
function workshopRunChecks() {
    if(town.getBuildingCount()>0){message="Resume test needs an empty village; restart to run this fixture";return;}
    chooseMode("workshop");editLevel=0;palette=3;
    local original=requireHouse(workshop,{op="encode",value=requireHouse(workshop,{op="snapshot"})});
    editWorkshop(2,2,true);
    local edited=requireHouse(workshop,{op="encode",value=requireHouse(workshop,{op="snapshot"})});
    if(original==edited)throw "Expected default chair for resume test";
    undoStep(false);
    if(requireHouse(workshop,{op="encode",value=requireHouse(workshop,{op="snapshot"})})!=original)throw "Workshop undo differed";
    undoStep(true);captureHouse(false);
    local recipeText=requireHouse(workshop,{op="encode",value=blueprint});
    if(fs.writeTextAtomic("saves/workshop-check.json",recipeText)!=1)throw "Resume fixture save failed";
    editWorkshop(2,2,false);
    local loaded=workshopLoadText(fs.readText("saves/workshop-check.json"));
    if(!loaded.ok)throw loaded.message;
    foreach(p in state.instances)if(p.buildingId=="village.chair")throw "Load did not replace workshop parts";
    local before=requireHouse(workshop,{op="encode",value=requireHouse(workshop,{op="snapshot"})});
    local future=requireHouse(workshop,{op="decode",json=recipeText});future.version=999;
    foreach(text in ["{", "{}", requireHouse(workshop,{op="encode",value=future})]) {
        local rejected=workshopLoadText(text);
        if(rejected.ok)throw "Invalid blueprint load succeeded";
        workshopCheckUnchanged(before,recipeText);
    }
    // A valid schema with an out-of-workshop part must fail before publishing either state.
    local oversized=requireHouse(workshop,{op="decode",json=recipeText});
    oversized.parts[0].instance.cellX=80;
    local rejected=workshopLoadText(requireHouse(workshop,{op="encode",value=oversized}));
    if(rejected.ok)throw "Out-of-bounds blueprint accepted";
    workshopCheckUnchanged(before,recipeText);
    local unsupported=requireHouse(workshop,{op="decode",json=recipeText});
    foreach(p in unsupported.parts)p.instance.level+=4;
    local unsupportedResult=workshopLoadText(requireHouse(workshop,{op="encode",value=unsupported}));
    if(unsupportedResult.ok || unsupportedResult.code!="UnsupportedLevel")throw "Unsupported walk level accepted";
    workshopCheckUnchanged(before,recipeText);
    // Tool availability comes from definitions, even when the loaded recipe lacks that part.
    palette=3;editWorkshop(2,2,false);captureHouse(false);
    chooseMode("town");quarter=0;placeHouseAt(5,5);local first=selected;
    quarter=1;placeHouseAt(17,7);local second=selected;
    local other=requireHouse(town,{op="encode",value=requireHouse(town,{op="members",assemblyId=second})});
    selected=first;chooseMode("interior");palette=6;quarter=0;remodelAt(10,5,false);
    local stairs=false;foreach(p in requireHouse(town,{op="members",assemblyId=first}))if(p.buildingId=="village.stairs")stairs=true;
    if(!stairs)throw "Missing recipe part prevented stair remodeling";
    undoStep(false);
    if(requireHouse(town,{op="encode",value=requireHouse(town,{op="members",assemblyId=second})})!=other)throw "Remodel changed another house";
    local saved=requireHouse(town,{op="snapshot"});
    if(fs.writeTextAtomic("saves/workshop-check-world.json",requireHouse(town,{op="encode",value=saved}))!=1)throw "Resume village save failed";
    requireHouse(town,{op="restore",state=requireHouse(town,{op="decode",json=fs.readText("saves/workshop-check-world.json")})});
    refresh();selected=first;editLevel=0;chooseMode("visit");navReset();navVisit(first,0);
    workshopTest="walking";message="Resume checks passed; walking into the restored house";
    print("building_village.workshop resume checks PASS (undo, disk import, failure atomicity, full tools, independent copies)\n");
}
function workshopUpdateCheck() {
    if(workshopTest=="walking" && navGrounded && navRoute.len()==0 && navInside==selected && selected!="") {
        workshopTest="PASS";message="PASS: edit, disk reload, edit again, place copies, restore village, walk inside";
        print("building_village.workshop journey PASS\n");
    }
}
