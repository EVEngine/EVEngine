#include "rpg/Loot.h"
#include "common/Json.h"

#include <algorithm>
#include <random>
#include <unordered_set>

namespace eve::rpg {

namespace {
std::unordered_map<std::string, LootTableDefinition>& tables() {
    static std::unordered_map<std::string, LootTableDefinition> value;
    return value;
}
std::mt19937& rng() {
    static std::mt19937 value(0x4c4f4f54u);
    return value;
}
LootTableDefinition parse(eve::json::Value value) {
    LootTableDefinition table;
    table.id     = value.getString("id");
    table.rolls  = std::max(0, value.getInt("rolls", 1));
    table.unique = value.getBool("unique", false);
    auto entries = value.get("entries");
    for (size_t i = 0; i < entries.size(); ++i) {
        auto      item = entries.at(i);
        LootEntry entry;
        entry.itemId      = item.getString("itemId");
        entry.minQuantity = std::max(1, item.getInt("minQuantity", 1));
        entry.maxQuantity = std::max(entry.minQuantity, item.getInt("maxQuantity", entry.minQuantity));
        entry.weight      = std::max(0.0, item.getDouble("weight", 1.0));
        entry.chance      = std::clamp(item.getDouble("chance", 1.0), 0.0, 1.0);
        entry.tags        = item.getStringArray("tags");
        if (!entry.itemId.empty() && entry.weight > 0.0) table.entries.push_back(std::move(entry));
    }
    return table;
}
}  // namespace

bool LootRegistry::registerTable(const LootTableDefinition& definition) {
    if (definition.id.empty() || definition.entries.empty()) return false;
    tables()[definition.id] = definition;
    return true;
}
const LootTableDefinition* LootRegistry::find(const std::string& id) {
    auto i = tables().find(id);
    return i == tables().end() ? nullptr : &i->second;
}
void LootRegistry::clear() { tables().clear(); }
int  LootRegistry::count() { return int(tables().size()); }
int  LootRegistry::loadFromJson(const std::string& json, std::string* error) {
    std::string parseError;
    auto        doc = eve::json::Document::parse(json, &parseError);
    if (!doc.valid()) {
        if (error) *error = parseError;
        return 0;
    }
    auto root  = doc.root();
    int  count = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i)
            if (registerTable(parse(root.at(i)))) ++count;
    } else if (root.isObject() && registerTable(parse(root)))
        ++count;
    return count;
}

std::string LootRoll::getItemId(int i) const {
    return i >= 0 && size_t(i) < itemIds_.size() ? itemIds_[size_t(i)] : std::string{};
}
int LootRoll::getQuantity(int i) const { return i >= 0 && size_t(i) < quantities_.size() ? quantities_[size_t(i)] : 0; }
void      LootSystem::setRandomSeed(uint32_t seed) { rng().seed(seed); }
LootRoll* LootSystem::roll(const std::string& tableId, double luck) {
    auto* result = new LootRoll();
    auto* table  = LootRegistry::find(tableId);
    if (!table) return result;
    std::vector<size_t> available;
    for (size_t i = 0; i < table->entries.size(); ++i) available.push_back(i);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (int roll = 0; roll < table->rolls && !available.empty(); ++roll) {
        double total = 0.0;
        for (size_t i : available) total += table->entries[i].weight;
        if (total <= 0.0) break;
        double pick     = unit(rng()) * total;
        size_t selected = available.back();
        for (size_t i : available) {
            pick -= table->entries[i].weight;
            if (pick <= 0.0) {
                selected = i;
                break;
            }
        }
        const auto& entry           = table->entries[selected];
        double      effectiveChance = std::clamp(entry.chance * (1.0 + std::max(0.0, luck)), 0.0, 1.0);
        if (unit(rng()) <= effectiveChance) {
            std::uniform_int_distribution<int> quantity(entry.minQuantity, entry.maxQuantity);
            result->itemIds_.push_back(entry.itemId);
            result->quantities_.push_back(quantity(rng()));
        }
        if (table->unique) available.erase(std::remove(available.begin(), available.end(), selected), available.end());
    }
    return result;
}

}  // namespace eve::rpg
