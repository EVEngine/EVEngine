// Engine-native immediate 2D presentation; simulation does not depend on this file.
local ctDisc=null,ctGlow=null,ctFont=null;
local ctColors=[[0.35,0.75,1.0],[1.0,0.72,0.30],[0.37,0.95,0.69],[0.95,0.36,0.31],[0.77,0.42,0.92]];
function ctPresentationInit() {
    gfx.setBackgroundColor(0.025,0.038,0.055,1.0);
    ctDisc=gfx.loadTexture("assets/disc.png");ctGlow=gfx.loadTexture("assets/glow.png");
    ctFont=gfx.newFont(eve.Font().newFontDataFromFile("assets/DejaVuSans-Bold.ttf",24),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.-:+[]>");
    gfx.setFont(ctFont);
}
function ctDot(x,y,r,c,a=1.0,soft=false) {
    gfx.drawTexturedRect(soft ? ctGlow : ctDisc,(x-r).tofloat(),(y-r).tofloat(),(2*r).tofloat(),(2*r).tofloat(),c[0].tofloat(),c[1].tofloat(),c[2].tofloat(),a.tofloat());
}
function ctRect(x,y,w,h,c,a=1.0) {gfx.drawSolidRect(x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),c[0].tofloat(),c[1].tofloat(),c[2].tofloat(),a.tofloat());}
function ctText(s,x,y,scale=0.65,c=null) {
    if(c==null) c=[0.78,0.84,0.88];
    gfx.print(s,x.tofloat(),y.tofloat(),c[0].tofloat(),c[1].tofloat(),c[2].tofloat(),1.0,scale.tofloat());
}
function ctRing(x,y,r,c,a=1.0,portion=1.0) {
    for(local i=0;i<80*portion;i++) {local t=i*6.2831853/80;ctDot(x+cos(t)*r,y+sin(t)*r,1.5,c,a);}
}
function ctLine(x,y,tx,ty,c,a=1.0) {
    local dx=tx-x,dy=ty-y,n=sqrt(dx*dx+dy*dy);local steps=ctMax(1,(n/5).tointeger());
    for(local i=0;i<=steps;i++) ctDot(x+dx*i/steps,y+dy*i/steps,2,c,a);
}
function ctCharacter(u,sim) {
    local c=ctColors[u.id];local x=u.x,y=u.y,alive=u.hp>0;
    ctDot(x,y+12,24,[0,0,0],0.45);
    if(!alive) {ctText("DOWN",x-22,y-10,0.5,c);return;}
    local bob=sin(sim.time*5+u.id)*1.5;
    if(u.id==sim.focus) {ctRing(x,y,34,c,0.7);ctText("FOCUS",x-24,y+43,0.45,c);}
    if(u.shield>0) {ctDot(x,y-10,66,ctColors[1],0.4,true);ctRing(x,y-10,35,ctColors[1],0.85);}
    if(u.state=="dodge" && u.team==0) {
        ctLine(x,y,u.dodgeX,u.dodgeY,[0.5,0.85,1.0],0.35);
        ctRing(u.dodgeX,u.dodgeY,13,[0.5,0.85,1.0],0.4);
    }
    ctDot(x,y-8,32,c,0.22,true);
    ctRect(x-11,y-4,8,20,[0.12,0.17,0.22]);ctRect(x+3,y-4,8,20,[0.12,0.17,0.22]);
    ctDot(x,y-12+bob,18,[c[0]*0.5,c[1]*0.5,c[2]*0.5]);
    ctRect(x-12,y-16+bob,24,20,c);
    ctDot(x,y-29+bob,12,[0.15,0.20,0.27]);ctDot(x,y-28+bob,7,[0.93,0.79,0.60]);
    if(u.id==1) {ctRect(x-28,y-19,15,27,c);ctRect(x+23,y-31,4,38,[0.9,0.92,0.9]);}
    else if(u.id==2) {ctRect(x+23,y-38,3,50,[0.6,0.48,0.29]);ctDot(x+24,y-42,8,c);ctDot(x+24,y-42,25,c,0.6,true);}
    else {ctRect(x+20,y-25,5,34,c);ctRect(x+16,y-4,13,3,[0.9,0.8,0.6]);}
    if(u.flash>0) ctDot(x,y-10,24,[1,1,1],u.flash*2);
    ctRect(x-27,y-54,54,5,[0.1,0.14,0.19]);ctRect(x-27,y-54,54*u.hp/u.maxHp,5,c);
    ctText(u.name,x-(u.name.len()*3.9),y-75,0.48,c);
    if(u.pending!=null) {
        local p=u.pending,s=CT_SKILLS[p.skill];
        ctRing(x,y-10,28,c,1.0,1-p.remaining/s.cast);
        ctText(s.name,x-21,y+27,0.49,c);
    } else if(u.agent!=null) ctText(u.state.toupper(),x-25,y+27,0.45,c);
}
function ctRender(sim) {
    gfx.clear();
    ctRect(26,151,948,560,[0.037,0.065,0.086]);
    for(local y=173;y<710;y+=32) for(local x=48;x<970;x+=32) ctDot(x,y,1,[0.16,0.25,0.29],0.5);
    ctRing(500,430,238,[0.14,0.24,0.27],0.4);ctRing(500,430,240,[0.14,0.24,0.27],0.4);
    ctText("EVENGINE / COOPERATIVE COMBAT LAB",30,27,0.6,[0.32,0.70,0.73]);
    ctText("COMPANION TACTICS",28,57,1.45,[0.94,0.92,0.83]);
    ctText("Read the threat. Share the task. Keep the player alive.",31,112,0.62,[0.49,0.62,0.68]);
    ctText("WAVE 0"+sim.wave,798,40,0.7,[0.95,0.72,0.37]);
    ctText(sim.auto ? "AUTO DEMO / P" : "PLAYER / WASD",798,73,0.55,ctColors[0]);
    local hero=sim.units[0];
    ctRing(hero.x,hero.y,CT_POSITION.minimum,[0.3,0.6,0.62],0.22);
    ctRing(hero.x,hero.y,ctSupportBand(sim.units[1]).max,ctColors[1],0.16);
    ctRing(hero.x,hero.y,ctSupportBand(sim.units[2]).max,ctColors[2],0.12);
    foreach(u in sim.units) if(u.agent!=null && u.hp>0) {
        local connected=ctDistance(u,hero)<=CT_SKILLS[ctSupportSkill(u)].range;
        ctLine(u.x,u.y,hero.x,hero.y,connected ? ctColors[u.id] : [1,0.3,0.2],0.13);
        if(u.position!=null) {
            ctRing(u.position.x,u.position.y,9,ctColors[u.id],0.7);
            ctLine(u.x,u.y,u.position.x,u.position.y,ctColors[u.id],0.25);
        }
    }
    foreach(h in sim.hazards) {
        local c=ctColors[h.source];
        if(h.remaining>0) {
            ctDot(h.x,h.y,h.radius,c,0.12);ctRing(h.x,h.y,h.radius,c,0.85);
            ctRing(h.x,h.y,h.radius*(1-h.remaining/h.total),c,0.7);
            ctText(format("IMPACT %.1fs",h.remaining),h.x-48,h.y+4,0.5,c);
        } else {ctDot(h.x,h.y,h.radius+15,c,ctMax(0,0.45+h.remaining));ctRing(h.x,h.y,h.radius,c,0.7);}
    }
    foreach(u in sim.units) ctCharacter(u,sim);
    foreach(e in sim.effects) {
        local c=e.kind=="heal" ? ctColors[2] : (e.kind=="guard" ? ctColors[1] : ctColors[0]);
        ctLine(e.x,e.y-20,e.tx,e.ty-20,c,e.life*1.8);
        ctRing(e.tx,e.ty-10,(0.5-e.life)*65,c,e.life*2);
        if(e.kind=="heal") {ctRect(e.tx-3,e.ty-38,6,20,c,e.life*2);ctRect(e.tx-10,e.ty-31,20,6,c,e.life*2);}
    }
    ctRect(994,25,340,686,[0.055,0.080,0.104]);
    ctText("SQUAD INTENT",1014,47,0.78,[0.94,0.92,0.83]);
    ctText("ORDER / "+sim.command,1014,83,0.6,[0.4,0.79,0.83]);
    for(local i=0;i<3;i++) {
        local u=sim.units[i],y=124+i*139,c=ctColors[i];
        ctRect(1014,y,3,126,c);ctText(u.name,1028,y+2,0.72,c);
        ctText(format("HP %.0f / %.0f     MP %.0f",u.hp,u.maxHp,u.mana),1028,y+32,0.51);
        ctRect(1028,y+57,280,4,[0.12,0.17,0.2]);ctRect(1028,y+57,280*u.hp/u.maxHp,4,c);
        ctText(i==0 ? (sim.auto ? "SHOWCASE PILOT" : "PLAYER CONTROL") : u.state.toupper(),1028,y+72,0.59,c);
        local reason=u.choice==null ? "WASD move / SPACE attack" : u.choice.reason;
        ctText(reason,1028,y+98,0.43,[0.52,0.65,0.7]);
        if(i!=0) ctText(format("GAP %.0f  /  SUPPORT %.2fs",ctDistance(u,hero),ctSupportEta(sim,u,hero,ctSupportSkill(u))),1028,y+117,0.43,c);
    }
    ctText("TEAM OUTCOMES",1014,557,0.59,[0.94,0.92,0.83]);
    ctText("MEND "+sim.stats.heals+"    AEGIS "+sim.stats.guards+"    EVADE "+sim.stats.dodges,1014,589,0.5);
    ctText("DAMAGE ABSORBED  "+sim.stats.absorbed,1014,617,0.51,ctColors[1]);
    ctText("Native AI transitions  "+sim.stats.transitions,1014,652,0.47,[0.48,0.62,0.68]);
    ctText("FIELD LOG",45,171,0.48,[0.45,0.61,0.67]);
    foreach(i,e in sim.events) ctText(format("%05.1f  ",e.time)+e.text,45,196+i*22,0.43,[0.60,0.72,0.75]);
    ctText("SPREAD 120+   /   AEGIS 185   /   MEND 225   /   CIRCLES = NEXT POSITION",45,685,0.45,[0.48,0.67,0.68]);
    local labels=["1  BALANCED","2  FOCUS","3  PROTECT","4  RETREAT"],orders=["BALANCED","FOCUS","PROTECT","RETREAT"];
    foreach(i,label in labels) {
        local active=sim.command==orders[i];
        ctRect(28+i*240,731,226,45,active ? [0.13,0.33,0.36] : [0.07,0.105,0.135]);
        ctText(label,45+i*240,746,0.6,active ? [0.62,0.95,0.88] : [0.53,0.65,0.7]);
    }
    ctText("WASD  move     SPACE  attack     T  target     P  auto demo     R  restart",30,803,0.57,[0.53,0.65,0.7]);
    if(sim.units[0].hp<=0) ctText("PLAYER DOWN / PRESS R TO RESTART",270,424,0.85,[1,0.7,0.5]);
}
