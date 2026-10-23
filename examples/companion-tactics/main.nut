dofile("behavior.nut");
dofile("planner.nut");
dofile("positioning.nut");
dofile("simulation.nut");
dofile("presentation.nut");
dofile("verification.nut");

local simulation=null, accumulator=0.0, previous={};
// Read-only observation projection. It is not a second mutable simulation root.
companionObservation <- {tick=0,heals=0,guards=0,dodges=0,absorbed=0,hits=0,transitions=0,
    playerHp=120.0,command="BALANCED",auto=true,guardState="follow",healerState="follow",tests="not-run",
    guardDistance=0.0,healerDistance=0.0,peerDistance=0.0,guardEta=0.0,healerEta=0.0};
game <- {};
game.reset <- function() {
    if(simulation!=null) simulation.close();
    simulation=CompanionSimulation();accumulator=0.0;
};
game.balanced <- function() {simulation.order("BALANCED");};
game.focus <- function() {simulation.order("FOCUS");};
game.protect <- function() {simulation.order("PROTECT");};
game.retreat <- function() {simulation.order("RETREAT");};
game.toggleAuto <- function() {simulation.auto=!simulation.auto;};
game.verify <- function() {ctVerify();companionObservation.tests="passed";};

function ctPressed(key) {
    local down=keyboard.isDown(key),was=key in previous ? previous[key] : false;
    previous[key] <- down;return down && !was;
}
eve_init = function() {
    ctPresentationInit();game.reset();
    print("COMPANION_TACTICS_READY native=npc_ai fixedHz=60 decisionHz=10\n");
};
eve_update = function(dt) {
    if(ctPressed("1")) game.balanced();
    if(ctPressed("2")) game.focus();
    if(ctPressed("3")) game.protect();
    if(ctPressed("4")) game.retreat();
    if(ctPressed("p")) game.toggleAuto();
    if(ctPressed("r")) game.reset();
    if(ctPressed("t")) simulation.focus=simulation.focus==3 ? 4 : 3;
    local input={x=0.0,y=0.0,attack=keyboard.isDown("space")};
    if(keyboard.isDown("a") || keyboard.isDown("left")) input.x-=1;
    if(keyboard.isDown("d") || keyboard.isDown("right")) input.x+=1;
    if(keyboard.isDown("w") || keyboard.isDown("up")) input.y-=1;
    if(keyboard.isDown("s") || keyboard.isDown("down")) input.y+=1;
    // Carry accumulated time; cap work per render, never discard simulation time.
    accumulator+=dt;
    local steps=0;
    while(accumulator>=1.0/60 && steps<12) {simulation.step(1.0/60,input);accumulator-=1.0/60;steps++;}
    foreach(k,v in simulation.stats) if(k in companionObservation) companionObservation[k]=v;
    companionObservation.tick=simulation.tick;companionObservation.playerHp=simulation.units[0].hp;
    companionObservation.command=simulation.command;companionObservation.auto=simulation.auto;
    companionObservation.guardState=simulation.units[1].state;companionObservation.healerState=simulation.units[2].state;
    companionObservation.guardDistance=ctDistance(simulation.units[1],simulation.units[0]);
    companionObservation.healerDistance=ctDistance(simulation.units[2],simulation.units[0]);
    companionObservation.peerDistance=ctDistance(simulation.units[1],simulation.units[2]);
    companionObservation.guardEta=ctSupportEta(simulation,simulation.units[1],simulation.units[0],1);
    companionObservation.healerEta=ctSupportEta(simulation,simulation.units[2],simulation.units[0],0);
};
eve_render = function() {if(simulation!=null) ctRender(simulation);};
