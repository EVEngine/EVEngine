// All political meaning lives here. The engine modules only store generic facts.
demoAttributes <- eve.Attributes();
demoSocial <- eve.Social();
demoOrders <- eve.Orders();
demoTags <- eve.Tags();
demoEffects <- eve.Effects();
demoEventStream <- eve.EventStream();
demoTransaction <- eve.Transaction();
demoStatePatch <- eve.StatePatch();
demoDefinitions <- eve.Definitions();
demoAuthority <- eve.Authority();
demoProduction <- eve.Production();
demoPolicyRegistry <- eve.PolicyRegistryModule();
demoSensing <- eve.Sensing();
demoSteering <- eve.Steering();
demoDecision <- eve.Decision();

demoState <- {};

reset_demo <- function() {
    demoSocial.clear();
    demoTags.clear();
    demoState = {
        ruler = "faction.crown"
        rebelFaction = "faction.frontier"
        general = "general.arden"
        baseId = "base.north"
        armyId = "army.first"
        officer = "officer.vela"
        rebelled = false
        generalStats = demoAttributes.newSet("general.arden")
        baseStats = demoAttributes.newSet("base.north")
        commandQueue = demoOrders.newQueue()
        statusEffects = demoEffects.newContainer()
        events = demoEventStream.newStream()
        transactions = demoTransaction.newLedger()
        authoritativeState = demoStatePatch.newStore()
        definitions = demoDefinitions.newRegistry()
        authority = demoAuthority.newStore()
        production = demoProduction.newQueue()
        policies = demoPolicyRegistry.newRegistry()
        sensing = demoSensing.newWorld()
        decisions = demoDecision.newContext()
    };

    demoState.generalStats.setBase("administration", 80.0);
    demoState.generalStats.setBase("loyalty", 48.0);
    demoState.generalStats.setBase("ambition", 82.0);
    demoState.baseStats.setBase("production_speed", 1.0);

    demoSocial.setOwner(demoState.general, demoState.ruler);
    demoSocial.setOwner(demoState.baseId, demoState.ruler);
    demoSocial.setOwner(demoState.armyId, demoState.ruler);
    demoSocial.assign(demoState.general, "governor", demoState.baseId);
    demoSocial.assign(demoState.general, "commander", demoState.armyId);
    demoSocial.assign(demoState.officer, "subordinate", demoState.general);
    demoSocial.setRelation(demoState.officer, demoState.general, "support", 0.8);
    demoTags.add(demoState.general, "character");
    demoTags.add(demoState.general, "commander");
    demoTags.addCapability(demoState.general, "govern_base");
    demoTags.addCapability(demoState.general, "command_army");
    demoState.authority.grant(
        demoState.general, demoState.baseId, "govern_base", "rank.general", 10, 0.0);
    demoState.authority.grant(
        demoState.general, demoState.armyId, "command_army", "rank.general", 10, 0.0);
    demoState.policies.registerPolicy(
        "administration", "governor_bonus", 1, 100, true,
        "script", "administration.governor", "{\"owner\":\"simulation.nut\"}");
    demoState.policies.registerPolicy(
        "administration", "no_bonus", 1, 0, true, "builtin", "", "{}");

    // The game mirrors only the facts its AI query needs; sensing assigns no value.
    demoState.sensing.upsert(demoState.armyId, 0.0, 0.0, demoState.ruler,
                             "unit,army", "faction.crown");
    demoState.sensing.upsert("army.raiders", 30.0, 0.0, demoState.rebelFaction,
                             "unit,army", "faction.crown");
    demoState.sensing.upsert("base.raiders", 20.0, 0.0, demoState.rebelFaction,
                             "building,base", "faction.crown");
    demoState.decisions.setState("defense.ai", "patrol");
    demoState.decisions.addTransition("defense.ai", "patrol", "enemy_seen", "engage");
    demoState.decisions.newGrid("threat", 8, 8, 10.0, 0.0, 0.0);
    demoState.decisions.setCell("threat", 3, 0, 0.8);

    demoState.definitions.registerDefinition(
        "rank", "rank.general", 1,
        "{\"authority\":[\"govern_base\",\"command_army\"],\"commandCapacity\":8}");

    local initialOwners = demoState.authoritativeState.newBatch();
    initialOwners.set(demoState.general, "owner", "\"faction.crown\"");
    initialOwners.set(demoState.baseId, "owner", "\"faction.crown\"");
    initialOwners.set(demoState.armyId, "owner", "\"faction.crown\"");
    demoState.authoritativeState.commit(initialOwners);
};

run_ai_preview <- function() {
    local count = demoState.sensing.circle(
        0.0, 0.0, 100.0, "unit", "", "", demoState.ruler,
        "faction.crown", 8);
    if (count == 0) return false;
    local target = demoState.sensing.resultAt(0);
    demoState.decisions.set("defense.ai", "target", "\"" + target.getId() + "\"");
    demoState.decisions.trigger("defense.ai", "enemy_seen");
    demoState.aiTarget <- target.getId();
    demoState.aiAction <- demoState.decisions.choose(
        "attack=0.9:2,0.8:1;retreat=0.2:2,0.4:1");
    demoState.aiVelocity <- demoSteering.arrive(
        0.0, 0.0, target.getX(), target.getY(), 6.0, 20.0, 2.0);
    demoState.aiThreat <- demoState.decisions.sample("threat", 35.0, 5.0, 0.0);
    return true;
};

refresh_governor_bonus <- function() {
    demoState.baseStats.removeBySource(demoState.general, "");
    if (!demoSocial.isAssigned(demoState.general, "governor", demoState.baseId)) return;
    if (!demoState.authority.can(demoState.general, demoState.baseId, "govern_base")) return;
    local selectedPolicy = demoState.policies.select("administration");
    if (selectedPolicy == null || selectedPolicy.getName() != "governor_bonus") return;
    local administration = demoState.generalStats.getFinal("administration", 0.0);
    demoState.baseStats.addModifier(
        "governor.production", "production_speed", demoState.general,
        "multiply", 1.0 + administration * 0.005, 10);
};

run_mixed_production <- function() {
    demoState.production.setSlotCount(demoState.baseId, 2);
    demoState.production.enqueue(
        demoState.baseId, "build_unit", "tank.medium",
        "{\"spawn\":\"north_gate\"}", 10.0, 10);
    demoState.production.enqueue(
        demoState.baseId, "issue_decree", "decree.tax_reform",
        "{\"district\":\"north\"}", 10.0, 10);
    local speed = demoState.baseStats.getFinal("production_speed", 1.0);
    demoState.production.update(10.0, speed);
};

apply_unpaid_salary <- function() {
    local effectId = demoState.statusEffects.apply(
        demoState.general, "salary_unpaid", demoState.ruler,
        20, 0.0, "salary", "replace");
    local effect = demoState.statusEffects.find(effectId);
    effect.addTag("politics");
    effect.getPayload().setNumber("loyalty_delta", -35.0);
    demoState.generalStats.addModifier(
        "salary.unpaid", "loyalty", demoState.ruler, "add", -35.0, 20);
};

evaluate_rebellion <- function() {
    local loyalty = demoState.generalStats.getFinal("loyalty", 0.0);
    local ambition = demoState.generalStats.getFinal("ambition", 0.0);
    local support = demoSocial.relation(
        demoState.officer, demoState.general, "support", 0.0);
    if (loyalty >= 20.0 || ambition <= 75.0 || support <= 0.6) return false;

    local plan = demoState.transactions.create("rebellion.north", "salary.unpaid");
    local generalOp = plan.stage(
        "set_owner", demoState.general, "{\"owner\":\"faction.frontier\"}");
    local baseOp = plan.stage(
        "set_owner", demoState.baseId, "{\"owner\":\"faction.frontier\"}");
    local armyOp = plan.stage(
        "set_owner", demoState.armyId, "{\"owner\":\"faction.frontier\"}");
    plan.markValid(generalOp);
    plan.markValid(baseOp);
    plan.markValid(armyOp);
    if (!plan.validate()) return false;

    // The state patch is the atomic source of truth; Social is a query projection.
    local ownership = demoState.authoritativeState.newBatch();
    ownership.setExpected(
        demoState.general, "owner", "\"faction.frontier\"", "\"faction.crown\"");
    ownership.setExpected(
        demoState.baseId, "owner", "\"faction.frontier\"", "\"faction.crown\"");
    ownership.setExpected(
        demoState.armyId, "owner", "\"faction.frontier\"", "\"faction.crown\"");
    if (!demoState.authoritativeState.commit(ownership)) {
        plan.fail("ownership_conflict");
        return false;
    }

    demoSocial.setOwner(demoState.general, demoState.rebelFaction);
    demoSocial.setOwner(demoState.baseId, demoState.rebelFaction);
    demoSocial.setOwner(demoState.armyId, demoState.rebelFaction);
    demoSocial.setRelation(demoState.rebelFaction, demoState.ruler, "hostility", 1.0);
    demoState.commandQueue.append("secure_assets", 100, 0.0);
    demoTags.add(demoState.rebelFaction, "faction");
    demoState.events.emit(
        "rebellion_started", demoState.general, demoState.rebelFaction,
        "", "rebellion.north", 1, 1,
        "{\"formerFaction\":\"faction.crown\"}");
    plan.commit();
    demoState.rebelled = true;
    return true;
};

run_rebellion_scenario <- function() {
    reset_demo();
    refresh_governor_bonus();
    run_mixed_production();
    run_ai_preview();
    apply_unpaid_salary();
    return evaluate_rebellion();
};
