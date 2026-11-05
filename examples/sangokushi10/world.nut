dofile("concepts.nut");

persist world = null;

PLAYER_NATION <- "nation.wei";
JOB_GOVERNOR <- "governor";
JOB_FARM <- "farm";
JOB_COMMERCE <- "commerce";
JOB_RECRUIT <- "recruit";
JOB_COMMANDER <- "commander";
REL_FRIEND <- "friendship";
REL_RIVAL <- "rivalry";
REL_ADJACENT <- "adjacent";
REL_IN_REGION <- "in_region";
REL_STATIONED <- "stationed";

function nationColor(id) {
    if (id == "nation.wei") return { r = 0.22, g = 0.45, b = 0.82 };
    if (id == "nation.shu") return { r = 0.22, g = 0.68, b = 0.38 };
    if (id == "nation.wu") return { r = 0.82, g = 0.28, b = 0.28 };
    return { r = 0.45, g = 0.45, b = 0.48 };
}

function ensureDbSchema(conn) {
    conn.execute(@"CREATE TABLE IF NOT EXISTS officers (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        nation_id TEXT,
        city_id TEXT,
        leadership INTEGER,
        war INTEGER,
        intellect INTEGER,
        politics INTEGER,
        charm INTEGER,
        loyalty INTEGER,
        age INTEGER
    )");
    conn.execute(@"CREATE TABLE IF NOT EXISTS cities (
        id TEXT PRIMARY KEY,
        name TEXT NOT NULL,
        nation_id TEXT,
        region_id TEXT,
        agriculture INTEGER,
        commerce INTEGER,
        defense INTEGER,
        gold INTEGER,
        food INTEGER
    )");
    conn.execute(@"CREATE TABLE IF NOT EXISTS relationships (
        from_id TEXT NOT NULL,
        to_id TEXT NOT NULL,
        kind TEXT NOT NULL,
        affinity REAL NOT NULL,
        PRIMARY KEY(from_id, to_id, kind)
    )");
    conn.execute("CREATE INDEX IF NOT EXISTS idx_officer_nation_lead ON officers(nation_id, leadership DESC)");
    conn.execute("CREATE INDEX IF NOT EXISTS idx_officer_city ON officers(city_id)");
}

function openCatalogDb() {
    local database = eve.Database();
    local conn = database.connectSQLite(":memory:");
    ensureDbSchema(conn);
    return conn;
}

function skMin(a, b) { return a < b ? a : b; }
function skMax(a, b) { return a > b ? a : b; }

function syncOfficerRow(conn, officer, nationId, cityId) {
    conn.upsertJson("officers",
        "{\"id\":\"" + officer.id +
        "\",\"name\":\"" + officer.name +
        "\",\"nation_id\":\"" + nationId +
        "\",\"city_id\":\"" + cityId +
        "\",\"leadership\":" + officer.leadership +
        ",\"war\":" + officer.war +
        ",\"intellect\":" + officer.intellect +
        ",\"politics\":" + officer.politics +
        ",\"charm\":" + officer.charm +
        ",\"loyalty\":" + officer.loyalty.tointeger() +
        ",\"age\":" + officer.age + "}");
}

function syncCityRow(conn, city, nationId) {
    conn.upsertJson("cities",
        "{\"id\":\"" + city.id +
        "\",\"name\":\"" + city.name +
        "\",\"nation_id\":\"" + nationId +
        "\",\"region_id\":\"" + city.regionId +
        "\",\"agriculture\":" + city.agriculture.tointeger() +
        ",\"commerce\":" + city.commerce.tointeger() +
        ",\"defense\":" + city.defense.tointeger() +
        ",\"gold\":" + city.gold.tointeger() +
        ",\"food\":" + city.food.tointeger() + "}");
}

function syncRelationRow(conn, fromId, toId, kind, affinity) {
    conn.upsertJson("relationships",
        "{\"from_id\":\"" + fromId +
        "\",\"to_id\":\"" + toId +
        "\",\"kind\":\"" + kind +
        "\",\"affinity\":" + affinity + "}");
}

function refreshCatalog(w) {
    if (w.db == null) return;
    if (!w.db.begin()) return;
    try {
        w.db.execute("DELETE FROM officers");
        w.db.execute("DELETE FROM cities");
        w.db.execute("DELETE FROM relationships");
        foreach (city in w.cities) {
            syncCityRow(w.db, city, w.social.ownerOf(city.id));
            foreach (oid in city.officers) {
                local officer = w.officers[oid];
                syncOfficerRow(w.db, officer, w.social.ownerOf(oid), city.id);
            }
        }
        foreach (edge in w.relationEdges) {
            syncRelationRow(w.db, edge.fromId, edge.toId, edge.kind, edge.affinity);
        }
        if (!w.db.commit()) w.db.rollback();
    } catch (err) {
        w.db.rollback();
        w.message = "catalog sync failed: " + err;
    }
}

function topOfficersJson(w, nationId, limitCount) {
    if (w.db == null) return "[]";
    local model = w.db.model("officers", "id");
    local q = model.query();
    q.where("nation_id", "=", "\"" + nationId + "\"");
    q.orderBy("leadership", false);
    q.limit(limitCount);
    return q.allJson();
}

function cityById(w, id) {
    foreach (city in w.cities) if (city.id == id) return city;
    return null;
}

function armyById(w, id) {
    foreach (army in w.armies) if (army.id == id) return army;
    return null;
}

function officerNation(w, officerId) {
    return w.social.ownerOf(officerId);
}

function setMessage(w, text) {
    w.message = text;
}

function nextEventId(w) {
    w.eventSerial += 1;
    local suffix = (100000000000 + w.eventSerial).tostring().slice(1); // 11 digits
    return "00000000-0000-4000-8000-0" + suffix;
}

function appendEvent(w, type, source, subject, correlation, payload) {
    local result = w.events.append(
        nextEventId(w), type, source, subject, "", correlation, w.tick, 0, payload);
    if (!result.ok) setMessage(w, "event " + type + " failed: " + result.status.summary);
}

function seedContent(w) {
    local schemas = w.schemas;
    local definitions = w.definitions;

    validateAndInsert(schemas, definitions, "sk10.nation", "nation", "nation.wei",
        { id="nation.wei", name="Wei", color="blue", capitalCityId="city.luoyang" });
    validateAndInsert(schemas, definitions, "sk10.nation", "nation", "nation.shu",
        { id="nation.shu", name="Shu", color="green", capitalCityId="city.chengdu" });
    validateAndInsert(schemas, definitions, "sk10.nation", "nation", "nation.wu",
        { id="nation.wu", name="Wu", color="red", capitalCityId="city.jianye" });

    validateAndInsert(schemas, definitions, "sk10.region", "region", "region.central",
        { id="region.central", name="Central Plains", theater="north" });
    validateAndInsert(schemas, definitions, "sk10.region", "region", "region.west",
        { id="region.west", name="Bashu", theater="west" });
    validateAndInsert(schemas, definitions, "sk10.region", "region", "region.south",
        { id="region.south", name="Jiangdong", theater="south" });

    local citySeeds = [
        { id="city.luoyang", name="Luoyang", regionId="region.central", mapX=430.0, mapY=210.0, agriculture=70, commerce=80, defense=75, population=42000, nation="nation.wei" },
        { id="city.xuchang", name="Xuchang", regionId="region.central", mapX=520.0, mapY=260.0, agriculture=65, commerce=70, defense=60, population=28000, nation="nation.wei" },
        { id="city.changan", name="Chang'an", regionId="region.central", mapX=300.0, mapY=230.0, agriculture=60, commerce=75, defense=80, population=36000, nation="nation.wei" },
        { id="city.chenliu", name="Chenliu", regionId="region.central", mapX=600.0, mapY=200.0, agriculture=55, commerce=55, defense=45, population=18000, nation="nation.wei" },
        { id="city.chengdu", name="Chengdu", regionId="region.west", mapX=210.0, mapY=430.0, agriculture=85, commerce=65, defense=70, population=38000, nation="nation.shu" },
        { id="city.hanzhong", name="Hanzhong", regionId="region.west", mapX=280.0, mapY=340.0, agriculture=60, commerce=45, defense=85, population=20000, nation="nation.shu" },
        { id="city.jianye", name="Jianye", regionId="region.south", mapX=720.0, mapY=430.0, agriculture=70, commerce=85, defense=65, population=40000, nation="nation.wu" },
        { id="city.chaisang", name="Chaisang", regionId="region.south", mapX=640.0, mapY=360.0, agriculture=60, commerce=70, defense=55, population=22000, nation="nation.wu" }
    ];

    local officerSeeds = [
        { id="officer.caocao", name="Cao Cao", leadership=96, war=72, intellect=91, politics=94, charm=88, loyalty=100, age=42, nation="nation.wei", city="city.luoyang", job=JOB_GOVERNOR },
        { id="officer.xiahoudun", name="Xiahou Dun", leadership=86, war=90, intellect=60, politics=55, charm=70, loyalty=95, age=38, nation="nation.wei", city="city.luoyang", job=JOB_COMMANDER },
        { id="officer.xunyu", name="Xun Yu", leadership=70, war=30, intellect=96, politics=92, charm=80, loyalty=90, age=36, nation="nation.wei", city="city.xuchang", job=JOB_COMMERCE },
        { id="officer.zhangliao", name="Zhang Liao", leadership=88, war=92, intellect=72, politics=50, charm=75, loyalty=85, age=34, nation="nation.wei", city="city.changan", job=JOB_COMMANDER },
        { id="officer.guojia", name="Guo Jia", leadership=62, war=28, intellect=97, politics=78, charm=70, loyalty=88, age=29, nation="nation.wei", city="city.chenliu", job=JOB_FARM },
        { id="officer.liubei", name="Liu Bei", leadership=80, war=70, intellect=75, politics=82, charm=98, loyalty=100, age=41, nation="nation.shu", city="city.chengdu", job=JOB_GOVERNOR },
        { id="officer.zhugeliang", name="Zhuge Liang", leadership=92, war=38, intellect=100, politics=95, charm=85, loyalty=100, age=28, nation="nation.shu", city="city.chengdu", job=JOB_COMMERCE },
        { id="officer.guanyu", name="Guan Yu", leadership=90, war=97, intellect=75, politics=60, charm=88, loyalty=100, age=45, nation="nation.shu", city="city.hanzhong", job=JOB_COMMANDER },
        { id="officer.zhangfei", name="Zhang Fei", leadership=78, war=96, intellect=40, politics=30, charm=55, loyalty=95, age=40, nation="nation.shu", city="city.hanzhong", job=JOB_RECRUIT },
        { id="officer.sunquan", name="Sun Quan", leadership=84, war=60, intellect=80, politics=88, charm=86, loyalty=100, age=26, nation="nation.wu", city="city.jianye", job=JOB_GOVERNOR },
        { id="officer.zhouyu", name="Zhou Yu", leadership=94, war=80, intellect=96, politics=78, charm=90, loyalty=95, age=32, nation="nation.wu", city="city.jianye", job=JOB_COMMANDER },
        { id="officer.lusu", name="Lu Su", leadership=75, war=45, intellect=90, politics=88, charm=82, loyalty=90, age=34, nation="nation.wu", city="city.chaisang", job=JOB_COMMERCE }
    ];

    local armySeeds = [
        { id="army.wei.north", name="Wei Northern Legion", troops=12000, city="city.luoyang", nation="nation.wei", commander="officer.xiahoudun" },
        { id="army.wei.west", name="Wei Western Legion", troops=9000, city="city.changan", nation="nation.wei", commander="officer.zhangliao" },
        { id="army.shu.guard", name="Shu Capital Guard", troops=10000, city="city.chengdu", nation="nation.shu", commander="officer.liubei" },
        { id="army.shu.pass", name="Hanzhong Garrison", troops=11000, city="city.hanzhong", nation="nation.shu", commander="officer.guanyu" },
        { id="army.wu.fleet", name="Wu River Fleet", troops=13000, city="city.jianye", nation="nation.wu", commander="officer.zhouyu" },
        { id="army.wu.border", name="Chaisang Corps", troops=8000, city="city.chaisang", nation="nation.wu", commander="officer.lusu" }
    ];

    local taskSeeds = [
        { id="task.farm", name="Land Development", kind="farm", months=1 },
        { id="task.commerce", name="Market Expansion", kind="commerce", months=1 },
        { id="task.recruit", name="Conscription", kind="recruit", months=1 },
        { id="task.fortify", name="Wall Repair", kind="fortify", months=1 }
    ];

    foreach (task in taskSeeds)
        validateAndInsert(schemas, definitions, "sk10.task", "task", task.id, task);

    w.cities = [];
    foreach (seed in citySeeds) {
        validateAndInsert(schemas, definitions, "sk10.city", "city", seed.id, {
            id=seed.id, name=seed.name, regionId=seed.regionId,
            mapX=seed.mapX, mapY=seed.mapY,
            agriculture=seed.agriculture, commerce=seed.commerce,
            defense=seed.defense, population=seed.population
        });
        local stats = w.attributes.newSet(seed.id);
        stats.setBase("agriculture", seed.agriculture.tofloat());
        stats.setBase("commerce", seed.commerce.tofloat());
        stats.setBase("defense", seed.defense.tofloat());
        stats.setBase("gold", 800.0);
        stats.setBase("food", 1200.0);
        local city = {
            id = seed.id
            name = seed.name
            regionId = seed.regionId
            mapX = seed.mapX
            mapY = seed.mapY
            agriculture = seed.agriculture.tofloat()
            commerce = seed.commerce.tofloat()
            defense = seed.defense.tofloat()
            gold = 800.0
            food = 1200.0
            population = seed.population
            officers = []
            armyId = ""
            stats = stats
            task = null
        };
        w.cities.push(city);
        w.social.setOwner(seed.id, seed.nation);
        w.social.link(seed.id, seed.regionId, REL_IN_REGION);
        w.tags.add(seed.id, "city");
        local slots = w.production.setSlotCount(seed.id, 2);
        if (!slots.ok) throw slots.status.summary;
        w.nations[seed.nation].cities.push(seed.id);
    }

    local adjacency = [
        ["city.luoyang", "city.xuchang"],
        ["city.luoyang", "city.changan"],
        ["city.luoyang", "city.chenliu"],
        ["city.xuchang", "city.chenliu"],
        ["city.xuchang", "city.chaisang"],
        ["city.changan", "city.hanzhong"],
        ["city.hanzhong", "city.chengdu"],
        ["city.chaisang", "city.jianye"],
        ["city.chenliu", "city.chaisang"]
    ];
    foreach (pair in adjacency) {
        w.social.link(pair[0], pair[1], REL_ADJACENT);
        w.social.link(pair[1], pair[0], REL_ADJACENT);
    }

    w.officers = {};
    foreach (seed in officerSeeds) {
        validateAndInsert(schemas, definitions, "sk10.officer", "officer", seed.id, {
            id=seed.id, name=seed.name,
            leadership=seed.leadership, war=seed.war, intellect=seed.intellect,
            politics=seed.politics, charm=seed.charm, loyalty=seed.loyalty, age=seed.age
        });
        local stats = w.attributes.newSet(seed.id);
        stats.setBase("leadership", seed.leadership.tofloat());
        stats.setBase("war", seed.war.tofloat());
        stats.setBase("intellect", seed.intellect.tofloat());
        stats.setBase("politics", seed.politics.tofloat());
        stats.setBase("charm", seed.charm.tofloat());
        stats.setBase("loyalty", seed.loyalty.tofloat());
        local officer = {
            id = seed.id
            name = seed.name
            leadership = seed.leadership
            war = seed.war
            intellect = seed.intellect
            politics = seed.politics
            charm = seed.charm
            loyalty = seed.loyalty.tofloat()
            age = seed.age
            cityId = seed.city
            job = seed.job
            stats = stats
        };
        w.officers[seed.id] <- officer;
        w.social.setOwner(seed.id, seed.nation);
        w.social.assign(seed.id, seed.job, seed.city);
        w.tags.add(seed.id, "officer");
        local city = cityById(w, seed.city);
        city.officers.push(seed.id);
        if (seed.job == JOB_GOVERNOR)
            w.authority.grant(seed.id, seed.city, "govern_city", "rank.governor", 10, 0.0);
        if (seed.job == JOB_COMMANDER)
            w.authority.grant(seed.id, seed.city, "command_army", "rank.general", 10, 0.0);
    }

    w.relationEdges = [
        { fromId="officer.caocao", toId="officer.xunyu", kind=REL_FRIEND, affinity=0.9 },
        { fromId="officer.caocao", toId="officer.guojia", kind=REL_FRIEND, affinity=0.85 },
        { fromId="officer.xiahoudun", toId="officer.caocao", kind=REL_FRIEND, affinity=0.95 },
        { fromId="officer.liubei", toId="officer.guanyu", kind=REL_FRIEND, affinity=1.0 },
        { fromId="officer.liubei", toId="officer.zhangfei", kind=REL_FRIEND, affinity=1.0 },
        { fromId="officer.zhugeliang", toId="officer.liubei", kind=REL_FRIEND, affinity=0.95 },
        { fromId="officer.sunquan", toId="officer.zhouyu", kind=REL_FRIEND, affinity=0.9 },
        { fromId="officer.zhouyu", toId="officer.lusu", kind=REL_FRIEND, affinity=0.8 },
        { fromId="officer.caocao", toId="officer.liubei", kind=REL_RIVAL, affinity=0.7 },
        { fromId="officer.zhouyu", toId="officer.zhugeliang", kind=REL_RIVAL, affinity=0.6 }
    ];
    foreach (edge in w.relationEdges) {
        w.social.setRelation(edge.fromId, edge.toId, edge.kind, edge.affinity);
    }

    w.armies = [];
    foreach (seed in armySeeds) {
        validateAndInsert(schemas, definitions, "sk10.army", "army", seed.id, {
            id=seed.id, name=seed.name, troops=seed.troops
        });
        local army = {
            id = seed.id
            name = seed.name
            troops = seed.troops
            cityId = seed.city
            commanderId = seed.commander
        };
        w.armies.push(army);
        w.social.setOwner(seed.id, seed.nation);
        w.social.setController(seed.id, seed.commander);
        w.social.link(seed.id, seed.city, REL_STATIONED);
        w.social.assign(seed.commander, JOB_COMMANDER, seed.id);
        local city = cityById(w, seed.city);
        city.armyId = seed.id;
        w.tags.add(seed.id, "army");
    }

    foreach (task in taskSeeds) w.tasks[task.id] <- task;
}

function createWorld() {
    local schemas = eve.Schema();
    if (!registerSangokushiConcepts(schemas))
        throw "failed to register sangokushi concept schemas";

    local productionResult = eve.Production().newWorkQueue();
    if (!productionResult.ok) throw productionResult.status.summary;
    local authorityResult = eve.Authority().newStore();
    if (!authorityResult.ok) throw authorityResult.status.summary;

    local w = {
        schemas = schemas
        definitions = eve.Definitions().newRegistry()
        social = eve.Social()
        attributes = eve.Attributes()
        tags = eve.Tags()
        authority = authorityResult.value
        production = productionResult.value
        orders = eve.Orders()
        effects = eve.Effects()
        events = eve.GameEvent().newLog()
        db = openCatalogDb()
        nations = {
            ["nation.wei"] = { id="nation.wei", name="Wei", cities=[] },
            ["nation.shu"] = { id="nation.shu", name="Shu", cities=[] },
            ["nation.wu"] = { id="nation.wu", name="Wu", cities=[] }
        }
        cities = []
        officers = {}
        armies = []
        tasks = {}
        relationEdges = []
        month = 1
        year = 190
        selectedCityId = "city.luoyang"
        selectedOfficerId = "officer.caocao"
        message = "Wei turn. Appoint jobs, develop cities, then End Month."
        outcome = ""
        tick = 0
        eventSerial = 0
    };

    seedContent(w);
    refreshCatalog(w);
    return w;
}

function selectedCity(w) {
    return cityById(w, w.selectedCityId);
}

function selectedOfficer(w) {
    if (!(w.selectedOfficerId in w.officers)) return null;
    return w.officers[w.selectedOfficerId];
}

function cityOwnerName(w, city) {
    local owner = w.social.ownerOf(city.id);
    if (owner in w.nations) return w.nations[owner].name;
    return owner;
}

function isPlayerCity(w, city) {
    return w.social.ownerOf(city.id) == PLAYER_NATION;
}

function adjacentEnemyCities(w, city) {
    local result = [];
    local count = w.social.relationTargetCount(city.id, REL_ADJACENT, 0.0);
    for (local i = 0; i < count; ++i) {
        local otherId = w.social.relationTargetAt(city.id, REL_ADJACENT, 0.0, i);
        local other = cityById(w, otherId);
        if (other != null && w.social.ownerOf(other.id) != w.social.ownerOf(city.id))
            result.push(other);
    }
    return result;
}

function assignJob(w, officerId, job, cityId) {
    if (!(officerId in w.officers)) return false;
    local officer = w.officers[officerId];
    if (officerNation(w, officerId) != PLAYER_NATION) {
        setMessage(w, "Can only assign Wei officers.");
        return false;
    }
    local city = cityById(w, cityId);
    if (city == null || !isPlayerCity(w, city)) {
        setMessage(w, "Target city must be Wei-owned.");
        return false;
    }
    if (officer.job != "")
        w.social.unassign(officerId, officer.job, officer.cityId);
    // move officer between city lists
    local oldCity = cityById(w, officer.cityId);
    if (oldCity != null) {
        local next = [];
        foreach (oid in oldCity.officers) if (oid != officerId) next.push(oid);
        oldCity.officers = next;
    }
    officer.cityId = cityId;
    officer.job = job;
    city.officers.push(officerId);
    w.social.assign(officerId, job, cityId);
    if (job == JOB_GOVERNOR)
        w.authority.grant(officerId, cityId, "govern_city", "rank.governor", 10, 0.0);
    setMessage(w, officer.name + " assigned as " + job + " in " + city.name);
    refreshCatalog(w);
    return true;
}

function startDomesticTask(w, kind) {
    local city = selectedCity(w);
    if (city == null || !isPlayerCity(w, city)) {
        setMessage(w, "Select a Wei city first.");
        return false;
    }
    if (city.task != null) {
        setMessage(w, city.name + " already has a task in progress.");
        return false;
    }
    local taskId = "task." + kind;
    if (!(taskId in w.tasks)) {
        setMessage(w, "Unknown task kind.");
        return false;
    }
    local worker = null;
    foreach (oid in city.officers) {
        local officer = w.officers[oid];
        if (officer.job == kind || (kind == "fortify" && officer.job == JOB_GOVERNOR)) {
            worker = officer;
            break;
        }
    }
    if (worker == null) {
        setMessage(w, "No officer with job '" + kind + "' in " + city.name);
        return false;
    }
    local duration = 1.0;
    local politics = worker.stats.getFinal("politics", 50.0);
    if (kind == "farm" || kind == "commerce") duration = 1.0;
    local context = "{\"officer\":\"" + worker.id + "\"}";
    local enqueue = w.production.enqueue(city.id, kind, taskId, context, duration, politics.tointeger());
    if (!enqueue.ok) {
        setMessage(w, "Could not enqueue task: " + enqueue.status.summary);
        return false;
    }
    city.task = { id = enqueue.value, kind = kind, workerId = worker.id, monthsLeft = 1 };
    setMessage(w, city.name + " started " + w.tasks[taskId].name + " (" + worker.name + ")");
    return true;
}

function resolveCityTask(w, city) {
    if (city.task == null) return;
    local kind = city.task.kind;
    local worker = w.officers[city.task.workerId];
    local politics = worker != null ? worker.stats.getFinal("politics", 50.0) : 40.0;
    local gain = 4.0 + politics * 0.08;
    if (kind == "farm") {
        city.agriculture = city.stats.getFinal("agriculture", 50.0) + gain;
        city.stats.setBase("agriculture", city.agriculture);
        city.food += 200.0 + politics;
        city.stats.setBase("food", city.food);
    } else if (kind == "commerce") {
        city.commerce = city.stats.getFinal("commerce", 50.0) + gain;
        city.stats.setBase("commerce", city.commerce);
        city.gold += 150.0 + politics;
        city.stats.setBase("gold", city.gold);
    } else if (kind == "recruit") {
        local army = armyById(w, city.armyId);
        if (army != null) {
            local war = worker != null ? worker.stats.getFinal("war", 50.0) : 40.0;
            army.troops += (300 + war * 8).tointeger();
        }
    } else if (kind == "fortify") {
        city.defense = city.stats.getFinal("defense", 50.0) + gain;
        city.stats.setBase("defense", city.defense);
    }
    city.task = null;
}

function monthlyIncome(w, city) {
    local agri = city.stats.getFinal("agriculture", 50.0);
    local comm = city.stats.getFinal("commerce", 50.0);
    local govBonus = 1.0;
    foreach (oid in city.officers) {
        local officer = w.officers[oid];
        if (officer.job == JOB_GOVERNOR)
            govBonus += officer.stats.getFinal("politics", 50.0) * 0.002;
    }
    city.food += agri * 3.0 * govBonus;
    city.gold += comm * 2.5 * govBonus;
    city.stats.setBase("food", city.food);
    city.stats.setBase("gold", city.gold);
}

function updateLoyalty(w) {
    foreach (id, officer in w.officers) {
        if (officerNation(w, id) != PLAYER_NATION) continue;
        local city = cityById(w, officer.cityId);
        local paid = city != null && city.gold >= 40.0;
        if (paid) {
            city.gold -= 40.0;
            city.stats.setBase("gold", city.gold);
            officer.loyalty = skMin(100.0, officer.stats.getFinal("loyalty", 70.0) + 2.0);
        } else {
            officer.loyalty = skMax(0.0, officer.stats.getFinal("loyalty", 70.0) - 6.0);
        }
        officer.stats.setBase("loyalty", officer.loyalty);
        if (officer.loyalty < 25.0 && officer.stats.getFinal("charm", 50.0) > 70.0) {
            local hostCity = cityById(w, officer.cityId);
            local enemies = hostCity != null ? adjacentEnemyCities(w, hostCity) : [];
            if (enemies.len() > 0) {
                local dest = enemies[0];
                local newNation = w.social.ownerOf(dest.id);
                w.social.setOwner(officer.id, newNation);
                assignJobForce(w, officer.id, JOB_FARM, dest.id);
                setMessage(w, officer.name + " defected to " + w.nations[newNation].name + "!");
                appendEvent(w, "defection", officer.id, dest.id, "politics",
                    "{\"loyalty\":" + officer.loyalty + "}");
            }
        }
    }
}

function assignJobForce(w, officerId, job, cityId) {
    local officer = w.officers[officerId];
    if (officer.job != "")
        w.social.unassign(officerId, officer.job, officer.cityId);
    local oldCity = cityById(w, officer.cityId);
    if (oldCity != null) {
        local next = [];
        foreach (oid in oldCity.officers) if (oid != officerId) next.push(oid);
        oldCity.officers = next;
    }
    officer.cityId = cityId;
    officer.job = job;
    local city = cityById(w, cityId);
    city.officers.push(officerId);
    w.social.assign(officerId, job, cityId);
}

function armyPower(w, army) {
    if (army == null) return 0.0;
    local commander = army.commanderId in w.officers ? w.officers[army.commanderId] : null;
    local lead = commander != null ? commander.stats.getFinal("leadership", 50.0) : 40.0;
    local war = commander != null ? commander.stats.getFinal("war", 50.0) : 40.0;
    local city = cityById(w, army.cityId);
    local defense = city != null ? city.stats.getFinal("defense", 50.0) : 40.0;
    return army.troops * 0.05 + lead * 18.0 + war * 12.0 + defense * 6.0;
}

function attackSelected(w) {
    if (w.outcome != "") return;
    local city = selectedCity(w);
    if (city == null || !isPlayerCity(w, city)) {
        setMessage(w, "Select a Wei city that holds an army.");
        return;
    }
    local army = armyById(w, city.armyId);
    if (army == null || army.troops < 1000) {
        setMessage(w, "Need a stationed legion with at least 1000 troops.");
        return;
    }
    local enemies = adjacentEnemyCities(w, city);
    if (enemies.len() == 0) {
        setMessage(w, "No adjacent enemy city.");
        return;
    }
    local target = enemies[0];
    foreach (candidate in enemies) {
        if (w.selectedCityId != city.id) break;
        // prefer capital targets when tied selection; otherwise first adjacent
        local owner = w.social.ownerOf(candidate.id);
        local capital = w.definitions.resolve("nation", owner);
        if (capital.ok) {
            local decoded = eve.json.decode(capital.value.json);
            if (decoded.ok && decoded.value.capitalCityId == candidate.id) {
                target = candidate;
                break;
            }
        }
    }
    local defenders = armyById(w, target.armyId);
    local atk = armyPower(w, army);
    local def = armyPower(w, defenders) * 1.05;
    if (atk > def) {
        local oldOwner = w.social.ownerOf(target.id);
        w.social.setOwner(target.id, PLAYER_NATION);
        // move conquering army
        w.social.removeRelation(army.id, city.id, REL_STATIONED);
        w.social.link(army.id, target.id, REL_STATIONED);
        city.armyId = "";
        army.cityId = target.id;
        target.armyId = army.id;
        army.troops = (army.troops * 0.85).tointeger();
        if (defenders != null) defenders.troops = (defenders.troops * 0.4).tointeger();
        // captured officers in city change allegiance if loyalty low
        local captured = [];
        foreach (oid in target.officers) captured.push(oid);
        foreach (oid in captured) {
            local officer = w.officers[oid];
            if (officer.loyalty < 80.0) {
                w.social.setOwner(oid, PLAYER_NATION);
                officer.loyalty = 55.0;
                officer.stats.setBase("loyalty", 55.0);
            }
        }
        setMessage(w, "Captured " + target.name + " from " + w.nations[oldOwner].name + "!");
        appendEvent(w, "capture", city.id, target.id, "war",
            "{\"atk\":" + atk + ",\"def\":" + def + "}");
        w.selectedCityId = target.id;
        checkVictory(w);
    } else {
        army.troops = (army.troops * 0.7).tointeger();
        if (defenders != null) defenders.troops = (defenders.troops * 0.9).tointeger();
        setMessage(w, "Assault on " + target.name + " failed. Legion weakened.");
        appendEvent(w, "assault_failed", city.id, target.id, "war",
            "{\"atk\":" + atk + ",\"def\":" + def + "}");
    }
    refreshCatalog(w);
}

function countCities(w, nationId) {
    local n = 0;
    foreach (city in w.cities) if (w.social.ownerOf(city.id) == nationId) n += 1;
    return n;
}

function checkVictory(w) {
    if (countCities(w, PLAYER_NATION) >= 6) {
        w.outcome = "victory";
        setMessage(w, "Victory! Wei controls most of the realm.");
        return;
    }
    // lose capitals of player?
    local capital = cityById(w, "city.luoyang");
    if (capital != null && w.social.ownerOf(capital.id) != PLAYER_NATION) {
        w.outcome = "defeat";
        setMessage(w, "Defeat — Luoyang has fallen.");
    }
}

function aiDevelop(w, nationId) {
    foreach (city in w.cities) {
        if (w.social.ownerOf(city.id) != nationId) continue;
        if (city.task != null) continue;
        local kind = "farm";
        if (city.commerce < city.agriculture) kind = "commerce";
        if (city.armyId != "" && armyById(w, city.armyId).troops < 9000) kind = "recruit";
        local worker = null;
        foreach (oid in city.officers) {
            worker = w.officers[oid];
            break;
        }
        if (worker == null) continue;
        local context = "{\"officer\":\"" + worker.id + "\"}";
        local enqueue = w.production.enqueue(city.id, kind, "task." + kind, context, 1.0, 50);
        if (enqueue.ok) city.task = { id = enqueue.value, kind = kind, workerId = worker.id, monthsLeft = 1 };
    }
}

function aiAttack(w, nationId) {
    foreach (city in w.cities) {
        if (w.social.ownerOf(city.id) != nationId) continue;
        local army = armyById(w, city.armyId);
        if (army == null || army.troops < 8000) continue;
        local enemies = adjacentEnemyCities(w, city);
        foreach (target in enemies) {
            if (w.social.ownerOf(target.id) != PLAYER_NATION) continue;
            local defenders = armyById(w, target.armyId);
            local atk = armyPower(w, army);
            local def = armyPower(w, defenders);
            if (atk <= def * 1.1) continue;
            w.social.setOwner(target.id, nationId);
            w.social.removeRelation(army.id, city.id, REL_STATIONED);
            w.social.link(army.id, target.id, REL_STATIONED);
            city.armyId = "";
            army.cityId = target.id;
            target.armyId = army.id;
            army.troops = (army.troops * 0.85).tointeger();
            setMessage(w, w.nations[nationId].name + " seized " + target.name + "!");
            checkVictory(w);
            return;
        }
    }
}

function endMonth(w) {
    if (w.outcome != "") return;
    w.tick += 1;
    w.production.advance(w.tick, 1.0);
    foreach (city in w.cities) {
        monthlyIncome(w, city);
        if (city.task != null) {
            city.task.monthsLeft -= 1;
            if (city.task.monthsLeft <= 0) resolveCityTask(w, city);
        }
    }
    updateLoyalty(w);
    aiDevelop(w, "nation.shu");
    aiDevelop(w, "nation.wu");
    if (w.month % 2 == 0) {
        aiAttack(w, "nation.shu");
        aiAttack(w, "nation.wu");
    }
    w.month += 1;
    if (w.month > 12) {
        w.month = 1;
        w.year += 1;
    }
    refreshCatalog(w);
    if (w.outcome == "")
        setMessage(w, "Month " + w.month + ", Year " + w.year + " — develop or march.");
}

function selectCityAt(w, x, y) {
    local best = null;
    local bestDist = 28.0;
    foreach (city in w.cities) {
        local dx = city.mapX - x;
        local dy = city.mapY - y;
        local d = sqrt(dx * dx + dy * dy);
        if (d < bestDist) {
            bestDist = d;
            best = city;
        }
    }
    if (best != null) {
        w.selectedCityId = best.id;
        if (best.officers.len() > 0) w.selectedOfficerId = best.officers[0];
        setMessage(w, best.name + " (" + cityOwnerName(w, best) + ")");
    }
}

function cycleOfficer(w, delta) {
    local city = selectedCity(w);
    if (city == null || city.officers.len() == 0) return;
    local index = 0;
    for (local i = 0; i < city.officers.len(); ++i)
        if (city.officers[i] == w.selectedOfficerId) index = i;
    index = (index + delta + city.officers.len()) % city.officers.len();
    w.selectedOfficerId = city.officers[index];
}

function resetWorld() {
    if (world != null && world.db != null) {
        world.db.close();
    }
    world = createWorld();
}
