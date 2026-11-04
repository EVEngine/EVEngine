// Fixed-step host: positions/input here, all HP/progression/inventory in native owners.
rpg <- null; inv <- null; saveFs <- null; hero <- null; enemy <- null;
gs <- null; tracker <- null; bag <- null; gear <- null; world <- null; save <- null; duel <- null;
fontData <- null; hudFont <- null;
heroId <- "01020304-0506-0708-890a-0b0c0d0e0f10";
enemyIds <- ["01020304-0506-0708-890a-0b0c0d0e0f11", "01020304-0506-0708-890a-0b0c0d0e0f12", "01020304-0506-0708-890a-0b0c0d0e0f13"];
px <- 300.0; py <- 360.0; ex <- 650.0; ey <- 360.0;
tick <- 0; remainder <- 0.0; cooldown <- 0; heavyCooldown <- 0; dodge <- 0; dodgeCooldown <- 0;
windup <- 0; recovery <- 60; strike <- 0; power <- 0.0; flash <- 0;
message <- "Defeat a sentinel, then claim its cache at the gold altar.";
keys <- {}; commands <- {x=0.0,y=0.0,attack=false,heavy=false,dodge=false};
gameState <- {};
function checked(result) { if (!result.ok) throw result.status.summary; return result.value; }
function readData(path) { local f=file(path,"r"); local s=f.read(); f.close(); return s; }
function minv(a,b) { return a<b?a:b; }
function maxv(a,b) { return a>b?a:b; }
function distance(x,y,a,b) { return sqrt((x-a)*(x-a)+(y-b)*(y-b)); }
function pressed(k) { local now=keyboard.isDown(k); local old=k in keys?keys[k]:false; keys[k]<-now; return now&&!old; }
function spawn() {
    if(duel!=null) duel.close(); duel=null;
    if (enemy!=null) { enemy.release(); enemy=null; }
    local index=gs.getVariable("kills").tointeger();
    if (index>=3) return;
    enemy=rpg.newActor(); enemy.setBaseAttribute("hp",60.0); enemy.setCurrent("hp",60.0);
    duel=eve.RPGActionEncounter(); checked(duel.bind(hero,enemy,heroId,enemyIds[index]));
    ex=650.0; ey=360.0; windup=0; recovery=60;
}
// Checkpoints restore persistent RPG owners; transient input/action state always starts clean.
function resetTransient() {
    px=300.0; py=360.0; tick=0; remainder=0.0;
    cooldown=0; heavyCooldown=0; dodge=0; dodgeCooldown=0;
    strike=0; power=0.0; windup=0; recovery=60; flash=0; keys={};
    commands={x=0.0,y=0.0,attack=false,heavy=false,dodge=false};
}
function reset() {
    if(duel!=null) duel.close(); duel=null; save=null; world=null;
    if (enemy!=null) { enemy.release(); enemy=null; }
    if (hero!=null) hero.release();
    gs=rpg.newGameState(); tracker=rpg.newTracker(); tracker.syncAuto();
    hero=rpg.newActor(); hero.setBaseAttribute("hp",100.0); hero.setCurrent("hp",100.0); hero.setBaseAttribute("attack",18.0);
    bag=inv.newBag(2); bag.setId("hero.bag"); gear=inv.newEquipmentSet(); gear.setId("hero.gear"); gear.defineSlot("weapon");
    world=rpg.newWorldState(gs); save=eve.RPGSaveSession();
    if (save.bind(gs,tracker,hero,bag,gear)!=1 || save.setContentVersion("arpg-loop-v1")!=1) throw "save binding failed";
    resetTransient();
    spawn(); message="J attack / K heavy / Space dodge. Three sentinels guard the altar.";
}
function pendingLoot() {
    for(local i=0;i<3;i+=1)
        if(world.isObjectConsumed("rpg.action-kill.v1",enemyIds[i]) && !world.isObjectConsumed("arpg.loot",enemyIds[i])) return i;
    return -1;
}
function pickup() {
    if(distance(px,py,790.0,170.0)>90.0) { message="Move to the gold altar to claim a cache."; return; }
    local i=pendingLoot(); if(i<0) { message="No unclaimed cache."; return; }
    local result=rpg.collectWorldLoot(tracker,gs,bag,"arpg.loot",enemyIds[i],"","sentinel_blade",1,"",0.0,"", "",0);
    message=result.ok?"Blade collected. Q equips it; full caches remain claimable.":"Bag full: equip a blade with Q to free a slot, then retry E.";
}
function equipBlade() {
    local slot=bag.findItem("sentinel_blade");
    if(slot>=0 && gear.equipFromBag("weapon",bag,slot)) { rpg.syncEquipModifiers(hero,gear); message="Blade equipped: attack +8."; }
}
function checkpoint() {
    if(enemy!=null && enemy.getCurrent("hp")>0.0 || duel!=null && duel.hasPendingReward() || hero.getCurrent("hp")<=0.0) { message="Save after a kill, before N starts the next fight."; return; }
    local data=save.snapshotJson();
    message=data!="" && saveFs.writeTextAtomic("checkpoint.json",data)!=0?"Checkpoint saved, including unclaimed caches.":"Save failed.";
}
function loadCheckpoint() {
    local data=saveFs.readText("checkpoint.json");
    if(data=="" || save.restoreSnapshotJson(data)!=1) { message="No valid checkpoint; current run preserved."; return; }
    if(duel!=null) duel.close(); duel=null; if(enemy!=null) { enemy.release(); enemy=null; }
    rpg.syncEquipModifiers(hero,gear); resetTransient();
    message="Checkpoint restored. Rewards and caches remain deduplicated. N continues.";
}
function nextFight() {
    if(hero.getCurrent("hp")<=0.0 || enemy!=null && enemy.getCurrent("hp")>0.0 || duel!=null && duel.hasPendingReward()) return;
    spawn();
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
    if(commands.dodge && dodgeCooldown==0) { dodge=18; dodgeCooldown=55; }
    commands.dodge=false;
    px=maxv(30.0,minv(930.0,px+dx*(dodge>0?7.0:3.0))); py=maxv(140.0,minv(600.0,py+dy*(dodge>0?7.0:3.0)));
    if(enemy==null || enemy.getCurrent("hp")<=0.0) {
        commands.attack=false; commands.heavy=false;
        if(duel!=null && duel.hasPendingReward()) { local claim=duel.claim(gs,tracker,"sentinel"); if(claim.ok) message="Sentinel defeated! E at altar / Q equip / F5 save / N next."; else message="Reward pending: retry next frame."; }
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
    gameState={tick=tick,hp=hero.getCurrent("hp"),enemyHp=enemy==null?0.0:enemy.getCurrent("hp"),kills=gs.getVariable("kills"),gold=gs.getVariable("gold"),level=hero.getLevel(),attack=hero.getFinalAttribute("attack"),items=bag.countItem("sentinel_blade"),pendingLoot=pendingLoot(),quest=tracker.getState("sentinels"),px=px,py=py,ex=ex,ey=ey,windup=windup,dodge=dodge,cooldown=cooldown,heavyCooldown=heavyCooldown,dodgeCooldown=dodgeCooldown,strike=strike,inputX=commands.x,inputY=commands.y,message=message};
}
game <- {};
game.attack <- function(){commands.attack=true;}; game.heavy <- function(){commands.heavy=true;}; game.dodge <- function(){commands.dodge=true;};
game.right <- function(){commands.x=1.0;commands.y=0.0;}; game.left <- function(){commands.x=-1.0;commands.y=0.0;};
game.up <- function(){commands.y=-1.0;commands.x=0.0;}; game.down <- function(){commands.y=1.0;commands.x=0.0;}; game.stop <- function(){commands.x=0.0;commands.y=0.0;};
game.pickup <- pickup.bindenv(getroottable()); game.equip <- equipBlade.bindenv(getroottable()); game.save <- checkpoint.bindenv(getroottable()); game.load <- loadCheckpoint.bindenv(getroottable()); game.next <- nextFight.bindenv(getroottable()); game.reset <- reset.bindenv(getroottable());
eve_init <- function(){
    rpg=eve.RPG(); inv=eve.Inventory(); saveFs=eve.Filesystem();
    if(!saveFs.setIdentity("arpg-loop",true)||!saveFs.setupWriteDirectory()) throw "save directory unavailable";
    if(rpg.registerSkillsFromJson("[{\"id\":\"sentinel.claw\"}]")!=1) throw "skill content rejected";
    checked(rpg.replaceQuestsFromJson("[{\"id\":\"sentinels\",\"startPolicy\":\"auto\",\"objectives\":[{\"id\":\"kills\",\"topic\":\"kill\",\"target\":\"sentinel\",\"count\":3}]}]"));
    checked(rpg.replaceEncountersFromJson(readData("data/encounters.json")));
    if(inv.registerItemsFromJson("[{\"id\":\"sentinel_blade\",\"displayName\":\"Sentinel Blade\",\"maxStack\":1,\"equipSlot\":\"weapon\"}]")!=1) throw "item content rejected";
    if(rpg.registerItemStatsFromJson("sentinel_blade","[{\"attribute\":\"attack\",\"op\":\"add\",\"value\":8}]")!=1) throw "equipment content rejected";
    fontData=eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",22);
    hudFont=gfx.newFont(fontData," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%");
    gfx.setBackgroundColor(0.025,0.035,0.06,1.0); reset(); observe();
};
eve_update <- function(dt){
    if(pressed("j")) game.attack(); if(pressed("k")) game.heavy(); if(pressed("space")) game.dodge();
    if(pressed("e")) pickup(); if(pressed("q")) equipBlade(); if(pressed("n")) nextFight();
    if(pressed("f5")) checkpoint(); if(pressed("f9")) loadCheckpoint(); if(pressed("r")) reset();
    remainder+=minv(dt,0.1); while(remainder>=1.0/60.0){simulate();remainder-=1.0/60.0;} observe();
};
function text(value,x,y){gfx.drawText(hudFont,value,x,y,0.9,0.93,1.0,1.0,0.75);}
eve_render <- function(){
    gfx.clear(); gfx.drawSolidRect(20.0,130.0,920.0,480.0,0.06,0.09,0.13,1.0);
    gfx.drawSolidRect(760.0,140.0,60.0,60.0,pendingLoot()>=0?0.95:0.25,0.65,0.18,1.0);
    gfx.drawSolidRect(px-18,py-18,36.0,36.0,0.2,dodge>0?1.0:0.6,1.0,1.0);
    if(enemy!=null && enemy.getCurrent("hp")>0.0){
        if(windup>0) gfx.drawSolidRect(ex-65,ey-65,130.0,130.0,0.4,0.15,0.08,1.0);
        gfx.drawSolidRect(ex-22,ey-22,44.0,44.0,1.0,flash>0?1.0:0.25,0.2,1.0);
        gfx.drawSolidRect(ex-30,ey-38,60.0*enemy.getCurrent("hp")/60.0,5.0,0.9,0.2,0.2,1.0);
    }
    text("THE THREE SENTINELS / ARPG",25.0,15.0);
    text("HP "+hero.getCurrent("hp").tointeger()+"  Level "+hero.getLevel()+"  ATK "+hero.getFinalAttribute("attack").tointeger()+"  Gold "+gs.getVariable("gold").tointeger()+"  Kills "+gs.getVariable("kills").tointeger()+"/3",25.0,45.0);
    text("WASD move  J attack  K heavy  Space dodge  E loot  Q equip",25.0,75.0);
    text("N next  F5 save  F9 load  R restart",25.0,102.0);
    text(message,25.0,615.0);
    if(hero.getCurrent("hp")<=0.0) text("DEFEAT / R restart or F9 restore checkpoint",220.0,300.0);
    else if(gs.getVariable("kills")>=3) text("VICTORY / claim caches and save your growth",220.0,300.0);
};
