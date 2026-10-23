function checked(r) { if (!r.ok) throw r.status.summary; return r.value; }
function require(condition, text) { if (!condition) throw text; }
function near(a,b) { return abs(a-b)<0.0001; }
eve_init = function() {
    local pool = checked(eve.Weapon().newCarrierRuntime(8));
    checked(pool.registerRecipe({id="test:function",speed=10.0,lifetime=4.0,motion=["linear"],
        triggers=["onExpire"],impacts=[{kind="release",on="onExpire"}]}));
    checked(pool.spawn("test:function",0.0,0.0,0.0,1.0,0.0,0.0));
    checked(pool.spawn("test:function",0.0,0.0,0.0,1.0,0.0,0.0));
    checked(pool.updateWithMotion(0.5,function(s,p,dt) {
        require(near(s.age,0.0) && near(dt,0.5),"callback time");
        require(near(p.x,5.0),"built-in proposed motion");
        require(!pool.clearTargets().ok,"target reentry must fail");
        require(!pool.update(dt).ok,"update reentry must fail");
        require(!pool.spawn("test:function",0.0,0.0,0.0,1.0,0.0,0.0).ok,"spawn reentry must fail");
        return {vx=4.0,vy=2.0,vz=0.0};
    }));
    local state=checked(pool.states())[0];
    require(near(state.x,2.0)&&near(state.y,1.0),"velocity integration");
    checked(pool.updateWithMotion(0.5,function(s,p,dt) {return {x=5.0,y=2.0,z=0.0};}));
    state=checked(pool.states())[0];
    require(near(state.vx,6.0)&&near(state.vy,2.0),"position differentiation");
    local bad=pool.updateWithMotion(0.5,function(s,p,dt) {
        if(s.slot==1) return {vx=1.0};
        return {vx=99.0,vy=0.0,vz=0.0};
    });
    require(!bad.ok,"incomplete result rejected");
    require(near(checked(pool.states())[0].age,1.0),"atomic invalid result");
    local thrown=pool.updateWithMotion(0.5,function(s,p,dt) {throw "EXPECTED_CUSTOM_MOTION_EXCEPTION";});
    require(!thrown.ok,"exception must return failure");
    require(near(checked(pool.states())[0].age,1.0),"atomic exception");
    require(!pool.updateWithMotion(0.5,42).ok,"non-callable rejected");
    require(!pool.updateWithMotion(0.5,function(s,p,dt){return {x=1,y=2,z=3,unexpected=4};}).ok,"unknown field");
    checked(pool.updateWithMotion(0.0,function(s,p,dt){throw "zero dt called";}));
    checked(pool.updateWithMotion(0.5,function(s,p,dt){return null;}));
    require(near(checked(pool.states())[0].x,8.0),"null preserves built-in");
    checked(pool.updateWithMotion(0.5,function(s,p,dt){return {x=1,y=2,z=3,vx=4,vy=5,vz=6};}));
    require(near(checked(pool.states())[0].vz,6.0),"full output");
    checked(pool.updateWithMotion(9.0,function(s,p,dt){require(near(dt,2.0),"lifetime clamp");return null;}));
    require(pool.activeCount()==0,"expired");
    // Returning proposed must retain the built-in curve bookkeeping, including float projection.
    local basePool=checked(eve.Weapon().newCarrierRuntime(2));
    local custom=checked(eve.Weapon().newCarrierRuntime(2));
    local curve={id="test:curve",speed=3.7,lifetime=4.0,
        motion=[{kind="sway",amplitude=1.1,frequency=0.8},"linear"],
        triggers=["onExpire"],impacts=[{kind="release",on="onExpire"}]};
    checked(basePool.registerRecipe(curve)); checked(custom.registerRecipe(curve));
    checked(basePool.spawn("test:curve",0.0,0.0,0.0,1.0,0.0,0.0));
    checked(custom.spawn("test:curve",0.0,0.0,0.0,1.0,0.0,0.0));
    for(local i=0;i<60;i++) {
        checked(basePool.update(1.0/60.0));
        checked(custom.updateWithMotion(1.0/60.0,function(s,p,dt){return p;}));
        local a=checked(basePool.states())[0], b=checked(custom.states())[0];
        require(a.x==b.x && a.y==b.y && a.z==b.z && a.vx==b.vx && a.vy==b.vy && a.vz==b.vz,
            "identity callback changed built-in curve");
    }
    local ricochet=checked(eve.Weapon().newCarrierRuntime(4));
    checked(ricochet.configureSimulation(0.0,256,64,4096));
    require(!ricochet.configureSimulation(0.01,0,64,4096).ok,"invalid solver budget");
    checked(ricochet.registerRecipe({id="test:ricochet",speed=10.0,lifetime=4.0,motion=["linear"],
        triggers=["onHit","onExpire",{kind="onInterval",interval=0.1}],
        impacts=[{kind="emitHit",on="onHit"},{kind="bounce",on="onHit",bounceCount=10,restitution=1.0},
            {kind="release",on="onHit"},{kind="release",on="onExpire"},{kind="emitHit",on="onInterval"}]}));
    checked(ricochet.addHitTarget(1,1,-1.0,0.0,0.0,1.0));
    checked(ricochet.addHitTarget(2,1,2.0,0.0,0.0,1.0));
    checked(ricochet.spawn("test:ricochet",0.5,0.0,0.0,1.0,0.0,0.0));
    require(!ricochet.configureSimulation(0.01,256,64,4096).ok,"live solver reconfiguration");
    local swept=checked(ricochet.update(0.4));
    local hitCount=0, intervalCount=0;
    foreach(event in swept.events) {
        if(event.trigger=="onHit") {
            require(near(event.age,0.05+0.1*hitCount),"impact timestamp"); ++hitCount;
        }
        if(event.trigger=="onInterval") ++intervalCount;
    }
    require(hitCount==4 && intervalCount==4,"remaining time events");
    local finalState=checked(ricochet.states())[0];
    require(near(finalState.x,0.5)&&near(finalState.distanceTravelled,4.0),"remaining time position");
    local optionalPool=checked(eve.Weapon().newCarrierRuntime(4));
    local optionalRecipe={id="test:optional",speed=10.0,lifetime=4.0,motion=["linear"],
        collision={enabled=false,worldSweep=true},triggers=["onHit"],
        impacts=[{kind="emitTrigger",on="onHit"}]};
    checked(optionalPool.registerRecipe(optionalRecipe));
    checked(optionalPool.addHitTarget(1,1,2.0,0.0,0.0,1.0));
    checked(optionalPool.spawn("test:optional",0.5,0.0,0.0,1.0,0.0,0.0));
    require(checked(optionalPool.update(0.2)).events.len()==0,"disabled collision fired");
    require(near(checked(optionalPool.states())[0].x,2.5),"disabled collision altered motion");
    optionalRecipe.id="test:notify";
    optionalRecipe.collision.enabled=true;
    optionalRecipe.collision.worldSweep=false;
    checked(optionalPool.registerRecipe(optionalRecipe));
    checked(optionalPool.spawn("test:notify",0.5,0.0,0.0,1.0,0.0,0.0));
    local notice=checked(optionalPool.update(0.1));
    require(notice.events.len()==1 && notice.events[0].impact=="emitTrigger" &&
        notice.events[0].damage==0.0,"notification-only collision");
    require(checked(optionalPool.states()).len()==2,"notification destroyed projectile");
    optionalRecipe.id="test:missingWorld";
    optionalRecipe.collision.worldSweep=true;
    checked(optionalPool.registerRecipe(optionalRecipe));
    checked(optionalPool.spawn("test:missingWorld",0.5,0.0,0.0,1.0,0.0,0.0));
    require(!optionalPool.update(0.1).ok,"missing world adapter silently ignored");
    optionalRecipe.id="test:invalidCollision";
    optionalRecipe.collision.enabled=1;
    require(!optionalPool.registerRecipe(optionalRecipe).ok,"numeric collision flag accepted");
    optionalRecipe.collision.enabled=true;
    optionalRecipe.collision.typo <- true;
    require(!optionalPool.registerRecipe(optionalRecipe).ok,"unknown collision field accepted");
    local noProvider=checked(eve.Weapon().newCarrierRuntime(2));
    require(!noProvider.bindWorld3D("100000000",0.1,-1,-1,-1).ok,"absent physics provider accepted");
    checked(noProvider.clearWorld3D());
    print("OPTIONAL_COLLISION_SCRIPT_CONTRACT_PASS\n");
    print("CONTINUOUS_SOLVER_SCRIPT_CONTRACT_PASS\n");
    print("CUSTOM_MOTION_SCRIPT_CONTRACT_PASS\n");
};
eve_update = function(dt) {};
eve_render = function() {gfx.clear();};
