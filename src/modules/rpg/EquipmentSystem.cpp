#include "rpg/EquipmentSystem.h"

#include "common/Json.h"
#include "inventory/Equipment.h"
#include "rpg/AttributeSystem.h"
#include "rpg/RPGActor.h"

#include <unordered_map>

namespace eve::rpg {

using eve::json::Value;

namespace {

std::unordered_map<std::string, std::vector<EquipmentStat>> &itemStatsTable() {
    static std::unordered_map<std::string, std::vector<EquipmentStat>> t;
    return t;
}

}  // namespace

void EquipmentSystem::registerItemStat(const std::string &itemId, const EquipmentStat &stat) {
    if (itemId.empty() || stat.attribute.empty()) return;
    itemStatsTable()[itemId].push_back(stat);
}

int EquipmentSystem::registerItemStatsFromJson(const std::string &itemId, const std::string &json) {
    if (itemId.empty()) return 0;
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) return 0;
    const Value root = doc.root();
    int n = 0;
    const auto addObject = [&](Value o) {
        if (!o.isObject()) return;
        EquipmentStat stat;
        stat.attribute = o.getString("attribute");
        stat.op = o.getString("op", "add");
        stat.value = o.getDouble("value", 0.0);
        stat.priority = o.getInt("priority", 0);
        if (stat.attribute.empty()) return;
        itemStatsTable()[itemId].push_back(std::move(stat));
        ++n;
    };
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) addObject(root.at(i));
    } else if (root.isObject()) {
        addObject(root);
    }
    return n;
}

void EquipmentSystem::clearItemStats(const std::string &itemId) { itemStatsTable().erase(itemId); }

void EquipmentSystem::clearAll() { itemStatsTable().clear(); }

int EquipmentSystem::statItemCount() { return int(itemStatsTable().size()); }

int EquipmentSystem::syncEquipModifiers(RPGActor *actor, eve::inventory::EquipmentSet *equip) {
    if (!actor || !equip) return 0;
    int applied = 0;
    for (int i = 0; i < equip->getSlotCount(); ++i) {
        const std::string slot = equip->getSlotName(i);
        const std::string source = "equip:" + slot;
        // 先清除该槽位旧加成（覆盖换装/卸下的情况）。
        AttributeSystem::removeAllModifiersBySource(actor, source);
        if (equip->isSlotEmpty(slot)) continue;
        const std::string itemId = equip->getSlotItemId(slot);
        auto it = itemStatsTable().find(itemId);
        if (it == itemStatsTable().end()) continue;
        for (const auto &stat : it->second) {
            AttributeSystem::addModifier(actor, stat.attribute, source, stat.op, stat.value,
                                         stat.priority);
            ++applied;
        }
    }
    return applied;
}

}  // namespace eve::rpg