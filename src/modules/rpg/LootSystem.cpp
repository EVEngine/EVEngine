#include "rpg/LootSystem.h"

#include "common/Json.h"
#include "inventory/Bag.h"

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

int LootSystem::roll(const std::string &tableId, eve::inventory::Bag *bag, std::mt19937 &rng) {
    if (!bag) return 0;
    const LootTableDefinition *def = find(tableId);
    if (!def) return 0;
    std::uniform_real_distribution<double> chanceDist(0.0, 1.0);
    int drops = 0;
    for (const auto &entry : def->entries) {
        if (chanceDist(rng) >= entry.chance) continue;
        std::uniform_int_distribution<int> qtyDist(entry.minQty, entry.maxQty);
        const int qty = qtyDist(rng);
        if (qty <= 0) continue;
        bag->addItem(entry.itemId, qty);
        ++drops;
    }
    return drops;
}

}  // namespace eve::rpg