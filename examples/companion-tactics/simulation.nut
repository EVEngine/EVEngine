// Single owner of health, resources, cooldowns and positions in this sandbox.
// NpcAiWorld owns companion state transitions; execution consumes its snapshot.
// Fixed roster indices are game-local references, never engine handles or saves.
class CompanionSimulation {
    world=null; units=null; hazards=null; effects=null; events=null; claims=null;
    tick=0; time=0.0; command="BALANCED"; focus=3; auto=true;
    wave=1; intermission=0.0; stats=null; manaRegen=7.0;
    constructor() {
        units=[];hazards=[];effects=[];events=[];claims=[-1,-1,-1,-1,-1];
        stats={heals=0,guards=0,absorbed=0,dodges=0,hits=0,cancels=0,transitions=0};
        world=ctRequire(npc_ai.newWorld(256,0),"create AI world");
        ctRequire(world.registerBehavior(ctBehavior()),"register companion behavior");
        addUnit("YOU",0,480,480,120,[2],0.0,0.0,1.0);
        addUnit("BASTION",0,325,480,145,[1,0,2],0.75,1.5,0.85);
        addUnit("LUMEN",0,390,636,100,[0,2],2.1,0.0,0.8);
        addUnit("SIEGE WARDEN",1,660,320,280,[],0.0,0.0,1.0);
        addUnit("ASH STALKER",1,300,280,185,[],0.0,0.0,1.0);
        note("Two companions online. Shared healing reservations.");
    }
    function addUnit(name,team,x,y,hp,skills,heal,guard,attack) {
        local id=units.len();
        units.append({id=id,name=name,team=team,x=x.tofloat(),y=y.tofloat(),hp=hp.tofloat(),maxHp=hp.tofloat(),
            mana=100.0,shield=0.0,shieldTime=0.0,skills=skills,healWeight=heal,guardWeight=guard,attackWeight=attack,
            cooldowns=[0.0,0.0,0.0],pending=null,choice=null,state="follow",agent=null,
            vx=0.0,vy=0.0,position=null,positionReason="SUPPORT ANGLE",band=null,positionTick=0,
            dodgeTime=0.0,dodgeX=0.0,dodgeY=0.0,enemyClock=id==3 ? 2.0 : 3.1,flash=0.0});
        if(id==1 || id==2) {
            units[id].agent=ctRequire(world.createAgent("companion"),"create companion");
            units[id].band=ctSupportBand(units[id]);
        }
    }
    function close() {
        foreach(u in units) if(u.agent!=null) {
            ctRequire(world.destroyAgent(u.agent),"destroy companion");
            if(!world.isAgentStale(u.agent)) throw "destroyed AI handle remained live";
            u.agent=null;
        }
        hazards.clear();effects.clear();claims=[-1,-1,-1,-1,-1];
    }
    function note(message) {
        events.append({time=time,text=message});
        if(events.len()>5) events.remove(0);
    }
    function order(value) {
        command=value;
        foreach(u in units) if(u.agent!=null) cancel(u);
        note("COMMAND / "+value);
    }
    function cancel(u) {
        if(u.pending!=null) {stats.cancels++;u.pending=null;}
        foreach(i,owner in claims) if(owner==u.id) claims[i]=-1;
    }
    function move(u,x,y,speed,dt) {
        local dx=x-u.x,dy=y-u.y,n=sqrt(dx*dx+dy*dy);
        if(n>0.01) {local s=ctMin(n,speed*dt);u.x+=dx/n*s;u.y+=dy/n*s;}
        u.x=ctClamp(u.x,70,940);u.y=ctClamp(u.y,190,680);
    }
    function damage(source,target,power) {
        local u=units[target];if(u.hp<=0) return;
        local blocked=ctMin(u.shield,power);u.shield-=blocked;
        if(blocked>0) {stats.absorbed+=blocked.tointeger();note(u.name+" / AEGIS absorbed "+blocked.tointeger());}
        u.hp=ctMax(0,u.hp-(power-blocked));u.flash=0.18;
        if(u.hp<=0) {
            cancel(u);note(u.name+" / down");
            foreach(other in units) if(other.pending!=null && other.pending.target==target) cancel(other);
        }
    }
    function start(u,c) {
        local s=CT_SKILLS[c.skill],t=units[c.target];
        if(u.pending!=null || u.hp<=0 || t.hp<=0 || u.cooldowns[c.skill]>0 || u.mana<s.cost || ctDistance(u,t)>s.range) return;
        if(u.agent!=null && !ctCastReach(u,t,c.skill)) return;
        if(s.kind=="heal") {
            if(claims[t.id]!=-1 && claims[t.id]!=u.id) return;
            claims[t.id]=u.id;
        }
        u.choice=c;
        local threat=s.kind=="guard" ? ctDanger(this,t) : null;
        u.pending={skill=c.skill,target=c.target,remaining=s.cast,deadline=threat==null ? null : time+threat.remaining};
    }
    function finish(u) {
        local p=u.pending,s=CT_SKILLS[p.skill],t=units[p.target];
        // Atomic admission: cancel does not consume mana/cooldown or apply effects.
        if(u.hp<=0 || t.hp<=0 || ctDistance(u,t)>s.range+12 || u.mana<s.cost) {cancel(u);return;}
        if(p.deadline!=null && time>=p.deadline) {cancel(u);return;}
        u.mana-=s.cost;u.cooldowns[p.skill]=s.cool;
        effects.append({x=u.x,y=u.y,tx=t.x,ty=t.y,life=0.5,kind=s.kind});
        if(s.kind=="heal") {
            local amount=ctMin(s.power,t.maxHp-t.hp);t.hp+=amount;stats.heals++;
            note(u.name+" / MEND "+t.name+" +"+amount.tointeger());
        } else if(s.kind=="guard") {
            t.shield=s.power;t.shieldTime=3.0;stats.guards++;
            note(u.name+" / AEGIS protects "+t.name);
        } else {damage(u.id,t.id,s.power);stats.hits++;}
        u.pending=null;
        foreach(i,owner in claims) if(owner==u.id) claims[i]=-1;
    }
    function decide() {
        // Stable roster order makes same-tick reservation arbitration deterministic.
        foreach(u in units) if(u.agent!=null) {
            local c=ctPlan(this,u);
            if(c.kind=="follow" && u.pending!=null && CT_SKILLS[u.pending.skill].kind=="attack") cancel(u);
            if(c.kind=="dodge" && u.dodgeTime<=0) {
                cancel(u);u.dodgeTime=0.48;u.dodgeX=c.x;u.dodgeY=c.y;stats.dodges++;
                note(u.name+" / evade predicted impact");
            }
            if(u.dodgeTime>0 && u.hp>0) c=ctCandidate("dodge",u.id,1000,"Finish evasive movement");
            local changed=u.choice==null || u.choice.kind!=c.kind || u.choice.target!=c.target;
            u.choice=c;
            if(c.kind!="dodge" && c.kind!="down" && u.pending==null &&
                (changed || u.position==null || tick>=u.positionTick)) {
                u.position=ctPosition(this,u,c);u.positionTick=tick+18;
            }
            foreach(i,owner in claims) if(owner==u.id) claims[i]=-1;
            if(c.kind=="heal") claims[c.target]=u.id;
            ctRequire(world.setBlackboardString(u.agent,"intent",c.kind),"publish intent");
        }
        local report=ctRequire(world.tick(tick,0.1,2,1),"tick AI");
        stats.transitions+=report.transitionsApplied;
        foreach(u in units) if(u.agent!=null) {
            u.state=ctRequire(world.snapshot(u.agent),"read AI state").activeState;
            if(u.state!=u.choice.kind) throw "AI state did not accept selected intent";
        }
    }
    function enemy(u,dt) {
        if(u.hp<=0 || units[0].hp<=0) return;
        u.enemyClock-=dt;
        if(u.enemyClock<=0) {
            u.enemyClock=u.id==3 ? 3.6 : 4.4;
            // Telegraphs are public combat facts; AI cannot see a future unannounced attack.
            local target=u.id==3 ? 0 : (tick/60%2==0 ? 1 : 2);
            if(units[target].hp<=0) target=0;
            local t=units[target];
            hazards.append({source=u.id,x=t.x,y=t.y,radius=u.id==3 ? 88.0 : 72.0,
                remaining=u.id==3 ? 1.15 : 0.95,total=u.id==3 ? 1.15 : 0.95,resolved=false});
        }
        local t=units[0];local d=ctDistance(u,t);
        local oldX=u.x,oldY=u.y;
        if(d>210) move(u,t.x,t.y,22,dt);
        u.vx=(u.x-oldX)/dt;u.vy=(u.y-oldY)/dt;
    }
    function step(dt,input) {
        tick++;time+=dt;
        foreach(u in units) {
            foreach(i,cd in u.cooldowns) u.cooldowns[i]=ctMax(0,cd-dt);
            u.mana=ctMin(100,u.mana+manaRegen*dt);u.flash=ctMax(0,u.flash-dt);
            u.shieldTime-=dt;if(u.shieldTime<=0) u.shield=0.0;
        }
        local hero=units[0];
        local oldX=hero.x,oldY=hero.y;
        if(hero.hp>0) {
            if(input.x!=0 || input.y!=0) {auto=false;move(hero,hero.x+input.x*100,hero.y+input.y*100,180,dt);}
            else if(auto) move(hero,480+sin(time*0.22)*75,480+sin(time*0.3)*28,70,dt);
            if((auto || input.attack) && units[focus].hp>0) start(hero,ctCandidate("attack",focus,0,"Player attack",2));
        }
        hero.vx=(hero.x-oldX)/dt;hero.vy=(hero.y-oldY)/dt;
        foreach(u in units) if(u.team==1) enemy(u,dt);
        if(tick%6==0) decide();
        foreach(u in units) {
            if(u.hp<=0) continue;
            local startX=u.x,startY=u.y;
            if(u.agent!=null && u.choice!=null) {
                local c=u.choice;
                if(u.state=="dodge") {move(u,u.dodgeX,u.dodgeY,350,dt);u.dodgeTime=ctMax(0,u.dodgeTime-dt);}
                else if(u.pending==null && u.state=="follow") {
                    if(u.position!=null && ctDistance(u,u.position)>8)
                        move(u,u.position.x,u.position.y,ctMoveSpeed(this,u),dt);
                } else if(u.pending==null && (u.state=="attack" || u.state=="heal" || u.state=="guard")) {
                    local t=units[c.target],s=CT_SKILLS[c.skill];
                    if(t.hp<=0) {cancel(u);continue;}
                    // Support can fire immediately from a safe valid cast location;
                    // offense must first restore spacing instead of planting forever.
                    local ready=ctCastReach(u,t,c.skill) && (s.kind!="attack" || ctCanAttackHere(this,u));
                    if(ready) start(u,c);
                    if(u.pending==null && u.position!=null && ctDistance(u,u.position)>8)
                        move(u,u.position.x,u.position.y,ctMoveSpeed(this,u),dt);
                }
            }
            if(u.team==0 && u.id!=0) {u.vx=(u.x-startX)/dt;u.vy=(u.y-startY)/dt;}
            if(u.pending!=null) {u.pending.remaining-=dt;if(u.pending.remaining<=0) finish(u);}
        }
        for(local i=hazards.len()-1;i>=0;i--) {
            local h=hazards[i];h.remaining-=dt;
            if(units[h.source].hp<=0 && !h.resolved) {hazards.remove(i);continue;}
            if(h.remaining<=0 && !h.resolved) {
                h.resolved=true;
                foreach(u in units) if(u.team==0 && ctDistance(u,h)<h.radius+12) damage(h.source,u.id,28.0);
            }
            if(h.remaining < -0.35) hazards.remove(i);
        }
        for(local i=effects.len()-1;i>=0;i--) {effects[i].life-=dt;if(effects[i].life<=0) effects.remove(i);}
        if(units[focus].hp<=0) foreach(u in units) if(u.team==1 && u.hp>0) {focus=u.id;break;}
        if(units[3].hp<=0 && units[4].hp<=0 && hero.hp>0) {
            intermission+=dt;
            if(intermission>3) {
                wave++;intermission=0;
                foreach(u in units) if(u.team==1) {u.hp=u.maxHp;u.enemyClock=2.0;}
                note("WAVE "+wave+" / enemies reinforced");
            }
        }
    }
}
