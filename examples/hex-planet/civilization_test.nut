// Opt-in acceptance journey: EVE_HEX_CIV_TEST=1. Uses the same commands as UI/AI.
function civAssert(condition,message) { if(!condition) throw "hex civilization test: "+message; }
function civJourney() {
    local initial=civPosition(1);
    local gold=civBalance(1,"gold");
    civFound(1);
    civAssert(civBalance(1,"gold")==gold && civCount(1)==1,"refused founding changed state");
    local invalid=civilization.battle.move(CIV_PLAYER,-1,0,0);
    civAssert(!invalid.ok && civPosition(1)==initial,"invalid movement changed occupancy");
    local capital=civCityAt(initial);
    civProject(capital,"library");
    local paid=civBalance(1,"gold");
    local task=capital.work.task;
    civProject(capital,"library");
    civAssert(capital.work.task==task && civBalance(1,"gold")==paid,"duplicate project charged twice");
    for(local turn=0;turn<65 && civilization.outcome=="";turn++) {
        local best=-1; local score=-1;
        foreach(entry in civReach(1)) {
            local distance=999;
            foreach(city in eve.view(Colony)) {
                local d=hexmap.sphereDistance(entry.cell.x,city.settlement.cell);
                if(d<distance) distance=d;
            }
            if(distance>score) {score=distance;best=entry.cell.x;}
        }
        if(civCount(1)<3 && best>=0 && best!=civPosition(1)) civMove(1,best);
        if(civCount(1)<3 && civCanFound(civPosition(1))) civFound(1);
        foreach(city in eve.view(Colony)) if(city.settlement.owner==1 && civCount(1)>=3) {
            local s=city.settlement;
            civProject(city,!s.mine ? "mine" : (!s.library ? "library" : "wonder"));
        }
        local selected=civilization.selected;
        civEndTurn();
        civAssert(civilization.selected==selected,"rival turn changed player selection");
    }
    civAssert(capital.settlement.library,"production never completed the library");
    civAssert(civilization.technology[1],"economy science never unlocked Writing");
    civAssert(civCount(1)>=3,"expedition never founded three cities");
    civAssert(civilization.outcome!="","race never reached a terminal result");
    print("HEX_CIV_JOURNEY PASS | turn "+civilization.turn+" | "+civilization.outcome+"\n");
    local oldBattle=civilization.battle;
    local oldQueue=capital.work.queue;
    civReset(planet.seed);
    civAssert(oldBattle.isStale() && oldQueue.isStale(),"new game retained old handles");
    civAssert(civCount(1)==1 && civCount(2)==1 && civBalance(1,"gold")==50,"new game leaked entities or resources");
    civFocus(planet);
    print("HEX_CIV_RESET PASS\n");
}

// Read-only UI contracts: the same projection/picking and tactics preview used by input.
function civUiContracts() {
    local position=civPosition(1);
    local points=civValue(civilization.battle.unitResources(CIV_PLAYER)).movePoints;
    local tested=false;
    foreach(entry in civilization.reachable) if(entry.cell.x!=position) {
        local screen=civProjectCell(planet,entry.cell.x);
        if(screen==null) continue;
        civAssert(civPick(planet,screen[0],screen[1])==entry.cell.x,"projected tile does not pick back to itself");
        local preview=civValue(civilization.battle.previewMove(CIV_PLAYER,entry.cell.x,0,0));
        civAssert(preview.cost==entry.cost && preview.path.len()>1,"route cost differs from reachability");
        tested=true; break;
    }
    civAssert(tested,"no visible route tested");
    civAssert(civPosition(1)==position && civValue(civilization.battle.unitResources(CIV_PLAYER)).movePoints==points,
        "preview changed authoritative movement");
    civAssert(!civMapInput(20,150) && !civMapInput(500,150) && !civMapInput(1000,740),"HUD did not block map input");
    foreach(host in ["civ-resources","civilization","civ-turn","civ-events","civ-hint"]) {
        ui.select(host);
        print("HEX_UI_LAYOUT "+ui.getLayoutDiagnostics()+"\n");
    }
    print("HEX_UI_ROUTE_INPUT PASS\n");
}
