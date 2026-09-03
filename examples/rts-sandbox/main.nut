// RTS composition sandbox: the facade owns roots while map, crowd, sensing,
// combat, weapon, economy and action remain the canonical providers.
persist sim = null
persist selected = []
persist accumulator = 0.0
persist serial = 1
persist uiBuilt = false
persist prevLeft = false
persist prevRight = false
persist prevKeys = {}
persist blueBarracks = null
persist blueCommand = null
persist blueTurret = null
persist blueAPC = null
persist blueFactionId = null
persist redFactionId = null

GRID_W <- 36; GRID_H <- 22; CELL <- 28.0; ORIGIN_X <- 24.0; ORIGIN_Y <- 94.0;
RTS_SANDBOX_BLUE_FACTION_ID <- "00000000-0000-7000-8000-00000000f001";
RTS_SANDBOX_RED_FACTION_ID  <- "00000000-0000-7000-8000-00000000f002";
RTS_SANDBOX_MATCH_ID <- "00000000-0000-7000-8000-00000000f003";

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}
function nextId() {
    local id = "00000000-0000-7000-8000-" + format("%012x", serial);
    ++serial; return id;
}
function readTextFile(path) {
    local handle=file(path,"r"), content="", n=handle.len();
    for(local i=0;i<n;++i) content += handle.readn('b').tochar();
    handle.close(); return content;
}
function wx() { return (mouse.getX()-ORIGIN_X)/CELL; }
function wy() { return (mouse.getY()-ORIGIN_Y)/CELL; }
function sx(x) { return ORIGIN_X+x*CELL; }
function sy(y) { return ORIGIN_Y+y*CELL; }
function fogVisible(x,y) {
    local cx=floor(x+0.5).tointeger(),cy=floor(y+0.5).tointeger();
    if(cx<0||cy<0||cx>=GRID_W||cy>=GRID_H)return false;
    return requireResult(sim.scriptCellVisible(blueFactionId,cx,cy),"query visibility");
}
function pressed(name) {
    local down=keyboard.isDown(name), before=(name in prevKeys)?prevKeys[name]:false;
    prevKeys[name] <- down; return down && !before;
}
function unitBySubject(state,id) {
    foreach(unit in state.units) if(unit.subject==id) return unit;
    return null;
}
function resetGame() {
    sim=eve.RTS(); selected=[]; accumulator=0.0; serial=1;
    requireResult(sim.loadScriptContent(readTextFile("data/content.json")),"load RTS content");
    requireResult(sim.configureScriptWorld(GRID_W,GRID_H,1.0,0.0,0.0),"configure RTS world");
    // newFaction validates creation but has no value payload. Persist the
    // sandbox-owned identity strings used to create and query each faction.
    blueFactionId=RTS_SANDBOX_BLUE_FACTION_ID;
    redFactionId=RTS_SANDBOX_RED_FACTION_ID;
    requireResult(sim.newFaction(blueFactionId),"create blue faction");
    requireResult(sim.newFaction(redFactionId),"create red faction");
    requireResult(sim.configureAutoConstruction(blueFactionId,true,2,2),"configure blue builders");
    requireResult(sim.configureAutoRepair(blueFactionId,true,2,2),"configure blue repairers");
    requireResult(sim.configureAutoConstruction(redFactionId,true,2,2),"configure red builders");
    requireResult(sim.configureAutoRepair(redFactionId,true,2,2),"configure red repairers");
    requireResult(sim.addScriptResource(blueFactionId,"minerals",300),"fund blue");
    requireResult(sim.addScriptResource(redFactionId,"minerals",300),"fund red");
    for(local y=2;y<20;++y) if(y!=6&&y!=16)
        requireResult(sim.setScriptNavigationBlocked(17,y,true),"place wall");
    for(local x=1;x<GRID_W-1;++x)
        requireResult(sim.setScriptNavigationCost(x,10,0.35),"place road");
    blueCommand=requireResult(sim.newBuilding(nextId(),"building:command_center",blueFactionId,4.5,10.5),"blue command");
    blueBarracks=requireResult(sim.newBuilding(nextId(),"building:barracks",blueFactionId,8.5,10.5),"blue barracks");
    blueTurret=requireResult(sim.newBuilding(nextId(),"building:turret",blueFactionId,13.5,6.5),"blue turret");
    requireResult(sim.newBuilding(nextId(),"building:command_center",redFactionId,31.0,10.5),"red command");
    requireResult(sim.newBuilding(nextId(),"building:turret",redFactionId,21.5,16.5),"red turret");
    requireResult(sim.newResourceNode(nextId(),"minerals",1800.0,11.5,4.0,4),"north minerals");
    requireResult(sim.newResourceNode(nextId(),"minerals",1800.0,24.5,18.0,4),"south minerals");
    for(local i=0;i<5;++i)
        requireResult(sim.newUnit(nextId(),"unit:worker",blueFactionId,5.5,8.0+i*0.7),"blue worker");
    for(local i=0;i<5;++i)
        requireResult(sim.newUnit(nextId(),"unit:worker",redFactionId,30.0,8.0+i*0.7),"red worker");
    for(local i=0;i<9;++i)
        requireResult(sim.newUnit(nextId(),"unit:marine",blueFactionId,9.0+(i%3),13.0+(i/3).tointeger()),"blue marine");
    blueAPC=requireResult(sim.newUnit(nextId(),"unit:apc",blueFactionId,7.0,16.5),"blue APC");
    for(local i=0;i<9;++i)
        requireResult(sim.newUnit(nextId(),"unit:marine",redFactionId,26.0-(i%3),13.0+(i/3).tointeger()),"red marine");
    requireResult(sim.newMatch(RTS_SANDBOX_MATCH_ID),"create match");
    requireResult(sim.configureMatch(RTS_SANDBOX_MATCH_ID,"headquarters","command_center",0.0),"configure victory");
    requireResult(sim.addMatchParticipant(RTS_SANDBOX_MATCH_ID,blueFactionId,1),"join blue");
    requireResult(sim.addMatchParticipant(RTS_SANDBOX_MATCH_ID,redFactionId,2),"join red");
    requireResult(sim.startMatch(RTS_SANDBOX_MATCH_ID),"start match");
}
function selectAtCursor() {
    local state=requireResult(sim.inspectState(),"inspect"), best=null, bestD=0.8*0.8;
    foreach(unit in state.units) {
        if(unit.faction!=blueFactionId || unit.garrisoned) continue;
        local dx=unit.x-wx(),dy=unit.y-wy(),d=dx*dx+dy*dy;
        if(d<bestD){bestD=d;best=unit.subject;}
    }
    selected=[]; if(best!=null)selected.push(best);
}
function selectArmy() {
    selected=[]; local state=requireResult(sim.inspectState(),"inspect");
    foreach(unit in state.units)
        if(unit.faction==blueFactionId && unit.definition=="unit:marine") selected.push(unit.subject);
}
function pruneSelection() {
    local state=requireResult(sim.inspectState(),"inspect"), live=[];
    foreach(id in selected) if(unitBySubject(state,id)!=null) live.push(id);
    selected=live;
}
function refreshHud() {
    if(!uiBuilt||sim==null)return;
    pruneSelection();
    local state=requireResult(sim.inspectState(),"inspect");
    local minerals=requireResult(sim.scriptResource(blueFactionId,"minerals"),"read minerals");
    local matchState=requireResult(sim.inspectMatch(RTS_SANDBOX_MATCH_ID),"inspect match");
    local isolated=0;
    foreach(id in selected){local unit=unitBySubject(state,id);if(unit!=null&&!unit.inCommand)++isolated;}
    ui.setText("stats","矿物 "+minerals+"  单位 "+state.units.len()+"  建筑 "+state.buildings.len()+
        "  选择 "+selected.len()+"  战局 "+matchState.phase+
        (matchState.winningTeam>=0?(" 胜方"+matchState.winningTeam):"")+(isolated>0?("  指挥失联 "+isolated):""));
    ui.setText("help","右键移动 | A攻击移动 | P巡逻 | Z对地攻击 | G进驻/V登车/N卸载/E疏散 | C隐形 | B/F/U/I生产科技 | Q/Y/T技能 | R重置");
}
function nearestEnemy(state) {
    local best=null,bestD=999999.0;
    foreach(unit in state.units) {
        if(unit.faction==blueFactionId)continue;
        local dx=unit.x-wx(),dy=unit.y-wy(),d=dx*dx+dy*dy;
        if(d<bestD){bestD=d;best=unit.subject;}
    }
    return best;
}
function firstSelectedWorker(state) {
    foreach(id in selected) {
        local unit=unitBySubject(state,id);
        if(unit!=null&&unit.definition=="unit:worker")return id;
    }
    return null;
}

eve_init=function(){
    gfx.setBackgroundColor(0.035,0.045,0.055,1.0);
    if(sim==null)resetGame();
    if(!uiBuilt){
        ui.beginBuild();ui.beginWindow("RTSSandbox","root");
        ui.text("组合式 RTS 综合沙盒","title");ui.text("","stats");ui.text("","help");
        ui.end();ui.mountBuildAs("hud");ui.select("hud");ui.setHostOverlay(true);
        ui.setHostPos(12.0,8.0,0.0,0.0);uiBuilt=true;
    }
    refreshHud();
};
eve_update=function(dt){
    if(pressed("r")||pressed("R"))resetGame();
    if(pressed("1"))selectArmy();
    local left=mouse.isDown(0);
    if(left&&!prevLeft)selectAtCursor();
    prevLeft=left;
    local right=mouse.isDown(2);
    if(right&&!prevRight&&selected.len()>0)
        requireResult(sim.moveUnits(selected,wx(),wy(),false,1.15),"formation move");
    prevRight=right;
    local shift=keyboard.isDown("Left Shift")||keyboard.isDown("Right Shift");
    if((pressed("a")||pressed("A"))&&selected.len()>0)
        requireResult(sim.attackMoveUnits(selected,wx(),wy(),shift,1.15),"attack move");
    if((pressed("d")||pressed("D"))&&selected.len()>0){
        local target=nearestEnemy(requireResult(sim.inspectState(),"inspect"));
        if(target!=null)requireResult(sim.attackUnits(selected,target,shift),"focus fire");
    }
    if((pressed("p")||pressed("P"))&&selected.len()>0)
        requireResult(sim.patrolUnits(selected,wx(),wy(),1.15),"patrol");
    if((pressed("z")||pressed("Z"))&&selected.len()>0)
        requireResult(sim.attackGroundUnits(selected,wx(),wy(),shift),"attack ground");
    if((pressed("g")||pressed("G"))&&selected.len()>0)
        requireResult(sim.garrisonUnits(selected,blueTurret,shift),"garrison turret");
    if((pressed("v")||pressed("V"))&&selected.len()>0)
        requireResult(sim.boardTransportUnits(selected,blueAPC,shift),"board APC");
    if(pressed("n")||pressed("N"))
        requireResult(sim.unloadTransport(blueAPC,wx(),wy()),"unload APC");
    if(pressed("e")||pressed("E"))
        requireResult(sim.evacuateBuilding(blueTurret,wx(),wy()),"evacuate turret");
    if((pressed("c")||pressed("C"))&&selected.len()>0){
        local state=requireResult(sim.inspectState(),"inspect");
        foreach(id in selected){local unit=unitBySubject(state,id);if(unit!=null)
            requireResult(sim.setUnitCloaked(id,!unit.cloaked),"toggle cloak");}
    }
    if(pressed("f")||pressed("F"))
        requireResult(sim.queueScriptUnit(blueBarracks,nextId(),"unit:marine",0),"queue marine");
    if(pressed("b")||pressed("B")){
        local worker=firstSelectedWorker(requireResult(sim.inspectState(),"inspect"));
        if(worker!=null)requireResult(sim.startScriptConstruction(
            blueFactionId,nextId(),"building:barracks",wx(),wy(),worker),"start barracks construction");
    }
    if(pressed("u")||pressed("U"))
        requireResult(sim.queueScriptResearch(blueBarracks,"infantry_weapons_1",0),"research weapons");
    if(pressed("i")||pressed("I"))
        requireResult(sim.queueScriptResearch(blueCommand,"frontline_shields",0),"research shields");
    if(pressed("q")||pressed("Q"))foreach(id in selected)
        requireResult(sim.castScriptAbility(id,"frag_grenade","",wx(),wy()),"cast grenade");
    if(pressed("y")||pressed("Y"))foreach(id in selected)
        requireResult(sim.castScriptAbility(id,"suppressive_barrage","",wx(),wy()),"cast barrage");
    if(pressed("t")||pressed("T"))foreach(id in selected)
        requireResult(sim.castScriptAbility(id,"combat_stim",id,wx(),wy()),"cast stim");
    if((pressed("h")||pressed("H"))&&selected.len()>0)
        requireResult(sim.holdUnits(selected),"hold");
    if((pressed("x")||pressed("X"))&&selected.len()>0)
        requireResult(sim.stopUnits(selected),"stop");
    accumulator+=dt;
    while(accumulator>=1.0/30.0){
        requireResult(sim.stepScript(1.0/30.0),"step RTS");
        accumulator-=1.0/30.0;
    }
    refreshHud();
};
eve_render=function(){
    gfx.clear();
    for(local y=0;y<GRID_H;++y)for(local x=0;x<GRID_W;++x){
        local wall=(x==17&&y>=2&&y<20&&y!=6&&y!=16);
        local road=(y==10&&!wall);
        if(wall)gfx.drawSolidRect(sx(x),sy(y),CELL-1,CELL-1,0.18,0.20,0.22,1.0);
        else if(road)gfx.drawSolidRect(sx(x),sy(y),CELL-1,CELL-1,0.24,0.20,0.12,1.0);
        else gfx.drawSolidRect(sx(x),sy(y),CELL-1,CELL-1,0.09,0.16,0.12,1.0);
    }
    local state=requireResult(sim.inspectState(),"inspect");
    foreach(node in state.resourceNodes){
        local nx=floor(node.x+0.5).tointeger(),ny=floor(node.y+0.5).tointeger();
        if(requireResult(sim.scriptCellExplored(blueFactionId,nx,ny),"query resource fog"))
            gfx.drawSolidRect(sx(node.x)-10.0,sy(node.y)-10.0,20.0,20.0,0.15,0.75,0.92,1.0);
    }
    foreach(building in state.buildings){
        if(building.faction!=blueFactionId&&!fogVisible(building.x,building.y))continue;
        local isBlue=building.faction==blueFactionId,size=building.definition=="building:command_center"?64.0:44.0;
        gfx.drawSolidRect(sx(building.x)-size*0.5,sy(building.y)-size*0.5,size,size,
            isBlue?0.18:0.75,isBlue?0.48:0.20,isBlue?0.88:0.18,building.powered?1.0:0.35);
    }
    foreach(unit in state.units){
        if(unit.garrisoned)continue;
        if(unit.faction!=blueFactionId&&!fogVisible(unit.x,unit.y))continue;
        local isBlue=unit.faction==blueFactionId,worker=unit.definition=="unit:worker",size=worker?10.0:14.0;
        gfx.drawSolidRect(sx(unit.x)-size*0.5,sy(unit.y)-size*0.5,size,size,
            isBlue?0.25:0.92,isBlue?0.62:0.25,isBlue?0.96:0.18,1.0);
    }
    foreach(id in selected){local unit=unitBySubject(state,id);if(unit!=null)
        gfx.drawSolidRect(sx(unit.x)-9,sy(unit.y)+9,18,2,0.2,1.0,0.35,1.0);}
    for(local y=0;y<GRID_H;++y)for(local x=0;x<GRID_W;++x){
        local explored=requireResult(sim.scriptCellExplored(blueFactionId,x,y),"query explored");
        local visible=requireResult(sim.scriptCellVisible(blueFactionId,x,y),"query visible");
        if(!explored)gfx.drawSolidRect(sx(x),sy(y),CELL-1,CELL-1,0.01,0.015,0.02,0.94);
        else if(!visible)gfx.drawSolidRect(sx(x),sy(y),CELL-1,CELL-1,0.02,0.03,0.05,0.58);
    }
    ui.beginFrameAndRender();
};
