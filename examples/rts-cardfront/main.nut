// Cardfront: simulation facts belong to RTS/Card/Economy; this script owns input,
// presentation and the scenario's deck/mission rules. No script HP/resource store.
fontOwner <- null; hudFont <- null;
sim <- null; cards <- null; hand <- null; deck <- null;
frame <- null; workers <- []; selected <- []; previousKeys <- {};
previousLeft <- false; previousRight <- false; accumulator <- 0.0;
serial <- 100; hq <- ""; barracks <- ""; enemyBase <- "";
message <- "Workers are collecting minerals. Build a barracks when you have 100.";
aimX <- 23.0; aimY <- 8.0; armedSlot <- -1; elapsed <- 0.0;
refillTime <- 0.0; skillPlays <- 0; matchPhase <- "running";
mission <- { tick=0 minerals=20 mana=16 gathered=0 built=false produced=0 skillPlays=0 enemyHealth=500.0 outcome="running" };
BLUE <- "00000000-0000-7000-8000-00000000b001";
RED <- "00000000-0000-7000-8000-00000000b002";
MATCH <- "00000000-0000-7000-8000-00000000b003";
MAP_X <- 24.0; MAP_Y <- 130.0; CELL_X <- 32.0; CELL_Y <- 27.0;

function required(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}
function attempt(result, success) {
    if (!result.ok) { message = result.status.summary; return false; }
    message = success; return true;
}
function nextId() { ++serial; return "00000000-0000-7000-8000-" + format("%012x", serial); }
function pressed(key) {
    local down=keyboard.isDown(key), was=(key in previousKeys)?previousKeys[key]:false;
    previousKeys[key] <- down; return down && !was;
}
function unitById(id) { foreach (u in frame.units) if (u.subject==id) return u; return null; }
function buildingById(id) { foreach (b in frame.buildings) if (b.subject==id) return b; return null; }
function army() {
    local result=[];
    foreach (u in frame.units) if (u.faction==BLUE && u.definition=="unit:marine") result.push(u.subject);
    return result;
}
function updateProjection() {
    frame=required(sim.inspectState(),"inspect RTS");
    local economy=required(sim.inspectScriptEconomy(BLUE),"inspect economy");
    foreach (r in economy.resources) {
        if (r.resource=="minerals") { mission.minerals=r.balance; mission.gathered=r.income-20; }
        if (r.resource=="mana") mission.mana=r.balance;
    }
    local factory=buildingById(barracks);
    mission.built=factory!=null && factory.complete;
    mission.produced=army().len(); mission.skillPlays=skillPlays;
    local enemy=buildingById(enemyBase); mission.enemyHealth=enemy==null?0.0:enemy.health;
    local matchState=required(sim.inspectMatch(MATCH),"inspect match");
    matchPhase=matchState.phase;
    mission.outcome=matchState.phase=="finished"?(matchState.winningTeam==1?"victory":"defeat"):"running";
    local liveSelection=[];
    foreach(id in selected) if(unitById(id)!=null) liveSelection.push(id);
    selected=liveSelection;
    if (selected.len()==0 && mission.produced>0) selected=army();
}
function resetGame() {
    hand=null; deck=null; frame=null; cards=null; sim=null;
    collectgarbage();
    sim=eve.RTS(); cards=eve.Card();
    serial=100; workers=[]; selected=[]; accumulator=0.0; elapsed=0.0; skillPlays=0; refillTime=0.0;
    barracks=""; armedSlot=-1;
    mission={ tick=0 minerals=20 mana=16 gathered=0 built=false produced=0 skillPlays=0 enemyHealth=500.0 outcome="running" };
    required(sim.loadScriptContent(fs.readText("data/content.json")),"load content");
    required(sim.configureScriptWorld(30,18,1.0,0.0,0.0),"configure world");
    required(sim.newFaction(BLUE),"create faction"); required(sim.newFaction(RED),"create opponent");
    required(sim.addScriptResource(BLUE,"minerals",20),"starting minerals");
    required(sim.addScriptResource(BLUE,"mana",16),"starting command energy");
    hq=required(sim.newBuilding(nextId(),"building:hq",BLUE,4.0,8.0),"create command center");
    enemyBase=required(sim.newBuilding(nextId(),"building:hq",RED,25.0,8.0),"create hostile outpost");
    local mine=required(sim.newResourceNode(nextId(),"minerals",3000.0,8.5,4.0,4),"create mine");
    for (local i=0;i<3;++i) {
        local worker=required(sim.newUnit(nextId(),"unit:worker",BLUE,5.0+i*0.6,6.0),"create worker");
        workers.push(worker); required(sim.assignWorker(worker,mine,hq),"assign collection");
    }
    for(local i=0;i<3;++i)
        required(sim.newUnit(nextId(),"unit:raider",RED,23.0+(i%2),7.0+i),"create guard");
    for(local y=0;y<18;++y) if(y<6||y>11)
        required(sim.setScriptNavigationBlocked(14,y,true),"place canyon");
    required(sim.newMatch(MATCH),"create mission");
    required(sim.configureMatch(MATCH,"headquarters","hq",0.0),"configure victory");
    required(sim.addMatchParticipant(MATCH,BLUE,1),"join player");
    required(sim.addMatchParticipant(MATCH,RED,2),"join opponent");
    required(sim.startMatch(MATCH),"start mission");
    if(cards.registerCardsFromJson(fs.readText("data/cards.json"))!=4) throw "card definitions rejected";
    hand=cards.newHand(cards.newConfig()); hand.setOwner("commander"); deck=cards.newDeck();
    local kinds=["grenade","reinforcement","repair","stim"];
    for(local repeat=0;repeat<4;++repeat) for(local i=3;i>=0;--i) deck.push(cards.newCard(kinds[i]));
    for(local i=0;i<4;++i) cards.drawCard("commander");
    updateProjection(); message="Collect 100 minerals, then press B to build your first barracks.";
}
function buildBarracks() {
    if(barracks!="") { message="The barracks is already deployed."; return; }
    local result=sim.startScriptConstruction(BLUE,nextId(),"building:barracks",8.0,10.0,workers[0]);
    if(attempt(result,"Construction accepted. Your worker will build it.")) barracks=result.value;
}
function produceMarine() {
    if(barracks=="") { message="Build a barracks first [B]."; return; }
    attempt(sim.queueScriptUnit(barracks,nextId(),"unit:marine",0),"Marine queued. Minerals paid once.");
}
function selectArmy() { selected=army(); message="Army selected. Right-click to move; A attacks the outpost."; }
function assault() {
    selectArmy();
    if(selected.len()==0) { message="Produce a marine first [F]."; return; }
    required(sim.setUnitStance(selected,"aggressive",0.0),"set assault stance");
    attempt(sim.attackMoveUnits(selected,24.0,8.0,false,1.2),"Assault order issued. Use BREACH near the outpost.");
}
function playSlot(slot,x,y) {
    if(slot<0||slot>=hand.count()) return;
    local card=hand.getCard(slot), kind=card.getDefinitionId(), result=null;
    local batch=transaction.newAtomicBatch();
    local staged=cards.stagePlay(batch,card);
    if(!staged.ok) { attempt(staged,""); return; }
    if(kind=="reinforcement") {
        if(barracks=="") { message="REINFORCE needs a completed barracks."; return; }
        result=sim.stageProduction(batch,barracks,nextId(),"unit:marine");
    } else {
        local caster=null;
        foreach(id in selected) { local u=unitById(id); if(u!=null&&u.definition=="unit:marine") { caster=id; break; } }
        if(caster==null) { message="Select a marine to use a skill card [1]."; return; }
        result=sim.stageAbility(batch,caster,kind,"",x,y);
    }
    if(!result.ok) { attempt(result,""); return; }
    result=batch.commit();
    if(attempt(result,card.getName()+" committed. Card, energy and effects use one transaction.")) {
        ++skillPlays; armedSlot=-1;
        while(hand.count()<4 && !deck.isEmpty()) cards.drawCard("commander");
    }
    updateProjection();
}
function playNamed(name,x,y) {
    for(local i=0;i<hand.count();++i) if(hand.getCard(i).getDefinitionId()==name) { playSlot(i,x,y); return; }
    message="That card is no longer in your hand.";
}
function commandMove(x,y) {
    if(selected.len()>0) attempt(sim.moveUnits(selected,x,y,false,1.2),"Move order issued.");
}
function selectAt(x,y) {
    local closest=null,best=1.2;
    foreach(u in frame.units) if(u.faction==BLUE) {
        local dx=u.x-x,dy=u.y-y,d=dx*dx+dy*dy;
        if(d<best) { best=d; closest=u.subject; }
    }
    selected=[]; if(closest!=null) selected.push(closest);
}
function stepSimulation(seconds) {
    required(sim.stepScript(seconds),"step simulation"); ++mission.tick; elapsed+=seconds; refillTime+=seconds;
    if(refillTime>=8.0) {
        refillTime=0.0;
        if(hand.count()<4 && !deck.isEmpty()) cards.drawCard("commander");
    }
    updateProjection();
}
function initPresentation() {
    gfx.setBackgroundColor(0.025,0.035,0.06,1.0);
    fontOwner=eve.Font();
    local fontData=fontOwner.newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",26);
    hudFont=gfx.newFont(fontData," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]()/|<>-:+',.!?%_");
    gfx.setFont(hudFont);
}
eve_init=function(){ initPresentation(); resetGame(); };
eve_reload <- function(){ initPresentation(); resetGame(); };
eve_update=function(dt){
    if(pressed("r")) resetGame();
    if(mission.outcome=="running") {
        if(pressed("b")) buildBarracks(); if(pressed("f")) produceMarine();
        if(pressed("1")) selectArmy(); if(pressed("a")) assault();
        local keys=["q","w","e","t"];
        for(local i=0;i<4;++i) if(pressed(keys[i])) playSlot(i,aimX,aimY);
        local x=(mouse.getX()-MAP_X)/CELL_X,y=(mouse.getY()-MAP_Y)/CELL_Y;
        local inMap=x>=0&&x<30&&y>=0&&y<18;
        local left=mouse.isDown(0),right=mouse.isDown(2);
        if(inMap) { aimX=x; aimY=y; }
        if(left&&!previousLeft) {
            if(inMap) { if(armedSlot>=0) playSlot(armedSlot,x,y); else selectAt(x,y); }
            else if(mouse.getY()>=655&&mouse.getY()<770) {
                local slot=((mouse.getX()-24)/244).tointeger();
                if(slot>=0&&slot<hand.count()) { armedSlot=slot; message="Card armed. Click a target on the map, or use its keyboard key."; }
            } else if(mouse.getX()>1014) {
                if(mouse.getY()>=205&&mouse.getY()<258) buildBarracks();
                if(mouse.getY()>=270&&mouse.getY()<323) produceMarine();
                if(mouse.getY()>=335&&mouse.getY()<388) assault();
            }
        }
        if(right&&!previousRight&&inMap) commandMove(x,y);
        previousLeft=left; previousRight=right;
        accumulator+=dt;
        while(accumulator>=1.0/30.0) { stepSimulation(1.0/30.0); accumulator-=1.0/30.0; }
    }
};
function rect(x,y,w,h,r,g,b,a=1.0) { gfx.drawSolidRect(x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),r.tofloat(),g.tofloat(),b.tofloat(),a.tofloat()); }
function text(value,x,y,scale=0.55,r=0.8,g=0.86,b=0.92) { gfx.print(value,x.tofloat(),y.tofloat(),r.tofloat(),g.tofloat(),b.tofloat(),1.0,scale.tofloat()); }
function px(x) { return MAP_X+x*CELL_X; } function py(y) { return MAP_Y+y*CELL_Y; }
function button(label,hint,y,enabled) {
    rect(1014,y,240,53,enabled?0.10:0.055,enabled?0.19:0.075,enabled?0.27:0.10);
    rect(1014,y,3,53,0.27,0.73,0.82); text(label,1027,y+12,0.65); text(hint,1027,y+33,0.45,0.53,0.65,0.75);
}
eve_render=function(){
    gfx.clear();
    rect(0,0,1280,105,0.055,0.078,0.11);
    text("CARDFRONT",24,24,1.25,0.94,0.97,1.0); text("CANYON OPERATIONS / 01",26,65,0.48,0.48,0.64,0.74);
    text("MINERALS",495,23,0.45,0.45,0.65,0.78); text(""+mission.minerals,495,44,1.0,0.44,0.86,0.94);
    text("COMMAND ENERGY",710,23,0.45,0.55,0.66,0.82); text(""+mission.mana,710,44,1.0,0.71,0.78,1.0);
    text("COLLECT > BUILD > PRODUCE > PLAY",940,26,0.47,0.89,0.68,0.36);
    text(format("%02d:%02d",(elapsed/60).tointeger(),(elapsed-floor(elapsed/60)*60).tointeger()),1140,56,0.8);
    for(local y=0;y<18;++y) for(local x=0;x<30;++x) {
        local ridge=x==14&&(y<6||y>11), road=y>=7&&y<=9;
        rect(px(x),py(y),31,26,ridge?0.22:0.07,ridge?0.21:road?0.12:0.105,ridge?0.23:road?0.18:0.14);
        if(ridge) rect(px(x)+7,py(y)+4,18,15,0.30,0.28,0.31);
    }
    foreach(n in frame.resourceNodes) {
        for(local i=0;i<5;++i) rect(px(n.x)+(i%3)*7-10,py(n.y)+(i/3)*9-10,6,15,0.16,0.66,0.76);
        text("MINERAL FIELD",px(n.x)-48,py(n.y)-28,0.39,0.43,0.74,0.81);
    }
    foreach(b in frame.buildings) {
        local friendly=b.faction==BLUE,size=b.definition=="building:barracks"?46.0:64.0;
        local x=px(b.x),y=py(b.y);
        rect(x-size/2-4,y-size/2-4,size+8,size+8,0.035,0.05,0.07);
        rect(x-size/2,y-size/2,size,size,friendly?0.12:0.48,friendly?0.35:0.19,friendly?0.48:0.17);
        rect(x-size/2+8,y-size/2+8,size-16,10,friendly?0.30:0.80,friendly?0.70:0.37,friendly?0.81:0.27);
        rect(x-size/2,y+size/2+7,size,4,0.15,0.20,0.24);
        rect(x-size/2,y+size/2+7,size*b.health/b.maxHealth,4,friendly?0.25:0.85,friendly?0.77:0.35,0.38);
        text(b.definition=="building:hq"?"COMMAND":b.definition=="building:barracks"?"BARRACKS":"HOSTILE OUTPOST",x-size/2,y-size/2-19,0.40);
        if(!b.complete) { rect(x-size/2,y+size/2+14,size*b.progress,4,0.95,0.68,0.32); text("BUILDING",x-25,y,0.35); }
    }
    foreach(u in frame.units) {
        local x=px(u.x),y=py(u.y),worker=u.definition=="unit:worker",friendly=u.faction==BLUE;
        rect(x-7,y-7,14,14,friendly?0.30:0.86,friendly?0.66:0.34,friendly?0.82:0.24);
        rect(x-3,y-12,6,5,worker?0.97:0.72,worker?0.73:0.86,worker?0.37:0.96);
        if(worker&&u.cargo>0) rect(x+8,y-3,5,6,0.16,0.75,0.88);
        rect(x-8,y+10,16*u.health/u.maxHealth,2,0.43,0.83,0.50);
        foreach(id in selected) if(id==u.subject) { rect(x-10,y+14,20,2,0.98,0.77,0.35); }
        if(u.activeEffects>0) rect(x-10,y-16,20,2,0.68,0.78,1.0);
    }
    if(armedSlot>=0) { rect(px(aimX)-26,py(aimY),52,1,0.96,0.77,0.34); rect(px(aimX),py(aimY)-22,1,44,0.96,0.77,0.34); }
    rect(1014,130,240,63,0.075,0.105,0.15); text("OPERATIONS",1027,142,0.65);
    text("BARRACKS  "+(mission.built?"ONLINE":barracks!=""?"BUILDING":"NOT DEPLOYED"),1027,170,0.42);
    button("[B] DEPLOY BARRACKS","100 minerals / worker construction",205,barracks==""&&mission.minerals>=100);
    button("[F] TRAIN MARINE","40 minerals / 2 sec production",270,mission.built&&mission.minerals>=40);
    button("[A] ASSAULT OUTPOST","Select army [1] / right-click move",335,mission.produced>0);
    text("MISSION STATUS",1027,416,0.55); text("Collected: "+mission.gathered,1027,449,0.48);
    text("Marines: "+mission.produced,1027,474,0.48); text("Cards played: "+skillPlays,1027,499,0.48);
    text("Destroy the hostile outpost.",1027,545,0.43); text("Protect your command center.",1027,568,0.43);
    text("[R] Restart mission",1027,596,0.43,0.55,0.68,0.78);
    text(message,24,626,0.43,0.91,0.76,0.48);
    local keys=["Q","W","E","T"];
    for(local i=0;i<4;++i) {
        local x=24+i*244.0; rect(x,655,230,116,0.075,0.105,0.16);
        if(i>=hand.count()) { text("EMPTY SLOT",x+16,697,0.6,0.32,0.40,0.48); continue; }
        local c=hand.getCard(i),kind=c.getDefinitionId();
        rect(x,655,230,3,armedSlot==i?0.98:0.33,armedSlot==i?0.75:0.62,armedSlot==i?0.36:0.83);
        text("["+keys[i]+"] "+c.getName(),x+14,674,0.68); text(""+c.getCost()+" COMMAND ENERGY",x+14,706,0.43,0.64,0.72,0.93);
        text(kind=="grenade"?"Area damage / +15 minerals":kind=="reinforcement"?"Queue a marine / +40 minerals":kind=="repair"?"Restore caster health":"Damage +60% / 8 seconds",x+14,733,0.39,0.47,0.66,0.74);
    }
    text("Click a card, then a map target. Skill timing, range and cooldown follow the RTS content definitions.",24,783,0.39,0.43,0.55,0.66);
    if(mission.outcome!="running") {
        rect(260,265,660,150,0.035,0.07,0.11,0.96);
        text(mission.outcome=="victory"?"OUTPOST SECURED":"COMMAND CENTER LOST",300,295,1.3,0.83,0.93,0.97);
        text("Mission complete / [R] deploy again",300,359,0.70,0.68,0.77,0.85);
    }
};
