// KayKit interactive tactics showcase. Tactics owns turn order and board
// occupancy; this example owns player input, presentation, hit points, skills
// and lightweight enemy AI.

const BOARD_W = 12;
const BOARD_H = 5;
const CELL = 1.08;
const HERO_MOVE_TILES = 5;
const BATTLE = "00000000-0000-0000-0000-000000002000";
const HERO_SIDE = "00000000-0000-0000-0000-000000002001";
const ENEMY_SIDE = "00000000-0000-0000-0000-000000002002";

persist battle = null;
persist actors = [];
persist actorById = {};
persist tiles = [];
persist camera = null;
persist model3d = null;
persist modelCache = {};
persist anim = null;
persist damageFont = null;
persist vfxTexture = null;
persist vfxQuad = null;
persist vfxSprite = null;
persist vfxTime = -1.0;
persist vfxRow = 0;
persist floaters = [];
persist elapsed = 0.0;
persist battleTick = 0;
persist actionDelay = 0.7;
persist paused = false;
persist failed = "";
persist banner = "The battle begins";
persist selectedSkill = 0;
persist mouseWasDown = false;
persist hoverX = -1;
persist hoverZ = -1;
persist gameOver = false;

local HEROES = [
    { id="00000000-0000-0000-0000-000000002101", name="Alden", role="Knight", model="Knight.glb", x=1, z=0, hp=125,
      attacks=[{name="Shield Bash", range=1, damage=18, clip=19, row=0}, {name="Cross Slash", range=1, damage=24, clip=2, row=0}, {name="Lion's Charge", range=2, damage=30, clip=10, row=3}] },
    { id="00000000-0000-0000-0000-000000002102", name="Lyra", role="Mage", model="Mage.glb", x=0, z=1, hp=88,
      attacks=[{name="Arc Bolt", range=4, damage=17, clip=60, row=2}, {name="Rune Nova", range=3, damage=23, clip=61, row=2}, {name="Frost Lance", range=5, damage=27, clip=62, row=2}] },
    { id="00000000-0000-0000-0000-000000002103", name="Nyx", role="Rogue", model="Rogue.glb", x=1, z=3, hp=96,
      attacks=[{name="Quick Stab", range=1, damage=16, clip=3, row=0}, {name="Twin Fang", range=1, damage=22, clip=32, row=0}, {name="Smoke Knife", range=3, damage=20, clip=65, row=1}] },
    { id="00000000-0000-0000-0000-000000002104", name="Brom", role="Barbarian", model="Barbarian.glb", x=0, z=4, hp=142,
      attacks=[{name="Axe Chop", range=1, damage=21, clip=0, row=3}, {name="Whirlwind", range=1, damage=26, clip=9, row=3}, {name="Earthbreaker", range=2, damage=32, clip=8, row=1}] }
];

local ENEMIES = [
    { id="00000000-0000-0000-0000-000000002201", name="Bone Grunt", role="Minion", model="Skeleton_Minion.glb", x=10, z=0, hp=58, damage=12, range=1, row=3 },
    { id="00000000-0000-0000-0000-000000002202", name="Crypt Guard", role="Warrior", model="Skeleton_Warrior.glb", x=11, z=1, hp=92, damage=15, range=1, row=0 },
    { id="00000000-0000-0000-0000-000000002203", name="Grave Archer", role="Archer", model="Skeleton_Rogue.glb", x=10, z=2, hp=62, damage=13, range=4, row=1 },
    { id="00000000-0000-0000-0000-000000002204", name="Hex Adept", role="Mage", model="Skeleton_Mage.glb", x=11, z=3, hp=65, damage=16, range=4, row=2 },
    { id="00000000-0000-0000-0000-000000002205", name="Bone Cutthroat", role="Rogue", model="Skeleton_Rogue.glb", x=10, z=4, hp=70, damage=17, range=1, row=0 },
    { id="00000000-0000-0000-0000-000000002206", name="Ossuary Captain", role="Champion", model="Skeleton_Warrior.glb", x=11, z=4, hp=115, damage=20, range=1, row=3 }
];

function checked(result, label) {
    if (result.ok) return true;
    failed = label + ": " + result.status.summary;
    print("tactics demo: " + failed + "\n");
    return false;
}

function worldX(x) { return (x - (BOARD_W - 1) * 0.5) * CELL; }
function worldZ(z) { return (z - (BOARD_H - 1) * 0.5) * CELL; }
function distance(a, b) { return abs(a.x - b.x) + abs(a.z - b.z); }

function setActorPosition(actor, x, z) {
    actor.x = x; actor.z = z;
    actor.visualWorldX = worldX(x); actor.visualWorldZ = worldZ(z);
    foreach (r in actor.renderables) r.setPosition(worldX(x), 0.08, worldZ(z));
}

function quatMul(a, b) {
    return { x=a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
             y=a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
             z=a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
             w=a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z };
}

function quatRotate(q, x, y, z) {
    local p={x=x,y=y,z=z,w=0.0};
    local r=quatMul(quatMul(q,p), {x=-q.x,y=-q.y,z=-q.z,w=q.w});
    return [r.x,r.y,r.z];
}

function equipmentBone(modelName,index) {
    if (modelName == "Knight.glb") {
        if (index == 9) return "handslot.l";
        if (index == 11) return "handslot.r";
    } else if (modelName == "Mage.glb") {
        if (index == 7) return "handslot.l";
        if (index == 8) return "handslot.r";
    } else if (modelName == "Rogue.glb") {
        if (index == 6) return "handslot.l";
        if (index == 9) return "handslot.r";
    } else if (modelName == "Barbarian.glb") {
        if (index == 7) return "handslot.l";
        if (index == 8) return "handslot.r";
    }
    return "";
}

function attachmentBone(modelName,index) {
    local handBone=equipmentBone(modelName,index);
    if (handBone != "") return handBone;
    if (modelName == "Knight.glb") {
        if (index == 13) return "head";
        if (index == 14) return "chest";
    } else if (modelName == "Mage.glb") {
        if (index == 10) return "head";
        if (index == 11) return "chest";
    } else if (modelName == "Rogue.glb") {
        if (index == 11) return "chest";
    } else if (modelName == "Barbarian.glb") {
        if (index == 11) return "head";
        if (index == 12) return "chest";
    }
    return "";
}

function keepEquipmentMesh(modelName,index) {
    if (index < 6) return true;
    local bone=equipmentBone(modelName,index);
    if (bone != "") return true;
    if (modelName == "Knight.glb") return index == 13 || index == 14;
    if (modelName == "Mage.glb") return index == 10 || index == 11;
    if (modelName == "Rogue.glb") return index == 11;
    if (modelName == "Barbarian.glb") return index == 11 || index == 12;
    return true;
}

function updateEquipment(actor, pose) {
    local actorYaw=actor.hero ? 1.5707963 : -1.5707963;
    local aq={x=0.0,y=sin(actorYaw*0.5),z=0.0,w=cos(actorYaw*0.5)};
    foreach (item in actor.attachments) {
        local current={x=pose.getWorldRotationX(item.bone), y=pose.getWorldRotationY(item.bone),
                       z=pose.getWorldRotationZ(item.bone), w=pose.getWorldRotationW(item.bone)};
        local corrected=quatMul(current, {x=item.cqx,y=item.cqy,z=item.cqz,w=item.cqw});
        local delta=quatMul(corrected, {x=-item.bqx,y=-item.bqy,z=-item.bqz,w=item.bqw});
        local rotatedBind=quatRotate(delta,item.bpx,item.bpy,item.bpz);
        local dx=pose.getWorldPositionX(item.bone)-rotatedBind[0];
        local dy=pose.getWorldPositionY(item.bone)-rotatedBind[1];
        local dz=pose.getWorldPositionZ(item.bone)-rotatedBind[2];
        local cy=cos(actorYaw), sy=sin(actorYaw);
        item.renderable.setPosition(actor.visualWorldX+0.62*(cy*dx+sy*dz),
                                    0.08+0.62*dy,
                                    actor.visualWorldZ+0.62*(-sy*dx+cy*dz));

        local q=quatMul(aq,delta);
        local r02=2.0*(q.x*q.z+q.w*q.y);
        local r22=1.0-2.0*(q.x*q.x+q.y*q.y);
        local r10=2.0*(q.x*q.y+q.w*q.z);
        local r11=1.0-2.0*(q.x*q.x+q.z*q.z);
        local r12=2.0*(q.y*q.z-q.w*q.x);
        local clamped=(-r12 < -1.0) ? -1.0 : ((-r12 > 1.0) ? 1.0 : -r12);
        item.renderable.setRotation(atan2(r02,r22),asin(clamped),atan2(r10,r11));
    }
}

function boneJson(actor,pose,bone) {
    local qx=pose.getWorldRotationX(bone), qy=pose.getWorldRotationY(bone);
    local qz=pose.getWorldRotationZ(bone), qw=pose.getWorldRotationW(bone);
    local r02=2.0*(qx*qz+qw*qy), r22=1.0-2.0*(qx*qx+qy*qy);
    local r10=2.0*(qx*qy+qw*qz), r11=1.0-2.0*(qx*qx+qz*qz);
    local r12=2.0*(qy*qz-qw*qx);
    local clamped=(-r12 < -1.0) ? -1.0 : ((-r12 > 1.0) ? 1.0 : -r12);
    local radToDeg=57.2957795;
    return "{\"index\":"+bone+",\"name\":\""+actor.skeleton.getBoneName(bone)+
           "\",\"parent\":"+actor.skeleton.getParent(bone)+
           ",\"bindLocal\":{\"position\":["+actor.skeleton.getBindPositionX(bone)+","+
           actor.skeleton.getBindPositionY(bone)+","+actor.skeleton.getBindPositionZ(bone)+
           "],\"rotationQuaternion\":["+actor.skeleton.getBindRotationX(bone)+","+
           actor.skeleton.getBindRotationY(bone)+","+actor.skeleton.getBindRotationZ(bone)+","+
           actor.skeleton.getBindRotationW(bone)+"]},\"currentWorld\":{\"position\":["+
           pose.getWorldPositionX(bone)+","+pose.getWorldPositionY(bone)+","+pose.getWorldPositionZ(bone)+
           "],\"rotationQuaternion\":["+qx+","+qy+","+qz+","+qw+
           "],\"rotationEulerDegrees\":["+(atan2(r02,r22)*radToDeg)+","+
           (asin(clamped)*radToDeg)+","+(atan2(r10,r11)*radToDeg)+"]}}";
}

// MCP inspection surface. It returns an owning JSON snapshot and never retains
// skeleton/pose pointers across calls. Empty bone selects the full hierarchy.
eve_mcp_skeleton_inspect <- function(actorName,boneName) {
    local found=null;
    foreach (actor in actors) if (actor.name == actorName) { found=actor; break; }
    if (found == null) return "{\"error\":\"actor not found\"}";
    local pose=found.player.getPose(); pose.computeWorld(found.skeleton);
    local out="{\"actor\":\""+found.name+"\",\"boneCount\":"+found.skeleton.getBoneCount()+",\"bones\":[";
    local first=true;
    for (local i=0;i<found.skeleton.getBoneCount();i+=1) {
        if (boneName != "" && found.skeleton.getBoneName(i) != boneName) continue;
        if (!first) out+=","; first=false; out+=boneJson(found,pose,i);
    }
    return out+"]}";
};

function loadClip(source, skeleton, index, looped) {
    local clip = anim.newClipFromModel(source, skeleton, index);
    clip.setLoop(looped);
    return clip;
}

function cachedModel(path) {
    if (!(path in modelCache)) modelCache[path] <- model3d.newModelDataFromFile(path);
    return modelCache[path];
}

function makeVisual(spec, hero) {
    local folder = hero ? "assets/kaykit/adventurers/" : "assets/kaykit/skeletons/";
    local model = cachedModel(folder + spec.model);
    local skeleton = anim.newSkeletonFromModel(model);
    local bindPose = anim.newPose(skeleton.getBoneCount());
    skeleton.applyBindPose(bindPose); bindPose.computeWorld(skeleton);
    local renderables = [];
    local skins = [];
    local attachments = [];
    for (local i = 0; i < model.getMeshCount(); i += 1) {
        // Knight.glb contains an equipment catalogue, not one loadout: an
        // offhand sword, four shield variants, a 1H sword and a 2H sword.
        // Render one authored loadout only. The selected meshes already carry
        // KayKit's handslot transforms, so sword and round shield sit at the
        // intended grip and angle instead of overlapping under the shield.
        if (hero && !keepEquipmentMesh(spec.model,i)) continue;
        local renderable = model3d.createRenderable(gfx, model, i);
        renderable.setScale(0.62, 0.62, 0.62);
        renderable.setYaw(hero ? 1.5707963 : -1.5707963);
        renderables.push(renderable);
        if (model.hasBones(i)) {
            local skin = anim.newSkinFromModel(model, i, skeleton);
            local mesh = renderable.getMesh();
            if (!skin.bindGpuMesh(gfx, mesh)) {
                failed = "GPU skin binding failed for " + spec.model + " mesh " + i;
                print("tactics demo: " + failed + "\n");
            }
            skins.push({ skin=skin, mesh=mesh });
        } else if (hero && attachmentBone(spec.model,i) != "") {
            local bone=skeleton.findBone(attachmentBone(spec.model,i));
            // Lyra carries the open book in her left hand. Remove the asset's
            // quarter-turn roll, then cant it slightly inward toward her;
            // do not reverse the page facing by 180 degrees.
            local correction=(spec.model == "Mage.glb" && i == 7)
                ? {x=-0.43045933,y=0.43045933,z=-0.56098553,w=0.56098553}
                : {x=0.0,y=0.0,z=0.0,w=1.0};
            attachments.push({renderable=renderable,bone=bone,
                bpx=bindPose.getWorldPositionX(bone),bpy=bindPose.getWorldPositionY(bone),bpz=bindPose.getWorldPositionZ(bone),
                bqx=bindPose.getWorldRotationX(bone),bqy=bindPose.getWorldRotationY(bone),
                bqz=bindPose.getWorldRotationZ(bone),bqw=bindPose.getWorldRotationW(bone),
                cqx=correction.x,cqy=correction.y,cqz=correction.z,cqw=correction.w});
        }
    }

    local combat = hero ? model : cachedModel("assets/kaykit/adventurers/Knight.glb");
    local general = hero ? model : cachedModel("assets/kaykit/skeletons/Rig_Medium_General.glb");
    local movement = hero ? model : cachedModel("assets/kaykit/skeletons/Rig_Medium_MovementBasic.glb");
    local clips = { idle=loadClip(general, skeleton, hero ? 36 : 6, true), walk=loadClip(movement, skeleton, hero ? 72 : 8, true),
                    hit=loadClip(general, skeleton, hero ? 34 : 4, false), attacks={} };
    if (hero) {
        foreach (skill in spec.attacks) clips.attacks[skill.clip] <- loadClip(combat, skeleton, skill.clip, false);
    } else {
        clips.attacks[2] <- loadClip(combat, skeleton, 2, false);
    }
    local player = anim.newPlayer(skeleton);
    player.play(clips.idle);
    player.setLoop(true);
    return { model=model, skeleton=skeleton, renderables=renderables, skins=skins,
             bindPose=bindPose, attachments=attachments, player=player, clips=clips };
}

function addActor(spec, hero) {
    local visual = makeVisual(spec, hero);
    local actor = { id=spec.id, name=spec.name, role=spec.role, hero=hero, hp=spec.hp, maxHp=spec.hp,
                    x=spec.x, z=spec.z, renderables=visual.renderables, skins=visual.skins,
                    bindPose=visual.bindPose, attachments=visual.attachments, skeleton=visual.skeleton, player=visual.player,
                    clips=visual.clips, state="idle", oneShot=0.0,
                    visualWorldX=worldX(spec.x), visualWorldZ=worldZ(spec.z),
                    moving=false, fromX=spec.x, fromZ=spec.z, toX=spec.x, toZ=spec.z, moveTime=0.0,
                    movePath=null, moveIndex=0,
                    skillCursor=0, attacks=hero ? spec.attacks : null,
                    damage=hero ? 0 : spec.damage, range=hero ? 0 : spec.range, row=hero ? 0 : spec.row };
    setActorPosition(actor, spec.x, spec.z);
    actors.push(actor); actorById[actor.id] <- actor;
    checked(battle.addUnit(actor.id, hero ? HERO_SIDE : ENEMY_SIDE, "tactics:unit", actor.x, actor.z, 0,
                           1, hero ? HERO_MOVE_TILES*100 : 110, 1, hero ? 20 : 12), "add " + actor.name);
}

function playState(actor, state, attackClip) {
    local clip = actor.clips.idle;
    local looped = true;
    if (state == "walk") clip = actor.clips.walk;
    else if (state == "hit") { clip = actor.clips.hit; looped = false; }
    else if (state == "attack") { clip = actor.clips.attacks[attackClip]; looped = false; }
    actor.player.crossFade(clip, 0.12);
    actor.player.setLoop(looped);
    actor.state = state;
    actor.oneShot = looped ? 0.0 : clip.getDuration();
}

function aliveEnemies(actor) {
    local result = [];
    foreach (other in actors) if (other.hp > 0 && other.hero != actor.hero) result.push(other);
    return result;
}

function nearestEnemy(actor) {
    local candidates = aliveEnemies(actor);
    if (candidates.len() == 0) return null;
    local best = candidates[0];
    foreach (candidate in candidates) if (distance(actor, candidate) < distance(actor, best)) best = candidate;
    return best;
}

function occupied(x, z) {
    foreach (actor in actors) if (actor.hp > 0 && actor.x == x && actor.z == z) return true;
    return false;
}

function actorAt(x, z) {
    foreach (actor in actors) if (actor.hp > 0 && actor.x == x && actor.z == z) return actor;
    return null;
}

function battleIsActing() {
    if (battle == null || battle.isStale()) return false;
    local status = battle.status();
    if (!status.ok || status.value != "running") return false;
    local phase = battle.phase();
    return phase.ok && phase.value == "acting";
}

function finishTurn(actor) {
    if (battleIsActing()) checked(battle.wait(actor.id), "finish " + actor.name);
}

function beginMove(actor, target) {
    local choices=[ [actor.x+1,actor.z], [actor.x-1,actor.z], [actor.x,actor.z+1], [actor.x,actor.z-1] ];
    local nx=actor.x, nz=actor.z, best=1000000;
    foreach (cell in choices) {
        if (cell[0] < 0 || cell[0] >= BOARD_W || cell[1] < 0 || cell[1] >= BOARD_H || occupied(cell[0],cell[1])) continue;
        local score=abs(target.x-cell[0])+abs(target.z-cell[1]);
        if (score < best) { best=score; nx=cell[0]; nz=cell[1]; }
    }
    if (best == 1000000) return false;
    local moved = battle.move(actor.id, nx, nz, 0);
    if (!moved.ok) return false;
    actor.fromX=actor.x; actor.fromZ=actor.z; actor.toX=nx; actor.toZ=nz; actor.moveTime=0.0; actor.moving=true;
    actor.x=nx; actor.z=nz; playState(actor, "walk", 0);
    banner = actor.name + " advances";
    return true;
}

function beginMoveTo(actor, nx, nz) {
    if (nx < 0 || nx >= BOARD_W || nz < 0 || nz >= BOARD_H || occupied(nx, nz)) return false;
    if (abs(nx - actor.x) + abs(nz - actor.z) > HERO_MOVE_TILES) return false;
    local moved = battle.move(actor.id, nx, nz, 0);
    if (!moved.ok) { banner = moved.status.summary; return false; }
    actor.movePath=moved.value.path; actor.moveIndex=1;
    actor.fromX=actor.x; actor.fromZ=actor.z;
    actor.toX=actor.movePath[1].x; actor.toZ=actor.movePath[1].y;
    actor.moveTime=0.0; actor.moving=true; actor.x=nx; actor.z=nz;
    playState(actor, "walk", 0); banner = actor.name + " moves";
    return true;
}

function spawnImpact(target, row, damage) {
    vfxRow = row; vfxTime = 0.0; vfxSprite.setVisible(true);
    // Approximate isometric projection for the fixed showcase camera.
    vfxSprite.setPosition(655.0 + worldX(target.x) * 52.0 - worldZ(target.z) * 13.0,
                          355.0 + worldZ(target.z) * 43.0 + worldX(target.x) * 8.0);
    floaters.push({ text="-" + damage, x=650.0 + worldX(target.x) * 52.0, y=315.0 + worldZ(target.z) * 43.0, life=1.05 });
}

function attack(attacker, target) {
    local skill = null;
    local damage = attacker.damage;
    local range = attacker.range;
    local row = attacker.row;
    local clipIndex = 2;
    if (attacker.hero) {
        skill = attacker.attacks[attacker.skillCursor % attacker.attacks.len()];
        attacker.skillCursor += 1; damage=skill.damage; range=skill.range; row=skill.row; clipIndex=skill.clip;
    }
    if (distance(attacker, target) > range) return false;
    playState(attacker, "attack", clipIndex);
    playState(target, "hit", 0);
    target.hp -= damage;
    if (target.hp < 0) target.hp = 0;
    spawnImpact(target, row, damage);
    banner = attacker.name + " uses " + (skill == null ? "Bone Strike" : skill.name) + " — " + damage + " damage";
    if (target.hp == 0) {
        checked(battle.defeatUnit(target.id), "defeat " + target.name);
        foreach (r in target.renderables) r.setTint(0.28, 0.3, 0.34, 1.0);
        banner += " (defeated " + target.name + ")";
    }
    return true;
}

function performTurn() {
    if (battle == null || battle.isStale()) return;
    local status = battle.status();
    if (!status.ok || status.value == "ended") return;
    local phase = battle.phase();
    if (!phase.ok) return;
    if (phase.value != "acting") { battleTick += 1; checked(battle.advance(battleTick, 1000000), "advance"); return; }
    local active = battle.activeUnit();
    if (!active.ok || !(active.value in actorById)) return;
    local actor = actorById[active.value];
    if (actor.hp <= 0) { checked(battle.wait(actor.id), "skip defeated"); return; }
    if (actor.hero) {
        local skill = actor.attacks[selectedSkill];
        banner = actor.name + "'s turn — " + skill.name + " selected";
        return;
    }
    local target = nearestEnemy(actor);
    if (target == null) return;
    if (!attack(actor, target)) beginMove(actor, target);
    finishTurn(actor);
}

function refreshBattleOutcome() {
    if (gameOver || battle == null || battle.isStale()) return;
    local status=battle.status();
    if (!status.ok || status.value != "ended") return;
    local heroesAlive=0, enemiesAlive=0;
    foreach (actor in actors) if (actor.hp > 0) {
        if (actor.hero) heroesAlive+=1; else enemiesAlive+=1;
    }
    gameOver=true;
    if (enemiesAlive == 0) banner="VICTORY — the skeleton host is defeated";
    else if (heroesAlive == 0) banner="DEFEAT — the adventurers have fallen";
    else banner="BATTLE ENDED";
}

function resetDemo() {
    if (battle != null && !battle.isStale()) battle.release();
    foreach (actor in actors) foreach (r in actor.renderables) r.setVisible(false);
    foreach (tile in tiles) tile.renderable.setVisible(false);
    if (vfxSprite != null) vfxSprite.setVisible(false);
    if (camera != null) camera.setActive(false);
    battle=null; actors=[]; actorById={}; tiles=[]; camera=null; anim=null;
    damageFont=null; vfxTexture=null; vfxQuad=null; vfxSprite=null;
    vfxTime=-1.0; floaters=[]; elapsed=0.0; battleTick=0; actionDelay=0.7;
    paused=false; failed=""; banner="The battle begins"; selectedSkill=0;
    mouseWasDown=false; hoverX=-1; hoverZ=-1; gameOver=false;
    eve_init();
}

function cellFromMouse() {
    // Pick the nearest projected tile centre. This matches the pixels the
    // player sees and avoids small platform-dependent differences between the
    // Vulkan unprojection ray and the shallow board plane.
    local ex=7.7, ey=8.2, ez=9.8;
    local fx=-ex, fy=-ey, fz=-ez;
    local fl=sqrt(fx*fx+fy*fy+fz*fz); fx/=fl; fy/=fl; fz/=fl;
    local rx=-fz, ry=0.0, rz=fx;
    local rl=sqrt(rx*rx+rz*rz); rx/=rl; rz/=rl;
    local ux=ry*fz-rz*fy, uy=rz*fx-rx*fz, uz=rx*fy-ry*fx;
    local width=gfx.getWidth().tofloat(), height=gfx.getHeight().tofloat();
    local tanHalf=tan(43.0*0.00872664626), aspect=width/height;
    local mx=mouse.getX(), my=mouse.getY();
    local best=null, bestD2=3600.0;
    foreach (tile in tiles) {
        local vx=worldX(tile.x)-ex, vy=-0.04-ey, vz=worldZ(tile.z)-ez;
        local depth=vx*fx+vy*fy+vz*fz;
        if (depth <= 0.0) continue;
        local ndcX=(vx*rx+vy*ry+vz*rz)/(depth*tanHalf*aspect);
        local ndcY=(vx*ux+vy*uy+vz*uz)/(depth*tanHalf);
        local sx=(ndcX+1.0)*0.5*width, sy=(1.0-ndcY)*0.5*height;
        local dx=mx-sx, dy=my-sy, d2=dx*dx+dy*dy;
        if (d2 < bestD2) { bestD2=d2; best=[tile.x,tile.z]; }
    }
    return best;
}

function activeHero() {
    if (!battleIsActing()) return null;
    local active = battle.activeUnit();
    if (!active.ok || !(active.value in actorById)) return null;
    local actor = actorById[active.value];
    return actor.hero && actor.hp > 0 ? actor : null;
}

function handlePlayerTurn() {
    local hero = activeHero();
    if (hero == null || paused) { mouseWasDown = mouse.isDown(1); return; }
    for (local i=0; i<3; i+=1) {
        local key=(i+1).tostring();
        if (key_just_pressed(key)) { selectedSkill=i; banner=hero.name + " selected " + hero.attacks[i].name; }
    }
    local cell=cellFromMouse(); hoverX=cell == null ? -1 : cell[0]; hoverZ=cell == null ? -1 : cell[1];
    local down=mouse.isDown(1); local clicked=down && !mouseWasDown; mouseWasDown=down;
    if (!clicked || cell == null) return;
    local target=actorAt(cell[0], cell[1]);
    if (target != null && target.hero != hero.hero) {
        hero.skillCursor=selectedSkill;
        if (attack(hero, target)) finishTurn(hero);
        else banner=hero.attacks[selectedSkill].name + " is out of range";
        return;
    }
    if (target == null && beginMoveTo(hero, cell[0], cell[1])) finishTurn(hero);
}

function updateHighlights() {
    local hero=activeHero();
    foreach (tile in tiles) {
        local shade=((tile.x+tile.z)%2==0) ? 0.48 : 0.37;
        local r=shade*0.65, g=shade*0.82, b=shade;
        if (hero != null) {
            local d=abs(tile.x-hero.x)+abs(tile.z-hero.z);
            local occupant=actorAt(tile.x, tile.z);
            if (occupant == null && d <= HERO_MOVE_TILES) { r=0.22; g=0.72; b=0.48; }
            else if (occupant != null && !occupant.hero && d <= hero.attacks[selectedSkill].range) {
                r=0.92; g=0.28; b=0.18;
            }
        }
        if (tile.x == hoverX && tile.z == hoverZ) { r=0.95; g=0.82; b=0.28; }
        tile.renderable.setTint(r, g, b, 1.0);
    }
}

function updateActors(dt) {
    foreach (actor in actors) {
        if (actor.moving) {
            actor.moveTime += dt;
            local segmentDuration=actor.movePath == null ? 0.52 : 0.22;
            local t = actor.moveTime / segmentDuration;
            if (t > 1.0) t = 1.0;
            local px = actor.fromX + (actor.toX - actor.fromX) * t;
            local pz = actor.fromZ + (actor.toZ - actor.fromZ) * t;
            actor.visualWorldX = worldX(px);
            actor.visualWorldZ = worldZ(pz);
            foreach (r in actor.renderables) r.setPosition(worldX(px), 0.08, worldZ(pz));
            if (t >= 1.0) {
                if (actor.movePath != null && actor.moveIndex+1 < actor.movePath.len()) {
                    actor.moveIndex+=1;
                    actor.fromX=actor.toX; actor.fromZ=actor.toZ;
                    actor.toX=actor.movePath[actor.moveIndex].x; actor.toZ=actor.movePath[actor.moveIndex].y;
                    actor.moveTime=0.0;
                } else {
                    actor.moving=false; actor.movePath=null; playState(actor, "idle", 0);
                }
            }
        }
        if (actor.oneShot > 0.0) {
            actor.oneShot -= dt;
            if (actor.oneShot <= 0.0 && actor.hp > 0) playState(actor, "idle", 0);
        }
        actor.player.update(dt);
        local pose = actor.player.getPose();
        pose.computeWorld(actor.skeleton);
        foreach (binding in actor.skins) binding.skin.updateGpuMesh(binding.mesh, pose);
        updateEquipment(actor,pose);
    }
}

function updateVfx(dt) {
    if (vfxTime >= 0.0) {
        vfxTime += dt;
        local frame = (vfxTime * 10.0).tointeger();
        if (frame > 3) frame = 3;
        vfxQuad.setViewport(frame * 256, vfxRow * 384, 256, 384);
        if (vfxTime > 0.42) { vfxTime=-1.0; vfxSprite.setVisible(false); }
    }
    for (local i = floaters.len() - 1; i >= 0; i -= 1) {
        floaters[i].life -= dt; floaters[i].y -= dt * 42.0;
        if (floaters[i].life <= 0.0) floaters.remove(i);
    }
}

function setupBoard() {
    local cube = gfx.newMeshCube(1.0);
    for (local z=0; z<BOARD_H; z+=1) for (local x=0; x<BOARD_W; x+=1) {
        checked(battle.addCell(x, z, 0, 100), "add cell");
        local tile=eve.Renderable3D(); tile.setMesh(cube);
        tile.setPosition(worldX(x), -0.12, worldZ(z)); tile.setScale(0.51, 0.08, 0.51);
        local shade=((x+z)%2==0) ? 0.48 : 0.37;
        tile.setTint(shade*0.65, shade*0.82, shade, 1.0);
        tiles.push({ renderable=tile, x=x, z=z });
    }
}

function setupUi() {
    local data=eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 28);
    damageFont=gfx.newFont(data, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]()/|-:+',.!?");
    vfxTexture=gfx.newTextureFromFile("assets/vfx/attack-vfx.png");
    vfxQuad=gfx.newQuad(0, 0, 256, 384);
    vfxSprite=gfx.newSprite2D(); vfxSprite.setTexture(vfxTexture); vfxSprite.setQuad(vfxQuad);
    vfxSprite.setSize(180.0, 220.0); vfxSprite.setAnchor(0.5, 0.65); vfxSprite.setBlend("additive");
    vfxSprite.setReceiveLight(false); vfxSprite.setLayer(50); vfxSprite.setVisible(false);
}

eve_init = function() {
    gfx.setBackgroundColor(0.035, 0.055, 0.075, 1.0);
    gfx.setDirectionalLight(-0.5, -1.0, -0.35, 1.35, 1.25, 1.05);
    model3d=eve.Model3D(); anim=eve.Animation();
    camera=eve.Camera3D(); camera.setEye(7.7, 8.2, 9.8); camera.setTarget(0.0, 0.0, 0.0);
    camera.setUp(0.0, 1.0, 0.0); camera.setFov(43.0); camera.setAmbient(0.42, 0.46, 0.52); camera.setActive(true);
    local created=eve.Tactics().newBattle(BATTLE, 20260828);
    if (!checked(created, "new battle")) return; battle=created.value;
    setupBoard(); checked(battle.addSide(HERO_SIDE), "add heroes"); checked(battle.addSide(ENEMY_SIDE), "add enemies");
    foreach (spec in HEROES) addActor(spec, true);
    foreach (spec in ENEMIES) addActor(spec, false);
    checked(battle.addEliminateObjective("kaykit:defeat-skeletons", HERO_SIDE, ENEMY_SIDE, true), "hero objective");
    checked(battle.addEliminateObjective("kaykit:defeat-adventurers", ENEMY_SIDE, HERO_SIDE, true), "enemy objective");
    checked(battle.start("initiative"), "start"); setupUi();
    print("tactics: 4 heroes, 6 enemy roles, 12x5 board; Space pauses\n");
};

eve_update = function(dt) {
    elapsed += dt;
    if (key_just_pressed("r") || key_just_pressed("R")) { resetDemo(); return; }
    if (key_just_pressed("space")) paused=!paused;
    updateActors(dt); updateVfx(dt); refreshBattleOutcome(); handlePlayerTurn(); updateHighlights();
    if (!paused && failed == "") { actionDelay -= dt; if (actionDelay <= 0.0) { performTurn(); actionDelay=0.72; } }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    gfx.renderSprites();
    gfx.setFont(damageFont);
    gfx.drawSolidRect(18.0, 16.0, 1244.0, 68.0, 0.02, 0.03, 0.05, 0.84);
    gfx.print("KAYKIT TACTICS  |  " + banner, 36.0, 31.0, 0.93, 0.9, 0.72, 1.0, 0.62);
    gfx.print(paused ? "PAUSED — SPACE TO RESUME" : "HERO: CLICK GREEN TO MOVE / RED TO ATTACK — 1 2 3 SKILLS", 36.0, 59.0, 0.55, 0.78, 0.96, 1.0, 0.42);
    local hy=105.0; local ey=105.0;
    foreach (actor in actors) {
        local line=actor.name + " [" + actor.role + "]  " + actor.hp + "/" + actor.maxHp;
        if (actor.hero) { gfx.print(line, 26.0, hy, 0.55, 0.82, 1.0, 1.0, 0.42); hy+=23.0; }
        else { gfx.print(line, 1010.0, ey, 1.0, 0.62, 0.48, 1.0, 0.36); ey+=21.0; }
    }
    foreach (f in floaters) {
        local alpha = f.life * 2.0;
        if (alpha > 1.0) alpha = 1.0;
        gfx.print(f.text, f.x, f.y, 1.0, 0.22, 0.12, alpha, 0.72);
    }
    if (gameOver) {
        gfx.drawSolidRect(350.0, 300.0, 580.0, 150.0, 0.015, 0.02, 0.035, 0.92);
        gfx.print(banner, 405.0, 340.0, 1.0, 0.84, 0.32, 1.0, 0.72);
        gfx.print("PRESS R TO RESTART", 505.0, 395.0, 0.65, 0.86, 1.0, 1.0, 0.52);
    }
    if (failed != "") gfx.print("ERROR: " + failed, 30.0, 720.0, 1.0, 0.25, 0.2, 1.0, 0.5);
};

eve_quit = function() { if (battle != null && !battle.isStale()) battle.release(); };
