// Presentation consumes physics snapshots; renderables never write simulation state.
function syncVisual3(r) {
    local b=r.body; r.visual.setPosition(b.getX(),b.getY(),b.getZ());
    local x=b.getRotX(),y=b.getRotY(),z=b.getRotZ(),w=b.getRotW();
    r.visual.setRotation(atan2(2.0*(x*z+y*w),1.0-2.0*(x*x+y*y)),
        asin(clamp3(2.0*(x*w-y*z),-1.0,1.0)),atan2(2.0*(x*y+z*w),1.0-2.0*(x*x+z*z)));
}
function runnerPart3(i,lx,ly,lz,sx,sy,sz) {
    local a=site.facing,x=state.x+cos(a)*lx+sin(a)*lz,z=state.z-sin(a)*lx+cos(a)*lz;
    local p=site.parts[i]; p.setPosition(x,state.y+ly,z); p.setScale(sx,sy,sz); p.setRotation(a,0.0,0.0);
}
function presentCourse(dt) {
    foreach(r in site.bodies) if(r.type!="static") syncVisual3(r);
    foreach(p in site.pendulums) segment3(p.cable,0.0,11.0,p.z,
        p.record.body.getX(),p.record.body.getY(),p.z,0.14);
    local speed=sqrt(site.runner.body.getLinearVelocityX()*site.runner.body.getLinearVelocityX()+
        site.runner.body.getLinearVelocityZ()*site.runner.body.getLinearVelocityZ());
    local swing=site.grounded?sin(site.clock*11.0)*min3(speed/6.0,1.0)*0.22:0.12;
    runnerPart3(0,0.0,1.10,0.0,0.58,0.64,0.34);
    runnerPart3(1,0.0,1.64,0.22,0.43,0.18,0.10);
    runnerPart3(2,0.0,1.62,0.0,0.51,0.46,0.43);
    runnerPart3(3,-0.19,0.42,swing,0.23,0.72,0.25);
    runnerPart3(4,0.19,0.42,-swing,0.23,0.72,0.25);
    runnerPart3(5,-0.43,1.07,-swing,0.20,0.65,0.23);
    runnerPart3(6,0.43,1.07,swing,0.20,0.65,0.23);
    runnerPart3(7,0.0,1.12,-0.23,0.42,0.52,0.20);
    // Visor overlays helmet after construction order; distinct cyan face points along intent.
    site.parts[1].setPosition(state.x+sin(site.facing)*0.24,state.y+1.66,state.z+cos(site.facing)*0.24);
    local cp=state.checkpoint<6?site.checkpoints[state.checkpoint]:{x=26.0,y=4.0,z=49.0};
    site.beacon.setPosition(cp.x,cp.y+4.0+sin(site.clock*2.0)*0.2,cp.z);
    site.beacon.setRotation(site.clock*0.7,0.0,0.0);
    if(site.overview) {
        site.camera.setEye(110.0,108.0,-112.0);
        site.camera.setTarget(0.0,1.0,8.0); site.camera.setFov(49.0);
    } else {
        local tx=state.x,ty=state.y+1.2,tz=state.z;
        local ex=tx-sin(site.yaw)*10.0,ey=state.y+7.0,ez=tz-cos(site.yaw)*10.0;
        local w=site.world;
        w.setQueryFilter(2,9); w.setQueryIgnoredBodyId(site.runner.body.getId());
        if(w.rayCast(tx,ty,tz,ex,ey,ez)>0) {
            local f=max3(0.12,w.getRayHitFraction()-0.08);
            ex=tx+(ex-tx)*f; ey=ty+(ey-ty)*f; ez=tz+(ez-tz)*f;
        }
        w.clearQueryIgnores(); w.resetQueryFilter();
        site.camera.setEye(ex,ey,ez);
        site.camera.setTarget(tx,ty,tz);
        site.camera.setFov(58.0);
    }
}
function hudRect3(x,y,w,h,r,g,b,a=1.0) {
    local s=min3(gfx.getWidth()/1440.0,gfx.getHeight()/900.0);
    gfx.drawSolidRect(x*s,y*s,w*s,h*s,r,g,b,a);
}
function hudText3(s,x,y,size=0.65,r=0.87,g=0.94,b=0.96) {
    local scale=min3(gfx.getWidth()/1440.0,gfx.getHeight()/900.0);
    gfx.drawText(hudFont,s,x*scale,y*scale,r,g,b,1.0,size*scale);
}
function mapPoint3(x,z) { return {x=1282.0-x*1.55,y=188.0-z*1.55}; }
function mapLine3(x1,z1,x2,z2,r,g,b) {
    local a=mapPoint3(x1,z1),c=mapPoint3(x2,z2);
    hudRect3(min3(a.x,c.x)-2.0,min3(a.y,c.y)-2.0,max3(abs(a.x-c.x),4.0),max3(abs(a.y-c.y),4.0),r,g,b);
}
function drawCourseHud() {
    hudRect3(24,22,650,105,0.025,0.055,0.08,0.94);
    hudRect3(24,22,5,105,0.05,0.85,0.88);
    hudText3("SKYFORGE",44,34,1.18);
    hudText3("THE MECHANICAL GAUNTLET / 8 SECTORS",46,78,0.57,0.95,0.62,0.25);
    hudRect3(24,140,540,85,0.025,0.055,0.08,0.91);
    hudText3("CHECKPOINT "+state.checkpoint+" / 6     RETRIES "+state.deaths+"     "+format("%05.1fs",state.time),43,155,0.61);
    hudText3(state.last,43,191,0.48);
    hudRect3(1162,22,250,290,0.025,0.055,0.08,0.95);
    hudText3("ROUTE / LIVE",1180,36,0.58);
    mapLine3(-26,-29,-26,46,0.2,0.5,0.6); mapLine3(-26,46,0,46,0.2,0.5,0.6);
    mapLine3(0,46,0,-22,0.2,0.5,0.6); mapLine3(0,-22,26,-22,0.2,0.5,0.6);
    mapLine3(26,-22,26,49,0.2,0.5,0.6);
    local points=[{x=-26,z=-25},{x=-26,z=5},{x=-26,z=29},{x=-12,z=46},
        {x=0,z=2},{x=26,z=-5},{x=26,z=22},{x=26,z=49}];
    foreach(i,p in points) {
        local m=mapPoint3(p.x,p.z); hudRect3(m.x-6,m.y-6,12,12,0.95,0.56,0.15);
        hudText3(""+(i+1),m.x+8,m.y-8,0.39);
    }
    local pos=mapPoint3(state.x,state.z); hudRect3(pos.x-4,pos.y-4,8,8,0.10,1.0,1.0);
    hudText3(state.grounded?"CONTACT / GROUNDED":"CONTACT / AIRBORNE",1180,272,0.42);
    hudRect3(24,831,1390,48,0.025,0.055,0.08,0.95);
    hudText3("WASD / MOVE    SPACE / JUMP    Q E / ROTATE CAMERA    F / OVERVIEW    R / CHECKPOINT    T / RESTART",43,845,0.55);
    if(!site.overview && !state.finished) {
        local mission="01 / CLIMB THE STAIRWAY";
        if(state.checkpoint==0 && state.z> -8.0) mission="02 / JUMP THE SWEEPERS OR DODGE ALONG THE EDGE";
        if(state.checkpoint==0 && state.z>19.0) mission="03 / PUSH THE AMBER CARGO ONTO THE CYAN PLATE";
        if(state.checkpoint==1) mission="04 / TURN EAST - BOARD LIFT - THEN TURN SOUTH";
        if(state.checkpoint==2) mission="04 / BOARD THE FERRY SOUTH ACROSS THE GAP";
        if(state.checkpoint==3) mission="05 / DODGE THE PENDULUMS - CONTINUE SOUTH";
        if(state.checkpoint==4) mission="06 / EAST BRIDGE - TURN NORTH - TIME THE PRESSES";
        if(state.checkpoint==5) mission="07 / AVOID THE ROLLING STEEL - CONTINUE NORTH";
        if(state.checkpoint==6) mission="08 / JUMP THE OFFSET ISLANDS TO THE FINISH ARCH";
        hudRect3(24,750,960,58,0.025,0.055,0.08,0.92);
        hudText3(mission,42,768,0.55,0.12,0.91,0.91);
    }
    if(site.overview) {
        hudRect3(24,620,640,184,0.025,0.055,0.08,0.94);
        hudText3("A FACILITY BUILT TO TEST YOUR FOOTING",43,640,0.65);
        hudText3("01 STAIRS   02 SWEEPERS   03 CARGO LOCK   04 LIFT + FERRY",43,677,0.46);
        hudText3("05 PENDULUMS   06 HYDRAULIC PRESSES   07 BOULDERS   08 ISLANDS",43,706,0.43);
        hudText3("ENTER / START RUN",43,751,0.83,0.12,0.91,0.91);
    }
    if(state.finished) {
        hudRect3(390,342,660,180,0.025,0.16,0.18,0.97);
        hudText3("GAUNTLET COMPLETE",425,370,1.1);
        hudText3("8 SECTORS / "+state.deaths+" RETRIES / "+format("%.1f SECONDS",state.time),425,424,0.63);
        hudText3("T / RUN AGAIN     F / INSPECT THE FACILITY",425,472,0.52);
    }
}
