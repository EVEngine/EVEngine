// Fixed-step host: camp/arena zones here; HP/progression/inventory stay native owners.
rpg <- null; inv <- null; saveFs <- null; hero <- null; enemy <- null;
gs <- null; tracker <- null; bag <- null; gear <- null; world <- null; save <- null; duel <- null;
fontData <- null; hudFont <- null;
heroId <- "01020304-0506-0708-890a-0b0c0d0e0f10";
enemyIds <- ["01020304-0506-0708-890a-0b0c0d0e0f11", "01020304-0506-0708-890a-0b0c0d0e0f12", "01020304-0506-0708-890a-0b0c0d0e0f13"];
zone <- "camp";
px <- 120.0; py <- 360.0; ex <- 650.0; ey <- 360.0;
tick <- 0; remainder <- 0.0; cooldown <- 0; heavyCooldown <- 0; dodge <- 0; dodgeCooldown <- 0;
windup <- 0; recovery <- 60; strike <- 0; power <- 0.0; flash <- 0;
message <- "Camp is safe. Talk (F), buy potion (B), then T enters the arena.";
keys <- {}; commands <- {x=0.0,y=0.0,attack=false,heavy=false,dodge=false};
gameState <- {};
function checked(result) { if (!result.ok) throw result.status.summary; return result.value; }
function readData(path) { local f=file(path,"r"); local s=f.read(); f.close(); return s; }
function minv(a,b) { return a<b?a:b; }
function maxv(a,b) { return a>b?a:b; }
function distance(x,y,a,b) { return sqrt((x-a)*(x-a)+(y-b)*(y-b)); }
function pressed(k) { local now=keyboard.isDown(k); local old=k in keys?keys[k]:false; keys[k]<-now; return now&&!old; }
function clearEnemy() {
    if (duel!=null) { duel.close(); duel=null; }
    if (enemy!=null) { enemy.release(); enemy=null; }
}
function spawn() {
    clearEnemy();
    local index=gs.getVariable("kills").tointeger();
    if (index>=3) return;
    enemy=rpg.newActor(); enemy.setBaseAttribute("hp",60.0); enemy.setCurrent("hp",60.0);
    duel=eve.RPGActionEncounter(); checked(duel.bind(hero,enemy,heroId,enemyIds[index]));
    ex=650.0; ey=360.0; windup=0; recovery=60;
}
function placeCamp() { zone="camp"; px=120.0; py=360.0; }
function placeArena() { zone="arena"; px=300.0; py=360.0; }
// Checkpoints restore persistent RPG owners; transient input/action state always starts clean.
function resetTransient() {
    tick=0; remainder=0.0;
    cooldown=0; heavyCooldown=0; dodge=0; dodgeCooldown=0;
    strike=0; power=0.0; windup=0; recovery=60; flash=0; keys={};
    commands={x=0.0,y=0.0,attack=false,heavy=false,dodge=false};
}
function reset() {
    clearEnemy(); save=null; world=null;
    if (hero!=null) hero.release();
    gs=rpg.newGameState(); tracker=rpg.newTracker(); tracker.syncAuto();
    hero=rpg.newActor(); hero.setBaseAttribute("hp",100.0); hero.setCurrent("hp",100.0); hero.setBaseAttribute("attack",18.0);
    bag=inv.newBag(3); bag.setId("hero.bag"); gear=inv.newEquipmentSet(); gear.setId("hero.gear"); gear.defineSlot("weapon");
    world=rpg.newWorldState(gs); save=eve.RPGSaveSession();
    if (save.bind(gs,tracker,hero,bag,gear)!=1 || save.setContentVersion("arpg-loop-v1")!=1) throw "save binding failed";
    resetTransient(); placeCamp();
    message="Camp: F talk / B buy / U drink / T arena. Defeat three sentinels.";
}
function pendingLoot() {
    for(local i=0;i<3;i+=1)
        if(world.isObjectConsumed("rpg.action-kill.v1",enemyIds[i]) && !world.isObjectConsumed("arpg.loot",enemyIds[i])) return i;
    return -1;
}
function enterArena() {
    if(hero.getCurrent("hp")<=0.0) { message="Dead heroes cannot leave camp."; return; }
    if(zone=="arena" && enemy!=null && enemy.getCurrent("hp")>0.0) { message="Finish the living sentinel first."; return; }
    if(duel!=null && duel.hasPendingReward()) { message="Claim the pending kill reward before traveling."; return; }
    placeArena();
    // Keep the next sentinel unspawned while an earlier cache is still claimable.
    if(enemy==null && gs.getVariable("kills").tointeger()<3 && pendingLoot()<0) spawn();
    if(enemy!=null) message="Arena: J/K attack, Space dodge, E altar loot.";
    else if(pendingLoot()>=0) message="Arena: claim the altar cache (E), then N for the next sentinel.";
    else message="Arena clear. G returns to camp.";
}
function returnCamp() {
    if(zone=="camp") { message="Already at camp."; return; }
    if(enemy!=null && enemy.getCurrent("hp")>0.0) { message="Cannot camp while a sentinel still lives."; return; }
    if(duel!=null && duel.hasPendingReward()) { message="Claim pending reward before returning."; return; }
    clearEnemy(); placeCamp(); resetTransient();
    message="Back at camp. F5 saves here only. B buys potions; T re-enters the arena.";
}
function nextFight() {
    if(hero.getCurrent("hp")<=0.0) return;
    if(zone!="arena") { enterArena(); return; }
    if(enemy!=null && enemy.getCurrent("hp")>0.0 || duel!=null && duel.hasPendingReward()) return;
    spawn();
}
function pickup() {
    if(zone!="arena") { message="Caches sit at the arena altar. T enters the arena."; return; }
    if(distance(px,py,790.0,170.0)>90.0) { message="Move to the gold altar to claim a cache."; return; }
    local i=pendingLoot(); if(i<0) { message="No unclaimed cache."; return; }
    local result=rpg.collectWorldLoot(tracker,gs,bag,"arpg.loot",enemyIds[i],"","sentinel_blade",1,"",0.0,"", "",0);
    message=result.ok?"Blade collected. Q equips it; full caches remain claimable.":"Bag full: equip a blade with Q to free a slot, then retry E.";
}
function equipBlade() {
    local slot=bag.findItem("sentinel_blade");
    if(slot>=0 && gear.equipFromBag("weapon",bag,slot)) { rpg.syncEquipModifiers(hero,gear); message="Blade equipped: attack +8."; }
}
function talkScout() {
    if(zone!="camp") { message="The scout only talks at camp."; return; }
    local state=tracker.getState("scout.report");
    if(state=="inactive") {
        if(!tracker.activate("scout.report")) { message="Could not activate scout.report."; return; }
        message="Side quest accepted: defeat any sentinel, then talk again to claim gold.";
        return;
    }
    if(state=="ready") {
        local claimed=rpg.claimQuestRewards(tracker,gs,bag,"scout.report");
        if(!claimed.ok) { message="Claim failed: "+claimed.status.summary; return; }
        message="Scout paid 8 gold. Main sentinel hunt continues independently.";
        return;
    }
    if(state=="completed") {
        message="Scout already paid you. Main sentinel hunt continues.";
        return;
    }
    message="Scout quest state: "+state+" (kill a sentinel, then return).";
}
function buyPotion() {
    if(zone!="camp") { message="Shop is only open at camp."; return; }
    local beforeGold=gs.getVariable("gold");
    local beforeQuest=tracker.getState("scout.report");
    local bought=rpg.buyShopOffer(gs,bag,"gold","camp_potion",1);
    if(!bought.ok) {
        message="Buy failed (gold/bag): "+bought.status.summary;
        if(tracker.getState("scout.report")!=beforeQuest) throw "shop must not mutate quest state";
        return;
    }
    message="Bought camp potion (-15 gold). U drinks it at camp. Quest state unchanged ("+beforeQuest+").";
    if(gs.getVariable("gold")>=beforeGold) throw "successful buy must spend gold";
}
function drinkPotion() {
    if(zone!="camp") { message="Drink potions at camp."; return; }
    local slot=bag.findItem("camp_potion");
    if(slot<0) { message="No camp potion in the bag."; return; }
    if(bag.removeAt(slot,1)!=1) { message="Could not remove potion."; return; }
    local maxHp=hero.getFinalAttribute("hp");
    hero.setCurrent("hp",minv(maxHp,hero.getCurrent("hp")+40.0));
    message="Drank potion. HP "+hero.getCurrent("hp").tointeger()+"/"+maxHp.tointeger()+".";
}
function checkpoint() {
    if(zone!="camp") { message="Save only at camp (G to return after a kill)."; return; }
    if(enemy!=null && enemy.getCurrent("hp")>0.0 || duel!=null && duel.hasPendingReward() || hero.getCurrent("hp")<=0.0) {
        message="Save after clearing the arena and returning to camp."; return;
    }
    local data=save.snapshotJson();
    message=data!="" && saveFs.writeTextAtomic("checkpoint.json",data)!=0?"Checkpoint saved at camp, including unclaimed caches.":"Save failed.";
}
function loadCheckpoint() {
    local data=saveFs.readText("checkpoint.json");
    if(data=="" || save.restoreSnapshotJson(data)!=1) { message="No valid checkpoint; current run preserved."; return; }
    clearEnemy(); rpg.syncEquipModifiers(hero,gear); resetTransient(); placeCamp();
    message="Checkpoint restored at camp. Rewards stay deduplicated. T continues the hunt.";
}
function simulate() {
    tick+=1;
    if(cooldown>0) cooldown-=1; if(heavyCooldown>0) heavyCooldown-=1;
    if(dodgeCooldown>0) dodgeCooldown-=1; if(dodge>0) dodge-=1; if(flash>0) flash-=1;
    if(hero.getCurrent("hp")<=0.0) { commands.attack=false; commands.heavy=false; commands.dodge=false; return; }
    local dx=commands.x; local dy=commands.y;
    if(keyboard.isDown("a")) dx-=1.0; if(keyboard.isDown("d")) dx+=1.0;
    if(keyboard.isDown("w")) dy-=1.0; if(keyboard.isDown("s")) dy+=1.0;
    local norm=sqrt(dx*dx+dy*dy); if(norm>0.0) { dx/=norm; dy/=norm; }
    if(zone=="arena" && commands.dodge && dodgeCooldown==0) { dodge=18; dodgeCooldown=55; }
    commands.dodge=false;
    local speed=dodge>0?7.0:3.0;
    if(zone=="camp") { px=maxv(30.0,minv(250.0,px+dx*speed)); py=maxv(140.0,minv(600.0,py+dy*speed)); }
    else { px=maxv(270.0,minv(930.0,px+dx*speed)); py=maxv(140.0,minv(600.0,py+dy*speed)); }
    if(zone!="arena") { commands.attack=false; commands.heavy=false; return; }
    if(enemy==null || enemy.getCurrent("hp")<=0.0) {
        commands.attack=false; commands.heavy=false;
        if(duel!=null && duel.hasPendingReward()) {
            local claim=duel.claim(gs,tracker,"sentinel");
            if(claim.ok) message="Sentinel defeated! E altar / Q equip / G camp save / N or T next.";
            else message="Reward pending: retry next frame.";
        }
        return;
    }
    if(cooldown==0 && strike==0 && (commands.attack || commands.heavy && heavyCooldown==0)) {
        local heavy=commands.heavy && heavyCooldown==0;
        strike=heavy?22:9; cooldown=heavy?46:24; power=hero.getFinalAttribute("attack")*(heavy?2.0:1.0);
        if(heavy) heavyCooldown=90;
    }
    commands.attack=false; commands.heavy=false;
    if(strike>0) {
        strike-=1;
        if(strike==0 && distance(px,py,ex,ey)<100.0) { checked(duel.hit(true,power,tick)); flash=10; }
    }
    if(enemy.getCurrent("hp")<=0.0) { windup=0; return; }
    if(recovery>0) { recovery-=1; return; }
    if(windup>0) {
        windup-=1;
        if(windup==0) { if(distance(px,py,ex,ey)<110.0 && dodge==0) checked(duel.hit(false,12.0,tick)); recovery=55; }
    } else {
        local d=distance(px,py,ex,ey);
        if(d<100.0) windup=36;
        else if(d>0) { ex+=(px-ex)/d*1.8; ey+=(py-ey)/d*1.8; }
    }
}
function observe() {
    gameState={
        tick=tick,zone=zone,hp=hero.getCurrent("hp"),
        enemyHp=enemy==null?0.0:enemy.getCurrent("hp"),
        kills=gs.getVariable("kills"),gold=gs.getVariable("gold"),
        level=hero.getLevel(),attack=hero.getFinalAttribute("attack"),
        items=bag.countItem("sentinel_blade"),potions=bag.countItem("camp_potion"),
        pendingLoot=pendingLoot(),quest=tracker.getState("sentinels"),
        sideQuest=tracker.getState("scout.report"),
        px=px,py=py,ex=ex,ey=ey,windup=windup,dodge=dodge,
        cooldown=cooldown,heavyCooldown=heavyCooldown,dodgeCooldown=dodgeCooldown,strike=strike,
        inputX=commands.x,inputY=commands.y,message=message
    };
}
game <- {};
game.attack <- function(){commands.attack=true;}; game.heavy <- function(){commands.heavy=true;}; game.dodge <- function(){commands.dodge=true;};
game.right <- function(){commands.x=1.0;commands.y=0.0;}; game.left <- function(){commands.x=-1.0;commands.y=0.0;};
game.up <- function(){commands.y=-1.0;commands.x=0.0;}; game.down <- function(){commands.y=1.0;commands.x=0.0;}; game.stop <- function(){commands.x=0.0;commands.y=0.0;};
game.pickup <- pickup.bindenv(getroottable()); game.equip <- equipBlade.bindenv(getroottable());
game.save <- checkpoint.bindenv(getroottable()); game.load <- loadCheckpoint.bindenv(getroottable());
game.next <- nextFight.bindenv(getroottable()); game.reset <- reset.bindenv(getroottable());
game.enterArena <- enterArena.bindenv(getroottable()); game.returnCamp <- returnCamp.bindenv(getroottable());
game.talk <- talkScout.bindenv(getroottable()); game.buy <- buyPotion.bindenv(getroottable());
game.drink <- drinkPotion.bindenv(getroottable());
eve_init <- function(){
    rpg=eve.RPG(); inv=eve.Inventory(); saveFs=eve.Filesystem();
    if(!saveFs.setIdentity("arpg-loop",true)||!saveFs.setupWriteDirectory()) throw "save directory unavailable";
    if(rpg.registerSkillsFromJson("[{\"id\":\"sentinel.claw\"}]")!=1) throw "skill content rejected";
    if(inv.registerItemsFromJson("[{\"id\":\"sentinel_blade\",\"displayName\":\"Sentinel Blade\",\"maxStack\":1,\"equipSlot\":\"weapon\"},{\"id\":\"camp_potion\",\"displayName\":\"Camp Potion\",\"maxStack\":5}]")!=2) throw "item content rejected";
    if(rpg.registerItemStatsFromJson("sentinel_blade","[{\"attribute\":\"attack\",\"op\":\"add\",\"value\":8}]")!=1) throw "equipment content rejected";
    checked(rpg.replaceQuestsFromJson(readData("data/quests.json")));
    checked(rpg.replaceEncountersFromJson(readData("data/encounters.json")));
    checked(rpg.replaceShopOffersFromJson(readData("data/shop.json")));
    fontData=eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",22);
    hudFont=gfx.newFont(fontData," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%");
    gfx.setBackgroundColor(0.025,0.035,0.06,1.0); reset(); observe();
};
eve_update <- function(dt){
    if(pressed("j")) game.attack(); if(pressed("k")) game.heavy(); if(pressed("space")) game.dodge();
    if(pressed("e")) pickup(); if(pressed("q")) equipBlade(); if(pressed("n")) nextFight();
    if(pressed("t")) enterArena(); if(pressed("g")) returnCamp();
    if(pressed("f")) talkScout(); if(pressed("b")) buyPotion(); if(pressed("u")) drinkPotion();
    if(pressed("f5")) checkpoint(); if(pressed("f9")) loadCheckpoint(); if(pressed("r")) reset();
    remainder+=minv(dt,0.1); while(remainder>=1.0/60.0){simulate();remainder-=1.0/60.0;} observe();
};
function text(value,x,y){gfx.drawText(hudFont,value,x,y,0.9,0.93,1.0,1.0,0.75);}
eve_render <- function(){
    gfx.clear();
    gfx.drawSolidRect(20.0,130.0,230.0,480.0,0.05,0.11,0.09,1.0); // camp
    gfx.drawSolidRect(260.0,130.0,680.0,480.0,0.06,0.09,0.13,1.0); // arena
    gfx.drawSolidRect(40.0,170.0,50.0,50.0,0.2,0.55,0.35,1.0); // scout
    gfx.drawSolidRect(40.0,250.0,50.0,50.0,0.55,0.4,0.15,1.0); // shop
    if(zone=="arena") gfx.drawSolidRect(760.0,140.0,60.0,60.0,pendingLoot()>=0?0.95:0.25,0.65,0.18,1.0);
    gfx.drawSolidRect(px-18,py-18,36.0,36.0,0.2,dodge>0?1.0:0.6,1.0,1.0);
    if(zone=="arena" && enemy!=null && enemy.getCurrent("hp")>0.0){
        if(windup>0) gfx.drawSolidRect(ex-65,ey-65,130.0,130.0,0.4,0.15,0.08,1.0);
        gfx.drawSolidRect(ex-22,ey-22,44.0,44.0,1.0,flash>0?1.0:0.25,0.2,1.0);
        gfx.drawSolidRect(ex-30,ey-38,60.0*enemy.getCurrent("hp")/60.0,5.0,0.9,0.2,0.2,1.0);
    }
    text("THE THREE SENTINELS / ARPG  ["+zone+"]",25.0,15.0);
    text("HP "+hero.getCurrent("hp").tointeger()+"  Level "+hero.getLevel()+"  ATK "+hero.getFinalAttribute("attack").tointeger()+"  Gold "+gs.getVariable("gold").tointeger()+"  Kills "+gs.getVariable("kills").tointeger()+"/3",25.0,45.0);
    text("WASD move  T arena  G camp  F talk  B buy  U drink",25.0,75.0);
    text("J/K fight  Space dodge  E loot  Q equip  N next  F5/F9 save",25.0,102.0);
    text(message,25.0,615.0);
    if(hero.getCurrent("hp")<=0.0) text("DEFEAT / R restart or F9 restore checkpoint",220.0,300.0);
    else if(gs.getVariable("kills")>=3) text("VICTORY / claim caches, camp save, optional scout claim",180.0,300.0);
};
