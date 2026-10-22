// Game-local utility policy. No graphics, keyboard, clock or random source.
// Candidates reference immutable roster slots; each execution rechecks life/range.
function ctMin(a,b) { return a < b ? a : b; }
function ctMax(a,b) { return a > b ? a : b; }
function ctClamp(v,a,b) { return ctMin(b,ctMax(a,v)); }
function ctDistance(a,b) { local x=a.x-b.x,y=a.y-b.y; return sqrt(x*x+y*y); }
function ctRequire(r,what) {
    if (!r.ok) throw what + ": " + r.status.summary;
    return r.value;
}

// Skills are described by effect semantics, not role-specific branches.
CT_SKILLS <- [
    { name="MEND", kind="heal", range=250.0, cast=0.65, cool=3.8, cost=24.0, power=30.0 },
    { name="AEGIS", kind="guard", range=210.0, cast=0.18, cool=4.4, cost=19.0, power=34.0 },
    { name="BOLT", kind="attack", range=255.0, cast=0.38, cool=0.95, cost=3.0, power=10.0 }
];

function ctCandidate(kind,target,score,reason,skill=-1) {
    return {kind=kind,target=target,score=score,reason=reason,skill=skill,x=0.0,y=0.0};
}

function ctDanger(sim,u) {
    local worst=null;
    foreach(h in sim.hazards) {
        if(h.remaining > 0.0 && h.remaining < 1.25 && ctDistance(u,h) < h.radius+19) {
            if(worst==null || h.remaining < worst.remaining) worst=h;
        }
    }
    return worst;
}

function ctEscape(sim,u) {
    local best=null, bestScore=-100000.0;
    // Test all authored danger discs, including landing safety and travel time.
    for(local i=0;i<16;i++) {
        local a=i*6.2831853/16;
        local p={x=ctClamp(u.x+cos(a)*145,70,940),y=ctClamp(u.y+sin(a)*145,190,680)};
        local score=0.0;
        foreach(h in sim.hazards) {
            if(h.remaining<=0) continue;
            local clearance=ctDistance(p,h)-h.radius-22;
            score+=ctMin(clearance,90.0);
            if(clearance<0) score-=1000;
            if(ctDistance(u,h)<h.radius+19 && h.remaining<0.35) score-=50;
        }
        score-=ctRouteRisk(sim,u,p,350);
        local band=ctSupportBand(u),gap=ctDistance(p,sim.units[0]);
        score-=ctMax(0,gap-band.max)*2+ctMax(0,band.min-gap)*2;
        score-=ctMax(0,CT_POSITION.peerMinimum-ctSeparation(sim,u,p))*3;
        if(score>bestScore) {bestScore=score;best=p;}
    }
    return best;
}

function ctPlan(sim,u) {
    if(u.hp<=0) return ctCandidate("down",u.id,1000,"Incapacitated");
    if(ctDanger(sim,u)!=null) {
        local c=ctCandidate("dodge",u.id,1000,"Predicted area impact");
        local p=ctEscape(sim,u);c.x=p.x;c.y=p.y;return c;
    }
    if(sim.command=="RETREAT") return ctCandidate("follow",0,500,"Regroup - offensive skills locked");
    if(u.pending!=null && CT_SKILLS[u.pending.skill].kind=="attack" && ctDistance(u,sim.units[0])>ctSupportBand(u).max)
        return ctCandidate("follow",0,450,"Cancel offense - restore support");
    if(u.pending!=null) return u.choice; // Cast commitment; danger can still cancel it.
    local best=ctCandidate("follow",0,8,"Maintain support spacing");
    foreach(k in u.skills) {
        local s=CT_SKILLS[k];
        if(u.cooldowns[k]>0 || u.mana<s.cost) continue;
        foreach(t in sim.units) {
            if(t.hp<=0 || (s.kind=="attack") == (t.team==u.team)) continue;
            local d=ctDistance(u,t);
            if(d>s.range+100) continue;
            if(sim.command=="PROTECT" && t.team!=u.team && ctDistance(t,sim.units[0])>280) continue;
            local score=0.0,reason="";
            if(s.kind=="heal") {
                local missing=t.maxHp-t.hp;
                if(missing<18 || sim.claims[t.id]!=-1 && sim.claims[t.id]!=u.id) continue;
                score=ctMin(missing,s.power)*u.healWeight + (t.hp<35 ? 50 : 0);
                reason="Missing health + urgency";
            } else if(s.kind=="guard") {
                local threat=ctDanger(sim,t);
                if(t.shield>5 || threat==null) continue;
                if(ctSupportEta(sim,u,t,k)+0.05>=threat.remaining) continue;
                score=90*u.guardWeight + (t.id==0 ? 28 : 0);
                reason="Ally inside attack telegraph";
            } else {
                score=18*u.attackWeight+(t.id==sim.focus ? 24 : 0);
                if(sim.command=="FOCUS") score+=32;
                reason=t.id==sim.focus ? "Commander focus target" : "Available damage window";
            }
            score-=ctSupportEta(sim,u,t,k)*12;
            if(sim.command=="PROTECT" && s.kind!="attack") score+=30;
            // Hysteresis favors the current task rather than oscillating by range.
            if(u.choice!=null && u.choice.kind==s.kind && u.choice.target==t.id) score+=7;
            if(score>best.score) best=ctCandidate(s.kind,t.id,score,reason,k);
        }
    }
    return best;
}
