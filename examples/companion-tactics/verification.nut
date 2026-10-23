// Isolated fixtures exercise the same simulation used by the visible game.
// No mutation of the live match; native AI handles are destroyed after each case.
function ctAssert(value,message) {if(!value) throw "COMPANION_TEST_FAILED: "+message;}
function ctQuiet(sim) {sim.auto=false;sim.units[3].enemyClock=1000;sim.units[4].enemyClock=1000;}
function ctSteps(sim,count) {for(local i=0;i<count;i++) sim.step(1.0/60,{x=0.0,y=0.0,attack=false});}
function ctVerify() {
    ctVerifyPositioning();
    local s=CompanionSimulation();ctQuiet(s);s.units[0].hp=45;
    ctSteps(s,6);
    local reservations=0;
    foreach(u in s.units) if(u.pending!=null && CT_SKILLS[u.pending.skill].kind=="heal" && u.pending.target==0) reservations++;
    ctAssert(reservations==1,"one healer owns the target reservation");
    ctSteps(s,45);ctAssert(s.units[0].hp>45 && s.stats.heals>0,"healing changes authoritative health");s.close();

    s=CompanionSimulation();ctQuiet(s);
    local u=s.units[1];u.x=320;u.y=480;
    local mana=u.mana;
    s.start(u,ctCandidate("guard",0,1,"fixture",1));ctSteps(s,12);
    ctAssert(s.units[0].shield>0 && u.mana<mana,"guard commits cost and shield");
    local hp=s.units[0].hp;s.damage(3,0,28);
    ctAssert(s.units[0].hp==hp && s.stats.absorbed==28,"shield absorbs damage");s.close();

    s=CompanionSimulation();ctQuiet(s);s.units[0].hp=45;
    u=s.units[2];s.start(u,ctCandidate("heal",0,1,"fixture",0));mana=u.mana;
    s.hazards.append({source=3,x=u.x,y=u.y,radius=72.0,remaining=1.0,total=1.0,resolved=false});
    ctSteps(s,6);
    ctAssert(u.state=="dodge" && u.pending==null && s.claims[0]!=2,"danger cancels cast and releases reservation");
    ctAssert(u.mana==mana && u.cooldowns[0]==0,"cancel consumes no resources");
    hp=u.hp;ctSteps(s,66);ctAssert(u.hp==hp,"evasive landing avoids area damage");s.close();

    s=CompanionSimulation();ctQuiet(s);s.order("RETREAT");ctSteps(s,120);
    ctAssert(s.stats.hits==0 && s.units[1].state=="follow" && s.units[2].state=="follow","retreat suppresses offense");s.close();

    s=CompanionSimulation();ctQuiet(s);u=s.units[2];s.units[0].hp=45;
    s.start(u,ctCandidate("heal",0,1,"fixture",0));s.damage(3,0,999);
    ctAssert(u.pending==null && s.claims[0]==-1,"dead target releases cast and claim");s.close();

    local a=CompanionSimulation(),b=CompanionSimulation();ctSteps(a,3600);ctSteps(b,3600);
    foreach(k,v in a.stats) ctAssert(v==b.stats[k],"deterministic statistic "+k);
    foreach(i,v in a.units) ctAssert(v.hp==b.units[i].hp && v.x==b.units[i].x && v.y==b.units[i].y,"deterministic roster");
    ctAssert(a.stats.heals>0 && a.stats.guards>0 && a.stats.dodges>0 && a.stats.absorbed>0 && a.stats.hits>0,"showcase covers all behaviors");
    print(format("COMPANION_TEST_PASS heals=%d guards=%d dodges=%d absorbed=%d hits=%d transitions=%d\n",
        a.stats.heals,a.stats.guards,a.stats.dodges,a.stats.absorbed,a.stats.hits,a.stats.transitions));
    a.close();b.close();
}

function ctVerifyPositioning() {
    local s=CompanionSimulation();ctQuiet(s);s.order("RETREAT");
    // Start stacked: spacing must be a runtime behavior, not just spawn placement.
    s.units[1].x=480;s.units[1].y=480;s.units[2].x=480;s.units[2].y=480;
    ctSteps(s,240);
    foreach(u in s.units) if(u.agent!=null) {
        local d=ctDistance(u,s.units[0]),band=ctSupportBand(u);
        ctAssert(d>=band.min-10 && d<=band.max+8,"stacked formation separates inside support band");
    }
    ctAssert(ctDistance(s.units[1],s.units[2])>=CT_POSITION.peerMinimum-10,"companions separate from one another");
    ctAssert(ctDistance(s.units[1],s.units[2])<CT_SKILLS[0].range,"companions retain mutual healing range");
    for(local i=0;i<120;i++) {
        s.step(1.0/60,{x=1.0,y=0.0,attack=false});
        foreach(u in s.units) if(u.agent!=null)
            ctAssert(ctDistance(u,s.units[0])<=CT_SKILLS[ctSupportSkill(u)].range,"continuous movement retains actual support range");
    }
    ctSteps(s,180);
    foreach(u in s.units) if(u.agent!=null)
        ctAssert(ctDistance(u,s.units[0])<=ctSupportBand(u).max+10,"moving leader and edge preserve support reach");
    s.close();

    s=CompanionSimulation();ctQuiet(s);
    s.units[3].x=350;s.units[3].y=300;
    s.start(s.units[1],ctCandidate("attack",3,1,"fixture",2));
    ctAssert(s.units[1].pending!=null,"offense fixture starts casting");
    for(local i=0;i<24;i++) s.step(1.0/60,{x=1.0,y=0.0,attack=false});
    ctAssert(s.stats.cancels>0 && ctDistance(s.units[1],s.units[0])<210,"sudden leader movement interrupts offense to preserve support");
    s.close();

    s=CompanionSimulation();ctQuiet(s);
    s.hazards.append({source=3,x=s.units[0].x,y=s.units[0].y,radius=88.0,remaining=1.15,total=1.15,resolved=false});
    local hp=s.units[0].hp;ctSteps(s,75);
    ctAssert(s.stats.absorbed==28 && s.units[0].hp==hp,"spaced guardian shields before impact without running into AoE");
    s.close();

    s=CompanionSimulation();ctQuiet(s);local u=s.units[1];
    u.x=70;u.y=190;
    s.hazards.append({source=3,x=s.units[0].x,y=s.units[0].y,radius=88.0,remaining=0.25,total=0.25,resolved=false});
    ctAssert(ctPlan(s,u).kind!="guard","unreachable shield deadline is rejected");
    u.cooldowns[1]=3.0;
    ctAssert(ctSupportEta(s,u,s.units[0],1)>=3.18,"support ETA includes cooldown readiness");
    s.hazards.clear();u.x=200;u.y=480;
    s.hazards.append({source=3,x=350.0,y=480.0,radius=60.0,remaining=0.75,total=0.75,resolved=false});
    ctAssert(ctRouteRisk(s,u,{x=500.0,y=480.0},195)>0,"safe endpoint does not hide hazardous approach");
    s.close();
    print("COMPANION_POSITION_PASS spread mutual-support moving-leader edge shield-deadline approach-risk\n");
}
