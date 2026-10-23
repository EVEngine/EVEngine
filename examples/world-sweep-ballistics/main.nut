// World3D owns all obstacles; the weapon runtime owns every projectile trajectory.
local labs=[];local clock=0.0;local accumulator=0.0;local face=null;local fontOwner=null;local ready=false;
function checked(r) {if(!r.ok) throw "world sweep: "+r.status.summary;return r.value;}
function rect(x,y,w,h,r,g,b,a=1.0) {gfx.drawSolidRect(x.tofloat(),y.tofloat(),w.tofloat(),h.tofloat(),r,g,b,a);}
function text(s,x,y,size,r=0.9,g=0.94,b=0.99) {gfx.drawText(face,s,x.tofloat(),y.tofloat(),r,g,b,1.0,size);}
function makeLab(title,subtitle,mode,color) {
    local world=physics.newWorld3D(0.0,0.0,0.0,false);
    local obstacles=[];
    local xs=mode=="bounce"?[-4.0,4.0]:[-0.6,1.5];
    foreach(x in xs) {
        local body=world.newBody("static",x,0.0,0.0);
        local shape=body.newBoxShape(0.3,3.5,1.0,1.0,0.4,0.0);
        obstacles.append({body=body,shape=shape,x=x});
    }
    local pool=checked(eve.Weapon().newCarrierRuntime(16));
    checked(pool.bindWorld3D(world.queryToken(),0.12,-1,-1,-1));
    checked(pool.configureSimulation(1.0/120.0,256,64,4096));
    local impacts=[{kind="emitTrigger",on="onHit"},{kind="release",on="onExpire"}];
    if(mode=="bounce") impacts.append({kind="bounce",on="onHit",bounceCount=24,restitution=1.0});
    if(mode=="pierce") impacts.append({kind="pierce",on="onHit",pierceCount=8});
    checked(pool.registerRecipe({id="demo:bolt",speed=7.5,lifetime=3.4,motion=["linear"],
        collision={enabled=mode!="disabled",worldSweep=true},triggers=["onHit","onExpire"],impacts=impacts}));
    labs.append({title=title,subtitle=subtitle,mode=mode,color=color,world=world,obstacles=obstacles,pool=pool,
        timer=labs.len()*0.22,states=[],trails={},flashes=[],hits=0,lastBody=-1});
}
eve_init=function() {
    gfx.setBackgroundColor(0.025,0.035,0.055,1.0);
    fontOwner=eve.Font();local data=fontOwner.newFontDataFromFile("assets/DejaVuSans-Bold.ttf",24);
    face=gfx.newFont(data," ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/-.");gfx.setFont(face);
    makeLab("CONTINUOUS RICOCHET","REAL BOXES / SPHERE CENTER / REMAINING TIME","bounce",[0.25,0.85,0.9]);
    makeLab("CONTACT NOTIFICATION","ON HIT / EVENT ONLY / MOTION UNCHANGED","notify",[1.0,0.65,0.28]);
    makeLab("PIERCE MEMORY","RETURN FLIGHT / ONE EVENT PER BODY","pierce",[0.64,0.55,1.0]);
    makeLab("COLLISION DISABLED","SAME WORLD / ZERO QUERIES / FREE FLIGHT","disabled",[0.36,0.84,0.56]);
}
function tick(lab,dt) {
    lab.timer-=dt;
    if(lab.timer<=0.0) {lab.timer+=1.7;checked(lab.pool.spawn("demo:bolt",-3.3,0.0,0.0,1.0,0.0,0.0));}
    lab.world.update(dt);
    local result=lab.mode=="pierce" ? checked(lab.pool.updateWithMotion(dt,function(s,m,step) {
        // User-written speed function reverses flight once; world collision still runs afterward.
        return {vx=s.age<0.85?7.5:-7.5,vy=0.0,vz=0.0};
    })) : checked(lab.pool.update(dt));
    foreach(e in result.events) {lab.hits++;lab.lastBody=e.bodyId;lab.flashes.append({x=e.x,y=e.y,t=0.45,nx=e.nx});}
    foreach(f in lab.flashes) f.t-=dt;
    for(local i=lab.flashes.len()-1;i>=0;i--) if(lab.flashes[i].t<=0) lab.flashes.remove(i);
    lab.states=checked(lab.pool.states());local live={};
    foreach(s in lab.states) {
        local key=s.handle.slot+":"+s.handle.generation;live[key]<-true;
        if(!(key in lab.trails)) lab.trails[key]<-[];
        lab.trails[key].append([s.x,s.y]);if(lab.trails[key].len()>110) lab.trails[key].remove(0);
    }
    local gone=[];foreach(k,v in lab.trails) if(!(k in live)) gone.append(k);
    foreach(k in gone) delete lab.trails[k];
}
eve_update=function(dt) {
    accumulator+=dt;local count=0;
    while(accumulator>=1.0/120.0 && count<8) {
        clock+=1.0/120.0;foreach(lab in labs) tick(lab,1.0/120.0);accumulator-=1.0/120.0;count++;
    }
    if(!ready && labs[0].hits>0 && labs[1].hits>0 && labs[2].hits>0) {
        if(labs[3].hits!=0) throw "disabled collision emitted a hit";
        print("WORLD_SWEEP_BALLISTICS_READY realWorlds=4 fixedHz=120\n");ready=true;
    }
}
function point(left,top,x,y,size,c,a=1.0) {
    local px=left+290+x*49,py=top+170-y*44;
    if(px<=left+14 || px>=left+566 || py<=top+66 || py>=top+265) return;
    if(size<=3.0) {rect(px-size/2,py-size/2,size,size,c[0],c[1],c[2],a);return;}
    // Scanline ellipse projects the actual spherical collider in this XY view.
    local strips=12,h=size*44.0/49.0;
    for(local row=0;row<strips;row++) {
        local v=(row+0.5)*2.0/strips-1.0,span=size*sqrt(1.0-v*v);
        rect(px-span/2,py-h/2+row*h/strips,span,h/strips+0.15,c[0],c[1],c[2],a);
    }
}
eve_render=function() {
    gfx.clear();rect(28,28,4,62,0.25,0.85,0.9);
    text("WORLD3D / PROJECTILE SWEEPS",46,26,1.2);
    text("OPTIONAL PHYSICS. EXPLICIT TRIGGERS. CUSTOM MOTION.",46,66,0.52,0.4,0.62,0.72);
    text("LIVE / 120 HZ",1050,41,0.6,0.3,0.85,0.8);rect(28,111,1224,1,0.17,0.25,0.32);
    foreach(i,lab in labs) {
        local left=28+(i%2)*624,top=136+(i/2)*326,c=lab.color;
        rect(left,top,600,300,0.045,0.063,0.09);rect(left,top,600,2,c[0],c[1],c[2]);
        text(lab.title,left+20,top+19,0.75);text(lab.subtitle,left+20,top+50,0.39,0.42,0.56,0.67);
        for(local x=0;x<19;x++) for(local y=0;y<6;y++) rect(left+28+x*30,top+88+y*30,1,1,0.17,0.24,0.32);
        foreach(o in lab.obstacles) {
            local x=left+290+o.x*49;
            rect(x-7.35,top+93,14.7,154,0.26,0.34,0.43,0.75);
            rect(x-7.35,top+93,2,154,c[0],c[1],c[2],0.65);
        }
        foreach(k,path in lab.trails) foreach(j,p in path) point(left,top,p[0],p[1],2.5,c,0.08+0.5*j/path.len());
        foreach(s in lab.states) {point(left,top,s.x,s.y,17,c,0.12);point(left,top,s.x,s.y,11.76,[0.93,0.97,1.0]);}
        foreach(f in lab.flashes) {point(left,top,f.x,f.y,12+(0.45-f.t)*70,[1.0,0.73,0.32],f.t*1.4);
            point(left,top,f.x+f.nx*0.35,f.y,4,[1.0,0.73,0.32],f.t*2);}
        text("ACTIVE "+lab.states.len()+"   CONTACTS "+lab.hits+"   BODY "+lab.lastBody,left+20,top+276,0.43,0.42,0.59,0.7);
    }
    text("WHITE / SPHERE     COLOR / TRAIL     AMBER / CONTACT NORMAL     WALLS / WORLD3D BOX SHAPES",28,795,0.42,0.4,0.55,0.65);
};
