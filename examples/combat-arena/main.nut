// Native AbilityRuntime + AttributeSet + EffectContainer, composed by a small
// game adapter. Only pose/VFX and the audit log are presentation-owned state.
persist arena = null;
persist playerId = "11121314-1516-1718-991a-1b1c1d1e1f20";
persist enemyId = "01020304-0506-0708-890a-0b0c0d0e0f10";
persist arenaAbilities = null;
persist arenaStats = null;
persist arenaTargetStats = null;
persist arenaHeavyVariant = false;
persist arenaInspect = false;
persist arenaEffectEventCount = 0;
persist arenaLastExecution = 0;
persist arenaEffects = null;
persist arenaCamera = null;
persist arenaFont = null;
persist arenaObjects = [];
persist arenaRobots = [];
persist arenaFx = [];
persist arenaCube = null;
persist arenaSphere = null;
persist arenaCylinder = null;
persist arenaSkills = [];
persist arenaPending = {};
persist arenaKeys = {};
persist arenaLog = [];
persist tick = 0;
persist elapsed = 0.0;
persist arenaAuto = 0;
persist arenaPhase = "ready";
persist arenaMessage = "SYSTEM READY";
persist arenaFlash = 0.0;
persist arenaLastDamage = 0.0;
persist arenaHitCount = 0;
persist arenaPlayerState = null;
persist arenaEnemyState = null;
persist arenaYaw = 1.5708;
persist arenaStrike = 0.0;

function requireArena(result, label) {
    if (!result.ok) throw label + ": " + result.status.summary;
    return result.value;
}
function limitArena(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
function arenaNote(text) {
    arenaMessage = text;
    arenaLog.insert(0, text);
    if (arenaLog.len() > 5) arenaLog.pop();
    print("combat-arena: " + text + "\n");
}
function arenaPart(mesh, x, y, z, sx, sy, sz, r, g, b, lit=true) {
    local part = eve.Renderable3D(); part.setMesh(mesh);
    part.setPosition(x,y,z); part.setScale(sx,sy,sz); part.setTint(r,g,b,1.0);
    part.setRoughness(0.7); part.setReceiveLight(lit);
    arenaObjects.push(part); return part;
}
function arenaBox(x,y,z,sx,sy,sz,r,g,b,lit=true) {
    return arenaPart(arenaCube,x,y,z,sx,sy,sz,r,g,b,lit);
}
function arenaRobot(r,g,b) {
    local parts = [];
    local definitions = [
        [0.0,1.02,0.0,0.55,0.62,0.36,0.16,0.22,0.3],
        [0.0,1.08,0.205,0.36,0.32,0.055,r,g,b],
        [0.0,0.64,0.0,0.38,0.18,0.3,0.1,0.14,0.2],
        [-0.17,0.32,0.0,0.2,0.5,0.23,0.22,0.29,0.38],
        [0.17,0.32,0.0,0.2,0.5,0.23,0.22,0.29,0.38],
        [-0.17,0.1,0.08,0.24,0.15,0.4,0.1,0.14,0.2],
        [0.17,0.1,0.08,0.24,0.15,0.4,0.1,0.14,0.2],
        [-0.4,1.15,0.0,0.26,0.25,0.36,r,g,b],
        [0.4,1.15,0.0,0.26,0.25,0.36,r,g,b],
        [-0.4,0.84,0.0,0.18,0.42,0.23,0.26,0.32,0.42],
        [0.4,0.84,0.0,0.18,0.42,0.23,0.26,0.32,0.42],
        [0.0,1.56,0.0,0.43,0.43,0.39,0.22,0.28,0.36],
        [0.0,1.57,0.205,0.32,0.075,0.04,r,g,b],
        [0.48,0.84,0.56,0.065,0.07,0.95,r,g,b],
        [-0.47,0.87,0.2,0.12,0.45,0.42,0.12,0.18,0.25]
    ];
    foreach (d in definitions) {
        local p=arenaBox(d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7],d[8],parts.len()!=12 && parts.len()!=13);
        parts.push({object=p,x=d[0],y=d[1],z=d[2]});
    }
    return parts;
}
function buildArenaScene() {
    arenaCube=gfx.newMeshCube(1.0); arenaSphere=gfx.newMeshSphere(12,8);
    arenaCylinder=gfx.newMeshCylinder(48,1,true);
    gfx.setBackgroundColor(0.025,0.038,0.065,1.0);
    gfx.setDirectionalLight(-0.4,0.9,0.5,0.9,0.95,1.0);
    arenaCamera=eve.Camera3D(); arenaCamera.setEye(7.8,6.8,10.5);
    arenaCamera.setTarget(0.0,0.2,0.0); arenaCamera.setFov(43.0);
    arenaCamera.setAmbient(0.28,0.32,0.42); arenaCamera.setActive(true);
    arenaBox(0.0,-0.32,0.0,11.4,0.5,8.4,0.06,0.09,0.15);
    arenaBox(0.0,-0.045,0.0,11.0,0.1,8.0,0.13,0.18,0.25);
    for(local x=-5;x<=5;++x) arenaBox(x.tofloat(),0.012,0.0,0.012,0.01,7.8,0.2,0.28,0.36);
    for(local z=-3;z<=3;++z) arenaBox(0.0,0.012,z.tofloat(),10.8,0.01,0.012,0.2,0.28,0.36);
    arenaBox(0.0,0.02,-3.95,11.1,0.04,0.045,0.12,0.8,0.9,false);
    arenaBox(0.0,0.02,3.95,11.1,0.04,0.045,0.12,0.8,0.9,false);
    arenaBox(-5.5,0.02,0.0,0.045,0.04,7.9,0.12,0.8,0.9,false);
    arenaBox(5.5,0.02,0.0,0.045,0.04,7.9,1.0,0.55,0.18,false);
    // Segmented inlay around the training zone; shares the same mesh.
    for(local i=0;i<32;++i) {
        local a=i*6.283185/32.0;
        local p=arenaBox(cos(a)*3.1,0.025,sin(a)*3.1,0.36,0.012,0.035,0.35,0.53,0.64,false);
        p.setYaw(-a);
    }
    foreach(x in [-5.0,5.0]) foreach(z in [-3.5,3.5]) {
        arenaBox(x,0.55,z,0.45,1.1,0.45,0.07,0.11,0.18);
        arenaBox(x,1.15,z,0.54,0.12,0.54,0.2,0.29,0.4);
        arenaBox(x,0.72,z+0.24,0.12,0.65,0.035,0.18,0.8,0.95,false);
    }
    arenaRobots.push(arenaRobot(0.12,0.8,0.95));
    arenaRobots.push(arenaRobot(1.0,0.5,0.15));
    for(local i=0;i<12;++i) {
        local p=arenaBox(0.0,-10.0,0.0,0.04,0.04,0.04,0.6,0.95,1.0,false);
        arenaFx.push(p);
    }
    local fontData=eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",28);
    arenaFont=gfx.newFont(fontData," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-:/+.%[]()_>=!|");
}
function resetArena() {
    arena=requireArena(eve.Combat().newRuntime(),"Create combat runtime");
    arenaAbilities=requireArena(eve.Action().newRuntime(),"Create ability runtime");
    requireArena(arena.registerFighter(playerId,"fighter:player",-1.0,0.0,100.0,40.0,3.4,20.0),"Player");
    requireArena(arena.registerFighter(enemyId,"fighter:enemy",1.0,0.0,200.0,80.0,3.0,18.0),"Target");
    arenaStats=attributes.newSet(playerId);
    arenaStats.setBase("Attribute.Energy",100.0); arenaStats.setBase("Attribute.Attack",12.0);
    arenaTargetStats=attributes.newSet(enemyId);
    arenaTargetStats.setBase("Attribute.DamageTaken",1.0);
    arenaHeavyVariant=false; arenaEffectEventCount=0; arenaLastExecution=0;
    if(arenaEffects!=null) arenaEffects.release();
    arenaEffects=requireArena(eve.Effects().newContainer(),"Create effects");
    arenaSkills=[
        {key="j",label="VECTOR SLASH",definition="light-attack",cost=15.0,damage=1.0,cd=2.0,tag="Ability.Attack.Slash",grant=0,cooldown=0.0},
        {key="k",label="BREAKER",definition="heavy-attack",cost=40.0,damage=2.4,cd=4.5,tag="Ability.Attack.Heavy",grant=0,cooldown=0.0},
        {key="l",label="OVERDRIVE",definition="buff",cost=30.0,damage=0.0,cd=14.0,tag="Ability.Buff.Overdrive",grant=0,cooldown=0.0}
    ];
    foreach(s in arenaSkills) {
        local id="arena-ability:"+s.definition;
        requireArena(arenaAbilities.registerTimelineAbility(id,fs.readText("data/"+s.definition+".json"),s.cd,
            "per-execution","exclusive-blocking",s.tag),"Register timeline ability");
        s.grant=requireArena(arenaAbilities.grantAbility("fighter:player",id),"Grant ability").grantId;
    }
    arenaPending={}; arenaLog=[];
    tick=0; elapsed=0.0; arenaAuto=0; arenaPhase="ready";
    arenaFlash=0.0; arenaLastDamage=0.0; arenaHitCount=0; arenaStrike=0.0;
    arenaNote("Granted 3 abilities / attributes initialized");
}
function arenaBuffRemaining() {
    if(arenaEffects.taggedCount(playerId,"State.Empowered")==0) return 0.0;
    return arenaEffects.taggedAt(playerId,"State.Empowered",0).getRemaining();
}
function activateArenaSkill(index) {
    local s=arenaSkills[index];
    if(arenaEnemyState!=null && arenaEnemyState.health<=0.0) {arenaNote("Target defeated / press R to reset");return false;}
    if(s.grant==0) {arenaNote("Blocked: skill is unequipped / U to grant");return false;}
    if(index==2 && arenaBuffRemaining()>0.0) {arenaNote("Blocked: State.Empowered is already active");return false;}
    if(arenaStats.getBase("Attribute.Energy",0.0)<s.cost) {arenaNote("Blocked: insufficient energy");return false;}
    local matching=requireArena(arenaAbilities.matchingAbilities("fighter:player",s.tag),"Match gameplay event");
    if(matching.len()!=1 || matching[0]!=s.grant) throw "Gameplay event must resolve one grant";
    local result=arenaAbilities.activateAbility(matching[0],tick);
    if(!result.ok) {arenaNote("Blocked: cooldown or activation group");return false;}
    // The game adapter debits the native AttributeSet only after admission.
    // This demo does not claim a native transactional CostSpec binding.
    arenaStats.modifyBase("Attribute.Energy",-s.cost);
    arenaPending[result.value.activationId] <- {skill=index,consumed=false};
    arenaLastExecution=result.value.executionId;
    arenaNote(s.tag+" / cost "+s.cost.tointeger());
    return true;
}
// Event records are consumed before clearEvents; no borrowed effect/event is
// retained across mutations. AttributeSet remains the modifier authority.
function drainArenaEffectEvents() {
    for(local i=0;i<arenaEffects.eventCount();++i) {
        local event=arenaEffects.eventAt(i);
        local kind=event.getKind();local id=event.getEffectId();local type=event.getType();
        local source="effect:"+id;
        arenaEffectEventCount+=1;
        if(kind=="applied" || kind=="refreshed") {
            local effect=arenaEffects.find(id);
            if(effect==null) continue;
            if(effect.hasTag("State.Empowered")) {
                local multiplier=effect.getPayload().getJson("attackMultiplier").tofloat();
                if(!arenaStats.addModifier(source,"Attribute.Attack",source,"multiply",multiplier,10))
                    throw "Apply attack modifier failed";
            } else if(effect.hasTag("State.Exposed")) {
                local additive=effect.getPayload().getJson("damageTakenAdd").tofloat();
                if(!arenaTargetStats.addModifier(source,"Attribute.DamageTaken",source,"add",additive,0))
                    throw "Apply vulnerability modifier failed";
            }
        } else if(kind=="removed" || kind=="expired") {
            arenaStats.removeBySource(source,"Attribute.Attack");
            arenaTargetStats.removeBySource(source,"Attribute.DamageTaken");
        }
        arenaNote("Effect "+kind+" / "+type);
    }
    arenaEffects.clearEvents();
}
function applyArenaOverdrive() {
    local id=requireArena(arenaEffects.apply(playerId,"Effect.Buff.Overdrive","ability:overdrive",10,12.0,"overdrive","refresh"),"Apply buff");
    local effect=arenaEffects.find(id);
    requireArena(effect.addTag("State.Empowered"),"Tag buff");
    effect.getPayload().setNumber("attackMultiplier",1.5);
    drainArenaEffectEvents(); return id;
}
function applyArenaExposed() {
    if(arenaEffects.taggedCount(enemyId,"State.Exposed")>=3) {
        local oldest=arenaEffects.taggedAt(enemyId,"State.Exposed",0).getId();
        requireArena(arenaEffects.remove(oldest,"stack-cap"),"Replace oldest exposure");
        drainArenaEffectEvents();
    }
    local id=requireArena(arenaEffects.apply(enemyId,"Effect.Debuff.Exposed","ability:melee",1,8.0,"exposed","stack"),"Apply exposure");
    local effect=arenaEffects.find(id);
    requireArena(effect.addTag("State.Exposed"),"Tag exposure");
    effect.getPayload().setNumber("damageTakenAdd",0.1);
    drainArenaEffectEvents();
}
function consumeArenaActivation(record) {
    local s=arenaSkills[record.skill];
    if(record.skill==2) {applyArenaOverdrive();return;}
    local dx=arenaPlayerState.position.x-arenaEnemyState.position.x;
    local dz=arenaPlayerState.position.z-arenaEnemyState.position.z;
    if(dx*dx+dz*dz>2.7*2.7) {arenaNote("Miss / target outside 2.7m range");return;}
    local damage=arenaStats.getFinal("Attribute.Attack",0.0)*s.damage*
        arenaTargetStats.getFinal("Attribute.DamageTaken",1.0);
    local hit=requireArena(arena.applyDamage(playerId,enemyId,"Damage.Physical.Slash",damage,8.0,0.0,0.0,0.0,false,tick),"Apply damage");
    arenaLastDamage=hit.appliedHealthDamage; arenaHitCount+=1; arenaFlash=0.55;
    if(hit.appliedHealthDamage>0.0) applyArenaExposed();
    arenaNote(format("Damage %.2f / target %.2f HP",arenaLastDamage,hit.health));
}
function cancelArenaAbility() {
    foreach(id,record in arenaPending) {
        local cancelled=arenaAbilities.cancelAbility(id,tick);
        if(!cancelled.ok) {arenaNote("Cancel rejected: execution is terminal");return;}
        delete arenaPending[id];
        arenaNote("Ability cancelled / paid energy not refunded");return;
    }
    arenaNote("No active ability to cancel");
}
function toggleArenaHeavyGrant() {
    local skill=arenaSkills[1];
    if(skill.grant!=0) {
        local revoked=arenaAbilities.revokeAbility(skill.grant);
        if(!revoked.ok) {arenaNote("Revoke rejected: cancel active heavy first");return;}
        skill.grant=0;skill.cooldown=0.0;
        arenaNote("Heavy grant revoked / tag no longer matches");
    } else {
        skill.grant=requireArena(arenaAbilities.grantAbility("fighter:player","arena-ability:heavy-attack"),"Re-grant heavy").grantId;
        arenaNote("Heavy re-granted / new native grant identity");
    }
}
function replaceArenaHeavyDefinition() {
    local variant=!arenaHeavyVariant;
    local file=variant?"heavy-charge":"heavy-attack";
    requireArena(arenaAbilities.replaceTimelineAbility("arena-ability:heavy-attack",fs.readText("data/"+file+".json"),
        variant?6.0:4.5,"per-execution","exclusive-blocking","Ability.Attack.Heavy"),"Replace heavy definition");
    arenaHeavyVariant=variant;
    arenaNote(variant?"Heavy hot-reloaded: 1.8s windup / grants retained":"Heavy restored / active snapshots unchanged");
}
function refreshArenaBuff() {
    if(arenaBuffRemaining()<=0.0) {arenaNote("No Overdrive effect to refresh");return;}
    local previous=arenaEffects.taggedAt(playerId,"State.Empowered",0).getId();
    local refreshed=applyArenaOverdrive();
    if(previous!=refreshed) throw "Refresh must preserve the native effect identity";
    arenaNote("Buff refreshed / same effect ID / no extra modifier");
}
function dispelArenaEffects() {
    local ids=[];
    for(local i=0;i<arenaEffects.effectCount();++i) ids.push(arenaEffects.effectAt(i).getId());
    foreach(id in ids) requireArena(arenaEffects.remove(id,"training-dispel"),"Dispel effect");
    drainArenaEffectEvents();arenaNote("Dispelled all effects / source modifiers removed");
}
function poseArenaRobot(parts,state,yaw,swing,target=false) {
    local bob=target?0.0:sin(elapsed*8.0)*0.025;
    foreach(i,p in parts) {
        local lx=p.x; local lz=p.z;
        if(i==13) {lx+=sin(swing)*0.55;lz+=cos(swing)*0.25;}
        p.object.setPosition(state.position.x+lx*cos(yaw)+lz*sin(yaw),p.y+bob,state.position.z-lx*sin(yaw)+lz*cos(yaw));
        p.object.setYaw(yaw+(i==13?swing:0.0));
        if(i==1 && target) p.object.setTint(arenaFlash>0.0?1.0:0.95,0.45,0.12,1.0);
        if(i==11 && target) p.object.setTint(arenaFlash>0.0?1.0:0.22,arenaFlash>0.0?0.8:0.28,arenaFlash>0.0?0.6:0.36,1.0);
    }
}
function arenaPressed(key) {
    local down=keyboard.isDown(key);local was=(key in arenaKeys)?arenaKeys[key]:false;
    arenaKeys[key] <- down; return down && !was;
}
eve_init=function() {
    if(arenaCamera==null) buildArenaScene();
    if(arena==null) resetArena();
};
eve_update=function(dt) {
    if(arenaPressed("r")) resetArena();
    elapsed+=dt;tick+=1;
    arenaFlash=limitArena(arenaFlash-dt,0.0,1.0);
    requireArena(arenaEffects.update(dt),"Advance effects");
    drainArenaEffectEvents();
    if(arenaPressed("c")) {arenaAuto=3;cancelArenaAbility();}
    if(arenaPressed("u")) {arenaAuto=3;toggleArenaHeavyGrant();}
    if(arenaPressed("h")) {arenaAuto=3;replaceArenaHeavyDefinition();}
    if(arenaPressed("f")) {arenaAuto=3;refreshArenaBuff();}
    if(arenaPressed("v")) {arenaAuto=3;dispelArenaEffects();}
    if(arenaPressed("i")) arenaInspect=!arenaInspect;
    arenaStats.setBase("Attribute.Energy",limitArena(arenaStats.getBase("Attribute.Energy",0.0)+dt*4.0,0.0,100.0));
    local x=0.0;local z=0.0;
    if(keyboard.isDown("a")) x-=1.0;if(keyboard.isDown("d")) x+=1.0;
    if(keyboard.isDown("w")) z-=1.0;if(keyboard.isDown("s")) z+=1.0;
    arenaPlayerState=requireArena(arena.state(playerId),"Read player");
    // Soft movement limits are enforced through native movement intents.
    if(arenaPlayerState.position.x < -4.5 && x<0.0) x=0.0;
    if(arenaPlayerState.position.x>4.5 && x>0.0) x=0.0;
    if(arenaPlayerState.position.z < -3.0 && z<0.0) z=0.0;
    if(arenaPlayerState.position.z>3.0 && z>0.0) z=0.0;
    if(x!=0.0 || z!=0.0) {
        arenaAuto=3;requireArena(arena.setMoveIntent(playerId,x,z,1.0),"Move player");
    } else requireArena(arena.stop(playerId),"Stop player");
    requireArena(arena.advance(tick,dt),"Advance combat");
    arenaPlayerState=requireArena(arena.state(playerId),"Read player");
    arenaEnemyState=requireArena(arena.state(enemyId),"Read target");
    foreach(i,s in arenaSkills) if(arenaPressed(s.key)) {arenaAuto=3;activateArenaSkill(i);}
    // Unattended showcase: normal hit -> native effect -> empowered heavy hit.
    if(arenaAuto==0 && elapsed>0.6) {activateArenaSkill(0);arenaAuto=1;}
    else if(arenaAuto==1 && elapsed>1.6) {activateArenaSkill(2);arenaAuto=2;}
    else if(arenaAuto==2 && elapsed>2.8) {activateArenaSkill(1);arenaAuto=3;}
    local advanced=requireArena(arenaAbilities.advanceAbilities(tick,dt),"Advance abilities");
    arenaPhase="ready";arenaStrike=0.0;
    foreach(execution in advanced.advances) {
        arenaPhase=execution.phase;
        if(execution.phase=="active") arenaStrike=1.0;
        if(execution.activationId in arenaPending) {
            local record=arenaPending[execution.activationId];
            local reached=execution.phase=="active" || execution.phase=="recover" || execution.phase=="completed";
            if(!record.consumed && reached) {record.consumed=true;consumeArenaActivation(record);}
            if(execution.phase=="completed" || execution.phase=="cancelled" || execution.phase=="failed") delete arenaPending[execution.activationId];
        }
    }
    foreach(s in arenaSkills) s.cooldown=s.grant==0?0.0:
        requireArena(arenaAbilities.abilityGrant(s.grant),"Read cooldown").cooldownSeconds;
    arenaEnemyState=requireArena(arena.state(enemyId),"Read target after damage");
    local dx=arenaEnemyState.position.x-arenaPlayerState.position.x;
    local dz=arenaEnemyState.position.z-arenaPlayerState.position.z;
    if(dx*dx+dz*dz>0.01) arenaYaw=atan2(dx,dz);
    poseArenaRobot(arenaRobots[0],arenaPlayerState,arenaYaw,arenaStrike*1.4);
    poseArenaRobot(arenaRobots[1],arenaEnemyState,-1.5708,0.0,true);
    foreach(i,p in arenaFx) {
        p.setVisible(arenaFlash>0.0);
        local a=i*6.283185/arenaFx.len(); local radius=(0.55-arenaFlash)*3.0+0.35;
        p.setPosition(arenaEnemyState.position.x+cos(a)*radius,0.9+sin(a*3.0)*0.35,arenaEnemyState.position.z+sin(a)*radius);
        p.setScale(0.04,0.04,0.16);p.setYaw(-a);
    }
};
function arenaText(text,x,y,scale=0.55,r=0.78,g=0.86,b=0.94) {
    gfx.print(text,x,y,r,g,b,1.0,scale);
}
function arenaPanel(x,y,w,h) {
    gfx.drawSolidRect(x,y,w,h,0.025,0.04,0.065,0.94);
    gfx.drawSolidRect(x,y,2.0,h,0.12,0.75,0.9,1.0);
}
eve_render=function() {
    gfx.clear();gfx.render3D();
    if(arenaPlayerState==null) return;
    gfx.setFont(arenaFont);
    arenaText("CIRCUIT / ABILITY LAB",28.0,21.0,0.82,0.9,0.96,1.0);
    arenaText("03 ABILITIES    /    NATIVE RUNTIME    /    TRAINING 01",29.0,54.0,0.38,0.38,0.6,0.73);
    arenaPanel(28.0,96.0,260.0,185.0);
    arenaText("ATTRIBUTE SET",43.0,109.0,0.4,0.25,0.8,0.92);
    arenaText(format("ENERGY  %.0f / 100",arenaStats.getBase("Attribute.Energy",0.0)),43.0,141.0);
    gfx.drawSolidRect(43.0,169.0,225.0,5.0,0.1,0.18,0.25,1.0);
    gfx.drawSolidRect(43.0,169.0,225.0*arenaStats.getBase("Attribute.Energy",0.0)/100.0,5.0,0.12,0.8,0.95,1.0);
    arenaText(format("ATTACK  %.0f > %.0f",arenaStats.getBase("Attribute.Attack",0.0),arenaStats.getFinal("Attribute.Attack",0.0)),43.0,191.0);
    arenaText("PHASE / "+arenaPhase.toupper(),43.0,232.0,0.38);
    arenaPanel(1000.0,28.0,252.0,118.0);
    arenaText("SENTINEL / TARGET",1015.0,41.0,0.42,1.0,0.65,0.3);
    arenaText(format("%.1f / 200 HP",arenaEnemyState.health),1015.0,71.0,0.7);
    gfx.drawSolidRect(1015.0,111.0,220.0*arenaEnemyState.health/200.0,5.0,1.0,0.5,0.18,1.0);
    arenaText(format("LAST HIT  %.1f   /   HITS %d",arenaLastDamage,arenaHitCount),1015.0,124.0,0.35);
    arenaPanel(28.0,298.0,260.0,100.0);
    local buff=arenaBuffRemaining();
    arenaText("GAMEPLAY EFFECT",43.0,311.0,0.4,0.25,0.8,0.92);
    arenaText(buff>0.0?format("OVERDRIVE  %.1fs",buff):"NO ACTIVE EFFECT",43.0,342.0,0.52,buff>0.0?1.0:0.5,0.72,0.35);
    arenaText(buff>0.0?"State.Empowered / ATK x1.50":"L: apply 12s damage modifier",43.0,376.0,0.34);
    foreach(i,s in arenaSkills) {
        local px=330.0+i*205.0; arenaPanel(px,591.0,192.0,91.0);
        arenaText("["+s.key.toupper()+"] "+s.label,px+12.0,604.0,0.44);
        local status=s.grant==0?"UNEQUIPPED":s.cooldown>0.0?format("COOLDOWN %.2fs",s.cooldown):
            (arenaStats.getBase("Attribute.Energy",0.0)<s.cost?"LOW ENERGY":"READY");
        arenaText(status,px+12.0,634.0,0.38,0.23,0.85,0.88);
        arenaText(format("COST %.0f ENERGY",s.cost),px+12.0,657.0,0.32,0.46,0.6,0.72);
    }
    arenaText("RUNTIME EVENTS",28.0,449.0,0.36,0.28,0.6,0.72);
    foreach(i,line in arenaLog) arenaText(line,28.0,473.0+i*22.0,0.33,i==0?0.9:0.4,i==0?0.94:0.55,0.65);
    arenaText("WASD MOVE / J SLASH / K HEAVY / L BUFF / R RESET",28.0,689.0,0.34);
    arenaText("C CANCEL / U EQUIP / H RELOAD / F REFRESH / V DISPEL / I INSPECT",28.0,708.0,0.32);
    arenaPanel(1000.0,165.0,252.0,110.0);
    arenaText(format("EXPOSED / %d OF 3",arenaEffects.taggedCount(enemyId,"State.Exposed")),1015.0,178.0,0.43,1.0,0.65,0.3);
    arenaText(format("DAMAGE TAKEN x%.2f",arenaTargetStats.getFinal("Attribute.DamageTaken",1.0)),1015.0,211.0,0.43);
    arenaText(format("EFFECT EVENTS READ / %d",arenaEffectEventCount),1015.0,246.0,0.32);
    if(arenaInspect) {
        arenaPanel(1000.0,291.0,252.0,208.0);
        arenaText("NATIVE MODIFIER SOURCES",1015.0,304.0,0.34,0.25,0.8,0.92);
        local row=0;
        foreach(set in [arenaStats,arenaTargetStats]) {
            for(local i=0;i<set.getModifierCount();++i) {
                local source=set.getModifierSource(i);
                if(source.len()>24) source=source.slice(0,24);
                arenaText(source,1015.0,334.0+row*32.0,0.31);
                arenaText(set.getModifierOperation(i)+" / "+set.getModifierAttribute(i),1015.0,348.0+row*32.0,0.27);
                row+=1;
            }
        }
        if(row==0) arenaText("No active modifiers",1015.0,338.0,0.38);
    }
    arenaText(arenaHeavyVariant?"HEAVY / CHARGED PROFILE":"HEAVY / STANDARD PROFILE",1000.0,520.0,0.33);
    if(arenaInspect && arenaLastExecution>0) {
        local snapshot=requireArena(arenaAbilities.findAction(arenaLastExecution),"Inspect native execution");
        arenaText(format("EXEC %d / %s / %.2fs",snapshot.executionId,snapshot.phase,snapshot.elapsedSeconds),1000.0,545.0,0.3);
    }
    if(arenaEnemyState.health<=0.0) arenaText("TARGET DEFEATED / PRESS R",420.0,185.0,0.8,1.0,0.7,0.3);
};
