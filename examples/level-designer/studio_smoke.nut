// Run through eve_run_script on a live level-designer instance. This exercises
// the actual scene session, CPU/GPU projections, terrain codec and Box3D world.
dofile("json_smoke.nut");
dofile("smoke.nut");
levelNewDocument();
local hm=level.terrain.heightmap;
for(local z=0;z<hm.getHeight();++z) for(local x=0;x<hm.getWidth();++x) hm.setHeight(x,z,0.0);
terrainRefreshSurface();

// Stroke atomicity and persistence: sample data, not the procedural seed, wins.
local before=terrainEncodeSurface(level.terrain);
terrainBeginStroke(0.0,0.0);
level.brushMode="raise";
for(local i=0;i<10;++i) terrainSculptAt(0.0,0.0,1.0/60.0);
terrainEndStroke();
local raised=smokeSurfaceAt(0.0,0.0);
smokeCheck("sculpt raises visible terrain",raised>0.1);
terrainHistory(false);
smokeCheck("stroke undo restores exact samples",terrainEncodeSurface(level.terrain)==before);
terrainHistory(true);
smokeCheck("stroke redo restores raised surface",smokeApprox(smokeSurfaceAt(0.0,0.0),raised,0.001));
foreach(mode in ["lower","smooth","flatten"]) {
    level.brushMode=mode;terrainBeginStroke(0.0,0.0);terrainSculptAt(0.0,0.0,0.02);terrainEndStroke();
}
local sculpted=terrainEncodeSurface(level.terrain);
local saved=levelDocumentBuild();levelDocumentApply(saved);
smokeCheck("saved sculpted samples round trip",terrainEncodeSurface(level.terrain)==sculpted);
local originalSave=level.saveName;level.saveName="studio-smoke-roundtrip.json";
local wrote=levelSaveDocument();smokeCheck("atomic file save succeeds",wrote);
if(wrote) {
    levelNewDocument();smokeCheck("saved file reload succeeds",levelLoadDocument());
    smokeCheck("file reload preserves sculpted samples",terrainEncodeSurface(level.terrain)==sculpted);
}
level.saveName=originalSave;

// Nested abstraction and world transforms, using a rotated/scaled group.
local a=whiteboxPlace("prototype.cube",0.0,2.0,0.0);
local b=whiteboxPlace("prototype.cube",3.0,2.0,0.0);
levelSelect(a);levelSelect(b,true);levelGroupSelection();
local group=level.selected;
smokeCheck("selected siblings become a block",levelObject(a).parent==group && levelObject(b).parent==group);
local original=scene.localToWorldAt(levelHost(),a,0.0,0.0,0.0);
smokeCheck("grouping preserves world position",smokeApprox(original[0],0.0,0.001));
levelGroupSelection();local outer=level.selected;
smokeCheck("blocks can be abstracted again",levelObject(group).parent==outer);
local ot=levelObject(outer).transform;
levelCommand("scene.transform.set.v1",{object=outer,position=[ot.x,ot.y,ot.z],
    rotation=[0.0,0.7,0.0],scale=[1.5,1.5,1.5]});
scene.updateTransformsAll();whiteboxSyncTransforms();
local world=levelTransformRelativeTo(a,"");
smokeCheck("nested rendering inherits rotation and scale",
    smokeApprox(level.pieces[a].entity.getYaw(),0.7,0.001) && smokeApprox(world.scaleX,1.5,0.001));
levelSelect(outer);levelEnterBlock();
smokeCheck("enter edits the next abstraction level",level.scope==outer);
levelLeaveBlock();smokeCheck("up restores parent scope",level.scope=="root");
levelSelect(outer);levelCaptureBlock();local captured=level.library.len()-1;
local instance=levelPlaceBlock(captured,12.0,3.0,0.0);
smokeCheck("captured block instantiates a new subtree",instance!="" && instance!=outer);
local count=level.objects.len();
levelDeleteSelection();levelChecked(level.session.undo());levelRefreshObjects();
smokeCheck("subtree deletion undoes with semantic data intact",level.objects.len()==count && levelObject(instance)!=null);
levelSelect(outer);levelUnpackBlock();
smokeCheck("unpack preserves transformed child pose",levelObject(outer)==null &&
    smokeApprox(levelTransformRelativeTo(a,"").rotationY,0.7,0.001));

// v2 atomic load failures, including failures after valid scene parsing.
local doc=levelDocumentBuild(), validScene=level.session.saveJson(), validTerrain=terrainEncodeSurface(level.terrain);
local bad=levelCopy(doc);bad.surface="{broken";
local rejected=false;
try { levelDocumentApply(bad); } catch(error) { rejected=true; }
smokeCheck("bad terrain document rejects atomically",rejected && level.session.saveJson()==validScene &&
    terrainEncodeSurface(level.terrain)==validTerrain);
bad=levelCopy(doc);bad.pieces[0].recipe="prototype.does-not-exist";rejected=false;
try { levelDocumentApply(bad); } catch(error) { rejected=true; }
smokeCheck("unknown mesh recipe rejects atomically",rejected && level.session.saveJson()==validScene);
levelDocumentApply(doc);
smokeCheck("library and nesting survive document round trip",level.library.len()>0 && levelObject(instance)!=null);
local legacy=levelCopy(doc);legacy.schemaVersion=1;delete legacy.surface;delete legacy.library;
foreach(piece in legacy.pieces) delete piece.motion;
levelDocumentApply(legacy);
local allStatic=true;foreach(id,piece in level.pieces) if(piece.motion!="static") allStatic=false;
smokeCheck("v1 migrates to static bodies and empty library",allStatic && level.library.len()==0);

// Real dynamic rigid bodies: gravity, ground collisions, bilateral character push.
levelNewDocument();hm=level.terrain.heightmap;
for(local z=0;z<hm.getHeight();++z) for(local x=0;x<hm.getWidth();++x) hm.setHeight(x,z,0.0);
terrainRefreshSurface();
local crate=whiteboxPlace("prototype.cube",0.0,3.0,2.0);
local piece=level.pieces[crate];piece.motion="dynamic";
foreach(key in ["width","height","depth"]) if(key in piece.values) piece.values[key]="1.0";
whiteboxRebuildPiece(crate);
local npc=spawnPlace(5.0,0.05,5.0,0.0);
local player=spawnPlace(0.0,0.05,0.0,0.0);levelSelect(player);
local authored=level.session.saveJson();
levelStartPlay();smokeCheck("physics Play entered",level.mode=="play");
if(level.play!=null) {
    local play=level.play;play.inputDriven=true;play.inputForward=0.0;play.cameraYaw=0.0;
    smokeCheck("player has a dynamic capsule in shared world",play.playerBody!=null && play.dynamics.len()==1 && play.npcs.len()==1);
    local dynamic=play.dynamics[0].body,startY=dynamic.getY();
    for(local i=0;i<90;++i) levelUpdatePlay(1.0/60.0);
    smokeCheck("dynamic crate falls under gravity",dynamic.getY()<startY-1.0);
    smokeCheck("dynamic crate collides with terrain",dynamic.getY()>-0.2 && dynamic.getY()<1.5);
    local jumpY=play.positionY;play.inputJump=true;levelUpdatePlay(1.0/60.0);
    smokeCheck("jump uses the shared physics capsule",play.velocityY>1.0 && play.positionY>jumpY);
    for(local i=0;i<60;++i) levelUpdatePlay(1.0/60.0);
    local startZ=dynamic.getZ();play.inputForward=1.0;play.inputRunning=true;
    for(local i=0;i<150;++i) levelUpdatePlay(1.0/60.0);
    smokeCheck("character physically pushes the dynamic crate",dynamic.getZ()>startZ+1.0);
    smokeCheck("physics simulation leaves scene document unchanged",level.session.saveJson()==authored);
    play.paused=true;local steps=play.steps;levelUpdatePlay(0.2);
    smokeCheck("pause freezes simulation",play.steps==steps);
    levelStepPlay();smokeCheck("single-step advances one tick",play.steps==steps+1);
    levelStopPlay();
    smokeCheck("Stop restores authored object transforms",smokeApprox(piece.entity.getYaw(),0.0,0.001) &&
        level.session.saveJson()==authored);
}
foreach(tool in ["select","whitebox","spawn","terrain","blocks"]) {
    level.tool=tool;levelMountPanels();
}
level.tool="select";levelSelect(crate);levelRefreshPanels();levelFrameSelection();
print("scene-studio smoke: " + (smokeFailures==0 ? "PASS" : "FAIL ("+smokeFailures+")") + "\n");
if(smokeFailures!=0) throw "Scene studio integration tests failed: " + smokeFailures;
