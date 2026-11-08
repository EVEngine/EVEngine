persist sim = {
    tick = 0
    phase = "boot"
    note = ""
    intent = ""
    postOccupancy = 0
    squadSize = 0
    vacancies = 0
    spawnRequests = 0
    lastDef = ""
    passed = false
    failed = false
}

persist roster = null
persist nextSubject = 1

function roster_require(result, label) {
    if (!result.ok) {
        sim.failed = true;
        sim.note = label + " failed: " + result.status.summary;
        throw sim.note;
    }
    return result.value;
}

function makeSubject() {
    local id = "guard:" + nextSubject;
    nextSubject += 1;
    return id;
}

function refreshObservation() {
    sim.postOccupancy = roster_require(roster.postOccupancy("gate.north"), "postOccupancy");
    sim.squadSize = roster_require(roster.memberCount("patrol.east"), "memberCount");
    sim.vacancies = roster.vacancyCount();
    sim.spawnRequests = roster.spawnRequestCount();
    sim.intent = roster_require(roster.intent("patrol.east"), "intent");
}

function runContract() {
    roster = eve.Roster();
    roster.clear();
    roster_require(roster.registerPost("gate.north", 2, "sentry"), "registerPost");
    roster_require(roster.createSquad("patrol.east", 3, "patrol"), "createSquad");

    local a = makeSubject();
    local b = makeSubject();
    roster_require(roster.assignPost("gate.north", a, ""), "assign a");
    roster_require(roster.assignPost("gate.north", b, ""), "assign b");
    roster_require(roster.addMember("patrol.east", a, "lead"), "squad a");
    roster_require(roster.addMember("patrol.east", b, "support"), "squad b");
    roster_require(roster.setPolicy("gate-refill", "unit.sentry", 2, 3, "gate.north", "", "sentry"),
                   "setPolicy");
    roster_require(roster.setFallback("gate-refill", "unit.sentry", "unit.militia"), "setFallback");

    sim.tick = 10;
    roster_require(roster.releaseSubject(a, sim.tick), "kill lead");
    sim.phase = "vacancy";
    refreshObservation();
    sim.note = "lead down; waiting reinforce delay";

    sim.tick = 11;
    roster_require(roster.step(sim.tick), "step early");
    if (roster.spawnRequestCount() != 0) throw "spawn should wait for delay";

    sim.tick = 12;
    local created = roster_require(roster.step(sim.tick), "step ready");
    if (created != 1) throw "expected one spawn request";
    local req = roster_require(roster.spawnRequestAt(0), "spawnRequestAt");
    if (req.preferredDef != "unit.sentry") throw "preferred def mismatch";
    sim.phase = "spawn-sentry";
    sim.lastDef = req.preferredDef;
    sim.note = "spawn request ready: " + req.preferredDef;

    // Simulate preferred archetype unavailable → fallback.
    roster_require(roster.failSpawnRequest(req.requestId, sim.tick), "fail preferred");
    req = roster_require(roster.spawnRequestAt(0), "fallback request");
    if (req.preferredDef != "unit.militia") throw "fallback def mismatch";
    sim.phase = "spawn-militia";
    sim.lastDef = req.preferredDef;

    local replacement = makeSubject();
    roster_require(roster.ackSpawnRequest(req.requestId), "ack");
    roster_require(roster.assignPost(req.postId, replacement, req.role), "reassign post");
    roster_require(roster.addMember("patrol.east", replacement, "lead"), "rejoin squad");
    roster_require(roster.setIntent("patrol.east", "patrol"), "resume patrol");

    refreshObservation();
    if (sim.postOccupancy != 2 || sim.squadSize != 2) throw "roster not restored";
    if (sim.spawnRequests != 0) throw "spawn queue should be empty";
    sim.phase = "restored";
    sim.note = "militia filled gate; squad patrol resumes";
    sim.passed = true;
}
