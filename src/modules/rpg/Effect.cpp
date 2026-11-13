#include "rpg/Effect.h"
#include "rpg/SkillContent.h"

#include "rpg/EffectDefinitionCodec.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace eve::rpg {

bool EffectDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string EffectDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

std::unordered_map<std::string, EffectDefinition> &EffectRegistry::table() {
    static std::unordered_map<std::string, EffectDefinition> t;
    return t;
}

void swap(EffectDefinition &left, EffectDefinition &right) noexcept {
    static_assert(std::is_nothrow_swappable_v<decltype(EffectDefinition::id)>);
    static_assert(std::is_nothrow_swappable_v<decltype(EffectDefinition::modifiers)>);
    static_assert(std::is_nothrow_swappable_v<decltype(EffectDefinition::tags)>);
    static_assert(std::is_nothrow_swappable_v<decltype(EffectDefinition::extra)>);
    using std::swap;
    swap(left.id, right.id);
    swap(left.durationPolicy, right.durationPolicy);
    swap(left.duration, right.duration);
    swap(left.period, right.period);
    swap(left.stackPolicy, right.stackPolicy);
    swap(left.maxStacks, right.maxStacks);
    swap(left.modifiers, right.modifiers);
    swap(left.tags, right.tags);
    swap(left.extra, right.extra);
}

void EffectRegistry::registerEffect(const EffectDefinition &def) {
    if (def.id.empty()) return;
    static_assert(std::is_nothrow_swappable_v<EffectDefinition>);
    auto  candidate   = def;
    auto &definitions = table();
    auto  found       = definitions.find(def.id);
    if (found == definitions.end())
        definitions.emplace(def.id, std::move(candidate));
    else
        swap(found->second, candidate);
}

const EffectDefinition *EffectRegistry::find(const std::string &id) {
    if (auto content = SkillContentScope::current()) {
        auto definition = content->get().effect(id);
        if (!definition) return nullptr;
        return &definition.value().get();
    }
    auto &t = table();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool EffectRegistry::remove(const std::string &id) { return table().erase(id) > 0; }

void EffectRegistry::clear() { table().clear(); }

int EffectRegistry::count() { return int(table().size()); }

int EffectRegistry::loadFromJson(const std::string &json, std::string *error) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) {
        if (error) *error = parsed.status().describe();
        return 0;
    }
    int  count = 0;
    auto admit = [&](const eve::Value &value) {
        auto decoded = decodeEffectDefinition(value);
        if (!decoded) {
            if (error) *error = decoded.status().describe();
            return;
        }
        registerEffect(decoded.value());
        ++count;
    };
    if (parsed.value().isArray()) {
        for (const auto &value : *parsed.value().getIf<eve::Value::Array>()) admit(value);
    } else
        admit(parsed.value());
    return count;
}

}  // namespace eve::rpg

#include "rpg/EffectDefinitionCodec.inl"
