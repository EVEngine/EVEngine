#include "rpg/EquipmentSystem.h"

#include "common/Json.h"
#include "inventory/Equipment.h"
#include "rpg/AttributeSystem.h"
#include "rpg/EquipmentUpgrade.h"
#include "rpg/RPGActor.h"

#include <algorithm>
#include <cmath>
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

namespace {

bool isAdditiveEquipmentOp(std::string_view op) { return op.empty() || op == "add" || op == "additive_percent"; }

double enhanceBonusPerLevel(const EquipmentStat& stat) {
    // Classic forge step: each enhance level adds at least +1, else ~25% of the base add.
    const double magnitude = std::abs(stat.value);
    return std::max(1.0, std::floor(magnitude * 0.25 + 0.5));
}

std::vector<EquipmentStat> scaleStatsForEnhance(std::span<const EquipmentStat> stats, int enhanceLevel) {
    std::vector<EquipmentStat> scaled;
    scaled.reserve(stats.size());
    for (const EquipmentStat& stat : stats) {
        EquipmentStat next = stat;
        if (enhanceLevel > 0 && isAdditiveEquipmentOp(stat.op))
            next.value = stat.value + static_cast<double>(enhanceLevel) * enhanceBonusPerLevel(stat);
        scaled.push_back(std::move(next));
    }
    return scaled;
}

}  // namespace

int EquipmentSystem::syncEquipModifiers(RPGActor *actor, eve::inventory::EquipmentSet *equip) {
    if (!actor || !equip) return 0;
    int applied = 0;
    for (int i = 0; i < equip->getSlotCount(); ++i) {
        const std::string slot = equip->getSlotName(i);
        if (equip->isSlotEmpty(slot)) {
            projectSlot(*actor, slot, {}).ignore("legacy equipment synchronization projection");
            continue;
        }
        const std::string itemId = equip->getSlotItemId(slot);
        auto it = itemStatsTable().find(itemId);
        if (it == itemStatsTable().end()) {
            auto result = projectSlot(*actor, slot, {});
            if (result) applied += result.value();
            continue;
        }
        int enhanceLevel = 0;
        if (const inventory::ItemStack* stack = equip->stackAt(slot)) {
            auto parsed = EquipmentUpgrade::parseEnhanceLevel(stack->getProp(kEquipmentEnhanceProp, "0"));
            if (parsed) enhanceLevel = parsed.value();
        }
        const std::vector<EquipmentStat> scaled = scaleStatsForEnhance(it->second, enhanceLevel);
        auto                             result = projectSlot(*actor, slot, scaled);
        if (result) applied += result.value();
    }
    return applied;
}

eve::Result<int> EquipmentSystem::projectSlot(RPGActor& actor, std::string_view slot,
                                              std::span<const EquipmentStat> stats) {
    if (slot.empty())
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Equipment slot must not be empty"));
    auto              staged  = actor.attributes()->values;
    const std::string source  = "equip:" + std::string(slot);
    auto              removed = staged.removeBySource(source);
    if (!removed) return eve::Result<int>::failure(removed.status());
    for (const auto& stat : stats) {
        if (stat.attribute.empty() || !std::isfinite(stat.value))
            return eve::Result<int>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Invalid equipment statistic"));
        auto              parsed = eve::attributes::parseAttributeOperation(stat.op, stat.value);
        AttributeModifier modifier;
        modifier.attribute = stat.attribute;
        modifier.source    = source;
        modifier.priority  = stat.priority;
        if (parsed) {
            modifier.operation = parsed.value().operation;
            modifier.value     = parsed.value().value;
        } else if (AttributeSystem::customOps().has(stat.op)) {
            modifier.operation = AttributeOperation::Custom;
            modifier.policyId  = stat.op;
            modifier.value     = stat.value;
        } else
            return eve::Result<int>::failure(parsed.status());
        auto added = staged.addModifier(std::move(modifier));
        if (!added) return eve::Result<int>::failure(added.status());
    }
    actor.attributes()->values = std::move(staged);
    return eve::Result<int>::success(static_cast<int>(stats.size()));
}

}  // namespace eve::rpg
