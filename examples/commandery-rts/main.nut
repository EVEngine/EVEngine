// Commandery RTS: a playable RTS + general/administration composition demo.
// Domain meaning lives in this script. Engine modules only store/query generic facts.

persist game = null
persist previousMouse = { left=false, right=false }
persist previousKeys = {}
persist panelReady = false
persist selectionDrag = { active=false sx=0.0 sy=0.0 x=0.0 y=0.0 }

attributes <- eve.Attributes();
social <- eve.Social();
authority <- eve.Authority();
orders <- eve.Orders();
production <- eve.Production();
sensing <- eve.Sensing();
decision <- eve.Decision();
steering <- eve.Steering();
effects <- eve.Effects();
events <- eve.GameEvent();

function keyPressed(name) {
    local down = keyboard.isDown(name);
    local was = (name in previousKeys) ? previousKeys[name] : false;
    previousKeys[name] <- down;
    return down && !was;
}

function mousePressed(button) {
    local down = mouse.isDown(button);
    local key = button == 1 ? "left" : "right";
    local was = previousMouse[key];
    previousMouse[key] = down;
    return down && !was;
}

function clamp(v, lo, hi) { return v < lo ? lo : (v > hi ? hi : v); }
function distance(ax, ay, bx, by) { local x=ax-bx; local y=ay-by; return sqrt(x*x+y*y); }

function requireResult(result, context) {
    if (!result.ok) throw context + ": " + result.status.summary;
    return result.value;
}

function livingCount(faction) {
    local n = 0;
    foreach (u in game.units) if (u.alive && u.faction == faction) n+=1;
    return n;
}

function makeUnit(id, faction, kind, x, y, commander) {
    local tank = kind == "tank";
    local stats = attributes.newSet(id);
    stats.setBase("hp", tank ? 180.0 : 85.0);
    stats.setBase("damage", tank ? 24.0 : 10.0);
    stats.setBase("speed", tank ? 42.0 : 58.0);
    social.setOwner(id, faction);
    if (commander != "") social.assign(id, "subordinate", commander);
    local queue = requireResult(orders.newQueueOwned(), "create order queue for " + id);
    return {
        id=id faction=faction kind=kind x=x y=y tx=x ty=y
        hp=tank ? 180.0 : 85.0 maxHp=tank ? 180.0 : 85.0
        damage=tank ? 24.0 : 10.0 speed=tank ? 42.0 : 58.0
        range=tank ? 70.0 : 48.0 cooldown=0.0 selected=false alive=true
        radius=tank ? 17.0 : 10.0 holdFire=false orderId=""
        commander=commander stats=stats queue=queue aimX=1.0 aimY=0.0
    };
}

function resetGame() {
    social.clear();
    local authorityStore = requireResult(authority.newStore(), "create authority store");
    local mindContext = requireResult(decision.newContext(), "create decision context");
    local statusContainer = requireResult(effects.newContainer(), "create effect container");
    local sensor = requireResult(sensing.newWorld(), "create sensing world");
    local factory = requireResult(production.newWorkQueue(), "create production queue");
    local generalStats = attributes.newSet("general.arden");
    generalStats.setBase("administration", 76.0);
    generalStats.setBase("command", 84.0);
    generalStats.setBase("loyalty", 58.0);
    generalStats.setBase("ambition", 82.0);
    local baseStats = attributes.newSet("base.north");
    baseStats.setBase("production_speed", 1.0);

    game = {
        crown="faction.crown" frontier="faction.frontier"
        general="general.arden" baseId="base.north" enemyBase="base.frontier"
        generalStats=generalStats baseStats=baseStats
        authority=authorityStore factory=factory
        sensor=sensor mind=mindContext
        statuses=statusContainer stream=events.newLog()
        units=[] projectiles=[] particles=[] selected=null money=520.0 enemyMoney=400.0 incomeTimer=0.0
        salaryTimer=0.0 aiTimer=0.0 spawnCursor=0 governor=true rebelled=false outcome=""
        message="北方军区已就绪。左键选兵，右键移动。" time=0.0 productionTick=0
        points=[
            { id="mine.north" x=300.0 y=210.0 owner="faction.crown" governor="general.arden" capture=0.0 capturing="" contested=false },
            { id="mine.center" x=525.0 y=365.0 owner="faction.crown" governor="" capture=0.0 capturing="" contested=false },
            { id="mine.east" x=760.0 y=205.0 owner="faction.frontier" governor="" capture=0.0 capturing="" contested=false }
        ]
    };

    social.setOwner(game.baseId, game.crown);
    social.setOwner(game.enemyBase, game.frontier);
    social.setOwner(game.general, game.crown);
    social.assign(game.general, "governor", game.baseId);
    social.assign(game.general, "commander", "army.north");
    social.setRelation("officers.north", game.general, "support", 0.68);
    game.authority.grant(game.general, game.baseId, "govern_base", "rank.general", 10, 0.0);
    game.authority.grant(game.general, "army.north", "issue_orders", "rank.general", 10, 0.0);
    requireResult(game.factory.setSlotCount(game.baseId, 2), "north production slots");
    requireResult(game.factory.setSlotCount(game.enemyBase, 2), "frontier production slots");
    requireResult(game.mind.setState("frontier.ai", "raid"), "seed frontier AI");
    requireResult(game.mind.addTransition("frontier.ai", "raid", "base_exposed", "assault"),
                  "frontier raid->assault");
    requireResult(game.mind.addTransition("frontier.ai", "assault", "hold_mines", "raid"),
                  "frontier assault->raid");
    requireResult(game.mind.newGrid("threat", 10, 6, 100.0, 0.0, 80.0), "threat grid");

    // Crown holds north + a picket on the bridge mine; Frontier garrisons east.
    // The opening is an economic contest, not a map-wide deathball.
    game.units.push(makeUnit("crown.tank.1", game.crown, "tank", 240.0, 240.0, game.general));
    game.units.push(makeUnit("crown.infantry.1", game.crown, "infantry", 290.0, 200.0, game.general));
    game.units.push(makeUnit("crown.infantry.2", game.crown, "infantry", 500.0, 360.0, game.general));
    game.units.push(makeUnit("frontier.tank.1", game.frontier, "tank", 800.0, 250.0, "general.boros"));
    game.units.push(makeUnit("frontier.infantry.1", game.frontier, "infantry", 740.0, 210.0, "general.boros"));
    refreshAdministration();
    refreshPanel();
}

function refreshAdministration() {
    game.baseStats.removeBySource(game.general, "");
    local assigned = social.isAssigned(game.general, "governor", game.baseId);
    local allowed = game.authority.can(game.general, game.baseId, "govern_base");
    if (assigned && allowed && !game.rebelled) {
        local admin = game.generalStats.getFinal("administration", 0.0);
        game.baseStats.addModifier("governor.speed", "production_speed", game.general,
            "multiply", 1.0 + admin * 0.006, 10);
    }
}

function toggleGovernor() {
    if (game.outcome != "") return;
    if (game.governor) {
        social.unassign(game.general, "governor", game.baseId);
        game.authority.revokeBySource("rank.general", "dismissed");
        game.governor = false;
        game.message = "将领被撤职：行政加成和军区授权已失效。";
    } else {
        social.assign(game.general, "governor", game.baseId);
        game.authority.grant(game.general, game.baseId, "govern_base", "rank.general", 10, 0.0);
        game.authority.grant(game.general, "army.north", "issue_orders", "rank.general", 10, 0.0);
        game.governor = true;
        game.message = "将领重新获任北方总督。";
    }
    refreshAdministration();
}

function paySalary() {
    if (game.outcome != "") return;
    if (game.money < 80.0) { game.message="资金不足，无法支付军饷。"; return; }
    game.money -= 80.0;
    game.generalStats.removeBySource("treasury.unpaid", "");
    local loyalty = game.generalStats.getFinal("loyalty", 0.0);
    game.generalStats.setBase("loyalty", clamp(loyalty + 18.0, 0.0, 100.0));
    game.message = "已支付军饷：忠诚上升。";
}

function withholdSalary() {
    if (game.outcome != "") return;
    local loyalty = game.generalStats.getFinal("loyalty", 0.0);
    game.generalStats.setBase("loyalty", clamp(loyalty - 22.0, 0.0, 100.0));
    requireResult(game.statuses.apply(game.general, "salary_unpaid", game.crown, 10, 20.0,
                                      "salary", "refresh"), "apply unpaid-salary effect");
    social.addRelation("officers.north", game.general, "support", 0.08);
    evaluateRebellion();
    if (!game.rebelled) game.message = "军饷被拖欠：忠诚下降，部属抱团。";
}

function evaluateRebellion() {
    if (game.rebelled) return;
    local loyalty = game.generalStats.getFinal("loyalty", 0.0);
    local ambition = game.generalStats.getFinal("ambition", 0.0);
    local support = social.relation("officers.north", game.general, "support", 0.0);
    if (loyalty >= 25.0 || ambition <= 70.0 || support < 0.70) return;

    game.rebelled = true;
    game.governor = false;
    social.setOwner(game.general, game.frontier);
    social.setOwner(game.baseId, game.frontier);
    game.authority.revokeBySource("rank.general", "rebellion");
    foreach (p in game.points) if (p.owner == game.crown && p.x < 600.0) p.owner = game.frontier;
    foreach (u in game.units) {
        if (u.alive && u.commander == game.general) {
            u.faction = game.frontier;
            u.selected = false;
            social.setOwner(u.id, game.frontier);
        }
    }
    game.selected = null;
    game.stream.append("00000000-0000-4000-8000-000000000001", "rebellion_started",
                       game.general, game.frontier, "", "north.rebellion",
                       1, 1, "{\"base\":\"base.north\"}");
    game.message = "叛乱！将领带领北方基地、经济点和直属部队倒戈。";
    refreshAdministration();
}

function queueUnit(kind) {
    local cost = kind == "tank" ? 160.0 : 70.0;
    if (game.outcome != "" || game.money < cost || game.rebelled) {
        game.message="无法生产：资金不足、战局已结束或基地已失守。"; return;
    }
    local duration = kind == "tank" ? 8.0 : 4.0;
    local enqueue = game.factory.enqueue(game.baseId, "build_unit", kind, "{}", duration, 10);
    if (!enqueue.ok) { game.message = enqueue.status.summary; return; }
    game.money -= cost;
    game.message = kind == "tank" ? "坦克进入生产队列。" : "步兵进入训练队列。";
}

function spawnCompletedTask(task) {
    local kind = task.getProduct();
    local owner = task.getOwner();
    local faction = owner == game.enemyBase ? game.frontier : game.crown;
    local commander = faction == game.crown ? game.general : "general.boros";
    local id = (faction == game.crown ? "crown." : "frontier.") + kind + ".built." + game.spawnCursor;
    local x = faction == game.crown ? 145.0 + game.spawnCursor*18.0 : 880.0 - game.spawnCursor*18.0;
    local y = faction == game.crown ? 430.0 : 260.0;
    game.units.push(makeUnit(id, faction, kind, x, y, commander));
    game.spawnCursor += 1;
    local who = faction == game.crown ? "我军" : "敌军";
    game.message = who + (kind == "tank" ? "坦克完成并加入编队。" : "步兵完成训练。");
}

function updateProduction(dt) {
    local speed = game.baseStats.getFinal("production_speed", 1.0);
    game.productionTick += 1;
    local advanceResult = game.factory.advance(game.productionTick, dt * speed);
    if (!advanceResult.ok) { game.message = advanceResult.status.summary; return; }
    while (game.spawnCursor < game.factory.taskCount()) {
        local task = game.factory.taskAt(game.spawnCursor);
        if (task.getState() != "completed") break;
        spawnCompletedTask(task);
    }
}

function selectAt(x, y) {
    game.selected=null;local best=null,bestD=100000.0;
    foreach (u in game.units) {
        u.selected = false;
        if(u.alive&&u.faction==game.crown){local d=distance(x,y,u.x,u.y);if(d<24.0&&d<bestD){best=u;bestD=d;}}
    }
    game.selected=best;
    if (best != null) {
        best.selected=true;
        game.message = "已选择 " + best.kind + "；右键下达移动命令。";
    }
}

function selectBox(ax,ay,bx,by) {
    local left=ax<bx?ax:bx,right=ax>bx?ax:bx,top=ay<by?ay:by,bottom=ay>by?ay:by,count=0;
    game.selected=null;
    foreach(u in game.units){u.selected=u.alive&&u.faction==game.crown&&u.x>=left&&u.x<=right&&u.y>=top&&u.y<=bottom;
        if(u.selected){if(game.selected==null)game.selected=u;count+=1;}}
    game.message=count>0?"框选了 "+count+" 个单位；右键编队移动。":"选择框内没有己方单位。";
}

function selectedUnits(){local result=[];foreach(u in game.units)if(u.alive&&u.selected)result.push(u);return result;}

function issueMove(x, y) {
    local group=selectedUnits();if(group.len()==0)return;
    if (game.outcome != "") { game.message = "战局已结束，无法再下达命令。"; return; }
    if (!game.authority.can(game.general, "army.north", "issue_orders")) {
        game.message = "命令被拒绝：将领没有军区指挥权。";
        return;
    }
    local cols=group.len()<3?group.len():3;
    for(local i=0;i<group.len();i+=1){local col=i%cols,row=(i/cols).tointeger();
        local rows=((group.len()+cols-1)/cols).tointeger();
        group[i].tx=clamp(x+(col-(cols-1)*0.5)*42.0,30.0,930.0);
        group[i].ty=clamp(y+(row-(rows-1)*0.5)*42.0,100.0,675.0);
        local replaced = group[i].queue.replace("move",10,0.0);
        group[i].orderId = requireResult(replaced, "replace move order");
        local order = group[i].queue.find(group[i].orderId);
        if (order != null) {
            local payload = order.getPayload();
            requireResult(payload.setJson("x", "" + group[i].tx), "move payload x");
            requireResult(payload.setJson("y", "" + group[i].ty), "move payload y");
        }
    }
    game.message = "已向 "+group.len()+" 个单位下达编队移动命令。";
}

function handleBattlefieldInput() {
    // EVEngine mouse buttons are 1=left, 2=right, 3=middle.
    local mx=mouse.getX(),my=mouse.getY(),left=mouse.isDown(1);
    if(left&&!previousMouse.left&&mx<960.0){selectionDrag.active=true;selectionDrag.sx=mx;selectionDrag.sy=my;selectionDrag.x=mx;selectionDrag.y=my;}
    if(left&&selectionDrag.active){selectionDrag.x=clamp(mx,20.0,940.0);selectionDrag.y=clamp(my,80.0,680.0);}
    if(!left&&previousMouse.left&&selectionDrag.active){local dx=selectionDrag.x-selectionDrag.sx,dy=selectionDrag.y-selectionDrag.sy;
        if(dx*dx+dy*dy>49.0)selectBox(selectionDrag.sx,selectionDrag.sy,selectionDrag.x,selectionDrag.y);
        else selectAt(selectionDrag.sx,selectionDrag.sy);selectionDrag.active=false;}
    previousMouse.left=left;
    if(mx<960.0&&mousePressed(2)&&game.outcome=="")issueMove(mx,my);
}

function moveUnit(u, dt) {
    local d = distance(u.x,u.y,u.tx,u.ty);
    if (d < 2.0) {
        if (u.orderId != "") {
            local done = u.queue.complete(u.orderId);
            if (done.ok) u.orderId = "";
        }
        return;
    }
    requireResult(u.queue.update(dt), "advance order queue");
    steering.arrive(u.x,u.y,u.tx,u.ty,u.speed,70.0,2.0); // reusable engine primitive
    local step = d < u.speed*dt ? d : u.speed*dt;
    u.x += (u.tx-u.x)/d*step;
    u.y += (u.ty-u.y)/d*step;
}

// Resolve unit bodies after movement.  This is deliberately gameplay-level
// separation: scripts can replace it with formations, lanes or physics bodies.
function separateUnits() {
    for(local i=0;i<game.units.len();i+=1) {
        local a=game.units[i]; if(!a.alive)continue;
        for(local j=i+1;j<game.units.len();j+=1) {
            local b=game.units[j]; if(!b.alive)continue;
            local dx=b.x-a.x,dy=b.y-a.y,d=sqrt(dx*dx+dy*dy);
            local minD=a.radius+b.radius+3.0;
            if(d>=minD)continue;
            if(d<0.01){dx=((i+j)%2==0)?1.0:-1.0;dy=0.35;d=sqrt(dx*dx+dy*dy);}
            local push=(minD-d)*0.5,nx=dx/d,ny=dy/d;
            a.x=clamp(a.x-nx*push,25.0,935.0);a.y=clamp(a.y-ny*push,95.0,680.0);
            b.x=clamp(b.x+nx*push,25.0,935.0);b.y=clamp(b.y+ny*push,95.0,680.0);
        }
    }
}

function nearestEnemy(u) {
    local best=null; local bestD=100000.0;
    foreach (v in game.units) if (v.alive && v.faction != u.faction) {
        local d=distance(u.x,u.y,v.x,v.y); if(d<bestD){best=v;bestD=d;}
    }
    return best;
}

function spawnBurst(x, y, heavy) {
    local count = heavy ? 22 : 7;
    for(local i=0;i<count;i+=1) {
        local a=i*6.28318/count+game.time*0.7;
        local speed=(heavy?65.0:35.0)+(i%5)*12.0;
        game.particles.push({x=x y=y vx=cos(a)*speed vy=sin(a)*speed
            life=heavy?0.75:0.30 maxLife=heavy?0.75:0.30
            size=heavy?6.0+(i%3)*2.0:3.0 smoke=heavy&&i%3==0});
    }
}

function fireProjectile(shooter,target) {
    local dx=target.x-shooter.x,dy=target.y-shooter.y,d=sqrt(dx*dx+dy*dy);
    if(d<0.001)return;
    shooter.aimX=dx/d;shooter.aimY=dy/d;
    local heavy=shooter.kind=="tank",muzzle=heavy?25.0:14.0;
    game.projectiles.push({x=shooter.x+shooter.aimX*muzzle y=shooter.y+shooter.aimY*muzzle
        vx=shooter.aimX*(heavy?250.0:460.0) vy=shooter.aimY*(heavy?250.0:460.0)
        target=target damage=shooter.damage heavy=heavy alive=true life=2.2});
    spawnBurst(shooter.x+shooter.aimX*muzzle,shooter.y+shooter.aimY*muzzle,false);
}

function updateProjectiles(dt) {
    for(local i=game.projectiles.len()-1;i>=0;i-=1) {
        local p=game.projectiles[i];p.life-=dt;p.x+=p.vx*dt;p.y+=p.vy*dt;
        local hit=p.target!=null&&p.target.alive&&distance(p.x,p.y,p.target.x,p.target.y)<(p.heavy?15.0:9.0);
        if(hit){p.target.hp-=p.damage;spawnBurst(p.x,p.y,p.heavy);p.alive=false;
            if(p.target.hp<=0.0){p.target.alive=false;p.target.selected=false;if(game.selected==p.target)game.selected=null;spawnBurst(p.target.x,p.target.y,true);}}
        if(!p.alive||p.life<=0.0||p.x<0.0||p.x>960.0||p.y<70.0||p.y>710.0)game.projectiles.remove(i);
    }
}

function updateParticles(dt) {
    for(local i=game.particles.len()-1;i>=0;i-=1) {
        local p=game.particles[i];p.life-=dt;p.x+=p.vx*dt;p.y+=p.vy*dt;
        local drag=p.smoke?0.94:0.86;p.vx*=drag;p.vy=p.vy*drag+(p.smoke?-8.0:25.0)*dt;
        if(p.life<=0.0)game.particles.remove(i);
    }
}

function updateCombat(dt) {
    if (game.outcome != "") return;
    local assault = game.mind.state("frontier.ai") == "assault";
    foreach (u in game.units) if (u.alive) {
        u.cooldown = u.cooldown-dt > 0.0 ? u.cooldown-dt : 0.0;
        local enemy = nearestEnemy(u);
        local shouldMove=true;
        if (enemy != null) {
            local d=distance(u.x,u.y,enemy.x,enemy.y);
            local dx=enemy.x-u.x,dy=enemy.y-u.y;
            if(d>0.001){u.aimX=dx/d;u.aimY=dy/d;}
            // Once a target enters weapon range the current order is suspended,
            // not discarded.  The unit resumes it when no target is in range.
            if (d <= u.range) {
                shouldMove=false;
                if(u.cooldown<=0.0){fireProjectile(u,enemy);u.cooldown=u.kind=="tank"?1.4:0.8;}
            } else if (u.faction == game.frontier && assault && d < 280.0) {
                u.tx=enemy.x;u.ty=enemy.y;
            }
        }
        if(shouldMove)moveUnit(u,dt);
    }
    separateUnits();
}

function nearestPoint(faction, wantEnemyOwned) {
    local ax=0.0, ay=0.0, n=0;
    foreach (u in game.units) if (u.alive && u.faction == faction) { ax+=u.x; ay+=u.y; n+=1; }
    if (n == 0) return null;
    ax = ax / n; ay = ay / n;
    local best=null; local bestD=100000.0;
    foreach (p in game.points) {
        local takeIt = wantEnemyOwned ? (p.owner != faction) : (p.owner == faction);
        if (!takeIt) continue;
        local d=distance(ax,ay,p.x,p.y);
        if (d < bestD) { best=p; bestD=d; }
    }
    return best;
}

function activeTasks(owner) {
    local n=0;
    for (local i=0;i<game.factory.taskCount();i+=1) {
        local task=game.factory.taskAt(i);
        local st=task.getState();
        if (task.getOwner()==owner && st!="completed" && st!="cancelled" && st!="failed") n+=1;
    }
    return n;
}

function queueEnemyUnit() {
    local living = livingCount(game.frontier);
    local pending = activeTasks(game.enemyBase);
    if (living + pending >= 4) return;
    if (pending >= game.factory.slotCount(game.enemyBase)) return;
    local kind = living < 2 ? "tank" : "infantry";
    local cost = kind == "tank" ? 160.0 : 70.0;
    if (game.enemyMoney < cost) return;
    local duration = kind == "tank" ? 8.0 : 4.0;
    local enqueue = game.factory.enqueue(game.enemyBase, "build_unit", kind, "{}", duration, 8);
    if (!enqueue.ok) return;
    game.enemyMoney -= cost;
}

function updateEnemyAI(dt) {
    game.aiTimer += dt; if(game.aiTimer<1.0)return; game.aiTimer=0.0;
    if (game.outcome != "") return;
    foreach (u in game.units) if(u.alive && game.sensor != null) {
        local sensingUpdate = game.sensor.upsert(u.id,u.x,u.y,u.faction,"unit,"+u.kind,"all");
        if (!sensingUpdate.ok) { game.message = sensingUpdate.status.summary; return; }
    }
    local crownN = livingCount(game.crown);
    local frontierN = livingCount(game.frontier);
    local crownMines=0; foreach(p in game.points) if(p.owner==game.crown) crownMines+=1;
    local forceDelta = frontierN - crownN;
    // Assault is gated: outnumbered openings always held/raided. choose() is
    // deterministic, so a 0.60 assault score previously won every first tick.
    local assaultUrge = 0.05;
    if (crownN > 0 && frontierN >= 3 && forceDelta >= 1 && game.time > 15.0) {
        assaultUrge = clamp(0.40 + forceDelta * 0.18, 0.40, 0.90);
    }
    local captureUrge = (crownMines > 0 && frontierN >= 2) ? 0.70 : 0.15;
    local holdUrge = forceDelta < 0 ? 0.78 : 0.28;
    local action=game.mind.choose("assault="+assaultUrge+":1;capture="+captureUrge+":1;hold="+holdUrge+":1");
    if(action=="assault" && !game.rebelled && frontierN >= 3 && forceDelta >= 1 && game.time > 15.0) {
        local triggerResult = game.mind.trigger("frontier.ai", "base_exposed");
        if (!triggerResult.ok) { game.message = triggerResult.status.summary; return; }
    } else {
        local holdResult = game.mind.trigger("frontier.ai", "hold_mines");
        if (!holdResult.ok) { game.message = holdResult.status.summary; return; }
    }
    local aiState = game.mind.state("frontier.ai");
    local captureTarget = nearestPoint(game.frontier, true);
    local ownedMine = nearestPoint(game.frontier, false);
    local assigned = 0;
    foreach (u in game.units) if(u.alive && u.faction==game.frontier) {
        if (aiState=="assault") {
            local enemy=nearestEnemy(u);
            if(enemy!=null){u.tx=enemy.x;u.ty=enemy.y;}
        } else {
            assigned += 1;
            if (action == "hold" || (assigned == 1 && ownedMine != null)) {
                if (ownedMine != null) { u.tx = ownedMine.x; u.ty = ownedMine.y; }
            } else if (captureTarget != null) {
                u.tx = captureTarget.x; u.ty = captureTarget.y;
            }
        }
    }
    if (frontierN < 3) queueEnemyUnit();
}

function updateOutcome() {
    if (game.outcome != "" || game.rebelled) return;
    local crownN = livingCount(game.crown);
    local frontierN = livingCount(game.frontier);
    if (frontierN == 0) {
        game.outcome = "victory";
        game.message = "胜利：前线部队已被消灭。按 R 重置。";
    } else if (crownN == 0) {
        game.outcome = "defeat";
        game.message = "战败：北方直属部队全灭。按 R 重置。";
    }
}

function updateCapture(dt) {
    foreach(p in game.points) {
        local crownPower=0.0,frontierPower=0.0;
        foreach(u in game.units) if(u.alive && distance(u.x,u.y,p.x,p.y)<=48.0) {
            local power=u.kind=="tank"?1.35:1.0;
            if(u.faction==game.crown)crownPower+=power;else frontierPower+=power;
        }
        p.contested=crownPower>0.0&&frontierPower>0.0;
        if(p.contested){p.capturing="";continue;}
        local faction=crownPower>0.0?game.crown:(frontierPower>0.0?game.frontier:"");
        local power=crownPower>0.0?crownPower:frontierPower;
        if(faction==""){p.capture=clamp(p.capture-dt*8.0,0.0,100.0);if(p.capture<=0.0)p.capturing="";continue;}
        if(faction==p.owner){p.capture=clamp(p.capture-dt*22.0,0.0,100.0);if(p.capture<=0.0)p.capturing="";continue;}
        if(p.capturing!=faction){p.capturing=faction;p.capture=0.0;}
        p.capture+=dt*18.0*power;
        if(p.capture>=100.0){p.owner=faction;p.capture=0.0;p.capturing="";p.contested=false;
            if(faction!=game.crown)p.governor="";
            game.message=(faction==game.crown?"我军":"敌军")+"占领了经济点 "+p.id+"。";
            game.stream.append("00000000-0000-4000-8000-000000000002", "economy_point_captured",
                               faction, p.id, "", "capture", 1, 1, "{}");
        }
    }
}

function pointYield(p) {
    local value=18.0;
    if(p.owner==game.crown && p.governor==game.general && game.governor && !game.rebelled) {
        value*=1.0+game.generalStats.getFinal("administration",0.0)*0.004;
    }
    return value;
}

function updateEconomy(dt) {
    game.incomeTimer += dt; if(game.incomeTimer<2.0)return; game.incomeTimer=0.0;
    local crownIncome=0.0,frontierIncome=0.0;
    foreach(p in game.points){if(p.owner==game.crown)crownIncome+=pointYield(p);else frontierIncome+=pointYield(p);}
    game.money+=crownIncome;game.enemyMoney+=frontierIncome;
}

function buildPanel() {
    ui.setTheme("dark"); ui.beginBuild(); ui.beginWindow("COMMANDERY", "root");
    ui.text("RTS + GENERAL ORGANIZATION", "title");
    ui.text("", "resources"); ui.text("", "general"); ui.text("", "authority");
    ui.text("", "production"); ui.text("", "ai");ui.separator("intel_sep");
    ui.text("BASES & ECONOMY INTEL", "intel_title");
    ui.text("", "base_north");ui.text("", "base_enemy");
    ui.text("", "mine_north");ui.text("", "mine_center");ui.text("", "mine_east");
    ui.text("", "hover");ui.separator("sep");
    ui.button("Train Infantry [1]", "infantry"); ui.button("Build Tank [2]", "tank");
    ui.button("Appoint / Dismiss Governor [G]", "governor");
    ui.button("Pay Salary [P]", "pay"); ui.button("Withhold Salary [U]", "unpaid");
    ui.button("Reset Scenario [R]", "reset"); ui.separator("sep2");
    ui.text("", "message");
    ui.text("Left click unit / Right click ground", "help"); ui.end();
    ui.mountBuildAs("command"); ui.select("command"); ui.setHostOverlay(true);
    ui.setHostPos(970.0,20.0,0.0,0.0);
    ui.setHostSize(290.0,675.0);panelReady=true;
}

function ownerName(owner){return owner==game.crown?"CROWN":"FRONTIER";}

function pointIntel(name,p) {
    local state=p.contested?"CONTESTED":(p.capture>0.0?"CAPTURE "+p.capture.tointeger()+"%":"SECURE");
    local admin=(p.owner==game.crown&&p.governor==game.general&&game.governor&&!game.rebelled)?" +ADM":"";
    return name+": "+ownerName(p.owner)+" | "+state+" | +"+pointYield(p).tointeger()+admin;
}

function refreshPanel() {
    if(!panelReady||game==null)return; ui.select("command");
    local loyalty=game.generalStats.getFinal("loyalty",0.0).tointeger();
    local support=(social.relation("officers.north",game.general,"support",0.0)*100).tointeger();
    local speed=game.baseStats.getFinal("production_speed",1.0);
    local owned=0,income=0.0;foreach(p in game.points)if(p.owner==game.crown){owned+=1;income+=pointYield(p);}
    ui.setText("resources","Treasury: "+game.money.tointeger()+"   Mines "+owned+"   +"+income.tointeger()+" / 2s");
    ui.setText("general","General Arden  Loyalty "+loyalty+"  Support "+support+"%");
    ui.setText("authority","Governor: "+(game.governor?"YES":"NO")+
        "   Rebellion: "+(game.rebelled?"YES":"no")+
        "   "+(game.outcome=="victory"?"VICTORY":(game.outcome=="defeat"?"DEFEAT":"IN BATTLE")));
    ui.setText("production","Production speed x"+format("%.2f",speed)+
        "   Tasks "+game.factory.taskCount());
    ui.setText("ai","Enemy AI: "+game.mind.state("frontier.ai")+
        "   Crown "+livingCount(game.crown)+"  Frontier "+livingCount(game.frontier));
    ui.setText("base_north","North Base: "+ownerName(game.rebelled?game.frontier:game.crown)+
        " | Governor: "+(game.governor&&!game.rebelled?"Arden ADM76":"NONE"));
    ui.setText("base_enemy","Frontier Base: FRONTIER | Boros");
    ui.setText("mine_north",pointIntel("North Mine",game.points[0]));
    ui.setText("mine_center",pointIntel("Bridge Mine",game.points[1]));
    ui.setText("mine_east",pointIntel("East Mine",game.points[2]));
    local mx=mouse.getX(),my=mouse.getY(),hover="Hover a base or mine for details";
    foreach(p in game.points)if(distance(mx,my,p.x,p.y)<34.0)hover=p.id+"  income "+pointYield(p).tointeger()+" / 2s";
    if(distance(mx,my,115.0,245.0)<70.0)hover="North Base | Governor controls production + mine";
    if(distance(mx,my,875.0,245.0)<70.0)hover="Frontier Base | Commander General Boros";
    ui.setText("hover",hover);
    ui.setText("message",game.message);
}

function handlePanel() {
    local c=ui.consumeClick();while(c!=""){
        if(c=="command/infantry")queueUnit("infantry"); else if(c=="command/tank")queueUnit("tank");
        else if(c=="command/governor")toggleGovernor(); else if(c=="command/pay")paySalary();
        else if(c=="command/unpaid")withholdSalary(); else if(c=="command/reset")resetGame();
        c=ui.consumeClick();
    }
}

function eve_init() { gfx.setBackgroundColor(0.035,0.055,0.07,1.0); if(!panelReady)buildPanel();resetGame(); }

function eve_update(dt) {
    game.time+=dt; handlePanel();
    if(keyPressed("1"))queueUnit("infantry"); if(keyPressed("2"))queueUnit("tank");
    if(keyPressed("g")||keyPressed("G"))toggleGovernor();
    if(keyPressed("p")||keyPressed("P"))paySalary();
    if(keyPressed("u")||keyPressed("U"))withholdSalary();
    if(keyPressed("r")||keyPressed("R"))resetGame();
    handleBattlefieldInput();
    updateProduction(dt); updateEnemyAI(dt); updateCombat(dt);updateCapture(dt);
    updateProjectiles(dt);updateParticles(dt);updateEconomy(dt);
    evaluateRebellion(); updateOutcome(); refreshPanel();
}

function drawBase(x,y,faction,hp) {
    local blue=faction==game.crown;
    gfx.drawSolidRect(x-50.0,y-42.0,100.0,84.0,blue?0.18:0.72,blue?0.48:0.22,blue?0.82:0.18,1.0);
    gfx.drawSolidRect(x-28.0,y-62.0,56.0,26.0,blue?0.32:0.45,blue?0.38:0.20,blue?0.55:0.16,1.0);
    gfx.drawSolidRect(x-18.0,y-18.0,36.0,28.0,0.90,0.78,0.28,1.0);
}
function drawUnit(u){if(!u.alive)return;local blue=u.faction==game.crown;local s=u.kind=="tank"?30.0:18.0;
    // Health bar is submitted first because the solid batch draws in reverse order.
    gfx.drawSolidRect(u.x-s/2.0,u.y-s/2.0-10.0,s*clamp(u.hp/u.maxHp,0.0,1.0),4.0,0.25,0.9,0.35,1.0);
    gfx.drawSolidRect(u.x-s/2.0,u.y-s/2.0-10.0,s,4.0,0.12,0.12,0.14,1.0);
    if(u.kind=="tank") {
        // Five barrel segments follow the independently tracked turret direction.
        for(local k=5;k>=1;k-=1)gfx.drawSolidRect(u.x+u.aimX*k*4.0-2.5,u.y+u.aimY*k*4.0-2.5,5.0,5.0,0.10,0.12,0.11,1.0);
        gfx.drawSolidRect(u.x-7.0,u.y-7.0,14.0,14.0,blue?0.24:0.72,blue?0.50:0.23,blue?0.34:0.16,1.0);
        // Hull, front glacis and two dark tracks make the silhouette readable.
        gfx.drawSolidRect(u.x-11.0+u.aimX*3.0,u.y-8.0+u.aimY*3.0,22.0,16.0,blue?0.20:0.66,blue?0.42:0.20,blue?0.29:0.14,1.0);
        gfx.drawSolidRect(u.x-15.0,u.y-12.0,30.0,7.0,0.09,0.11,0.10,1.0);
        gfx.drawSolidRect(u.x-15.0,u.y+5.0,30.0,7.0,0.09,0.11,0.10,1.0);
    } else {
        // Rifle/muzzle direction, helmet, torso and legs.
        for(local k=4;k>=1;k-=1)gfx.drawSolidRect(u.x+u.aimX*k*3.0-1.5,u.y+u.aimY*k*3.0-1.5,3.0,3.0,0.12,0.11,0.09,1.0);
        gfx.drawSolidRect(u.x-4.0,u.y-8.0,8.0,8.0,0.72,0.56,0.38,1.0);
        gfx.drawSolidRect(u.x-6.0,u.y-1.0,12.0,11.0,blue?0.18:0.64,blue?0.46:0.20,blue?0.30:0.15,1.0);
        gfx.drawSolidRect(u.x-6.0,u.y+9.0,4.0,7.0,0.12,0.14,0.13,1.0);
        gfx.drawSolidRect(u.x+2.0,u.y+9.0,4.0,7.0,0.12,0.14,0.13,1.0);
    }
    if(u.selected)gfx.drawSolidRect(u.x-s/2.0-4.0,u.y-s/2.0-4.0,s+8.0,s+8.0,0.95,0.85,0.25,0.38);
}
function drawProjectile(p){
    local size=p.heavy?8.0:3.0;
    gfx.drawSolidRect(p.x-size/2.0,p.y-size/2.0,size,size,p.heavy?1.0:1.0,p.heavy?0.58:0.92,0.18,1.0);
    gfx.drawSolidRect(p.x-p.vx*0.025-2.0,p.y-p.vy*0.025-2.0,4.0,4.0,1.0,0.38,0.08,0.65);
}
function drawParticle(p){
    local t=clamp(p.life/p.maxLife,0.0,1.0),s=p.size*(0.45+0.8*(1.0-t));
    if(p.smoke)gfx.drawSolidRect(p.x-s/2.0,p.y-s/2.0,s,s,0.28,0.30,0.30,t*0.55);
    else gfx.drawSolidRect(p.x-s/2.0,p.y-s/2.0,s,s,1.0,0.28+0.55*t,0.05,t);
}
function eve_render(){gfx.clear();
    // Solid primitives currently preserve reverse submission order inside the
    // batch, so foreground objects are submitted first and terrain last.
    if(selectionDrag.active){local x0=selectionDrag.sx<selectionDrag.x?selectionDrag.sx:selectionDrag.x;
        local y0=selectionDrag.sy<selectionDrag.y?selectionDrag.sy:selectionDrag.y;
        local w=selectionDrag.x-selectionDrag.sx,h=selectionDrag.y-selectionDrag.sy;
        if(w<0.0)w=-w;if(h<0.0)h=-h;
        // Never submit zero-sized solid rectangles: the current reverse-order
        // batch cannot safely rasterize that degenerate geometry.
        if(w*w+h*h>49.0){local rw=w<2.0?2.0:w,rh=h<2.0?2.0:h;
            gfx.drawSolidRect(x0,y0,rw,2.0,0.25,0.95,0.45,1.0);gfx.drawSolidRect(x0,y0+rh-2.0,rw,2.0,0.25,0.95,0.45,1.0);
            gfx.drawSolidRect(x0,y0,2.0,rh,0.25,0.95,0.45,1.0);gfx.drawSolidRect(x0+rw-2.0,y0,2.0,rh,0.25,0.95,0.45,1.0);}}
    foreach(p in game.particles)drawParticle(p);
    foreach(p in game.projectiles)drawProjectile(p);
    foreach(u in game.units)drawUnit(u);
    foreach(p in game.points){local blue=p.owner==game.crown;
        if(p.capture>0.0)gfx.drawSolidRect(p.x-24.0,p.y-26.0,48.0*(p.capture/100.0),5.0,p.capturing==game.crown?0.20:0.90,p.capturing==game.crown?0.65:0.22,0.18,1.0);
        if(p.contested)gfx.drawSolidRect(p.x-24.0,p.y-26.0,48.0,5.0,1.0,0.78,0.12,1.0);
        gfx.drawSolidRect(p.x-14.0,p.y-14.0,28.0,28.0,blue?0.20:0.78,0.72,blue?0.95:0.18,1.0);
    }
    drawBase(115.0,245.0,social.ownerOf(game.baseId),500.0);drawBase(875.0,245.0,game.frontier,500.0);
    gfx.drawSolidRect(465.0,340.0,80.0,48.0,0.42,0.38,0.28,1.0);
    gfx.drawSolidRect(20.0,340.0,920.0,48.0,0.34,0.30,0.22,1.0);
    gfx.drawSolidRect(465.0,80.0,80.0,600.0,0.08,0.28,0.44,1.0);
    gfx.drawSolidRect(20.0,80.0,920.0,600.0,0.12,0.24,0.16,1.0);
    ui.beginFrameAndRender();
}
