// Execute the actual game adapter with native gameplay providers. Only keyboard
// input and the unbuilt presentation arrays are replaced for headless execution.
keyboard <- {isDown=function(key) { return false; }};
arenaRobots=[[],[]];
function checkArena(value,label) {
    if(!value) throw "GAS CHECK FAILED: "+label;
    print("PASS "+label+"\n");
}
function startArenaCheck() {
    resetArena();arenaAuto=3;eve_update(0.01);
}
startArenaCheck();
checkArena(arenaAbilities.matchingAbilities("fighter:player","Ability.Attack.Heavy").value.len()==1,"event tag resolves a native grant");
checkArena(activateArenaSkill(0),"light admitted");
local paid=arenaStats.getBase("Attribute.Energy",0.0);
checkArena(!activateArenaSkill(1) && arenaStats.getBase("Attribute.Energy",0.0)==paid,"exclusive group rejection costs no energy");
cancelArenaAbility();eve_update(0.01);
checkArena(arenaHitCount==0 && arenaAbilities.findAction(arenaLastExecution).value.phase=="cancelled" && arenaAbilities.advanceAbilities(tick,0.0).value.activeCount==0,"cancel windup prevents damage and marks execution terminal");
checkArena(arenaStats.getBase("Attribute.Energy",0.0)<86.0,"cancel does not refund admitted cost");

startArenaCheck();
local oldGrant=arenaSkills[1].grant;
replaceArenaHeavyDefinition();
checkArena(arenaSkills[1].grant==oldGrant,"hot replacement retains grant");
checkArena(activateArenaSkill(1),"charged heavy admitted");
local oldCooldown=arenaAbilities.abilityGrant(oldGrant).value.cooldownSeconds;
toggleArenaHeavyGrant();
checkArena(arenaSkills[1].grant==oldGrant,"active grant cannot be revoked");
replaceArenaHeavyDefinition();
checkArena(arenaAbilities.abilityGrant(oldGrant).value.cooldownSeconds==oldCooldown,"replacement preserves existing cooldown");
eve_update(0.8);
checkArena(arenaHitCount==0 && arenaPhase=="windup","active execution retains charged definition snapshot");
cancelArenaAbility();toggleArenaHeavyGrant();
checkArena(arenaSkills[1].grant==0 && arenaAbilities.matchingAbilities("fighter:player","Ability.Attack.Heavy").value.len()==0,"revoke removes gameplay event match");
checkArena(!activateArenaSkill(1),"unequipped ability cannot activate");
toggleArenaHeavyGrant();
checkArena(arenaSkills[1].grant!=oldGrant && arenaSkills[1].grant!=0,"re-grant gets new native identity");

startArenaCheck();
checkArena(activateArenaSkill(2),"buff admitted");eve_update(0.3);
checkArena(arenaStats.getFinal("Attribute.Attack",0.0)==18.0 && arenaStats.getModifierCount()==1,"applied event projects one native modifier");
local buffId=arenaEffects.taggedAt(playerId,"State.Empowered",0).getId();
paid=arenaStats.getBase("Attribute.Energy",0.0);
checkArena(!activateArenaSkill(2) && arenaStats.getBase("Attribute.Energy",0.0)==paid,"tag condition blocks duplicate buff without cost");
eve_update(3.0);refreshArenaBuff();
checkArena(arenaEffects.taggedAt(playerId,"State.Empowered",0).getId()==buffId && arenaBuffRemaining()>11.9,"refresh retains effect identity and resets duration");
checkArena(arenaStats.getModifierCount()==1 && arenaStats.getFinal("Attribute.Attack",0.0)==18.0,"refresh does not compound multiplier");
eve_update(12.1);
checkArena(arenaStats.getModifierCount()==0 && arenaStats.getFinal("Attribute.Attack",0.0)==12.0,"expired event removes source modifier");

startArenaCheck();
for(local hit=0;hit<4;++hit) {
    checkArena(activateArenaSkill(0),"stacking attack admitted");eve_update(2.1);
}
checkArena(arenaHitCount==4 && abs(arenaEnemyState.health-144.8)<0.01,"damage uses accumulating native vulnerability attribute");
checkArena(arenaEffects.taggedCount(enemyId,"State.Exposed")==3 && arenaTargetStats.getModifierCount()==3,"independent effect stacks capped at three");
checkArena(abs(arenaTargetStats.getFinal("Attribute.DamageTaken",1.0)-1.3)<0.01,"payload projects additive modifiers");
dispelArenaEffects();
checkArena(arenaEffects.effectCount()==0 && arenaTargetStats.getModifierCount()==0,"dispel removes all effects and source modifiers");
checkArena(arenaTargetStats.getFinal("Attribute.DamageTaken",1.0)==1.0,"dispel restores incoming damage baseline");
applyArenaExposed();applyArenaOverdrive();eve_update(12.1);
checkArena(arenaEffects.effectCount()==0 && arenaStats.getModifierCount()==0 && arenaTargetStats.getModifierCount()==0,"lifecycle events clean both subjects on expiry");
resetArena();
for(local frame=0;frame<50;++frame) eve_update(0.1);
checkArena(arenaHitCount==2 && abs(arenaEnemyState.health-140.48)<0.01,"automatic showcase composes buff and target vulnerability");
checkArena(arenaStats.getFinal("Attribute.Attack",0.0)==18.0 && arenaEffects.taggedCount(enemyId,"State.Exposed")==2,"automatic showcase exposes live native modifiers");
print("ALL ARENA GAS API CHECKS PASSED\n");
