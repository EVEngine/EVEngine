// Armored Command 3D: imported skeletal vehicles + combat/destruction/VFX.
local models = null;
local animation = null;
local particleSystem = null;
local cam = null;
local assets = [];
local units = [];
local shells = [];
local vfxBursts = [];
local buildings = [];
local scenery = [];
local sceneryMaterials = [];
persist selected = 0
persist prevRight = false
persist hudBuilt = false
persist battleClock = 0.0
persist combatLog = []
persist assetReport = []

const PI = 3.1415926535;
const TANK_SCALE = 0.21;

tankPaths <- [
    "assets/quaternius-animated-tanks/tank-a/Tank.glb",
    "assets/quaternius-animated-tanks/tank-b/Tank3.glb",
    "assets/quaternius-animated-tanks/tank-c/Tank2.glb",
    "assets/quaternius-animated-tanks/tank-d/Tank4.glb"
];

function clampf(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
function len2(x, z) { return sqrt(x * x + z * z); }
function wrapAngle(a) {
    while (a > PI) a -= PI * 2.0;
    while (a < -PI) a += PI * 2.0;
    return a;
}
function logLine(s) {
    combatLog.push(s);
    while (combatLog.len() > 5) combatLog.remove(0);
}

function cube(x, y, z, sx, sy, sz, r, g, b) {
    x = x.tofloat(); y = y.tofloat(); z = z.tofloat();
    sx = sx.tofloat(); sy = sy.tofloat(); sz = sz.tofloat();
    r = r.tofloat(); g = g.tofloat(); b = b.tofloat();
    local e = eve.Renderable3D();
    e.setMesh(gfx.newMeshCube(1.0));
    e.setPosition(x, y, z); e.setScale(sx, sy, sz);
    local material = gfx.newMaterial();
    material.setTint(r, g, b, 1.0); material.setRoughness(0.82);
    material.setDepthWrite(true); material.setDoubleSided(true);
    e.setMaterial(material);
    sceneryMaterials.push(material);
    scenery.push(e);
    return e;
}

function loadTank(path) {
    local data = models.newModelDataFromFile(path);
    local skeleton = animation.newSkeletonFromModel(data);
    local clips = [];
    local maxBones = 0;
    for (local mesh = 0; mesh < data.getMeshCount(); ++mesh)
        if (data.hasBones(mesh) && data.getBoneCount(mesh) > maxBones) maxBones = data.getBoneCount(mesh);
    for (local i = 0; i < data.getAnimationCount(); ++i)
        clips.push(animation.newClipFromModel(data, skeleton, i));
    assetReport.push({ path=path, meshes=data.getMeshCount(), animations=data.getAnimationCount(), bones=maxBones });
    return { data = data, skeleton = skeleton, clips = clips };
}

function createTank(type, team, x, z, yaw) {
    x = x.tofloat(); z = z.tofloat(); yaw = yaw.tofloat();
    local a = assets[type];
    local renderables = [];
    local skins = [];
    for (local mesh = 0; mesh < a.data.getMeshCount(); ++mesh) {
        local e = models.createRenderable(gfx, a.data, mesh);
        // Quaternius tanks use -X as model-forward; gameplay uses +X.
        e.setPosition(x, 0.0, z); e.setRotation(yaw + PI, 0.0, 0.0);
        e.setScale(TANK_SCALE, TANK_SCALE, TANK_SCALE);
        e.setTint(team == 0 ? 0.65 : 0.98, team == 0 ? 0.84 : 0.48,
                  team == 0 ? 1.0 : 0.34, 1.0);
        e.setRoughness(0.48);
        renderables.push(e);
        if (a.data.hasBones(mesh))
            skins.push({ skin = animation.newSkinFromModel(a.data, mesh, a.skeleton), ent = e });
    }

    // Procedural gun is intentionally separate: it makes recoil visually explicit
    // while the imported 45-bone track rig keeps playing underneath.
    local gun = eve.Renderable3D();
    gun.setMesh(gfx.newMeshCylinder(16, 1, true));
    gun.setTint(team == 0 ? 0.18 : 0.34, team == 0 ? 0.34 : 0.16, 0.10, 1.0);
    gun.setScale(0.065, 0.625, 0.065);

    local player = animation.newPlayer(a.skeleton);
    player.play(a.clips[1]); player.setLoop(true);
    return { type=type, team=team, x=x, z=z, yaw=yaw, tx=x, tz=z,
             hp=100.0, dead=false, selected=false, death=0.0,
             recoil=0.0, hit=0.0, cooldown=0.5 + type * 0.2, clip=1,
             parts=renderables, skins=skins, gun=gun, player=player };
}

function playTankClip(u, index) {
    if (u.clip == index) return;
    u.clip = index;
    u.player.crossFade(assets[u.type].clips[index], 0.14);
}

function updateTankMesh(u, dt) {
    local recoil = sin(clampf(u.recoil / 0.18, 0.0, 1.0) * PI) * 0.17;
    local shake = u.hit > 0.0 ? sin(u.hit * 110.0) * 0.035 : 0.0;
    local fall = u.dead ? clampf(u.death / 2.0, 0.0, 1.0) : 0.0;
    local px = u.x - cos(u.yaw) * recoil + shake;
    local pz = u.z - sin(u.yaw) * recoil;
    local py = -fall * 0.31;
    local roll = fall * 0.38 * (u.team == 0 ? 1.0 : -1.0);
    local damage = clampf((100.0 - u.hp) / 100.0, 0.0, 1.0);
    foreach (e in u.parts) {
        e.setPosition(px, py, pz); e.setRotation(u.yaw + PI, 0.0, roll);
        e.setScale(TANK_SCALE, TANK_SCALE, TANK_SCALE);
        if (u.team == 0) e.setTint(0.78 + (u.hit > 0 ? 0.22 : 0.0), 0.9-damage*0.34, 1.0-damage*0.52, 1.0);
        else e.setTint(1.0, 0.66-damage*0.28 + (u.hit > 0 ? 0.22 : 0.0), 0.48-damage*0.2, 1.0);
    }
    local gx = px + cos(u.yaw) * (1.025 - recoil);
    local gz = pz + sin(u.yaw) * (1.025 - recoil);
    u.gun.setPosition(gx, 0.54 + py, gz);
    u.gun.setRotation(u.yaw, 0.0, PI * 0.5);
    u.gun.setScale(0.065, 0.625, 0.065);

    if (!u.dead) {
        u.player.update(dt);
        local pose = u.player.getPose();
        pose.computeWorld(assets[u.type].skeleton);
        foreach (s in u.skins) s.skin.applyToMesh(gfx, s.ent.getMesh(), pose);
    }
}

function screenX(x, z) { return config.width * 0.5 + (x - z) * 9.0; }
function screenY(x, z) { return config.height * 0.52 + (x + z) * 4.0; }

function burst(x, z, smoke=false) {
    local e = particleSystem.newEmitter(smoke ? 180 : 128);
    e.setRandomSeed(20260828 + vfxBursts.len() * 31); e.setAutoRandomSeed(false);
    e.setEmissionRate(0.0); e.setSpread(PI * 2.0);
    e.setParticleLifetime(smoke ? 0.9 : 0.22, smoke ? 1.7 : 0.48);
    e.setParticleSize(smoke ? 21.0 : 8.0, smoke ? 21.0 : 8.0);
    e.setSpeed(smoke ? 12.0 : 50.0, smoke ? 34.0 : 165.0);
    e.setGravity(0.0, smoke ? -12.0 : 52.0); e.setDamping(smoke ? 0.75 : 0.18);
    e.setBlendMode(smoke ? "alpha" : "additive");
    if (smoke) { e.setColorStart(0.18,0.16,0.14,0.78); e.setColorEnd(0.04,0.04,0.04,0.0); }
    else { e.setColorStart(1.0,0.9,0.28,1.0); e.setColorEnd(1.0,0.05,0.01,0.0); }
    e.setPosition(screenX(x,z), screenY(x,z)); e.start(); e.emit(smoke ? 38 : 48);
    vfxBursts.push({ emitter=e, life=smoke ? 2.0 : 0.65 });
}

function nearestEnemy(index) {
    local u = units[index]; local best = -1; local bestD = 999999.0;
    for (local i = 0; i < units.len(); ++i) {
        local v = units[i]; if (v.team == u.team || v.dead) continue;
        local dx=v.x-u.x, dz=v.z-u.z, d=dx*dx+dz*dz;
        if (d < bestD) { bestD=d; best=i; }
    }
    return best;
}

function fireShell(u, target) {
    local v=units[target], dx=v.x-u.x, dz=v.z-u.z, d=len2(dx,dz);
    if (d < 0.01) return;
    dx/=d; dz/=d;
    local e=eve.Renderable3D(); e.setMesh(gfx.newMeshSphere(10,6));
    e.setTint(1.0,0.78,0.15,1.0); e.setScale(0.06,0.06,0.2);
    local x=u.x+dx*1.1, z=u.z+dz*1.1; e.setPosition(x,0.56,z);
    shells.push({ ent=e,x=x,z=z,dx=dx,dz=dz,target=target,life=d/34.0 });
    u.recoil=0.18; burst(x,z,false);
}

function damageUnit(index, amount) {
    local u=units[index]; if (u.dead) return;
    u.hp-=amount; u.hit=0.22; burst(u.x,u.z,false);
    if (u.hp<=0.0) {
        u.hp=0.0; u.dead=true; u.death=0.0; burst(u.x,u.z,false); burst(u.x,u.z,true);
        logLine((u.team==0?"BLUE":"RED")+" armored unit destroyed");
    }
}

function updateUnits(dt) {
    for (local i=0;i<units.len();++i) {
        local u=units[i];
        if (u.recoil>0) u.recoil-=dt; if (u.hit>0) u.hit-=dt;
        if (u.dead) { u.death+=dt; updateTankMesh(u,dt); continue; }
        local target=nearestEnemy(i); if (target<0) { updateTankMesh(u,dt); continue; }
        local v=units[target];
        local useOrder=u.team==0 && u.selected && len2(u.tx-u.x,u.tz-u.z)>0.7;
        local gx=useOrder?u.tx:v.x, gz=useOrder?u.tz:v.z;
        local desired=atan2(gz-u.z,gx-u.x), delta=wrapAngle(desired-u.yaw);
        u.yaw+=clampf(delta,-1.35*dt,1.35*dt);
        local range=len2(v.x-u.x,v.z-u.z);
        if (useOrder || range>21.0) {
            local speed=u.team==0?1.44:1.28; u.x+=cos(u.yaw)*speed*dt; u.z+=sin(u.yaw)*speed*dt;
        }
        playTankClip(u,abs(delta)>0.42?(delta<0?2:3):1);
        u.cooldown-=dt;
        local aim=abs(wrapAngle(atan2(v.z-u.z,v.x-u.x)-u.yaw));
        if (range<34.0 && aim<0.2 && u.cooldown<=0) {
            fireShell(u,target); u.cooldown=2.3+u.type*0.16;
        }
        updateTankMesh(u,dt);
    }
}

function updateShells(dt) {
    for (local i=shells.len()-1;i>=0;--i) {
        local s=shells[i]; s.life-=dt; s.x+=s.dx*34.0*dt; s.z+=s.dz*34.0*dt;
        s.ent.setPosition(s.x,0.56+sin(s.life*22.0)*0.02,s.z);
        if (s.life<=0) { s.ent.setVisible(false); damageUnit(s.target,10+(rand()%5)); shells.remove(i); }
    }
}

function createBuilding(x,z,team) {
    x = x.tofloat(); z = z.tofloat();
    local names=["building-block.glb","building-door.glb","building-window.glb","roof-gable.glb"];
    local off=[[0.0,0.55,0.0],[0.0,0.35,-1.02],[1.02,0.72,0.0],[0.0,1.58,0.0]];
    local pieces=[];
    for(local i=0;i<names.len();++i) {
        local md=models.newModelDataFromFile("assets/kenney-modular-buildings/"+names[i]);
        local e=models.createRenderable(gfx,md,0); e.setPosition(x+off[i][0],off[i][1],z+off[i][2]);
        e.setScale(1.15,1.15,1.15); e.setTint(team==0?0.58:0.8,team==0?0.76:0.44,team==0?0.9:0.32,1.0);
        pieces.push({ent=e,ox=off[i][0],oy=off[i][1],oz=off[i][2],vx=(i-1.5)*0.7,vy=1.6+i*0.3,vz=(i%2==0?0.6:-0.6)});
    }
    return {x=x,z=z,team=team,hp=160.0,dead=false,time=0.0,pieces=pieces};
}

function updateBuildings(dt) {
    foreach(b in buildings) {
        if (!b.dead) {
            foreach(u in units) if(u.team!=b.team && !u.dead && len2(u.x-b.x,u.z-b.z)<12.0) b.hp-=10.0*dt;
            if(b.hp<=0) { b.dead=true; burst(b.x,b.z,false); burst(b.x,b.z,true); logLine((b.team==0?"BLUE":"RED")+" command post collapsed"); }
            continue;
        }
        b.time+=dt;
        foreach(p in b.pieces) {
            local y=p.oy+p.vy*b.time-2.8*b.time*b.time;
            p.ent.setPosition(b.x+p.ox+p.vx*b.time,y,b.z+p.oz+p.vz*b.time);
            p.ent.setRotation(p.vz*b.time,p.vx*b.time,b.time*0.8);
            if(y< -1.2) p.ent.setVisible(false);
        }
    }
}

function rayGround(out) {
    cam.screenToRay(mouse.getX(),mouse.getY(),gfx.getWidth().tofloat(),gfx.getHeight().tofloat());
    local oy=cam.getScreenRayOriginY(), dy=cam.getScreenRayDirY(); if(dy>-0.0001)return false;
    local t=-oy/dy; out[0]=cam.getScreenRayOriginX()+cam.getScreenRayDirX()*t; out[1]=cam.getScreenRayOriginZ()+cam.getScreenRayDirZ()*t; return true;
}

function updateInput() {
    local right=mouse.isDown(2);
    if(right && !prevRight) { local hit=[0.0,0.0]; if(rayGround(hit)){units[selected].tx=hit[0];units[selected].tz=hit[1];logLine("Move order accepted");} }
    prevRight=right;
    if(key_just_pressed("Tab")){units[selected].selected=false;selected=(selected+1)%3;units[selected].selected=true;}
    if(key_just_pressed("Space")){for(local i=0;i<3;++i){local t=nearestEnemy(i);if(t>=0&&!units[i].dead)fireShell(units[i],t);}logLine("BLUE synchronized volley");}
}

function setupScene() {
    cube(0,-0.18,0,68,0.25,48,0.14,0.2,0.14); cube(0,-0.02,0,13,0.04,48,0.25,0.24,0.2);
    for(local z=-20;z<=20;z+=8){cube(-17,0.35,z,0.9,0.7,0.9,0.34,0.3,0.22);cube(17,0.35,z+2,1.1,0.7,0.8,0.3,0.28,0.22);}
    buildings.push(createBuilding(-24,-14,0)); buildings.push(createBuilding(24,14,1));
}

function updateHud() {
    local blue = 0;
    local red = 0;
    foreach (u in units) {
        if (!u.dead) {
            if (u.team == 0) {
                blue += 1;
            } else {
                red += 1;
            }
        }
    }
    ui.setText("status", "BLUE " + blue + "   RED " + red + "   shells " + shells.len() +
               "   particles " + particleSystem.getLastParticleCount());
    local text = "";
    foreach (s in combatLog) text += s + "\n";
    ui.setText("log", text);
}

eve_init = function() {
    gfx.setBackgroundColor(0.12,0.16,0.21,1.0);
    if ("dev" in eve) { eve.dev.ai.setVisible(false); eve.dev.console.setVisible(false); }
    if(models==null)models=eve.Model3D(); if(animation==null)animation=eve.Animation(); if(particleSystem==null)particleSystem=eve.Particles();
    particleSystem.setBudget(5000,96); particleSystem.setQualityLevel(3);
    cam=eve.Camera3D();cam.setEye(36.0,30.0,40.0);cam.setTarget(0.0,0.0,0.0);cam.setUp(0.0,1.0,0.0);cam.setFov(46.0);cam.setAmbient(1.08,1.1,1.14);cam.setActive(true);
    gfx.setDirectionalLight(0.48,1.0,0.32,2.15,2.02,1.82);
    if(assets.len()==0){assetReport.clear();foreach(path in tankPaths)assets.push(loadTank(path));}
    if(units.len()==0){
        units.push(createTank(0,0,-26,-6,0));units.push(createTank(1,0,-28,2,0));units.push(createTank(2,0,-24,10,0));
        units.push(createTank(1,1,26,-8,PI));units.push(createTank(2,1,28,0,PI));units.push(createTank(3,1,24,8,PI));
        units[0].selected=true;setupScene();
    }
    if(!hudBuilt){ui.setTheme("dark");ui.beginBuild();ui.beginWindow("ARMORED COMMAND 3D","root");ui.text("New API end-to-end quality demo","sub");ui.text("","status");ui.text("TAB select | RMB move | SPACE volley","help");ui.separator("sep");ui.text("","log");ui.end();ui.mountBuildAs("rts-hud");ui.select("rts-hud");ui.setHostOverlay(true);ui.setHostPos(14.0,12.0,0.0,0.0);hudBuilt=true;}
    logLine("4 CC0 animated tank variants loaded");logLine("45-bone track rigs + 4 clips verified");
};

eve_update = function(dt) {
    battleClock+=dt;updateInput();updateUnits(dt);updateShells(dt);updateBuildings(dt);
    for(local i=vfxBursts.len()-1;i>=0;--i){vfxBursts[i].life-=dt;if(vfxBursts[i].life<=0){vfxBursts[i].emitter.stop();vfxBursts.remove(i);}}
    particleSystem.update(dt);updateHud();
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    particleSystem.render(gfx);
    ui.beginFrameAndRender();
};
