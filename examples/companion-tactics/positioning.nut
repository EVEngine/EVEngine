// Tactical positions refresh every 0.3s/on action changes; danger at 10 Hz.
// Movement runs at 60 Hz.
// All distances are sandbox pixels. No navigation/obstacle support is implied.
CT_POSITION <- {minimum=120.0, peerMinimum=120.0, rangeMargin=25.0,
    speed=195.0, catchupSpeed=235.0, reaction=0.1, hysteresis=12.0};

function ctSupportSkill(u) {
    local best=0;
    foreach(k in u.skills) if(CT_SKILLS[k].kind=="guard") return k;
    return best;
}
function ctSupportBand(u) {
    if(u.band!=null) return u.band;
    local range=CT_SKILLS[ctSupportSkill(u)].range;
    return {min=CT_POSITION.minimum,ideal=ctMin(180.0,range-55),max=range-CT_POSITION.rangeMargin};
}
function ctMoveSpeed(sim,u) {
    return ctDistance(u,sim.units[0])>ctSupportBand(u).max ? CT_POSITION.catchupSpeed : CT_POSITION.speed;
}
function ctSupportEta(sim,u,target,skill) {
    local s=CT_SKILLS[skill];
    if(u.pending!=null && u.pending.skill==skill && u.pending.target==target.id) return u.pending.remaining;
    local locked=u.pending==null ? 0.0 : u.pending.remaining;
    local travel=ctMax(0,ctDistance(u,target)-(s.range-CT_POSITION.rangeMargin))/ctMoveSpeed(sim,u);
    local resource=ctMax(0,s.cost-u.mana)/sim.manaRegen;
    return locked+ctMax(travel,ctMax(u.cooldowns[skill]-locked,resource-locked))+s.cast+CT_POSITION.reaction;
}
function ctPredicted(u,seconds) {
    return {x=ctClamp(u.x+u.vx*seconds,70,940),y=ctClamp(u.y+u.vy*seconds,190,680)};
}
function ctCastReach(u,target,skill) {
    local s=CT_SKILLS[skill];
    return ctMax(ctDistance(u,target),ctDistance(u,ctPredicted(target,s.cast)))<=s.range-8;
}
function ctSeparation(sim,u,p) {
    local nearest=10000.0;
    foreach(t in sim.units) if(t.team==u.team && t.id!=u.id && t.hp>0)
        nearest=ctMin(nearest,ctDistance(p,t));
    return nearest;
}
function ctRouteRisk(sim,u,p,speed) {
    local distance=ctDistance(u,p),risk=0.0;
    foreach(h in sim.hazards) {
        if(h.remaining<=0) continue;
        // Sample where the unit will actually be when the announced attack lands.
        local f=distance<0.01 ? 1.0 : ctMin(1.0,speed*h.remaining/distance);
        local atImpact={x=u.x+(p.x-u.x)*f,y=u.y+(p.y-u.y)*f};
        local clearance=ctDistance(atImpact,h)-h.radius-20;
        if(clearance<0) risk+=10000-clearance*40;
    }
    return risk;
}
function ctPositionCost(sim,u,p,c,context) {
    local band=context.band,hero=sim.units[0],anchor=context.anchor;
    local d=ctDistance(p,hero),future=ctDistance(p,anchor);
    local cost=ctMax(0,band.min-d)*16+ctMax(0,d-band.max)*16;
    cost+=fabs(future-band.ideal)*0.45;
    cost+=ctDistance(u,p)*0.16;
    cost+=ctRouteRisk(sim,u,p,context.speed);
    foreach(t in sim.units) {
        if(t.hp<=0 || t.id==u.id) continue;
        local gap=ctDistance(p,t);
        if(t.team==u.team && t.id!=0) {
            cost+=ctMax(0,CT_POSITION.peerMinimum-gap)*14;
            // Retain mutual support, not just separate links to the leader.
            local mutual=ctMin(CT_SKILLS[ctSupportSkill(u)].range,CT_SKILLS[ctSupportSkill(t)].range)-15;
            cost+=ctMax(0,gap-mutual)*3;
            if(t.position!=null) cost+=ctMax(0,CT_POSITION.peerMinimum-ctDistance(p,t.position))*8;
        } else if(t.team!=u.team) cost+=ctMax(0,115-gap)*4;
    }
    if(c.skill>=0 && sim.units[c.target].hp>0) {
        local s=CT_SKILLS[c.skill];
        local excess=ctMax(0,ctDistance(p,context.target)-(s.range-18));
        // Offense may improve the angle, but cannot buy a large leash violation.
        cost+=s.kind=="attack" ? ctMin(80,excess*2) : excess*8;
    }
    return cost;
}
function ctPosition(sim,u,c) {
    local band=ctSupportBand(u),anchor=ctPredicted(sim.units[0],0.20);
    local context={band=band,anchor=anchor,speed=ctMoveSpeed(sim,u),target=null};
    if(c.skill>=0) context.target=ctPredicted(sim.units[c.target],CT_SKILLS[c.skill].cast);
    local best={x=u.x,y=u.y},bestCost=ctPositionCost(sim,u,best,c,context);
    foreach(radius in [band.min+10,band.ideal,band.max-5]) {
        for(local i=0;i<16;i++) {
            local angle=i*6.2831853/16;
            local p={x=ctClamp(anchor.x+cos(angle)*radius,70,940),y=ctClamp(anchor.y+sin(angle)*radius,190,680)};
            local cost=ctPositionCost(sim,u,p,c,context);
            if(cost<bestCost) {best=p;bestCost=cost;}
        }
    }
    // Keep a still-useful waypoint to avoid left/right oscillation each decision.
    if(u.position!=null && ctPositionCost(sim,u,u.position,c,context)<=bestCost+CT_POSITION.hysteresis)
        best={x=u.position.x,y=u.position.y};
    local d=ctDistance(u,sim.units[0]);
    u.positionReason=d<band.min-8 ? "SPREAD" : (d>band.max ? "CATCH UP" : "SUPPORT ANGLE");
    return best;
}
function ctCanAttackHere(sim,u) {
    local band=ctSupportBand(u),d=ctDistance(u,sim.units[0]);
    if(d<band.min-8 || d>band.max || ctSeparation(sim,u,u)<CT_POSITION.peerMinimum-8) return false;
    if(ctDistance(u,ctPredicted(sim.units[0],CT_SKILLS[2].cast+CT_POSITION.reaction))>band.max) return false;
    return u.position==null || ctDistance(u,u.position)<24;
}
