// Scene-owned presentation of the canonical quest reward transition.
if (!("questSceneLog" in getroottable())) questSceneLog <- eve.GameEvent().newLog();
if (!("questSceneMarker" in getroottable())) questSceneMarker <- null;

function initializeQuestScene(tracker) {
    local scene = eve.Scene();
    scene.beginBuild();
    scene.beginNode("quest.root", "Quest presentation");
    scene.beginNode("quest.slayer.marker", "Slayer completion marker");
    scene.setBuildVisible(false);
    scene.end();
    scene.end();
    if (!scene.remountBuildAs("rpg-classic.quests")) throw "quest scene mount failed";
    questSceneMarker = scene.getNodeRefAt("rpg-classic.quests", "quest.slayer.marker");
    if (tracker != null && tracker.getState("quest.slayer") == "completed") {
        if (!questSceneMarker.setVisible(true)) throw "quest scene projection failed";
    }
}

function onQuestCompleted(envelope) {
    if (envelope.getType() != "quest.completed" || envelope.getSubject() != "quest.slayer") return false;
    if (questSceneMarker == null) return false;
    return questSceneMarker.setVisible(true);
}

function recordQuestCompletion(tracker, questId) {
    if (tracker == null || tracker.getState(questId) != "completed") return false;
    // Script-created logs require an explicit UUID. This transient presentation
    // occurrence uses gameplay RNG; it is not a durable or security identity.
    local eventId = "";
    for (local i = 0; i < 32; i += 1) {
        if (i == 8 || i == 12 || i == 16 || i == 20) eventId += "-";
        eventId += format("%x", i == 12 ? 4 : (i == 16 ? 8 + rand() % 4 : rand() % 16));
    }
    local appended = questSceneLog.append(eventId, "quest.completed", "rpg-classic", questId, "", "", 0, 0, "{}");
    if (!appended.ok) return false;
    if (questId != "quest.slayer") return true;
    return onQuestCompleted(questSceneLog.at(questSceneLog.size() - 1));
}

function questSceneMarkerVisible() {
    return questSceneMarker != null && questSceneMarker.isVisible();
}

// Restore/restart projects canonical quest state without inventing completion events.
function synchronizeQuestScene(tracker) {
    if (questSceneMarker == null) return;
    local visible = tracker != null && tracker.getState("quest.slayer") == "completed";
    if (questSceneMarker.isVisible() != visible && !questSceneMarker.setVisible(visible))
        throw "quest scene synchronization failed";
}
