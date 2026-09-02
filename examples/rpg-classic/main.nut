// ============================================================================
// EVEngine RPG 模块示例 —— 「经典回合制」
//
// 回合制战斗 + 职业/特征/等级/任务/GameState（数据驱动）。
// 战斗：1/2/3 行动，C 查看状态（占位），R 重开。
// ============================================================================

persist rpg = null
persist gs = null
persist player = null
persist companion = null
persist party = null
persist enemy = null
persist enemies = []
persist selectedEnemyIndex = 0
persist battle = null
persist quest = null
persist wave = 1
persist gold = 0
persist kills = 0
persist state = "idle"
persist log = []
persist hitFlash = { player = 0.0, enemy = 0.0 }
persist worldX = 2
persist worldY = 5
persist facingX = 1
persist facingY = 0
persist worldMapId = "village"
persist activeEncounter = ""
persist activeEncounterId = ""
persist activeConversation = ""
persist activeSaveSlot = 1
persist newRunBeforeFirstSave = false
persist masterVolume = 1.0
persist preferredLanguage = "zh-CN"
persist pauseReturnScreen = "explore"
persist acceptedQuestContent = null
persist acceptedDialogueContent = null
persist acceptedShopContent = null
persist acceptedEncounterContent = null
persist acceptedBattleTacticsContent = null
persist acceptedStoryEventContent = null
persist acceptedLocalizationContent = null
persist storyEvent = null
persist text = eve.I18n()

const BASE_HP = 100.0;
const BASE_MP = 40.0;
const SAVE_CONTENT_VERSION = "rpg-classic.content.v9";
const PREVIOUS_SAVE_CONTENT_VERSION = "rpg-classic.content.v8";
const PRE_LOCALIZATION_SAVE_CONTENT_VERSION = "rpg-classic.content.v7";
const SINGLE_ACTOR_SAVE_CONTENT_VERSION = "rpg-classic.content.v6";
const OLDER_SAVE_CONTENT_VERSION = "rpg-classic.content.v5";
const LEGACY_QUEST_CONTENT_VERSION = "rpg-classic.content.v4";
const SAVE_SLOT_COUNT = 3;
const LEGACY_SAVE_PRIMARY_PATH = "rpg-classic-save.json";
const LEGACY_SAVE_BACKUP_PATH = "rpg-classic-save.backup.json";
const SETTINGS_PATH = "rpg-classic-settings.txt";
const SETTINGS_SCHEMA = "rpg-classic.settings.v2";
const LEGACY_SETTINGS_SCHEMA = "rpg-classic.settings.v1";

inv <- null
saveFs <- null
saveSession <- null
bag <- null
equip <- null
statPoints <- 0
screen <- "title"
worldLayer <- null
worldPath <- null
worldObjectContract <- ""
worldState <- null
returnScreen <- "explore"
dialogueReady <- false
activeDialogueQuest <- ""
activeSpeakerName <- ""
titleMessage <- "选择一个存档槽开始冒险"
pauseMessage <- "游戏已暂停"
pendingDeleteSlot <- 0
pendingReturnToTitle <- false
pendingQuestContent <- null
pendingDialogueContent <- null
pendingShopContent <- null
pendingEncounterContent <- null
pendingBattleTacticsContent <- null
pendingStoryEventContent <- null
pendingLocalizationContent <- null
storyWaitRemaining <- 0.0

shop <- []

function randf(a, b) { return a + (b - a) * (rand().tofloat() / RAND_MAX.tofloat()); }
function roundi(v) { return floor(v + 0.5).tointeger(); }
function logLine(text) {
    log.push(text);
    while (log.len() > 8) log.remove(0);
}
function logMessage(key, params = {}) { logLine(text.getWithParams("gameplayLog." + key, params)); }

function tr(key) { return text.get(key); }
function trp(key, params) { return text.getWithParams(key, params); }
function localizedId(namespaceName, id, field = "name") {
    local key = "content." + namespaceName + "." + id + "." + field;
    return text.has(key) ? tr(key) : key;
}
function itemName(id) { return localizedId("item", id); }
function attributeName(id) { return localizedId("attribute", id); }
function questTitle(id) { return localizedId("quest", id, "title"); }
function encounterName(id) { return localizedId("encounter", id); }
function speakerName(questId) { return localizedId("speaker", questId == "quest.slayer" ? "elder" : "ranger"); }
function worldObjectName(objectId) { return localizedId("object", objectId); }
function encounterMemberName(encounterId, memberIndex) {
    local memberId = encounterId == "slime.forest" ? (memberIndex == 0 ? "forest.alpha" : "forest.beta") :
                     (encounterId == "slime.west" ? "west" : "north");
    return localizedId("encounterMember", encounterId + "." + memberId);
}

function savePrimaryPath(slot) { return "rpg-classic-slot" + slot + ".json"; }
function saveBackupPath(slot) { return "rpg-classic-slot" + slot + ".backup.json"; }

function applyMasterVolume() {
    if (has_module("audio")) audio.setVolume(masterVolume);
}

function loadSettings() {
    local encoded = saveFs.readText(SETTINGS_PATH);
    local currentPrefix = SETTINGS_SCHEMA + "|";
    local legacyPrefix = LEGACY_SETTINGS_SCHEMA + "|";
    if (encoded == "" || (encoded.find(currentPrefix) != 0 && encoded.find(legacyPrefix) != 0)) {
        applyMasterVolume();
        return false;
    }
    local current = encoded.find(currentPrefix) == 0;
    local tail = encoded.slice(current ? currentPrefix.len() : legacyPrefix.len());
    local separator = tail.find("|");
    local volumeText = separator == null ? tail : tail.slice(0, separator);
    local parsed = volumeText.tointeger();
    if (parsed < 0 || parsed > 10 || volumeText != ("" + parsed)) {
        applyMasterVolume();
        return false;
    }
    if (current) {
        if (separator == null || separator == tail.len() - 1) {
            applyMasterVolume();
            return false;
        }
        local language = tail.slice(separator + 1);
        if (language != "zh-CN" && language != "en") {
            applyMasterVolume();
            return false;
        }
        preferredLanguage = language;
    }
    masterVolume = parsed.tofloat() / 10.0;
    applyMasterVolume();
    return true;
}

function saveSettings() {
    local encoded = SETTINGS_SCHEMA + "|" + roundi(masterVolume * 10.0) + "|" + preferredLanguage;
    return saveFs.writeTextAtomic(SETTINGS_PATH, encoded) != 0;
}

function refreshSettingsUI() {
    local percent = roundi(masterVolume * 100.0);
    local volumeText = trp("ui.settings.volume", {percent = percent});
    ui.select("title"); ui.setText("title_volume", volumeText);
    ui.select("pause"); ui.setText("pause_volume", volumeText);
}

function adjustMasterVolume(delta) {
    masterVolume += delta;
    if (masterVolume < 0.0) masterVolume = 0.0;
    if (masterVolume > 1.0) masterVolume = 1.0;
    applyMasterVolume();
    if (!saveSettings()) pauseMessage = tr("ui.settings.writeFailed");
    refreshSettingsUI();
}

function readTextFile(path) {
    local handle = file(path, "r");
    if (handle == null) return null;
    local content = handle.read();
    handle.close();
    return content;
}

function rebuildShopProjection() {
    shop.clear();
    for (local i = 0; i < rpg.getShopOfferCount(); i += 1) {
        shop.push({
            id = rpg.getShopOfferId(i), itemId = rpg.getShopOfferItemId(i),
            name = rpg.getShopOfferName(i), desc = rpg.getShopOfferDescription(i),
            buyPrice = rpg.getShopOfferBuyPrice(i), sellPrice = rpg.getShopOfferSellPrice(i)
        });
    }
}

function shopItemId(offerId) {
    foreach (entry in shop) if (entry.id == offerId) return entry.itemId;
    return offerId;
}

function registerContent() {
    pendingQuestContent = null;
    pendingShopContent = null;
    pendingEncounterContent = null;
    pendingBattleTacticsContent = null;
    pendingStoryEventContent = null;
    local list = [
        ["effects", "registerEffectsFromJson"],
        ["skills", "registerSkillsFromJson"],
        ["traits", "registerTraitsFromJson"],
        ["classes", "registerClassesFromJson"]
    ];
    foreach (entry in list) {
        local json = readTextFile("data/" + entry[0] + ".json");
        local n = (json != null) ? rpg[entry[1]](json) : 0;
        print(format("rpg-classic: loaded %d from %s\n", n, "data/" + entry[0] + ".json"));
    }
    local itemsJson = readTextFile("data/items.json");
    local itemsLoaded = (itemsJson != null) ? inv.registerItemsFromJson(itemsJson) : 0;
    print(format("rpg-classic: loaded %d items\n", itemsLoaded));

    local shopJson = readTextFile("data/shop.json");
    if (shopJson == null) {
        print("rpg-classic: shop catalogue missing; previous catalogue retained\n");
        return false;
    }
    local shopResult = rpg.replaceShopOffersFromJson(shopJson);
    if (!shopResult.ok) {
        print("rpg-classic: shop catalogue rejected; previous catalogue retained: " +
              shopResult.status.summary + "\n");
        return false;
    }
    pendingShopContent = shopJson;
    rebuildShopProjection();

    local questsJson = readTextFile("data/quests.json");
    if (questsJson == null) {
        print("rpg-classic: quest catalogue missing; previous catalogue retained\n");
        return false;
    }
    local questResult = rpg.replaceQuestsFromJson(questsJson);
    if (!questResult.ok) {
        print("rpg-classic: quest catalogue rejected; previous catalogue retained: " +
              questResult.status.summary + "\n");
        return false;
    }
    pendingQuestContent = questsJson;
    print(format("rpg-classic: atomically loaded %d quests\n", questResult.value));
    rpg.clearSkillDamage();
    rpg.registerSkillDamage("skill.strike", "hp", "a.attack - b.defense", "", 0.0, 100);
    rpg.registerSkillDamage("skill.fireball", "hp", "a.attack * 2", "fire", 0.05, 95);
    rpg.registerSkillDamage("skill.cleave", "hp", "a.attack * 1.6", "", 0.0, 100);
    rpg.registerSkillDamage("skill.self_heal", "hpHeal", "a.attack", "", 0.0, 100);
    rpg.registerSkillDamage("skill.ranger_aid", "hpHeal", "a.attack * 1.2", "", 0.0, 100);
    rpg.registerSkillDamage("skill.enemy_claw", "hp", "a.attack - b.defense", "", 0.0, 100);

    local encounterJson = readTextFile("data/encounters.json");
    if (encounterJson == null) return false;
    local encounterResult = rpg.replaceEncountersFromJson(encounterJson);
    if (!encounterResult.ok) {
        print("rpg-classic: encounter catalogue rejected: " + encounterResult.status.summary + "\n");
        return false;
    }
    pendingEncounterContent = encounterJson;

    local tacticsJson = readTextFile("data/battle-tactics.json");
    if (tacticsJson == null) return false;
    local tacticsResult = rpg.replaceBattleTacticsFromJson(tacticsJson);
    if (!tacticsResult.ok) {
        print("rpg-classic: battle tactics rejected: " + tacticsResult.status.summary + "\n");
        return false;
    }
    pendingBattleTacticsContent = tacticsJson;

    local storyEventJson = readTextFile("data/story-events.json");
    if (storyEventJson == null) return false;
    local storyEventResult = rpg.replaceStoryEventsFromJson(storyEventJson);
    if (!storyEventResult.ok) {
        print("rpg-classic: story events rejected: " + storyEventResult.status.summary + "\n");
        return false;
    }
    pendingStoryEventContent = storyEventJson;

    rpg.registerItemStatsFromJson("iron_sword",
        @"[ {""attribute"":""attack"",""op"":""add"",""value"":8} ]");
    rpg.registerItemStatsFromJson("leather_armor",
        @"[ {""attribute"":""defense"",""op"":""add"",""value"":6} ]");
    return true;
}

function setupInventory() {
    if (bag == null) bag = inv.newBag(12);
    else bag.clear();
    if (equip == null) equip = inv.newEquipmentSet();
    equip.setId("hero");
    equip.defineSlot("weapon");
    equip.defineSlot("armor");
    bag.addItem("potion", 1);
    bag.addItem("iron_sword", 1);
}

function equipItem(itemId, slot) {
    local slotIndex = bag.findItem(itemId);
    if (slotIndex < 0) return false;
    if (!equip.equipFromBag(slot, bag, slotIndex)) return false;
    rpg.syncEquipModifiers(player, equip);
    logMessage("inventory.equipped", {item = itemName(itemId)});
    return true;
}

function unequipSlot(slot) {
    if (equip.isSlotEmpty(slot)) return;
    local itemId = equip.getSlotItemId(slot);
    if (equip.unequipToBag(slot, bag)) {
        rpg.syncEquipModifiers(player, equip);
        logMessage("inventory.unequipped", {item = itemName(itemId)});
    }
}

function usePotion() {
    local s = bag.findItem("potion");
    if (s < 0) { logMessage("inventory.noPotion"); return; }
    bag.removeAt(s, 1);
    player.heal("hp", 25.0);
    logMessage("inventory.potionUsed", {amount = 25});
}

function buyItem(offerId) {
    local bought = rpg.buyShopOffer(gs, bag, "gold", offerId, 1);
    if (!bought.ok) { logMessage("inventory.buyFailed", {error = bought.error.message}); return false; }
    gold = gs.getVariable("gold").tointeger();
    local itemId = shopItemId(offerId);
    logMessage("inventory.bought", {item = itemName(itemId)});
    return true;
}

function sellItem(offerId) {
    local sold = rpg.sellShopOffer(gs, bag, "gold", offerId, 1);
    if (!sold.ok) { logMessage("inventory.sellFailed", {error = sold.error.message}); return false; }
    gold = gs.getVariable("gold").tointeger();
    local itemId = shopItemId(offerId);
    logMessage("inventory.sold", {item = itemName(itemId)});
    return true;
}

function allocate(attr, delta) {
    if (statPoints <= 0) { logMessage("inventory.noPoints"); return; }
    statPoints -= 1;
    player.modifyBaseAttribute(attr, delta);
    logMessage("inventory.allocated", {attribute = attributeName(attr)});
}

function bagSummary() {
    local text = "";
    for (local i = 0; i < bag.getSlotCount(); i += 1) {
        if (bag.isSlotEmpty(i)) continue;
        local id = bag.getSlotItemId(i);
        local qty = bag.getSlotQuantity(i);
        local line = itemName(id) + " x" + qty;
        text = (text == "") ? line : (text + "\n" + line);
    }
    return (text == "") ? tr("gameplayLog.inventory.empty") : text;
}

function worldMapPath(mapId) {
    if (mapId == "village") return "data/village.json";
    if (mapId == "forest") return "data/forest.json";
    return "";
}

function worldMapTitle(mapId) { return localizedId("map", mapId); }
function worldMapCode(mapId) { return mapId == "forest" ? 1 : 0; }
function worldMapIdFromCode(code) { return code == 0 ? "village" : (code == 1 ? "forest" : ""); }

function loadWorldMap(mapId, x, y, fx, fy) {
    local path = worldMapPath(mapId);
    if (path == "" || worldObjectContract == "") return false;
    local admitted = map.loadFromFileWithObjectContract(path, worldObjectContract);
    if (!admitted.ok) {
        print("rpg-classic: map contract rejected " + path + ": " + admitted.status.summary + "\n");
        return false;
    }
    local candidateLayer = map.getLayer(0);
    if (candidateLayer == null) return false;
    local candidatePath = map.newPathfinder(candidateLayer);
    if (candidatePath == null) return false;
    candidatePath.blockGid(2);
    candidatePath.syncFromLayer();
    if (!candidatePath.isWalkable(x, y)) return false;
    worldLayer = candidateLayer;
    worldPath = candidatePath;
    worldMapId = mapId;
    worldX = x; worldY = y; facingX = fx; facingY = fy;
    return true;
}

function setupWorld() {
    if (worldLayer != null) return;
    if (!loadWorldMap(worldMapId, worldX, worldY, facingX, facingY))
        throw "rpg-classic: failed to load initial world map";
}

function reportDialogueDiagnostics() {
    for (local i = 0; i < dialogueFlow.getDiagnosticCount(); i += 1) {
        print("rpg-classic dialogue " + dialogueFlow.getDiagnosticSeverity(i) + ": " +
              dialogueFlow.getDiagnosticPath(i) + ":" + dialogueFlow.getDiagnosticLine(i) + " " +
              dialogueFlow.getDiagnosticMessage(i) + "\n");
    }
}

function loadLocalizationContent() {
    pendingLocalizationContent = null;
    local source = readTextFile("data/localization.json");
    if (source == null) {
        print("rpg-classic: localization bundle missing\n");
        return false;
    }
    local admitted = text.replaceBundleFromJson(source);
    if (!admitted.ok) {
        print("rpg-classic: localization bundle rejected: " + admitted.status.summary + "\n");
        return false;
    }
    local selected = text.selectLanguage(preferredLanguage);
    if (!selected.ok) {
        print("rpg-classic: preferred localization language is unavailable: " +
              selected.status.summary + "\n");
        return false;
    }
    pendingLocalizationContent = source;
    return true;
}

function loadDialogueContent(reload = false) {
    dialogueReady = false;
    pendingDialogueContent = null;
    local source = readTextFile("data/village-dialogue.dnut");
    if (source == null) { print("rpg-classic: dialogue source missing\n"); return false; }
    local loaded = reload ? dialogueFlow.reloadFromDnut(source, "data/village-dialogue.dnut")
                          : dialogueFlow.loadFromDnut(source, "data/village-dialogue.dnut");
    if (loaded <= 0 || !dialogueFlow.lintAll()) {
        reportDialogueDiagnostics();
        return false;
    }
    pendingDialogueContent = source;
    dialogueReady = true;
    return true;
}

function validateWorldContentLinks() {
    local dialogueStates = ["offer", "active", "turnin", "completed"];
    local mapIds = ["village", "forest"];
    foreach (mapId in mapIds) {
        local candidate = eve.Map();
        local path = worldMapPath(mapId);
        local admitted = candidate.loadFromFileWithObjectContract(path, worldObjectContract);
        if (!admitted.ok) {
            print("rpg-classic: cross-content map rejected " + path + ": " +
                  admitted.status.summary + "\n");
            return false;
        }
        for (local i = 0; i < candidate.getObjectCount(); i += 1) {
            local type = candidate.getObjectType(i);
            local objectName = candidate.getObjectName(i);
            if (type == "quest_npc") {
                local questId = candidate.getObjectProperty(i, "questId", "");
                local dialogueBase = candidate.getObjectProperty(i, "dialogueBase", "");
                if (!rpg.hasQuestDefinition(questId)) {
                    print("rpg-classic: " + mapId + "/" + objectName +
                          " references unknown quest " + questId + "\n");
                    return false;
                }
                foreach (stateName in dialogueStates) {
                    local conversationId = dialogueBase + "." + stateName;
                    if (!dialogueFlow.hasConversation(conversationId)) {
                        print("rpg-classic: " + mapId + "/" + objectName +
                              " references missing conversation " + conversationId + "\n");
                        return false;
                    }
                }
            } else if (type == "loot") {
                local requiredQuest = candidate.getObjectProperty(i, "requiredQuest", "");
                if (requiredQuest != "" && !rpg.hasQuestDefinition(requiredQuest)) {
                    print("rpg-classic: " + mapId + "/" + objectName +
                          " references unknown required quest " + requiredQuest + "\n");
                    return false;
                }
            } else if (type == "portal") {
                local targetMap = candidate.getObjectProperty(i, "targetMap", "");
                if (worldMapPath(targetMap) == "") {
                    print("rpg-classic: " + mapId + "/" + objectName +
                          " references unknown target map " + targetMap + "\n");
                    return false;
                }
            } else if (type == "encounter") {
                local encounterId = candidate.getObjectProperty(i, "encounterId", "");
                if (!rpg.hasEncounter(encounterId)) {
                    print("rpg-classic: " + mapId + "/" + objectName +
                          " references unknown encounter " + encounterId + "\n");
                    return false;
                }
            }
        }
    }
    return true;
}

function validateProductLocalization() {
    local requiredKeys = [
        "content.map.village.name", "content.map.forest.name",
        "content.class.warrior.name", "content.trait.mighty.name", "content.trait.fire_guard.name",
        "content.speaker.elder.name", "content.speaker.ranger.name", "content.speaker.story.name",
        "content.object.old_chest.name", "content.object.forest_cache.name",
        "content.item.potion.name", "content.item.iron_sword.name", "content.item.leather_armor.name",
        "content.quest.quest.slayer.title", "content.quest.quest.ranger_cache.title",
        "content.shop.potion.name", "content.shop.potion.description",
        "content.shop.iron_sword.name", "content.shop.iron_sword.description",
        "content.shop.leather_armor.name", "content.shop.leather_armor.description",
        "content.skill.skill.strike.name", "content.skill.skill.fireball.name",
        "content.skill.skill.self_heal.name", "content.skill.skill.ranger_aid.name",
        "content.encounter.slime.west.name", "content.encounter.slime.north.name",
        "content.encounter.slime.forest.name",
        "content.encounterMember.slime.west.west.name",
        "content.encounterMember.slime.north.north.name",
        "content.encounterMember.slime.forest.forest.alpha.name",
        "content.encounterMember.slime.forest.forest.beta.name",
        "content.actor.player.name", "content.actor.ranger.name", "content.actor.enemy.name",
        "content.attribute.attack.name", "content.attribute.defense.name",
        "content.attribute.hp.name", "content.attribute.mp.name", "content.attribute.gold.name"
    ];
    for (local languageIndex = 0; languageIndex < text.getLanguageCount(); languageIndex += 1) {
        local language = text.getLanguageAt(languageIndex);
        local dialogueValidation = dialogueFlow.validateLocalization(text, language);
        if (!dialogueValidation.ok) {
            print("rpg-classic: locale " + language + " dialogue links rejected: " +
                  dialogueValidation.status.summary + "\n");
            return false;
        }
    }
    foreach (key in requiredKeys) {
        local coverage = text.validateKeyCoverage(key);
        if (!coverage.ok) {
            print("rpg-classic: product key coverage rejected: " + coverage.status.summary + "\n");
            return false;
        }
    }
    return true;
}

function restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,
                                 previousBattleTactics, previousStoryEvents, previousLocalization) {
    local restored = true;
    if (previousLocalization != null) {
        local previousLanguage = preferredLanguage;
        local localizationRollback = text.replaceBundleFromJson(previousLocalization);
        local languageRollback = localizationRollback.ok ? text.selectLanguage(previousLanguage) : null;
        if (!localizationRollback.ok || languageRollback == null || !languageRollback.ok) {
            print("rpg-classic: fatal localization bundle rollback failure\n");
            restored = false;
        }
    } else text.clear();
    if (previousQuest != null) {
        local questRollback = rpg.replaceQuestsFromJson(previousQuest);
        if (!questRollback.ok) {
            print("rpg-classic: fatal quest catalogue rollback failure: " +
                  questRollback.status.summary + "\n");
            restored = false;
        }
    }
    if (previousDialogue != null) {
        local loaded = dialogueFlow.reloadFromDnut(previousDialogue, "data/village-dialogue.dnut");
        if (loaded <= 0 || !dialogueFlow.lintAll()) {
            print("rpg-classic: fatal dialogue catalogue rollback failure\n");
            reportDialogueDiagnostics();
            restored = false;
        }
    }
    if (previousShop != null) {
        local shopRollback = rpg.replaceShopOffersFromJson(previousShop);
        if (!shopRollback.ok) {
            print("rpg-classic: fatal shop catalogue rollback failure: " +
                  shopRollback.status.summary + "\n");
            restored = false;
        } else rebuildShopProjection();
    } else {
        rpg.clearShopOffers();
        rebuildShopProjection();
    }
    if (previousEncounter != null) {
        local encounterRollback = rpg.replaceEncountersFromJson(previousEncounter);
        if (!encounterRollback.ok) restored = false;
    } else rpg.clearEncounters();
    if (previousBattleTactics != null) {
        local tacticsRollback = rpg.replaceBattleTacticsFromJson(previousBattleTactics);
        if (!tacticsRollback.ok) restored = false;
    } else rpg.clearBattleTactics();
    if (previousStoryEvents != null) {
        local storyRollback = rpg.replaceStoryEventsFromJson(previousStoryEvents);
        if (!storyRollback.ok) restored = false;
    } else rpg.clearStoryEvents();
    dialogueReady = restored && previousDialogue != null;
    pendingQuestContent = null;
    pendingDialogueContent = null;
    pendingShopContent = null;
    pendingEncounterContent = null;
    pendingBattleTacticsContent = null;
    pendingStoryEventContent = null;
    pendingLocalizationContent = null;
    return restored;
}

function publishNarrativeContentPackage(reload = false) {
    local previousQuest = acceptedQuestContent;
    local previousDialogue = acceptedDialogueContent;
    local previousShop = acceptedShopContent;
    local previousEncounter = acceptedEncounterContent;
    local previousBattleTactics = acceptedBattleTacticsContent;
    local previousStoryEvents = acceptedStoryEventContent;
    local previousLocalization = acceptedLocalizationContent;
    if (!registerContent()) {
        restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,
                                previousBattleTactics, previousStoryEvents, previousLocalization);
        print("rpg-classic: content package rejected; accepted content restored\n");
        return false;
    }
    if (!loadLocalizationContent()) {
        restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,
                                previousBattleTactics, previousStoryEvents, previousLocalization);
        print("rpg-classic: localization package rejected; accepted content restored\n");
        return false;
    }
    if (!loadDialogueContent(reload)) {
        restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,
                                previousBattleTactics, previousStoryEvents, previousLocalization);
        print("rpg-classic: narrative package rejected; accepted content restored\n");
        return false;
    }
    if (!validateProductLocalization()) {
        restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,
                                previousBattleTactics, previousStoryEvents, previousLocalization);
        print("rpg-classic: product localization links rejected\n");
        return false;
    }
    if (!validateWorldContentLinks()) {
        restoreNarrativeContent(previousQuest, previousDialogue, previousShop, previousEncounter,
                                previousBattleTactics, previousStoryEvents, previousLocalization);
        print("rpg-classic: narrative package links rejected; accepted content restored\n");
        return false;
    }
    acceptedQuestContent = pendingQuestContent;
    acceptedDialogueContent = pendingDialogueContent;
    acceptedShopContent = pendingShopContent;
    acceptedEncounterContent = pendingEncounterContent;
    acceptedBattleTacticsContent = pendingBattleTacticsContent;
    acceptedStoryEventContent = pendingStoryEventContent;
    acceptedLocalizationContent = pendingLocalizationContent;
    pendingQuestContent = null;
    pendingDialogueContent = null;
    pendingShopContent = null;
    pendingEncounterContent = null;
    pendingBattleTacticsContent = null;
    pendingStoryEventContent = null;
    pendingLocalizationContent = null;
    dialogueReady = true;
    print("rpg-classic: narrative content package committed\n");
    return true;
}

function choiceLabel(id) {
    if (id == "accept") return tr("ui.dialogue.accept");
    if (id == "later") return tr("ui.dialogue.later");
    if (id == "claim") return tr("ui.dialogue.claim");
    return id;
}

function refreshDialogueUI() {
    ui.select("dialogue");
    if (!dialogueFlow.isActive()) {
        ui.setHostModal(false);
        setScreen("explore");
        activeConversation = "";
        activeDialogueQuest = "";
        activeSpeakerName = "";
        if (storyEvent != null && storyEvent.isActive() &&
            storyEvent.getStepKind() == "dialogue") {
            local advanced = storyEvent.advance(gs);
            if (!advanced.ok) {
                logMessage("story.advanceFailed", {error = advanced.status.summary});
                return;
            }
            presentStoryEventStep();
        }
        return;
    }
    ui.setText("speaker", activeSpeakerName);
    ui.setText("body", dialogueFlow.getText());
    local kind = dialogueFlow.getNodeKind();
    if (kind == "choice") {
        local choices = "";
        for (local i = 0; i < dialogueFlow.getRouteCount(); i += 1) {
            local route = dialogueFlow.getRouteId(i);
            choices += (i + 1) + ". " + choiceLabel(route) + "\n";
        }
        ui.setText("choices", choices);
        ui.setText("dialogue_help", tr("ui.dialogue.choose"));
    } else {
        ui.setText("choices", "");
        ui.setText("dialogue_help", tr("ui.dialogue.continue"));
    }
}

function presentStoryEventStep() {
    while (storyEvent != null && storyEvent.isActive()) {
        local kind = storyEvent.getStepKind();
        if (kind == "dialogue") {
            local conversationId = storyEvent.getReference();
            local started = dialogueFlow.startChecked(conversationId, {});
            if (!started.ok) {
                reportDialogueDiagnostics();
                logMessage("story.dialogueStartFailed", {error = started.status.summary});
                return false;
            }
            activeConversation = conversationId;
            activeDialogueQuest = "";
            activeSpeakerName = localizedId("speaker", "story");
            setScreen("dialogue");
            ui.select("dialogue"); ui.setHostModal(true);
            refreshDialogueUI();
            return true;
        }
        if (kind == "wait") {
            storyWaitRemaining = storyEvent.getDuration();
            return true;
        }
        if (kind == "message") {
            logLine(tr(storyEvent.getReference()));
        } else if (kind == "move") {
            if (storyEvent.getActorId() != "player" ||
                !worldPath.isWalkable(storyEvent.getX().tointeger(), storyEvent.getY().tointeger())) {
                logMessage("story.moveRejected");
                return false;
            }
            worldX = storyEvent.getX().tointeger();
            worldY = storyEvent.getY().tointeger();
        } else if (kind == "camera") {
            logMessage("story.camera", {x = storyEvent.getX(), y = storyEvent.getY()});
        } else {
            logMessage("story.unknownStep");
            return false;
        }
        local advanced = storyEvent.advance(gs);
        if (!advanced.ok) {
            logMessage("story.advanceFailed", {error = advanced.status.summary});
            return false;
        }
    }
    storyWaitRemaining = 0.0;
    return true;
}

function startStoryEvent(eventId) {
    local candidate = rpg.newStoryEventSession();
    local begun = candidate.begin(eventId, gs);
    if (!begun.ok) return false;
    storyEvent = candidate;
    storyWaitRemaining = 0.0;
    return presentStoryEventStep();
}

function resumePendingStoryEvent() {
    local scope = "story.event:forest.arrival";
    if (gs.hasSelfVariable(scope, "cursor") &&
        (!gs.hasSelfVariable(scope, "completed") || gs.getSelfVariable(scope, "completed") != 1.0))
        return startStoryEvent("forest.arrival");
    return false;
}

function beginQuestNpcDialogue(index) {
    if (!dialogueReady) { logMessage("dialogue.notReady"); return false; }
    local questId = map.getObjectProperty(index, "questId", "");
    local dialogueBase = map.getObjectProperty(index, "dialogueBase", "");
    local rawSpeakerName = map.getObjectProperty(index, "speakerName", "");
    local questState = quest.getState(questId);
    if (questId == "" || dialogueBase == "" || rawSpeakerName == "" || questState == "") {
        logMessage("dialogue.invalidNpc");
        return false;
    }
    local suffix = "offer";
    if (questState == "active") suffix = "active";
    else if (questState == "ready") suffix = "turnin";
    else if (questState == "completed") suffix = "completed";
    local id = dialogueBase + "." + suffix;
    local started = dialogueFlow.startChecked(id, {});
    if (!started.ok) {
        reportDialogueDiagnostics();
        logMessage("dialogue.startFailed", {error = started.status.summary});
        return false;
    }
    activeConversation = id;
    activeDialogueQuest = questId;
    activeSpeakerName = speakerName(questId);
    setScreen("dialogue");
    ui.select("dialogue"); ui.setHostModal(true);
    refreshDialogueUI();
    return true;
}

function advanceDialogue() {
    if (!dialogueFlow.isActive()) return;
    if (dialogueFlow.getNodeKind() == "choice") return;
    local advanced = dialogueFlow.advanceChecked();
    if (!advanced.ok) logMessage("dialogue.advanceFailed", {error = advanced.status.summary});
    refreshDialogueUI();
}

function selectDialogueChoice(index) {
    if (!dialogueFlow.isActive() || dialogueFlow.getNodeKind() != "choice" ||
        index < 0 || index >= dialogueFlow.getRouteCount()) return;
    local route = dialogueFlow.getRouteId(index);
    local selected = dialogueFlow.select(route);
    if (!selected.ok) { logMessage("dialogue.choiceFailed", {error = selected.status.summary}); return; }
    if (route == "accept" && activeDialogueQuest != "") {
        if (quest.activate(activeDialogueQuest))
            logMessage("dialogue.accepted", {quest = questTitle(activeDialogueQuest)});
    } else if (route == "claim" && activeDialogueQuest != "") {
        claimQuestRewards(activeDialogueQuest);
    }
    refreshDialogueUI();
}

function objectAt(x, y, type = "") {
    return map.findObjectAt(x.tofloat(), y.tofloat(), type);
}

function objectConsumed(index) {
    return index >= 0 && worldState != null &&
           worldState.isObjectConsumed(worldMapId, map.getObjectName(index));
}

function markObjectConsumed(index) {
    if (index < 0 || worldState == null) return false;
    local consumed = worldState.consumeObject(worldMapId, map.getObjectName(index));
    if (!consumed.ok) logMessage("dialogue.worldStateFailed", {error = consumed.status.summary});
    return consumed.ok;
}

function migrateLegacyWorldState(savedKills) {
    if (gs.isSwitchOn("save.world")) return false;
    local encounters = ["village.slime_west", "village.slime_north", "forest.forest_slime"];
    for (local i = 0; i < encounters.len() && i < savedKills; i += 1) {
        local separator = encounters[i].find(".");
        worldState.consumeObject(encounters[i].slice(0, separator), encounters[i].slice(separator + 1));
    }
    gs.switchOn("save.world");
    return true;
}

function migrateUnscopedWorldEvents() {
    local migrations = [
        ["old_chest", "village.old_chest"],
        ["slime_west", "village.slime_west"],
        ["slime_north", "village.slime_north"],
        ["slime_east", "forest.forest_slime"]
    ];
    foreach (migration in migrations)
        if (gs.isSwitchOn("world.event." + migration[0])) {
            local separator = migration[1].find(".");
            worldState.consumeObject(migration[1].slice(0, separator), migration[1].slice(separator + 1));
        }
}

function migrateScopedWorldEvents() {
    local objects = [
        ["village", "old_chest"], ["village", "slime_west"], ["village", "slime_north"],
        ["forest", "forest_slime"], ["forest", "forest_cache"]
    ];
    local migrated = false;
    foreach (entry in objects) {
        if (gs.isSwitchOn("world.event." + entry[0] + "." + entry[1])) {
            local result = worldState.consumeObject(entry[0], entry[1]);
            if (result.ok) migrated = true;
        }
    }
    return migrated;
}

function strictObjectInt(index, name) {
    local raw = map.getObjectProperty(index, name, "");
    if (raw == "") return null;
    local value = raw.tointeger();
    return raw == ("" + value) ? value : null;
}

function claimQuestRewards(questId) {
    local rewardText = "";
    for (local i = 0; i < quest.getRewardCount(questId); i += 1) {
        local type = quest.getRewardType(questId, i);
        local id = quest.getRewardId(questId, i);
        local amount = quest.getRewardAmount(questId, i).tointeger();
        if (type == "item")
            rewardText += (rewardText == "" ? "" : tr("gameplayLog.loot.separator")) +
                          trp("gameplayLog.loot.itemReward", {item = itemName(id), count = amount});
        else if (type == "attribute")
            rewardText += (rewardText == "" ? "" : tr("gameplayLog.loot.separator")) +
                          attributeName(id) + " " + amount;
    }
    local claimed = rpg.claimQuestRewards(quest, gs, bag, questId);
    if (!claimed.ok) {
        logMessage("quest.rewardFailed", {error = claimed.error.message});
        return false;
    }
    gold = gs.getVariable("gold").tointeger();
    logMessage("quest.completed", {quest = questTitle(questId), rewards = rewardText});
    return true;
}

function openLootObject(index) {
    if (objectConsumed(index)) { logMessage("loot.alreadySearched"); return false; }
    local requiredQuest = map.getObjectProperty(index, "requiredQuest", "");
    if (requiredQuest != "" && quest.getState(requiredQuest) != "active") {
        logMessage("loot.questUnknown");
        return false;
    }
    local itemId = map.getObjectProperty(index, "itemId", "");
    local itemCount = strictObjectInt(index, "itemCount");
    local goldAmount = strictObjectInt(index, "gold");
    local notifyTopic = map.getObjectProperty(index, "notifyTopic", "");
    local notifyTarget = map.getObjectProperty(index, "notifyTarget", "");
    local notifyAmount = strictObjectInt(index, "notifyAmount");
    local displayName = worldObjectName(map.getObjectName(index));
    if (itemCount == null) itemCount = 0;
    if (goldAmount == null) goldAmount = 0;
    if (notifyAmount == null) notifyAmount = 0;
    if ((itemId == "" && itemCount != 0) || itemCount < 0 || goldAmount < 0 || notifyAmount < 0 ||
        ((notifyTopic == "") != (notifyTarget == "")) ||
        (itemCount == 0 && goldAmount == 0 && notifyTopic == "")) {
        logMessage("loot.invalid", {object = displayName});
        return false;
    }
    local settled = rpg.collectWorldLoot(
        quest, gs, bag, worldMapId, map.getObjectName(index), requiredQuest,
        itemId, itemCount, goldAmount > 0 ? "gold" : "", goldAmount.tofloat(),
        notifyTopic, notifyTarget, notifyAmount);
    if (!settled.ok) {
        logMessage("loot.openFailed", {object = displayName, error = settled.error.message});
        return false;
    }
    gold = gs.getVariable("gold").tointeger();
    local rewards = itemCount > 0 ? trp("gameplayLog.loot.itemReward", {item = itemName(itemId), count = itemCount}) : "";
    if (itemCount > 0 && goldAmount > 0) rewards += tr("gameplayLog.loot.separator");
    if (goldAmount > 0) rewards += trp("gameplayLog.loot.goldReward", {amount = goldAmount});
    logMessage("loot.opened", {object = displayName, rewards = rewards});
    if (requiredQuest != "" && quest.getState(requiredQuest) == "ready")
        logMessage("quest.objectiveReady");
    return true;
}

function transitionThroughPortal(index) {
    local targetMap = map.getObjectProperty(index, "targetMap", "");
    local targetX = strictObjectInt(index, "targetX");
    local targetY = strictObjectInt(index, "targetY");
    local targetFacingX = strictObjectInt(index, "facingX");
    local targetFacingY = strictObjectInt(index, "facingY");
    if (worldMapPath(targetMap) == "" || targetX == null || targetY == null ||
        targetFacingX == null || targetFacingY == null ||
        abs(targetFacingX) + abs(targetFacingY) != 1) {
        logMessage("world.invalidPortal");
        return false;
    }
    local oldMap = worldMapId;
    local oldX = worldX, oldY = worldY, oldFx = facingX, oldFy = facingY;
    if (!loadWorldMap(targetMap, targetX, targetY, targetFacingX, targetFacingY)) {
        local rolledBack = loadWorldMap(oldMap, oldX, oldY, oldFx, oldFy);
        if (rolledBack)
            logMessage("world.targetLoadFailed");
        else
            logMessage("world.rollbackFailed");
        return false;
    }
    logMessage("world.entered", {area = worldMapTitle(worldMapId)});
    if (oldMap == "village" && worldMapId == "forest")
        startStoryEvent("forest.arrival");
    return true;
}

function beginEncounter(index) {
    if (index < 0 || objectConsumed(index)) return false;
    if (quest.getState("quest.slayer") == "inactive") {
        logMessage("world.questGate");
        return false;
    }
    activeEncounter = map.getObjectName(index);
    activeEncounterId = map.getObjectProperty(index, "encounterId", "");
    if (!rpg.hasEncounter(activeEncounterId)) {
        activeEncounter = "";
        activeEncounterId = "";
        return false;
    }
    releaseEncounterEnemies();
    wave = kills + 1;
    local memberCount = rpg.getEncounterMemberCount(activeEncounterId);
    for (local memberIndex = 0; memberIndex < memberCount; memberIndex += 1) {
        local member = rpg.newEncounterMemberActor(activeEncounterId, memberIndex);
        if (member == null) {
            releaseEncounterEnemies();
            activeEncounter = "";
            activeEncounterId = "";
            return false;
        }
        enemies.push(member);
    }
    if (enemies.len() == 0) {
        activeEncounter = "";
        activeEncounterId = "";
        return false;
    }
    selectedEnemyIndex = 0;
    enemy = enemies[0];
    battle = rpg.newBattle();
    local composed = party.addToBattle(battle, 0);
    if (!composed.ok) {
        logMessage("battle.partyComposeFailed", {error = composed.error.message});
        battle = null;
        releaseEncounterEnemies();
        return false;
    }
    foreach (member in enemies) battle.addActor(member, 1);
    battle.setPlayerSide(0);
    state = "idle";
    setScreen("battle");
    logMessage("battle.started", {encounter = encounterName(activeEncounterId)});
    return true;
}

function checkEncounter() {
    local index = objectAt(worldX, worldY, "encounter");
    if (index >= 0) beginEncounter(index);
}

function tryWorldMove(dx, dy) {
    local nx = worldX + dx;
    local ny = worldY + dy;
    facingX = dx; facingY = dy;
    if (!worldPath.isWalkable(nx, ny)) {
        logMessage("world.blocked");
        return false;
    }
    worldX = nx; worldY = ny;
    checkEncounter();
    return true;
}

function interactWorld() {
    local index = objectAt(worldX + facingX, worldY + facingY);
    if (index < 0) index = objectAt(worldX, worldY);
    if (index < 0) { logMessage("world.nothingHere"); return; }
    local type = map.getObjectType(index);
    if (type == "merchant") {
        logMessage("world.merchant");
        setScreen("adjust");
    } else if (type == "quest_npc") {
        beginQuestNpcDialogue(index);
    } else if (type == "loot") {
        openLootObject(index);
    } else if (type == "encounter") {
        if (objectConsumed(index)) logMessage("world.encounterCleared");
        else beginEncounter(index);
    } else if (type == "portal") {
        transitionThroughPortal(index);
    } else logMessage("world.noReaction");
}

function setScreen(s) {
    screen = s;
    ui.select("title"); ui.setHostVisible(s == "title");
    ui.select("pause"); ui.setHostVisible(s == "pause");
    ui.select("adjust"); ui.setHostVisible(s == "adjust");
    ui.select("status"); ui.setHostVisible(s == "status");
    ui.select("player"); ui.setHostVisible(s == "battle");
    ui.select("enemy"); ui.setHostVisible(s == "battle");
    ui.select("quest"); ui.setHostVisible(s != "title" && s != "pause");
    ui.select("world"); ui.setHostVisible(s == "explore");
    ui.select("dialogue"); ui.setHostVisible(s == "dialogue");
    if (s == "title") refreshTitleUI();
    if (s == "pause") refreshPauseUI();
    if (s == "adjust") refreshAdjustUI();
    if (s == "status") refreshStatusUI();
}

function refreshStatusUI() {
    ui.select("status");
    local s = trp("ui.status.stats", {
        className = localizedId("class", "warrior"), level = player.getLevel(),
        attack = roundi(player.getFinalAttribute("attack")),
        defense = roundi(player.getFinalAttribute("defense")),
        hp = roundi(player.getCurrent("hp")), maxHp = roundi(player.getMax("hp")),
        mp = roundi(player.getCurrent("mp")), maxMp = roundi(player.getMax("mp")), points = statPoints
    });
    ui.setText("st_stats", s);
    local weaponName = equip.isSlotEmpty("weapon") ? tr("ui.status.none") : itemName(equip.getSlotItemId("weapon"));
    local armorName = equip.isSlotEmpty("armor") ? tr("ui.status.none") : itemName(equip.getSlotItemId("armor"));
    ui.setText("st_eq", trp("ui.status.weapon", {item = weaponName}) + "\n" +
                        trp("ui.status.armor", {item = armorName}));
    ui.setText("st_bag", bagSummary());
}

function refreshAdjustUI() {
    ui.select("adjust");
    ui.setText("adj_points", trp("ui.adjust.points", {points = statPoints}));
    ui.setText("adj_gold", trp("ui.adjust.gold", {gold = gold}));
    for (local i = 0; i < 3; i += 1) {
        ui.setText("shop" + i, trp("ui.adjust.offer", {
            name = localizedId("shop", shop[i].id),
            description = localizedId("shop", shop[i].id, "description"),
            buy = shop[i].buyPrice, sell = shop[i].sellPrice
        }));
    }
    local weaponName = equip.isSlotEmpty("weapon") ? tr("ui.status.none") : itemName(equip.getSlotItemId("weapon"));
    local armorName = equip.isSlotEmpty("armor") ? tr("ui.status.none") : itemName(equip.getSlotItemId("armor"));
    ui.setText("eq_weapon", trp("ui.status.weapon", {item = weaponName}));
    ui.setText("eq_armor", trp("ui.status.armor", {item = armorName}));
    ui.setText("adj_bag", trp("ui.adjust.bag", {items = bagSummary()}));
}

function makePlayer() {
    local p = rpg.newActor();
    p.setBaseAttribute("attack", 18.0);
    p.setBaseAttribute("defense", 6.0);
    p.setBaseAttribute("hp", BASE_HP);
    p.setBaseAttribute("mp", BASE_MP);
    p.setCurrent("hp", BASE_HP);
    p.setCurrent("mp", BASE_MP);
    p.setXpToNext(50.0);
    p.setClass("class.warrior");
    return p;
}

function releaseEncounterEnemies() {
    foreach (member in enemies)
        if (member != null) member.release();
    enemies.clear();
    enemy = null;
    selectedEnemyIndex = 0;
}

function makeCompanion() {
    local p = rpg.newActor();
    p.setBaseAttribute("attack", 12.0);
    p.setBaseAttribute("defense", 8.0);
    p.setBaseAttribute("hp", 80.0);
    p.setBaseAttribute("mp", 20.0);
    p.setBaseAttribute("speed", 12.0);
    p.setCurrent("hp", 80.0);
    p.setCurrent("mp", 20.0);
    p.setXpToNext(50.0);
    p.learnSkill("skill.strike");
    p.learnSkill("skill.ranger_aid");
    return p;
}

function selectLivingEnemy(step = 0) {
    if (enemies.len() == 0) { enemy = null; return false; }
    for (local offset = 0; offset < enemies.len(); offset += 1) {
        local index = (selectedEnemyIndex + step + offset + enemies.len()) % enemies.len();
        if (!enemies[index].isDead("hp")) {
            selectedEnemyIndex = index;
            enemy = enemies[index];
            return true;
        }
    }
    enemy = null;
    return false;
}

function selectNextEnemy() {
    if (selectLivingEnemy(1))
        logMessage("battle.targetSelected", {target = encounterMemberName(activeEncounterId, selectedEnemyIndex)});
}

function enemyMaxHp() {
    return rpg.getEncounterMemberMaxHp(activeEncounterId, selectedEnemyIndex);
}

function runCombat(playerSkill, target) {
    if (battle == null) battle = rpg.newBattle();
    local queued = battle.setActionChecked(player, playerSkill, target);
    if (!queued.ok) {
        logMessage("battle.actionFailed", {error = queued.error.message});
        return false;
    }
    if (playerSkill == "skill.fireball") player.takeDamage("mp", 10.0, "skill");
    if (companion != null && !companion.isDead("hp")) {
        local companionQueued = rpg.queueBattleTactic(battle, companion, "companion.ranger");
        if (!companionQueued.ok) {
            logMessage("battle.companionActionFailed", {error = companionQueued.error.message});
            return false;
        }
    }
    battle.autoEnemyActions();
    battle.startRound();
    while (battle.executeNextAction() && !battle.isFinished()) {}
    battle.pollEvents();
    for (local i = 0; i < battle.getEventCount(); i += 1) {
        local act = battle.getEventAction(i);
        local amount = roundi(battle.getEventAmount(i));
        local actor = battle.getEventCaster(i) == player ? localizedId("actor", "player") :
                      (battle.getEventCaster(i) == companion ? localizedId("actor", "ranger") :
                                                               localizedId("actor", "enemy"));
        if (act == "damage")
            logMessage("battle.damage", {actor = actor, amount = amount,
                       critical = battle.getEventCrit(i) ? tr("gameplayLog.battle.critical") : ""});
        else if (act == "heal")
            logMessage("battle.healed", {actor = actor, amount = amount});
        else if (act == "miss")
            logMessage("battle.missed", {actor = actor});
    }
    selectLivingEnemy();
    return true;
}

function onVictory() {
    local settled = rpg.settleEncounterVictory(
        player, quest, gs, activeEncounterId, worldMapId, activeEncounter);
    if (!settled.ok) {
        logMessage("battle.settlementFailed", {error = settled.error.message});
        return false;
    }
    gold = gs.getVariable("gold").tointeger();
    kills = gs.getVariable("kills").tointeger();
    statPoints = gs.getVariable("statPoints").tointeger();
    if (settled.value.levelsGained > 0) {
        logMessage("battle.levelUp", {level = player.getLevel()});
    }
    if (settled.value.skillsLearned > 0) logMessage("battle.skillLearned");
    if (quest.getState("quest.slayer") == "ready") {
        logMessage("battle.mainQuestReady");
    }
    activeEncounter = "";
    activeEncounterId = "";
    wave = kills + 1;
    releaseEncounterEnemies();
    battle = null;
    setScreen("explore");
    logMessage("battle.ended", {area = worldMapTitle("village")});
    return true;
}

function tryPlayerSkill(skillId) {
    if (skillId == "skill.fireball" && player.getCurrent("mp") < 10.0) {
        logMessage("battle.noMana", {name = localizedId("skill", "skill.fireball")});
        return;
    }
    local target = skillId == "skill.self_heal" ? player : enemy;
    if (!runCombat(skillId, target)) return;
    if (battle.isVictory()) {
        logMessage("battle.victory");
        hitFlash.enemy = 0.2;
        onVictory();
    } else if (battle.isDefeat()) {
        logMessage("battle.defeat");
        state = "gameover";
        showGameOver(true);
    }
}

function buildUI(remount = false) {
    local pad = 14.0;
    ui.beginBuild();
    ui.beginWindow("RPG Classic", "root");
    ui.text("RPG CLASSIC", "title_name");
    ui.text(tr("ui.title.subtitle"), "title_subtitle");
    ui.separator("title_sep");
    for (local i = 1; i <= SAVE_SLOT_COUNT; i += 1) {
        ui.text(trp("ui.title.slot", {slot = i}), "slot" + i + "_label");
        ui.button(tr("ui.title.continue"), "slot" + i + "_continue");
        ui.button(tr("ui.title.newGame"), "slot" + i + "_new");
        ui.button(tr("ui.title.delete"), "slot" + i + "_delete");
        ui.separator("slot" + i + "_sep");
    }
    ui.text(trp("ui.settings.volume", {percent = 100}), "title_volume");
    ui.button(tr("ui.settings.volumeDown"), "volume_down");
    ui.button(tr("ui.settings.volumeUp"), "volume_up");
    ui.button(tr("ui.title.language"), "language_toggle");
    ui.button(tr("ui.title.confirmDelete"), "delete_confirm");
    ui.button(tr("ui.title.cancel"), "delete_cancel");
    ui.text("", "title_status");
    ui.end();
    if (remount) ui.remountBuildAs("title"); else ui.mountBuildAs("title");
    ui.select("title"); ui.setHostOverlay(true); ui.setHostModal(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.48, 0.5, 0.5);

    ui.beginBuild();
    ui.beginWindow(tr("ui.pause.title"), "root");
    ui.text(tr("ui.pause.status"), "pause_status");
    ui.button(tr("ui.pause.resume"), "resume");
    ui.button(tr("ui.pause.save"), "save");
    ui.separator("pause_sep1");
    ui.text(trp("ui.settings.volume", {percent = 100}), "pause_volume");
    ui.button(tr("ui.settings.volumeDown"), "volume_down");
    ui.button(tr("ui.settings.volumeUp"), "volume_up");
    ui.button(tr("ui.title.language"), "language_toggle");
    ui.separator("pause_sep2");
    ui.button(tr("ui.pause.returnTitle"), "title_request");
    ui.button(tr("ui.pause.confirmReturn"), "title_confirm");
    ui.button(tr("ui.title.cancel"), "title_cancel");
    ui.end();
    if (remount) ui.remountBuildAs("pause"); else ui.mountBuildAs("pause");
    ui.select("pause"); ui.setHostOverlay(true); ui.setHostModal(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.5, 0.5, 0.5);
    ui.setHostVisible(false);

    ui.beginBuild();
    ui.beginWindow("Player", "root");
    ui.text(tr("ui.player.title"), "title");
    ui.progress(1.0, "hpbar", "");
    ui.text("HP 0/0", "hptext");
    ui.progress(1.0, "mpbar", "");
    ui.text("MP 0/0", "mptext");
    ui.text(trp("ui.player.level", {className = localizedId("class", "warrior"), level = 1, xp = 0, nextXp = 50}), "leveltext");
    ui.text("", "pstatus");
    ui.separator("sep1");
    ui.text(trp("ui.player.skill", {slot = 1, name = localizedId("skill", "skill.strike")}), "s1");
    ui.text(trp("ui.player.skillCost", {slot = 2, name = localizedId("skill", "skill.fireball"), cost = 10}), "s2");
    ui.text(trp("ui.player.skill", {slot = 3, name = localizedId("skill", "skill.self_heal")}), "s3");
    ui.end();
    if (remount) ui.remountBuildAs("player"); else ui.mountBuildAs("player");
    ui.select("player"); ui.setHostOverlay(true); ui.setHostPos(pad, pad, 0.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Enemy", "root");
    ui.text(tr("ui.enemy.title"), "title");
    ui.progress(1.0, "ehpbar", "");
    ui.text("HP 0/0", "ehptext");
    ui.text("", "estatus");
    ui.end();
    if (remount) ui.remountBuildAs("enemy"); else ui.mountBuildAs("enemy");
    ui.select("enemy"); ui.setHostOverlay(true); ui.setHostPos(config.width - pad, pad, 1.0, 0.0);

    ui.beginBuild();
    ui.beginWindow("Quest", "root");
    ui.text(tr("ui.quest.none"), "qtext");
    ui.text(trp("ui.hud.goldLevel", {gold = 0, level = 1}), "goldtext");
    ui.text("", "log");
    ui.end();
    if (remount) ui.remountBuildAs("quest"); else ui.mountBuildAs("quest");
    ui.select("quest"); ui.setHostOverlay(true); ui.setHostPos(pad, config.height - pad, 0.0, 1.0);

    ui.beginBuild();
    ui.beginWindow(worldMapTitle(worldMapId), "root");
    ui.text(trp("ui.world.area", {area = worldMapTitle(worldMapId)}), "area");
    ui.text(tr("ui.world.move"), "move");
    ui.text(tr("ui.world.help"), "help");
    ui.text(tr("ui.world.goal"), "goal");
    ui.end();
    if (remount) ui.remountBuildAs("world"); else ui.mountBuildAs("world");
    ui.select("world"); ui.setHostOverlay(true); ui.setHostPos(config.width - pad, pad, 1.0, 0.0);

    ui.beginBuild();
    ui.beginWindow(tr("ui.dialogue.title"), "root");
    ui.text("", "speaker");
    ui.separator("dialogue_sep");
    ui.text("", "body");
    ui.text("", "choices");
    ui.text("", "dialogue_help");
    ui.end();
    if (remount) ui.remountBuildAs("dialogue"); else ui.mountBuildAs("dialogue");
    ui.select("dialogue"); ui.setHostOverlay(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.68, 0.5, 0.5);
    ui.setHostVisible(false);

    ui.beginBuild();
    ui.beginWindow("Game Over", "root");
    ui.text(tr("ui.gameOver.message"), "msg");
    ui.text("", "final");
    ui.button(tr("ui.gameOver.recover"), "recover");
    ui.button(tr("ui.gameOver.restart"), "restart");
    ui.end();
    if (remount) ui.remountBuildAs("gameover"); else ui.mountBuildAs("gameover");
    ui.select("gameover"); ui.setHostVisible(false);
    ui.setHostPos(config.width * 0.5, config.height * 0.45, 0.5, 0.5);

    ui.beginBuild();
    ui.beginWindow(tr("ui.status.title"), "root");
    ui.text(tr("ui.status.title"), "st_title");
    ui.text("", "st_stats");
    ui.separator("st_s1");
    ui.text(tr("ui.status.equipment"), "st_eq_title");
    ui.text("", "st_eq");
    ui.separator("st_s2");
    ui.text(tr("ui.status.bag"), "st_bag_title");
    ui.text("", "st_bag");
    ui.separator("st_s3");
    ui.button(tr("ui.status.close"), "st_close");
    ui.end();
    if (remount) ui.remountBuildAs("status"); else ui.mountBuildAs("status");
    ui.select("status"); ui.setHostOverlay(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.5, 0.5, 0.5);
    ui.setHostVisible(false);

    ui.beginBuild();
    ui.beginWindow(tr("ui.adjust.title"), "root");
    ui.text(tr("ui.adjust.attributes"), "adj_h1");
    ui.text(trp("ui.adjust.points", {points = 0}), "adj_points");
    ui.button(tr("ui.adjust.attack"), "stat_atk");
    ui.button(tr("ui.adjust.defense"), "stat_def");
    ui.button(tr("ui.adjust.hp"), "stat_hp");
    ui.button(tr("ui.adjust.mp"), "stat_mp");
    ui.separator("adj_s1");
    ui.text(tr("ui.adjust.shop"), "adj_h2");
    ui.text(trp("ui.adjust.gold", {gold = 0}), "adj_gold");
    ui.text("", "shop0"); ui.button(tr("ui.adjust.buy"), "shop0_buy"); ui.button(trp("ui.adjust.sell", {price = 10}), "shop0_sell");
    ui.text("", "shop1"); ui.button(tr("ui.adjust.buy"), "shop1_buy"); ui.button(trp("ui.adjust.sell", {price = 60}), "shop1_sell");
    ui.text("", "shop2"); ui.button(tr("ui.adjust.buy"), "shop2_buy"); ui.button(trp("ui.adjust.sell", {price = 45}), "shop2_sell");
    ui.separator("adj_s2");
    ui.text(tr("ui.adjust.equipmentBag"), "adj_h3");
    ui.text(trp("ui.status.weapon", {item = tr("ui.status.none")}), "eq_weapon");
    ui.text(trp("ui.status.armor", {item = tr("ui.status.none")}), "eq_armor");
    ui.text(trp("ui.adjust.bag", {items = ""}), "adj_bag");
    ui.button(tr("ui.adjust.equipSword"), "eq_sword");
    ui.button(tr("ui.adjust.equipArmor"), "eq_armor_btn");
    ui.button(tr("ui.adjust.unequipWeapon"), "uneq_weapon");
    ui.button(tr("ui.adjust.unequipArmor"), "uneq_armor");
    ui.separator("adj_s3");
    ui.button(tr("ui.adjust.back"), "adj_back");
    ui.end();
    if (remount) ui.remountBuildAs("adjust"); else ui.mountBuildAs("adjust");
    ui.select("adjust"); ui.setHostOverlay(true);
    ui.setHostPos(config.width * 0.5, config.height * 0.5, 0.5, 0.5);
    ui.setHostVisible(false);
}

function refreshLocalizedPresentation() {
    local activeScreen = screen;
    buildUI(true);
    setScreen(activeScreen);
    if (player != null) refreshHud();
    if (state == "gameover") showGameOver(true);
    if (activeScreen == "dialogue") presentDialogue();
}

function selectProductLanguage(language) {
    if (language == preferredLanguage && text.getLanguage() == language) return true;
    local dialogueValidation = dialogueFlow.validateLocalization(text, language);
    if (!dialogueValidation.ok) {
        print("rpg-classic: requested language rejected: " + dialogueValidation.status.summary + "\n");
        return false;
    }
    local previousLanguage = preferredLanguage;
    local selected = text.selectLanguage(language);
    if (!selected.ok) return false;
    preferredLanguage = language;
    if (!saveSettings()) {
        preferredLanguage = previousLanguage;
        local rollback = text.selectLanguage(previousLanguage);
        if (!rollback.ok) throw "rpg-classic: language settings rollback failed";
        return false;
    }
    titleMessage = tr("ui.title.prompt");
    pauseMessage = trp("ui.pause.currentSlot", {slot = activeSaveSlot});
    refreshLocalizedPresentation();
    return true;
}

function toggleProductLanguage() {
    return selectProductLanguage(preferredLanguage == "zh-CN" ? "en" : "zh-CN");
}

function refreshHud() {
    ui.select("player");
    ui.setValue("hpbar", (player.getCurrent("hp") / player.getMax("hp")).tofloat());
    ui.setText("hptext", "HP " + roundi(player.getCurrent("hp")) + "/" + roundi(player.getMax("hp")));
    ui.setValue("mpbar", (player.getCurrent("mp") / player.getMax("mp")).tofloat());
    ui.setText("mptext", "MP " + roundi(player.getCurrent("mp")) + "/" + roundi(player.getMax("mp")));
    ui.setText("leveltext", trp("ui.player.level", {
        className = localizedId("class", "warrior"), level = player.getLevel(),
        xp = roundi(player.getXp()), nextXp = roundi(player.getXpToNext())
    }));

    local pn = player.getTraitCount();
    local stat = "";
    for (local i = 0; i < pn; i += 1) {
        local id = player.getTraitIdAt(i);
        if (id == "trait.mighty") {
            local name = localizedId("trait", "mighty");
            stat = (stat == "") ? name : (stat + " + " + name);
        }
        if (id == "trait.fire_guard") {
            local name = localizedId("trait", "fire_guard");
            stat = (stat == "") ? name : (stat + " + " + name);
        }
    }
    local companionText = companion == null ? "" : trp("ui.player.companion", {
        hp = roundi(companion.getCurrent("hp")), maxHp = roundi(companion.getMax("hp"))
    });
    ui.setText("pstatus", trp("ui.player.traits", {traits = stat == "" ? tr("ui.status.none") : stat}) + companionText);

    ui.select("enemy");
    if (enemy != null) {
        local roster = "";
        for (local memberIndex = 0; memberIndex < enemies.len(); memberIndex += 1) {
            local member = enemies[memberIndex];
            roster += (memberIndex == 0 ? "" : "  ") +
                      (memberIndex == selectedEnemyIndex ? ">" : "") +
                      encounterMemberName(activeEncounterId, memberIndex) + " " +
                      roundi(member.getCurrent("hp")) + "/" +
                      roundi(rpg.getEncounterMemberMaxHp(activeEncounterId, memberIndex));
        }
        ui.setText("title", encounterName(activeEncounterId) + "\n" + roster);
        ui.setValue("ehpbar", (enemy.getCurrent("hp") / enemyMaxHp()).tofloat());
        ui.setText("ehptext", "HP " + roundi(enemy.getCurrent("hp")) + "/" + roundi(enemyMaxHp()));
        ui.setText("estatus", trp("ui.enemy.attack", {attack = roundi(enemy.getFinalAttribute("attack"))}));
    }

    ui.select("quest");
    local questText = "";
    for (local qi = 0; qi < quest.getCount(); qi += 1) {
        local questId = quest.getId(qi);
        local questState = quest.getState(questId);
        if (questState == "inactive" && !quest.hasTag(questId, "main")) continue;
        local title = questTitle(questId);
        local progress = "";
        if (quest.getObjectiveCount(questId) > 0)
            progress = " " + quest.getObjectiveCurrent(questId, 0) + "/" +
                       quest.getObjectiveCountRequired(questId, 0);
        local stateText = questState == "ready" ? tr("ui.quest.ready") :
                          (questState == "completed" ? tr("ui.quest.completed") : "");
        questText += (questText == "" ? "" : "\n") + title + progress + stateText;
    }
    ui.setText("qtext", questText == "" ? tr("ui.quest.none") : questText);
    ui.setText("goldtext", trp("ui.hud.goldLevel", {gold = gold, level = player.getLevel()}));
    local logText = "";
    for (local i = 0; i < log.len(); i += 1) logText += log[i] + "\n";
    ui.setText("log", logText);
    ui.select("world");
    ui.setText("area", trp("ui.world.area", {area = worldMapTitle(worldMapId)}));
}

function showGameOver(show) {
    ui.select("gameover");
    ui.setHostVisible(show);
    if (show) { ui.setHostModal(true); ui.setText("final", trp("ui.gameOver.summary", {wave = wave, level = player.getLevel(), gold = gold})); }
    else ui.setHostModal(false);
}

function recoverPartyAtVillage() {
    if (state != "gameover" || party == null) return false;
    local session = makeSaveSession();
    if (session == null) { logMessage("recovery.sessionFailed"); return false; }
    local rollbackJson = session.snapshotJson();
    if (rollbackJson == "") { logMessage("recovery.snapshotFailed"); return false; }
    local oldMapId = worldMapId;
    local oldWorldX = worldX; local oldWorldY = worldY;
    local oldFacingX = facingX; local oldFacingY = facingY;
    local recovered = party.recoverAtCheckpoint("hp", 0.5, "mp", 0.5);
    if (!recovered.ok) {
        logMessage("recovery.partyFailed", {error = recovered.error.message});
        return false;
    }
    local penalty = floor(gold.tofloat() * 0.1).tointeger();
    if (gold > 0 && penalty < 1) penalty = 1;
    gold -= penalty;
    gs.setVariable("gold", gold.tofloat());
    if (!loadWorldMap("village", 2, 5, 1, 0)) {
        local participantRollback = session.restoreSnapshotJson(rollbackJson) != 0;
        local sceneRollback = loadWorldMap(oldMapId, oldWorldX, oldWorldY, oldFacingX, oldFacingY);
        logMessage(participantRollback && sceneRollback ?
                   "recovery.checkpointRolledBack" : "recovery.rollbackIncomplete");
        return false;
    }
    activeEncounter = ""; activeEncounterId = "";
    releaseEncounterEnemies();
    battle = null; state = "idle";
    showGameOver(false); setScreen("explore");
    logMessage("recovery.recovered", {penalty = penalty});
    if (!saveCheckpoint()) logMessage("recovery.autosaveFailed");
    return true;
}

function makeSaveSession() {
    local session = eve.RPGSaveSession();
    if (session.setContentVersion(SAVE_CONTENT_VERSION) == 0 ||
        session.allowCompatibleContentVersion(PREVIOUS_SAVE_CONTENT_VERSION) == 0 ||
        session.allowCompatibleContentVersion(PRE_LOCALIZATION_SAVE_CONTENT_VERSION) == 0 ||
        session.allowSingleActorPartyMigration(SINGLE_ACTOR_SAVE_CONTENT_VERSION) == 0 ||
        session.allowSingleActorPartyMigration(OLDER_SAVE_CONTENT_VERSION) == 0 ||
        session.allowSingleActorPartyMigration(LEGACY_QUEST_CONTENT_VERSION) == 0 ||
        session.addQuestAdditionMigration(LEGACY_QUEST_CONTENT_VERSION, "quest.ranger_cache") == 0 ||
        session.bindParty(gs, quest, party, bag, equip) == 0) return null;
    return session;
}

function inspectSaveSlot(slot) {
    local session = makeSaveSession();
    if (session == null) return 3;
    local primary = saveFs.readText(savePrimaryPath(slot));
    if (primary != "" && session.validateSnapshotJson(primary) != 0) return 1;
    local backup = saveFs.readText(saveBackupPath(slot));
    if (backup != "" && session.validateSnapshotJson(backup) != 0) return 2;
    return (primary == "" && backup == "") ? 0 : 3;
}

function migrateLegacySingleSlot() {
    if (saveFs.readText(savePrimaryPath(1)) != "" || saveFs.readText(saveBackupPath(1)) != "")
        return false;
    local session = makeSaveSession();
    if (session == null) return false;
    local legacy = saveFs.readText(LEGACY_SAVE_PRIMARY_PATH);
    if (legacy == "" || session.validateSnapshotJson(legacy) == 0)
        legacy = saveFs.readText(LEGACY_SAVE_BACKUP_PATH);
    if (legacy == "" || session.validateSnapshotJson(legacy) == 0) return false;
    if (saveFs.writeTextAtomic(savePrimaryPath(1), legacy) == 0) return false;
    titleMessage = tr("ui.title.migrated");
    return true;
}

function refreshTitleUI() {
    ui.select("title");
    for (local i = 1; i <= SAVE_SLOT_COUNT; i += 1) {
        local state = inspectSaveSlot(i);
        local suffix = state == 1 ? tr("ui.title.slotContinue") :
                       state == 2 ? tr("ui.title.slotBackup") :
                       state == 3 ? tr("ui.title.slotCorrupt") : tr("ui.title.slotEmpty");
        ui.setText("slot" + i + "_label", trp("ui.title.slot", {slot = i}) + suffix);
    }
    ui.setText("title_status", titleMessage);
    refreshSettingsUI();
}

function requestDeleteSaveSlot(slot) {
    pendingDeleteSlot = slot;
    titleMessage = trp("ui.title.deletePrompt", {slot = slot});
    refreshTitleUI();
}

function confirmDeleteSaveSlot() {
    if (pendingDeleteSlot < 1 || pendingDeleteSlot > SAVE_SLOT_COUNT) {
        titleMessage = tr("ui.title.deleteNone");
        refreshTitleUI();
        return false;
    }
    local slot = pendingDeleteSlot;
    local backupPath = saveBackupPath(slot);
    local primaryPath = savePrimaryPath(slot);
    local backupRemoved = saveFs.readText(backupPath) == "" || saveFs.remove(backupPath);
    local primaryRemoved = saveFs.readText(primaryPath) == "" || saveFs.remove(primaryPath);
    pendingDeleteSlot = 0;
    if (!backupRemoved || !primaryRemoved || inspectSaveSlot(slot) != 0) {
        titleMessage = trp("ui.title.deleteFailed", {slot = slot});
        refreshTitleUI();
        return false;
    }
    titleMessage = trp("ui.title.deleted", {slot = slot});
    refreshTitleUI();
    return true;
}

function cancelDeleteSaveSlot() {
    pendingDeleteSlot = 0;
    titleMessage = tr("ui.title.deleteCancelled");
    refreshTitleUI();
}

function refreshPauseUI() {
    ui.select("pause");
    ui.setText("pause_status", pauseMessage);
    refreshSettingsUI();
}

function openPauseMenu() {
    if (screen == "title" || screen == "pause" || state == "gameover") return false;
    pauseReturnScreen = screen;
    pendingReturnToTitle = false;
    pauseMessage = trp("ui.pause.currentSlot", {slot = activeSaveSlot});
    setScreen("pause");
    return true;
}

function resumeFromPause() {
    if (screen != "pause") return false;
    pendingReturnToTitle = false;
    setScreen(pauseReturnScreen);
    return true;
}

function saveFromPause() {
    if (pauseReturnScreen != "explore" && pauseReturnScreen != "adjust") {
        pauseMessage = tr("ui.pause.unsafeSave");
        refreshPauseUI();
        return false;
    }
    local safeScreen = pauseReturnScreen;
    setScreen(safeScreen);
    local saved = saveCheckpoint();
    setScreen("pause");
    pauseMessage = saved ? trp("ui.pause.saved", {slot = activeSaveSlot}) : tr("ui.pause.saveFailed");
    refreshPauseUI();
    return saved;
}

function requestReturnToTitle() {
    pendingReturnToTitle = true;
    pauseMessage = newRunBeforeFirstSave ? tr("ui.pause.returnUnsaved") : tr("ui.pause.returnPrompt");
    refreshPauseUI();
}

function confirmReturnToTitle() {
    if (!pendingReturnToTitle) {
        pauseMessage = tr("ui.pause.selectReturn");
        refreshPauseUI();
        return false;
    }
    pendingReturnToTitle = false;
    titleMessage = tr("ui.title.returned");
    setScreen("title");
    return true;
}

function continueSaveSlot(slot) {
    local oldSlot = activeSaveSlot;
    activeSaveSlot = slot;
    if (loadCheckpoint(true)) return true;
    activeSaveSlot = oldSlot;
    titleMessage = trp("ui.title.noSave", {slot = slot});
    setScreen("title");
    return false;
}

function saveCheckpoint() {
    local resumableStory = storyEvent != null && storyEvent.isActive();
    if (state == "gameover" ||
        (screen != "explore" && screen != "adjust" && !resumableStory)) {
        logMessage("save.unsafe");
        return false;
    }
    gs.setVariable("save.wave", wave.tofloat());
    gs.setVariable("save.gold", gold.tofloat());
    gs.setVariable("save.kills", kills.tofloat());
    gs.setVariable("save.statPoints", statPoints.tofloat());
    gs.setVariable("save.worldX", worldX.tofloat());
    gs.setVariable("save.worldY", worldY.tofloat());
    gs.setVariable("save.facingX", facingX.tofloat());
    gs.setVariable("save.facingY", facingY.tofloat());
    gs.setVariable("save.mapCode", worldMapCode(worldMapId).tofloat());
    gs.switchOn("save.scene");
    gs.switchOn("save.world");
    gs.switchOn("save.valid");
    saveSession = makeSaveSession();
    if (saveSession == null) { logMessage("save.sessionFailed"); return false; }
    local json = saveSession.snapshotJson();
    if (json == "") { logMessage("save.snapshotFailed"); return false; }

    local primaryPath = savePrimaryPath(activeSaveSlot);
    local backupPath = saveBackupPath(activeSaveSlot);
    local previous = saveFs.readText(primaryPath);
    if (previous != "" && saveSession.validateSnapshotJson(previous) != 0) {
        if (saveFs.writeTextAtomic(backupPath, previous) == 0) {
            logMessage("save.backupRotateFailed");
            return false;
        }
    } else if (previous != "") {
        logMessage("save.corruptPrimary");
    }
    if (saveFs.writeTextAtomic(primaryPath, json) == 0) {
        logMessage("save.writeFailed");
        return false;
    }
    newRunBeforeFirstSave = false;
    logMessage("save.saved", {slot = activeSaveSlot});
    return true;
}

function loadCheckpoint(replaceUnsavedNewRun = false) {
    if (newRunBeforeFirstSave && !replaceUnsavedNewRun) {
        logMessage("save.unsavedProtected");
        return false;
    }
    saveSession = makeSaveSession();
    if (saveSession == null) { logMessage("save.loadSessionFailed"); return false; }
    local rollbackJson = saveSession.snapshotJson();
    if (rollbackJson == "") { logMessage("save.loadSnapshotFailed"); return false; }

    local oldMapId = worldMapId;
    local oldWorldX = worldX, oldWorldY = worldY, oldFacingX = facingX, oldFacingY = facingY;
    local json = saveFs.readText(savePrimaryPath(activeSaveSlot));
    local recovered = false;
    if (json == "" || saveSession.validateSnapshotJson(json) == 0) {
        json = saveFs.readText(saveBackupPath(activeSaveSlot));
        recovered = true;
    }
    if (json == "" || saveSession.validateSnapshotJson(json) == 0) {
        logMessage("save.unavailable");
        return false;
    }
    if (saveSession.restoreSnapshotJson(json) == 0) {
        logMessage("save.restoreFailed");
        return false;
    }
    local savedWave = gs.getVariable("save.wave").tointeger();
    local savedGold = gs.getVariable("save.gold").tointeger();
    local savedKills = gs.getVariable("save.kills").tointeger();
    local savedPoints = gs.getVariable("save.statPoints").tointeger();
    local hadWorldState = gs.isSwitchOn("save.world");
    local savedWorldX = hadWorldState ? gs.getVariable("save.worldX").tointeger() : 2;
    local savedWorldY = hadWorldState ? gs.getVariable("save.worldY").tointeger() : 5;
    local savedFacingX = hadWorldState ? gs.getVariable("save.facingX").tointeger() : 1;
    local savedFacingY = hadWorldState ? gs.getVariable("save.facingY").tointeger() : 0;
    local savedMapId = gs.isSwitchOn("save.scene") ?
                       worldMapIdFromCode(gs.getVariable("save.mapCode").tointeger()) : "village";
    local validFacing = (abs(savedFacingX) + abs(savedFacingY)) == 1;
    if (!gs.isSwitchOn("save.valid") || savedWave < 1 || savedGold < 0 || savedKills < 0 ||
        savedPoints < 0 || !validFacing || savedMapId == "") {
        if (saveSession.restoreSnapshotJson(rollbackJson) == 0) {
            logMessage("save.invalidRollbackFailed");
            return false;
        }
        rpg.syncEquipModifiers(player, equip);
        logMessage("save.invalidRolledBack");
        return false;
    }
    if (!loadWorldMap(savedMapId, savedWorldX, savedWorldY, savedFacingX, savedFacingY)) {
        local participantRollback = saveSession.restoreSnapshotJson(rollbackJson) != 0;
        local sceneRollback = loadWorldMap(oldMapId, oldWorldX, oldWorldY, oldFacingX, oldFacingY);
        rpg.syncEquipModifiers(player, equip);
        if (!participantRollback || !sceneRollback)
            logMessage("save.sceneRollbackFailed");
        else
            logMessage("save.sceneRolledBack");
        return false;
    }
    local migratedWorldState = migrateLegacyWorldState(savedKills);
    migrateUnscopedWorldEvents();
    if (migrateScopedWorldEvents()) migratedWorldState = true;
    wave = savedWave;
    gold = savedGold; kills = savedKills; statPoints = savedPoints;
    activeEncounter = "";
    activeEncounterId = "";
    newRunBeforeFirstSave = false;
    rpg.syncEquipModifiers(player, equip);
    releaseEncounterEnemies();
    battle = null;
    state = "idle"; setScreen("explore"); showGameOver(false);
    log = [];
    if (recovered) logMessage("save.backupRecovered");
    if (migratedWorldState) logMessage("save.migrated");
    logMessage("save.loaded", {area = worldMapTitle("village")});
    refreshHud();
    storyEvent = null;
    storyWaitRemaining = 0.0;
    resumePendingStoryEvent();
    return true;
}

function startNewGame(slot = activeSaveSlot) {
    activeSaveSlot = slot;
    newRunBeforeFirstSave = true;
    if (player != null) player.release();
    if (companion != null) companion.release();
    if (party != null) party.clear();
    releaseEncounterEnemies();
    wave = 1; gold = 0; kills = 0; statPoints = 0;
    activeEncounter = "";
    activeEncounterId = "";
    storyEvent = null;
    storyWaitRemaining = 0.0;
    log = [];
    hitFlash.player = 0.0; hitFlash.enemy = 0.0;
    state = "idle";
    if (gs == null) gs = rpg.newGameState();
    gs.clear(); gs.switchOn("new_game"); gs.setVariable("gold", 0.0);
    worldState = rpg.newWorldState(gs);
    if (worldState == null) throw "rpg-classic: failed to bind world state";
    player = makePlayer();
    companion = makeCompanion();
    party = rpg.newParty();
    local heroAdded = party.addMember("hero", player);
    local companionAdded = party.addMember("companion.ranger", companion);
    if (!heroAdded.ok || !companionAdded.ok) throw "rpg-classic: failed to compose player party";
    setupInventory();
    battle = null;
    quest = rpg.newTracker();
    if (!loadWorldMap("village", 2, 5, 1, 0)) throw "rpg-classic: failed to reset village";
    local slotState = inspectSaveSlot(slot);
    local replacingExistingSlot = slotState == 1 || slotState == 2;
    showGameOver(false);
    setScreen("explore");
    logMessage("save.newGame", {slot = activeSaveSlot, area = worldMapTitle("village")});
    if (replacingExistingSlot) logMessage("save.existingPreserved");
}

eve_init = function() {
    gfx.setBackgroundColor(0.06, 0.05, 0.09, 1.0);
    if (rpg == null) rpg = eve.RPG();
    if (inv == null) inv = eve.Inventory();
    if (saveFs == null) saveFs = eve.Filesystem();
    loadSettings();
    if (gs != null) worldState = rpg.newWorldState(gs);
    local contractSource = readTextFile("data/world-object-contract.json");
    if (contractSource == null || contractSource == "")
        throw "rpg-classic: world object contract missing";
    worldObjectContract = contractSource;
    if (!publishNarrativeContentPackage(false))
        throw "rpg-classic: narrative content package validation failed";
    titleMessage = tr("ui.title.prompt");
    pauseMessage = tr("ui.pause.status");
    setupWorld();
    buildUI();
    if (player == null) {
        startNewGame(activeSaveSlot);
        if (!migrateLegacySingleSlot()) titleMessage = tr("ui.title.prompt");
        setScreen("title");
    } else {
        setScreen(screen);
        refreshHud();
    }
};

eve_reload <- function() {
    publishNarrativeContentPackage(true);
};

eve_update = function(dt) {
    local id = ui.consumeClick();
    while (id != "") {
        if (id == "title/slot1_continue") continueSaveSlot(1);
        else if (id == "title/slot2_continue") continueSaveSlot(2);
        else if (id == "title/slot3_continue") continueSaveSlot(3);
        else if (id == "title/slot1_new") startNewGame(1);
        else if (id == "title/slot2_new") startNewGame(2);
        else if (id == "title/slot3_new") startNewGame(3);
        else if (id == "title/slot1_delete") requestDeleteSaveSlot(1);
        else if (id == "title/slot2_delete") requestDeleteSaveSlot(2);
        else if (id == "title/slot3_delete") requestDeleteSaveSlot(3);
        else if (id == "title/delete_confirm") confirmDeleteSaveSlot();
        else if (id == "title/delete_cancel") cancelDeleteSaveSlot();
        else if (id == "title/volume_down" || id == "pause/volume_down") adjustMasterVolume(-0.1);
        else if (id == "title/volume_up" || id == "pause/volume_up") adjustMasterVolume(0.1);
        else if (id == "title/language_toggle" || id == "pause/language_toggle") toggleProductLanguage();
        else if (id == "pause/resume") resumeFromPause();
        else if (id == "pause/save") saveFromPause();
        else if (id == "pause/title_request") requestReturnToTitle();
        else if (id == "pause/title_confirm") confirmReturnToTitle();
        else if (id == "pause/title_cancel") { pendingReturnToTitle = false; pauseMessage = tr("ui.pause.cancelled"); refreshPauseUI(); }
        else if (id == "gameover/recover") recoverPartyAtVillage();
        else if (id == "gameover/restart") startNewGame(activeSaveSlot);
        else if (id == "status/st_close") setScreen(returnScreen);
        else if (id == "adjust/adj_back") setScreen("explore");
        else if (id == "adjust/stat_atk") { allocate("attack", 1.0); refreshAdjustUI(); }
        else if (id == "adjust/stat_def") { allocate("defense", 1.0); refreshAdjustUI(); }
        else if (id == "adjust/stat_hp") { allocate("hp", 10.0); refreshAdjustUI(); }
        else if (id == "adjust/stat_mp") { allocate("mp", 10.0); refreshAdjustUI(); }
        else if (id == "adjust/shop0_buy") { buyItem(shop[0].id); refreshAdjustUI(); }
        else if (id == "adjust/shop1_buy") { buyItem(shop[1].id); refreshAdjustUI(); }
        else if (id == "adjust/shop2_buy") { buyItem(shop[2].id); refreshAdjustUI(); }
        else if (id == "adjust/shop0_sell") { sellItem(shop[0].id); refreshAdjustUI(); }
        else if (id == "adjust/shop1_sell") { sellItem(shop[1].id); refreshAdjustUI(); }
        else if (id == "adjust/shop2_sell") { sellItem(shop[2].id); refreshAdjustUI(); }
        else if (id == "adjust/eq_sword") { equipItem("iron_sword", "weapon"); refreshAdjustUI(); }
        else if (id == "adjust/eq_armor_btn") { equipItem("leather_armor", "armor"); refreshAdjustUI(); }
        else if (id == "adjust/uneq_weapon") { unequipSlot("weapon"); refreshAdjustUI(); }
        else if (id == "adjust/uneq_armor") { unequipSlot("armor"); refreshAdjustUI(); }
        id = ui.consumeClick();
    }
    if (hitFlash.player > 0.0) hitFlash.player -= dt;
    if (hitFlash.enemy > 0.0) hitFlash.enemy -= dt;
    if (storyEvent != null && storyEvent.isActive() && storyEvent.getStepKind() == "wait") {
        storyWaitRemaining -= dt;
        if (storyWaitRemaining <= 0.0) {
            local advancedStory = storyEvent.advance(gs);
            if (!advancedStory.ok) logMessage("story.advanceFailed", {error = advancedStory.status.summary});
            else presentStoryEventStep();
        }
    }
    if (screen == "title") {
    } else if (screen == "pause") {
        if (key_just_pressed("escape", "Escape")) resumeFromPause();
    } else if (screen == "status") {
        if (key_just_pressed("C")) setScreen(returnScreen);
    } else if (screen == "dialogue") {
        if (key_just_pressed("escape", "Escape")) openPauseMenu();
        else if (storyEvent != null && storyEvent.isActive() && key_just_pressed("F5"))
            saveCheckpoint();
        else
        if (dialogueFlow.getNodeKind() == "choice") {
            if (key_just_pressed("1")) selectDialogueChoice(0);
            else if (key_just_pressed("2")) selectDialogueChoice(1);
        } else if (key_just_pressed("Space", "E")) advanceDialogue();
    } else if (screen == "adjust") {
        if (key_just_pressed("escape", "Escape")) openPauseMenu();
        else if (key_just_pressed("F5")) saveCheckpoint();
        else if (key_just_pressed("F9")) loadCheckpoint();
    } else if (state == "gameover") {
        if (key_just_pressed("E")) recoverPartyAtVillage();
        else if (key_just_pressed("R")) startNewGame(activeSaveSlot);
    } else if (storyEvent != null && storyEvent.isActive() &&
               storyEvent.getStepKind() == "wait") {
        if (key_just_pressed("F5")) saveCheckpoint();
    } else if (screen == "explore") {
        if (key_just_pressed("escape", "Escape")) openPauseMenu();
        else if (key_just_pressed("Up", "W")) tryWorldMove(0, -1);
        else if (key_just_pressed("Down", "S")) tryWorldMove(0, 1);
        else if (key_just_pressed("Left", "A")) tryWorldMove(-1, 0);
        else if (key_just_pressed("Right", "D")) tryWorldMove(1, 0);
        else if (key_just_pressed("E")) interactWorld();
        else if (key_just_pressed("C")) {
            returnScreen = "explore"; refreshStatusUI(); setScreen("status");
        } else if (key_just_pressed("Q")) usePotion();
        else if (key_just_pressed("F5")) saveCheckpoint();
        else if (key_just_pressed("F9")) loadCheckpoint();
    } else if (screen == "battle") {
        if (key_just_pressed("escape", "Escape")) openPauseMenu();
        else if (key_just_pressed("tab", "Tab")) selectNextEnemy();
        else if (key_just_pressed("1")) tryPlayerSkill("skill.strike");
        else if (key_just_pressed("2")) tryPlayerSkill("skill.fireball");
        else if (key_just_pressed("3")) tryPlayerSkill("skill.self_heal");
        else if (key_just_pressed("C")) {
            returnScreen = "battle"; refreshStatusUI(); setScreen("status");
        } else if (key_just_pressed("Q")) usePotion();
    }
    refreshHud();
};

eve_render = function() {
    gfx.clear();
    local presentedScreen = screen == "pause" ? pauseReturnScreen : screen;
    if (presentedScreen == "title") {
        gfx.drawSolidRect(0.0, 0.0, config.width.tofloat(), config.height.tofloat(),
                          0.04, 0.03, 0.08, 1.0);
        gfx.drawSolidRect(0.0, config.height * 0.72, config.width.tofloat(), config.height * 0.28,
                          0.12, 0.08, 0.18, 1.0);
    } else if (presentedScreen == "explore" || presentedScreen == "adjust" ||
        presentedScreen == "dialogue" ||
        (presentedScreen == "status" && returnScreen == "explore")) {
        map.render(gfx);
        for (local i = 0; i < map.getObjectCount(); i += 1) {
            local type = map.getObjectType(i);
            if (type == "spawn" || objectConsumed(i)) continue;
            local ox = worldLayer.tileToWorldX(map.getObjectX(i).tointeger(), map.getObjectY(i).tointeger());
            local oy = worldLayer.tileToWorldY(map.getObjectX(i).tointeger(), map.getObjectY(i).tointeger());
            if (type == "encounter") gfx.drawSolidRect(ox + 7.0, oy + 7.0, 18.0, 18.0, 0.3, 0.9, 0.35, 1.0);
            else if (type == "merchant") gfx.drawSolidRect(ox + 6.0, oy + 4.0, 20.0, 24.0, 0.95, 0.72, 0.25, 1.0);
            else if (type == "quest_npc") gfx.drawSolidRect(ox + 6.0, oy + 3.0, 20.0, 26.0, 0.7, 0.55, 0.95, 1.0);
            else if (type == "loot") gfx.drawSolidRect(ox + 5.0, oy + 11.0, 22.0, 15.0, 0.7, 0.4, 0.12, 1.0);
            else if (type == "portal") gfx.drawSolidRect(ox + 9.0, oy + 2.0, 14.0, 28.0, 0.25, 0.85, 0.95, 1.0);
        }
        local wx = worldLayer.tileToWorldX(worldX, worldY);
        local wy = worldLayer.tileToWorldY(worldX, worldY);
        gfx.drawSolidRect(wx + 5.0, wy + 4.0, 22.0, 25.0, 0.25, 0.55, 1.0, 1.0);
        gfx.drawSolidRect(wx + 12.0 + facingX * 7.0, wy + 11.0 + facingY * 7.0, 8.0, 8.0,
                          0.95, 0.95, 1.0, 1.0);
    } else {
        gfx.drawSolidRect(0.0, config.height - 60.0, config.width * 1.0, 60.0, 0.12, 0.1, 0.14, 1.0);
        local px = config.width * 0.28; local py = config.height - 150.0;
        local pf = hitFlash.player > 0.0 ? 0.6 : 0.0;
        gfx.drawSolidRect(px - 30.0, py, 60.0, 90.0, 0.25 + pf, 0.45 + pf, 0.8 + pf, 1.0);
        gfx.drawSolidRect(px - 16.0, py - 34.0, 32.0, 32.0, 0.35 + pf, 0.55 + pf, 0.85 + pf, 1.0);
        local cx = config.width * 0.16; local cy = config.height - 130.0;
        local companionDown = companion == null || companion.isDead("hp");
        gfx.drawSolidRect(cx - 22.0, cy, 44.0, companionDown ? 24.0 : 68.0,
                          companionDown ? 0.25 : 0.35, companionDown ? 0.25 : 0.72,
                          companionDown ? 0.3 : 0.42, 1.0);
        local ex = config.width * 0.72; local ey = config.height - 160.0;
        local ef = hitFlash.enemy > 0.0 ? 0.6 : 0.0;
        local esize = 70.0 + (wave.tofloat() * 3.0); if (esize > 130.0) esize = 130.0;
        gfx.drawSolidRect(ex - esize * 0.5, ey, esize, esize, 0.75 + ef, 0.25 + ef, 0.2 + ef, 1.0);
        gfx.drawSolidRect(ex - esize * 0.22, ey + esize * 0.25, esize * 0.14, esize * 0.14, 1.0, 1.0, 0.6, 1.0);
        gfx.drawSolidRect(ex + esize * 0.08, ey + esize * 0.25, esize * 0.14, esize * 0.14, 1.0, 1.0, 0.6, 1.0);
    }
    ui.beginFrameAndRender();
};

eve_quit = function() {
    releaseEncounterEnemies();
    if (party != null) party.clear();
    if (player != null) player.release();
    if (companion != null) companion.release();
    player = null; companion = null; party = null; battle = null;
};
