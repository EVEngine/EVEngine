// Civilisation rules. Geometry belongs to hexmap; occupancy and movement to tactics.
// City entities own population/buildings; economy owns all stock; WorkQueue owns progress.
class ColonyState extends eve.Component {
    cell = -1
    owner = 1
    population = 1
    granary = false
    mine = false
    library = false
    wonder = false
}
class ColonyWork extends eve.Component {
    queue = null
    task = ""
}
class Colony extends eve.Entity {
    settlement = ColonyState
    work = ColonyWork
}

civEconomy <- eve.Economy();
civProduction <- eve.Production();
civTactics <- eve.Tactics();
civilization <- null;
const CIV_BATTLE = "00000000-0000-0000-0000-000000009000";
const CIV_PLAYER = "00000000-0000-0000-0000-000000009101";
const CIV_RIVAL = "00000000-0000-0000-0000-000000009102";
const CIV_SIDE_A = "00000000-0000-0000-0000-000000009201";
const CIV_SIDE_B = "00000000-0000-0000-0000-000000009202";

// One rules catalogue is shared by command validation and UI projections.
civProjects <- {
    granary={label="Granary",gold=12,work=6.0,effect="+2 food / round"},
    mine={label="Mine",gold=16,work=8.0,effect="Production: 2 -> 3 / round"},
    library={label="Library",gold=20,work=10.0,effect="Science: 1 -> 4 / round"},
    wonder={label="Beacon",gold=50,work=30.0,effect="Complete to win the race"}
};
function civCityName(cell) { return "Haven " + (cell+1); }
function civYield(city) {
    local s=city.settlement;
    return {gold=3+s.population,food=(hexmap.sphereTerrainType(s.cell)==1 ? 3 : 2)+(s.granary ? 2 : 0),
            science=s.library ? 4 : 1,work=s.mine ? 3.0 : 2.0};
}
function civNotify(text) {
    civilization.notices.push("T"+civilization.turn+"  "+text);
    if(civilization.notices.len()>3) civilization.notices.remove(0);
}
function civFoundReason(owner) {
    if(civilization.outcome!="") return "The campaign has ended.";
    if(!civCanFound(civPosition(owner))) return "Needs 3 tiles from every city.";
    if(civBalance(owner,"gold")<30) return "Needs 30 gold.";
    return "";
}
function civProjectReason(city,product) {
    if(civilization.outcome!="") return "Campaign ended";
    if(city==null) return "Select a city";
    local s=city.settlement;
    if(s[product]) return "Built";
    if(city.work.task!="") return "Queue occupied";
    if(product=="wonder" && (!civilization.technology[s.owner] || civCount(s.owner)<3))
        return "Needs Writing + 3 cities";
    if(civBalance(s.owner,"gold")<civProjects[product].gold) return "Not enough gold";
    return "";
}

function civValue(result) {
    if (!result.ok) throw "hex civilization: " + result.status.summary;
    return ("value" in result) ? result.value : null;
}
function civActor(owner) { return owner == 1 ? CIV_PLAYER : CIV_RIVAL; }
function civBalance(owner, resource) { return civEconomy.get(owner, "planet." + resource); }
function civCredit(owner, resource, amount) {
    local accepted = civEconomy.credit(owner, "planet." + resource, amount);
    if (accepted != amount) throw "hex civilization: unexpected stock cap";
}
function civPay(owner, resource, amount) { return civEconomy.debit(owner, "planet." + resource, amount); }
function civCityAt(cell) {
    foreach (city in eve.view(Colony)) if (city.settlement.cell == cell) return city;
    return null;
}
function civCount(owner) {
    local count = 0;
    foreach (city in eve.view(Colony)) if (city.settlement.owner == owner) count++;
    return count;
}
function civCities() {
    local result=[];
    foreach(city in eve.view(Colony)) result.push(city);
    result.sort(function(a,b){ return a.settlement.cell <=> b.settlement.cell; });
    return result;
}
function civUpdateReach() { civilization.reachable=civReach(1); }
function civPosition(owner) { return civValue(civilization.battle.unitCell(civActor(owner))).x; }
function civReveal(cell) {
    civilization.explored[cell] <- true;
    for (local i=0; i<hexmap.sphereNeighborCount(cell); i++)
        civilization.explored[hexmap.sphereNeighbor(cell,i)] <- true;
}
function civCity(cell, owner) {
    local queue = civValue(civProduction.newWorkQueue());
    local city = Colony.create();
    city.settlement.cell = cell;
    city.settlement.owner = owner;
    city.work.queue = queue;
    return city;
}
function civCanFound(cell) {
    if (hexmap.sphereIsUnderwater(cell)) return false;
    foreach (city in eve.view(Colony))
        if (hexmap.sphereDistance(cell,city.settlement.cell)<3) return false;
    return true;
}
function civFound(owner) {
    local reason=civFoundReason(owner);
    if(reason!="") { if(owner==1) civilization.message=reason; return; }
    local cell = civPosition(owner);
    // Allocate before charging; this command runs synchronously, outside every ECS view.
    local city = civCity(cell,owner);
    if (!civPay(owner,"gold",30)) {
        civValue(city.work.queue.release()); city.destroy();
        throw "hex civilization: founding payment changed during command";
    }
    if(owner==1) civilization.selected=cell;
    civNotify(owner==1 ? civCityName(cell)+" founded." : "Rival founded a city.");
    if(owner==1) civilization.message="A new city joins your civilisation.";
}
function civProject(city, product) {
    local reason=civProjectReason(city,product);
    if(reason!="") { if(city!=null && city.settlement.owner==1) civilization.message=reason; return; }
    local s=city.settlement;
    local definition=civProjects[product];
    local task=civValue(city.work.queue.enqueue("city."+s.cell,"building",product,"{}",definition.work,0));
    if (!civPay(s.owner,"gold",definition.gold)) {
        civValue(city.work.queue.cancel(task,"payment refused"));
        throw "hex civilization: production payment changed during command";
    }
    city.work.task=task;
    if(s.owner==1) civilization.message="Building "+civProjects[product].label+" in "+civCityName(s.cell)+".";
}

// View: Colony (settlement + work). Reads terrain/technology, writes population,
// building completion, economy and each city's queue. No structural mutations.
// Phase: once per round, after both explorations. dt=1 named turn, never frame dt.
class ColonyTurnSystem extends eve.System {
    constructor() { base.constructor(Colony); }
    function update(dt) {
        foreach (city in civCities()) {
            local s=city.settlement;
            local income=civYield(city);
            civCredit(s.owner,"gold",income.gold);
            civCredit(s.owner,"food",income.food);
            civCredit(s.owner,"science",income.science);
            if (s.population<5 && civPay(s.owner,"food",8+s.population*4)) s.population++;
            civValue(city.work.queue.advance(civilization.turn,dt*income.work));
            if (city.work.task!="") {
                local task=city.work.queue.find(city.work.task);
                if (task==null) throw "hex civilization: production task disappeared";
                if (task.getState()=="completed") {
                    s[task.getProduct()]=true;
                    if(s.owner==1) civNotify(civCityName(s.cell)+": "+civProjects[task.getProduct()].label+" complete.");
                    city.work.task="";
                }
            }
            civValue(city.work.queue.clearEvents());
        }
    }
}
function civActing() {
    for(local i=0;i<20;i++) {
        if(civValue(civilization.battle.phase())=="acting") return;
        civilization.tick++;
        civValue(civilization.battle.advance(civilization.tick,1000000));
    }
    throw "hex civilization: turn policy did not reach acting";
}
function civMove(owner,cell) {
    if(civilization.outcome!="") return false;
    local result=civilization.battle.move(civActor(owner),cell,0,0);
    if(!result.ok) { civilization.message=result.status.summary; return false; }
    if(owner==1) { civReveal(cell); civilization.selected=cell; civUpdateReach(); }
    return true;
}
function civReach(owner) {
    local budget=civValue(civilization.battle.unitResources(civActor(owner))).movePoints;
    return civValue(civilization.battle.reachable(civActor(owner),budget)).cells;
}
function civAi() {
    local best=-1; local score=-1;
    foreach(entry in civReach(2)) {
        local cell=entry.cell.x;
        local distance=999;
        foreach(city in eve.view(Colony)) {
            local d=hexmap.sphereDistance(cell,city.settlement.cell);
            if(d<distance) distance=d;
        }
        local value=distance*10+((cell+civilization.turn)%7);
        if(value>score) {score=value;best=cell;}
    }
    if(best>=0 && best!=civPosition(2)) civMove(2,best);
    if(civCount(2)<3 && civCanFound(civPosition(2))) civFound(2);
    foreach(city in eve.view(Colony)) if(city.settlement.owner==2) {
        local s=city.settlement;
        civProject(city,!s.mine ? "mine" : (!s.library ? "library" : "wonder"));
    }
}
function civEndTurn() {
    if(civilization.outcome!="") return;
    civValue(civilization.battle.endTurn(CIV_PLAYER)); civActing();
    if(civValue(civilization.battle.activeUnit())!=CIV_RIVAL) throw "hex civilization: wrong rival turn";
    civAi();
    civValue(civilization.battle.endTurn(CIV_RIVAL)); civActing();
    if(civValue(civilization.battle.activeUnit())!=CIV_PLAYER) throw "hex civilization: wrong player turn";
    civilization.turn++;
    civilization.system.update(1.0);
    foreach(owner in [1,2]) {
        if(!civilization.technology[owner] && civPay(owner,"science",24)) {
            civilization.technology[owner]=true;
            civNotify(owner==1 ? "Writing discovered. Beacon unlocked." : "Rival discovered Writing.");
        }
        foreach(city in eve.view(Colony)) if(city.settlement.owner==owner && city.settlement.wonder)
            civilization.outcome=owner==1 ? "VICTORY - Your Beacon unites the planet" : "DEFEAT - The rival completed its Beacon";
    }
    civilization.message="New turn. Explore, expand and choose city projects.";
    civUpdateReach();
    civEconomy.clearEvents();
}
function civReset(seed) {
    local old=[];
    foreach(city in eve.view(Colony)) old.push(city);
    foreach(city in old) { civValue(city.work.queue.release()); city.destroy(); }
    if(civilization!=null) civValue(civilization.battle.release());
    foreach(resource in ["gold","food","science"]) {
        local name="planet."+resource;
        if(!civEconomy.hasType(name) && !civEconomy.registerResourceType(name,"stock",0,"renewable"))
            throw "hex civilization: resource registration failed";
        foreach(owner in [1,2]) if(!civEconomy.debit(owner,name,civEconomy.get(owner,name)))
            throw "hex civilization: ledger reset failed";
    }
    local battle=civValue(civTactics.newBattle(CIV_BATTLE,seed));
    civilization={battle=battle,turn=1,tick=0,selected=-1,reachable=[],explored={},technology={ [1]=false,[2]=false },
                  outcome="",notices=[],message="Right click to select a city or move. C focuses your expedition.",system=ColonyTurnSystem()};
    civValue(battle.setTopology("graph"));
    local land=[];
    for(local i=0;i<hexmap.sphereCellCount();i++) if(!hexmap.sphereIsUnderwater(i)) {
        land.push(i);
        civValue(battle.addCell(i,0,0,hexmap.sphereTerrainType(i)==3 ? 2 : 1));
    }
    if(land.len()<2) throw "hex civilization: planet needs at least two land tiles";
    foreach(i in land) for(local d=0;d<hexmap.sphereNeighborCount(i);d++) {
        local n=hexmap.sphereNeighbor(i,d);
        if(!hexmap.sphereIsUnderwater(n)) civValue(battle.addEdge(i,0,0,n,0,0,"{}"));
    }
    // Prefer a large connected landmass so both expeditions can expand.
    local component=[]; local seen={};
    foreach(start in land) if(!(start in seen)) {
        local cells=[start]; seen[start]<-true;
        for(local cursor=0;cursor<cells.len();cursor++) {
            local cell=cells[cursor];
            for(local d=0;d<hexmap.sphereNeighborCount(cell);d++) {
                local n=hexmap.sphereNeighbor(cell,d);
                if(!(n in seen) && !hexmap.sphereIsUnderwater(n)) {seen[n]<-true;cells.push(n);}
            }
        }
        if(cells.len()>component.len()) component=cells;
    }
    // The component was enumerated breadth-first from its first tile.
    local first=component[0]; local second=component[component.len()-1];
    foreach(cell in component) if(hexmap.sphereTerrainType(cell)==1) {first=cell;break;}
    if(second==first) foreach(cell in land) if(cell!=first) {second=cell;break;}
    civValue(battle.addSide(CIV_SIDE_A)); civValue(battle.addSide(CIV_SIDE_B));
    civValue(battle.addUnit(CIV_PLAYER,CIV_SIDE_A,"planet:expedition",first,0,0,1,4,0,10));
    civValue(battle.addUnit(CIV_RIVAL,CIV_SIDE_B,"planet:expedition",second,0,0,1,4,0,5));
    civCity(first,1); civCity(second,2);
    civCredit(1,"gold",50); civCredit(2,"gold",50);
    civilization.selected=first; civReveal(first);
    civValue(battle.start("side_alternating")); civActing();
    civUpdateReach();
    print("hex civilization: ready | economy + tactics graph + production + ECS | seed "+seed+"\n");
}
