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
        const Value entries = o.get("entries");
        for (size_t i = 0; i < entries.size(); ++i) {
            const Value e = entries.at(i);
            LootEntry entry;
            entry.itemId = e.getString("itemId");
            entry.chance = e.getDouble("chance", 0.0);
            entry.minQty = e.getInt("minQty", 1);
            entry.maxQty = e.getInt("maxQty", 1);
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
eve::Result<int> rollAtomic(const LootTableDefinition &definition, inventory::Bag &bag, std::mt19937 &rng) {
    // Snapshot before inventory policy/hook callbacks can reload the registry.
    const auto entries = definition.entries;
    if (entries.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return eve::Result<int>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Too many loot entries"));
    for (const auto &entry : entries) {
        if (entry.itemId.empty() || !std::isfinite(entry.chance) || entry.chance < 0.0 || entry.chance > 1.0 ||
            entry.minQty <= 0 || entry.maxQty < entry.minQty)
            return eve::Result<int>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "Invalid loot entry", entry.itemId));
    }
    auto                                       candidateRng = rng;
    std::uniform_real_distribution<double> chanceDist(0.0, 1.0);
    std::vector<inventory::InventoryItemGrant> grants;
    for (const auto &entry : entries) {
        if (chanceDist(candidateRng) >= entry.chance) continue;
        std::uniform_int_distribution<int> qtyDist(entry.minQty, entry.maxQty);
        grants.push_back({entry.itemId, qtyDist(candidateRng)});
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
    if (!bag) return 0;
    const auto *definition = find(tableId);
    if (!definition) return 0;
    auto result = rollAtomic(*definition, *bag, rng);
    if (!result) return 0;
    return result.value();
}

}  // namespace eve::rpg
