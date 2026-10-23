function checked(r) { if(!r.ok) throw r.status.summary; return r.value; }
function require(b,m) {if(!b) throw "WORLD_SWEEP: "+m;}
function near(a,b) {return fabs(a-b)<0.035;}
function box(w,x,y=0.0) {
    local b=w.newBody("static",x,y,0.0); b.newBoxShape(0.2,4.0,2.0,1.0,0.3,0.0);return b;
}
function pool(w,mode="notify",mask=-1,ignored=-1) {
    local p=checked(eve.Weapon().newCarrierRuntime(8));
    checked(p.bindWorld3D(w.queryToken(),0.1,-1,mask,ignored));
    local effects=[{kind="emitTrigger",on="onHit"},{kind="release",on="onExpire"}];
    if(mode=="bounce") effects.append({kind="bounce",on="onHit",bounceCount=16,restitution=1.0});
    if(mode=="pierce") effects.append({kind="pierce",on="onHit",pierceCount=16});
    checked(p.registerRecipe({id="test:world",speed=10.0,lifetime=2.0,motion=["linear"],
        collision={enabled=mode!="disabled",worldSweep=true},triggers=["onHit","onExpire"],impacts=effects}));
    return p;
}
function spawn(p,x=0.0) {checked(p.spawn("test:world",x,0.0,0.0,1.0,0.0,0.0));}
eve_init=function() {
    local w=physics.newWorld3D(0.0,0.0,0.0,false);
    local left=box(w,-2.0),right=box(w,2.0);
    w.setQueryFilter(2,4);w.setQueryIgnoredBodyId(left.getId());
    local p=pool(w,"bounce");spawn(p);
    require(!p.bindWorld3D(w.queryToken(),0.1,-1,-1,-1).ok,"live rebind accepted");
    local frame=checked(p.updateWithMotion(1.0,function(s,m,dt){return null;}));
    require(frame.events.len()==3,"one frame must include three real world bounces");
    require(near(frame.events[0].x,1.8),"contact must be sphere center");
    require(frame.events[0].bodyId==right.getId(),"body provenance");
    require(frame.events[0].bodyIndex>=0 && frame.events[0].bodyGeneration>0,"body runtime identity");
    require(frame.events[0].targetId==0,"physics body was fabricated as ECS target");
    require(frame.events[0].nx < -0.9,"normal orientation");
    require(near(checked(p.states())[0].x,0.8),"remaining time position");
    require(w.getQueryCategoryBits()==2 && w.getQueryMaskBits()==4 &&
        w.getQueryIgnoredBodyId()==left.getId(),"query filters leaked");
    local quiet=pool(w,"disabled");spawn(quiet);
    require(checked(quiet.update(0.5)).events.len()==0,"disabled collision queried world");
    local filtered=pool(w,"notify",0);spawn(filtered);
    require(checked(filtered.update(0.5)).events.len()==0,"mask ignored");
    local ignored=pool(w,"notify",-1,right.getId());spawn(ignored);
    require(checked(ignored.update(0.5)).events.len()==0,"excluded body hit");
    local notify=pool(w);spawn(notify);
    local notices=checked(notify.update(0.5));
    require(notices.events.len()==1 && notices.events[0].damage==0.0,"notification contract");
    require(near(checked(notify.states())[0].x,5.0),"notification changed trajectory");
    local bound=pool(w);require(!bound.bindWorld3D("bad",0.1,-1,-1,-1).ok,"bad token accepted");
    require(!bound.bindWorld3D(w.queryToken(),-0.1,-1,-1,-1).ok,"bad radius accepted");
    spawn(bound);require(checked(bound.update(0.5)).events.len()==1,"failed bind lost old session");
    local pierce=pool(w,"pierce");spawn(pierce);
    require(checked(pierce.update(0.25)).events.len()==1,"pierce first hit");
    local returned=checked(pierce.updateWithMotion(0.5,function(s,m,dt){return {vx=-10.0,vy=0.0,vz=0.0};}));
    require(returned.events.len()==1 && returned.events[0].bodyId==left.getId(),"pierce remembered wrong body");
    local deadToken=w.queryToken();
    w.destroyBody(right);
    require(!ignored.update(0.01).ok,"destroyed ignored body binding remained live");
    w.destroy();
    local age=checked(p.states())[0].age;
    require(!p.update(0.01).ok && checked(p.states())[0].age==age,"stale world did not rollback");
    require(checked(quiet.update(0.01)).events.len()==0,"disabled collision requires live world");
    local stale=checked(eve.Weapon().newCarrierRuntime(1));
    require(!stale.bindWorld3D(deadToken,0.1,-1,-1,-1).ok,"destroyed world token rebound");
    checked(stale.clearWorld3D());
    local other=physics.newWorld3D(0.0,0.0,0.0,false);box(other,1.0);
    local removed=pool(other);checked(removed.clearWorld3D());spawn(removed);
    require(!removed.update(0.1).ok,"cleared world binding still queried");
    local fresh=pool(other);spawn(fresh);
    require(checked(fresh.update(0.2)).events.len()==1,"world isolation");
    other.destroy();
    print("WORLD_SWEEP_SCRIPT_CONTRACT_PASS\n");
};
eve_update=function(dt) {};
eve_render=function() {gfx.clear();};
