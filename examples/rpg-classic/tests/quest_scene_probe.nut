function runQuestSceneProbe(questsJson) {
    local rpg = eve.RPG();
    local replaced = rpg.replaceQuestsFromJson(questsJson);
    if (!replaced.ok) throw "quest definitions rejected";
    local tracker = rpg.newTracker();
    if (!tracker.activate("quest.slayer")) throw "quest activation failed";
    tracker.notify("kill", "slime", 3);
    initializeQuestScene(tracker);
    questSceneLog.reset();
    if (questSceneMarkerVisible() || recordQuestCompletion(tracker, "quest.slayer"))
        throw "unclaimed quest must not produce a completion event";
    local state = rpg.newGameState();
    state.setVariable("gold", 0.0);
    local bag = eve.Inventory().newBag(4);
    local claimed = rpg.claimQuestRewards(tracker, state, bag, "quest.slayer");
    if (!claimed.ok) throw "quest reward failed";
    if (!recordQuestCompletion(tracker, "quest.slayer")) throw "completion event dispatch failed";
    if (!questSceneMarkerVisible() || questSceneLog.size() != 1) throw "scene projection failed";
    return true;
}
