#include "rpg/LootSystem.h"

#include "common/Json.h"
#include "inventory/Bag.h"
#include "inventory/InventorySystem.h"

#include <cmath>
#include <limits>
#include <unordered_map>

namespace eve::rpg {

using eve::json::Value;

namespace {
std::unordered_map<std::string, LootTableDefinition> &lootTables() {
    static std::unordered_map<std::string, LootTableDefinition> t;
    return t;
}

bool parseSelectMode(const std::string &text, LootSelectMode &out) {
    if (text.empty() || text == "independent") {
        out = LootSelectMode::Independent;
        return true;
    }
    if (text == "weighted") {
        out = LootSelectMode::Weighted;
        return true;
    }
    return false;
}
}  // namespace

void LootSystem::registerTable(const LootTableDefinition &def) {
    if (def.id.empty()) return;
    lootTables()[def.id] = def;
}

int LootSystem::registerTablesFromJson(const std::string &json) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) return 0;
    const Value root = doc.root();
    int n = 0;
    const auto parseTable = [&](Value o) {
        if (!o.isObject()) return;
        LootTableDefinition def;
        def.id = o.getString("id");
        if (def.id.empty()) return;
        LootSelectMode mode = LootSelectMode::Independent;
        if (!parseSelectMode(o.getString("selectMode", "independent"), mode)) return;
        def.selectMode      = mode;
        const Value entries = o.get("entries");
        for (size_t i = 0; i < entries.size(); ++i) {
            const Value e = entries.at(i);
            LootEntry entry;
            entry.itemId = e.getString("itemId");
            entry.chance = e.getDouble("chance", 0.0);
            entry.minQty = e.getInt("minQty", 1);
            entry.maxQty = e.getInt("maxQty", 1);
            entry.weight = e.getDouble("weight", 0.0);
            entry.unique = e.getBool("unique", false);
            if (entry.itemId.empty()) continue;
            if (entry.maxQty < entry.minQty) entry.maxQty = entry.minQty;
            if (entry.maxQty <= 0) entry.maxQty = 1;
            if (entry.minQty <= 0) entry.minQty = 1;
            def.entries.push_back(std::move(entry));
        }
        lootTables()[def.id] = std::move(def);
        ++n;
    };
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) parseTable(root.at(i));
    } else if (root.isObject()) {
        parseTable(root);
    }
    return n;
}

const LootTableDefinition *LootSystem::find(const std::string &id) {
    auto &t = lootTables();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

void LootSystem::clear() { lootTables().clear(); }

int LootSystem::count() { return int(lootTables().size()); }

namespace {
bool alreadyOwnedOrQueued(const inventory::Bag &bag, const std::vector<inventory::InventoryItemGrant> &grants,
                          const std::string &itemId) {
    if (bag.countItem(itemId) > 0) return true;
    for (const auto &grant : grants) {
        if (grant.itemId == itemId) return true;
    }
    return false;
}

eve::Result<int> rollAtomic(const LootTableDefinition &definition, inventory::Bag &bag, std::mt19937 &rng,
                            const LootRollRequest &request) {
    if (!std::isfinite(request.luck) || request.luck < -1.0)
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "loot luck must be finite and >= -1", "luck"));

    // Snapshot before inventory policy/hook callbacks can reload the registry.
    const auto entries = definition.entries;
    if (entries.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Too many loot entries"));
    for (const auto &entry : entries) {
        if (entry.itemId.empty() || !std::isfinite(entry.chance) || entry.chance < 0.0 || entry.chance > 1.0 ||
            !std::isfinite(entry.weight) || entry.weight < 0.0 || entry.minQty <= 0 || entry.maxQty < entry.minQty)
            return eve::Result<int>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Invalid loot entry", entry.itemId));
    }

    auto                                       candidateRng = rng;
    std::vector<inventory::InventoryItemGrant> grants;

    if (definition.selectMode == LootSelectMode::Weighted) {
        double totalWeight = 0.0;
        for (const auto &entry : entries) {
            if (entry.weight > 0.0) totalWeight += entry.weight;
        }
        if (!(totalWeight > 0.0))
            return eve::Result<int>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "weighted loot table has no positive weights", definition.id));
        std::uniform_real_distribution<double> pickDist(0.0, totalWeight);
        double                                 pick   = pickDist(candidateRng);
        const LootEntry                       *chosen = nullptr;
        for (const auto &entry : entries) {
            if (!(entry.weight > 0.0)) continue;
            if (pick < entry.weight) {
                chosen = &entry;
                break;
            }
            pick -= entry.weight;
        }
        if (!chosen) chosen = &entries.back();
        if (!(chosen->unique && alreadyOwnedOrQueued(bag, grants, chosen->itemId))) {
            std::uniform_int_distribution<int> qtyDist(chosen->minQty, chosen->maxQty);
            grants.push_back({chosen->itemId, qtyDist(candidateRng)});
        }
    } else {
        std::uniform_real_distribution<double> chanceDist(0.0, 1.0);
        for (const auto &entry : entries) {
            const double scale     = 1.0 + request.luck;
            const double effective = std::min(1.0, std::max(0.0, entry.chance * scale));
            if (chanceDist(candidateRng) >= effective) continue;
            if (entry.unique && alreadyOwnedOrQueued(bag, grants, entry.itemId)) continue;
            std::uniform_int_distribution<int> qtyDist(entry.minQty, entry.maxQty);
            grants.push_back({entry.itemId, qtyDist(candidateRng)});
        }
    }

    if (grants.empty()) {
        rng = candidateRng;
        return eve::Result<int>::success(0);
    }
    auto prepared = inventory::InventorySystem::prepareAddBatch(&bag, grants);
    if (!prepared) return eve::Result<int>::failure(prepared.status());
    const auto before = rng;
    // Publish the random stream before inventory's post-commit hooks run.
    rng            = candidateRng;
    auto committed = inventory::InventorySystem::commitAddBatch(std::move(prepared).takeValue());
    if (!committed) {
        rng = before;
        return eve::Result<int>::failure(committed.status());
    }
    return eve::Result<int>::success(static_cast<int>(grants.size()));
}
}  // namespace

int LootSystem::roll(const std::string &tableId, eve::inventory::Bag *bag, std::mt19937 &rng) {
    auto result = rollDetailed(tableId, bag, rng, {});
    if (!result) return 0;
    return result.value();
}

eve::Result<int> LootSystem::rollDetailed(const std::string &tableId, eve::inventory::Bag *bag, std::mt19937 &rng,
                                          const LootRollRequest &request) {
    if (!bag)
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "loot roll requires a bag", "bag"));
    const auto *definition = find(tableId);
    if (!definition)
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "loot table not found", tableId));
    return rollAtomic(*definition, *bag, rng, request);
}

}  // namespace eve::rpg
