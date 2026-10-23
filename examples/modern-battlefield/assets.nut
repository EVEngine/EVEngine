import { buildArtilleryPosition } from "./models/artillery-position.nut"
import { buildMachineGunPosition } from "./models/machine-gun-position.nut"
import { buildBunker } from "./models/bunker.nut"
import { buildMedicalBox } from "./models/medical-box.nut"
import { buildAmmunitionBox } from "./models/ammunition-box.nut"
import { buildConcretePost } from "./models/concrete-post.nut"
import { buildBarbedWire } from "./models/barbed-wire.nut"
import { buildHedgehog } from "./models/hedgehog.nut"
import { buildWoodenCrate } from "./models/wooden-crate.nut"
import { buildSandbags } from "./models/sandbags.nut"
import { buildMediumFighter } from "./models/medium-fighter.nut"
import { buildBomber } from "./models/bomber.nut"
import { buildAttackAircraft } from "./models/attack-aircraft.nut"
import { buildHelicopter } from "./models/helicopter.nut"
import { buildMachineGun } from "./models/machine-gun.nut"
import { buildArmoredCar } from "./models/armored-car.nut"
import { buildTankDestroyer } from "./models/tank-destroyer.nut"
import { buildSelfPropelledMortar } from "./models/self-propelled-mortar.nut"
import { buildSelfPropelledArtillery } from "./models/self-propelled-artillery.nut"
import { buildHeavyHowitzer } from "./models/heavy-howitzer.nut"
import { buildHowitzer } from "./models/howitzer.nut"
import { buildMortar } from "./models/mortar.nut"
import { buildAntiTankGun } from "./models/anti-tank-gun.nut"
import { newWorkshop, poseDetailed } from "./models/workshop.nut"
import { buildTank } from "./models/tank.nut"
// Original procedural assets. Metres; +Y up, +Z forward; radians.
// Renderables and meshes are retained by the library and each returned instance.
local cube=null, ball=null;
export function initAssets() {
    cube=gfx.newMeshCube(1.0); ball=gfx.newMeshSphere(10,6);
}
local function makeShape(x,y,z,sx,sy,sz,c,round=false,unlit=false) {
    local e=eve.Renderable3D(); e.setMesh(round?ball:cube);
    e.setPosition(x.tofloat(),y.tofloat(),z.tofloat());
    e.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());
    e.setTint(c[0].tofloat(),c[1].tofloat(),c[2].tofloat(),1.0);
    e.setReceiveLight(!unlit); e.setRoughness(0.88);
    return e;
}
export function shape(x,y,z,sx,sy,sz,c,round=false,unlit=false) {
    return makeShape(x,y,z,sx,sy,sz,c,round,unlit);
}
// Factory ids are documented in README.md. Unknown ids fail visibly.
local function makeBattlefieldModel(kind,x,y,z) {
    local a={kind=kind,x=x,y=y,z=z,yaw=0.0,turret=0.0,elevation=0.15,
        rotor=0.0,recoil=0.0,gunY=1.55,gunZ=0.0,barrel=2.6,
        parts=[],armed=false,air=false};
    if(kind=="anti-tank-gun") { local m=newWorkshop(a); buildAntiTankGun(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="mortar") { local m=newWorkshop(a); buildMortar(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="howitzer") { local m=newWorkshop(a); buildHowitzer(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="heavy-howitzer") { local m=newWorkshop(a); buildHeavyHowitzer(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="self-propelled-artillery") { local m=newWorkshop(a); buildSelfPropelledArtillery(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="self-propelled-mortar") { local m=newWorkshop(a); buildSelfPropelledMortar(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="tank-destroyer") { local m=newWorkshop(a); buildTankDestroyer(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="armored-car") { local m=newWorkshop(a); buildArmoredCar(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="machine-gun") { local m=newWorkshop(a); buildMachineGun(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="helicopter") { local m=newWorkshop(a); buildHelicopter(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="attack-aircraft") { local m=newWorkshop(a); buildAttackAircraft(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="bomber") { local m=newWorkshop(a); buildBomber(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="medium-fighter") { local m=newWorkshop(a); buildMediumFighter(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="tank") {
        local m=newWorkshop(a); buildTank(m,a); a.rig <- m.finishModel(); return a;
    }
    if(kind=="wooden-crate") { local m=newWorkshop(a); buildWoodenCrate(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="hedgehog") { local m=newWorkshop(a); buildHedgehog(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="barbed-wire") { local m=newWorkshop(a); buildBarbedWire(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="concrete-post") { local m=newWorkshop(a); buildConcretePost(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="ammunition-box") { local m=newWorkshop(a); buildAmmunitionBox(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="medical-box") { local m=newWorkshop(a); buildMedicalBox(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="bunker") { local m=newWorkshop(a); buildBunker(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="machine-gun-position") { local m=newWorkshop(a); buildMachineGunPosition(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="artillery-position") { local m=newWorkshop(a); buildArtilleryPosition(m,a); a.rig <- m.finishModel(); return a; }
    if(kind=="sandbags") { local m=newWorkshop(a); buildSandbags(m,a); a.rig <- m.finishModel(); return a; }
    throw "Unknown battlefield asset: "+kind;
}
// Canonical factory name avoids collisions with engine binding names in static analysis.
export function createBattlefieldModel(kind,x,y,z) { return makeBattlefieldModel(kind,x,y,z); }
// Compatibility entry point for the initial version of this example.
export function createAsset(kind,x,y,z) { return makeBattlefieldModel(kind,x,y,z); }
// Body yaw -> independent turret yaw -> elevating/recoiling barrel.
// No mesh regeneration, resource I/O or string-based engine lookup per frame.
export function poseAsset(a) {
    if("rig" in a) { poseDetailed(a); return; }
    foreach(p in a.parts) {
        local x=p.x,y=p.y,z=p.z, yaw=a.yaw, pitch=0.0, roll=p.roll;
        if(p.joint=="turret"||p.joint=="barrel") {
            yaw+=a.turret;
            if(p.joint=="barrel") {
                z-=a.recoil; y=a.gunY+z*sin(a.elevation);
                z=z*cos(a.elevation)+a.gunZ; pitch=-a.elevation;
            }
            local xx=x*cos(a.turret)+z*sin(a.turret);
            z=-x*sin(a.turret)+z*cos(a.turret); x=xx;
        } else if(p.joint=="rotor") { y=1.3; z=-0.2; yaw+=a.rotor; }
        else if(p.joint=="tail-rotor") { x=0.28; y=0.45; z=-1.9; pitch=a.rotor*1.7; }
        p.e.setPosition((a.x+x*cos(a.yaw)+z*sin(a.yaw)).tofloat(),(a.y+y).tofloat(),
            (a.z-x*sin(a.yaw)+z*cos(a.yaw)).tofloat());
        p.e.setRotation(yaw.tofloat(),pitch.tofloat(),roll.tofloat());
    }
}
