#include <algorithm>
#include <climits>
#include <cmath>
#include <map>
#include <set>
#include "rpg/Effect.h"
#include "rpg/EffectDefinitionCodec.h"
#include "rpg/SkillContent.h"
#include "rpg/StatusPersistenceInternal.h"
namespace eve::rpg {
namespace {
struct Invalid {
    Status status;
};
[[noreturn]] void invalid(const char* text) {
    throw Invalid{Status::failure(Diagnostic::error(DiagnosticCode::Conflict, text, "rpg.statuses"))};
}
template <class T>
T take(Result<T> value) {
    if (!value) throw Invalid{value.status()};
    return std::move(value).takeValue();
}
void fields(const Value& value, std::initializer_list<const char*> keys) {
    const auto* object = value.getIf<Value::Object>();
    if (!object || object->size() != keys.size()) invalid("Unknown or missing RPG status fields");
    for (auto key : keys)
        if (!value.find(key)) invalid("Missing RPG status field");
}
const Value::Array& array(const Value& value) {
    const auto* result = value.getIf<Value::Array>();
    if (!result || result->size() > 1000000) invalid("Expected bounded RPG status array");
    return *result;
}
std::string text(const Value& value) {
    if (!value.isString()) invalid("Expected RPG status text");
    return value.asString();
}
void validate(const RPGActor::Statuses& state, const attributes::AttributeSet& attributes,
              const SkillContent* content) {
    const auto count = static_cast<std::size_t>(state.container.effectCount());
    if (state.metadata.size() != count || state.legacyIdByEffect.size() != count ||
        state.effectByLegacyId.size() != count || state.nextInstanceId < 0)
        invalid("RPG status adapter scope differs from lifecycle scope");
    std::map<std::string, const ::eve::attributes::AttributeModifier*> modifiers;
    for (int i = 0; i < attributes.modifierCount(); ++i) {
        const auto* modifier = attributes.modifierAt(i);
        modifiers.emplace(modifier->id, modifier);
    }
    std::set<std::string> bound;
    for (int i = 0; i < state.container.effectCount(); ++i) {
        const auto& effect = *state.container.effectAt(i);
        if (effect.subject != "actor" || effect.period != 0 || effect.periodElapsed != 0)
            invalid("RPG effect has foreign subject or a second periodic owner");
        auto metadata = state.metadata.find(effect.id);
        auto legacy   = state.legacyIdByEffect.find(effect.id);
        if (metadata == state.metadata.end() || legacy == state.legacyIdByEffect.end() || legacy->second <= 0 ||
            (state.nextInstanceId != 0 && legacy->second >= state.nextInstanceId))
            invalid("Invalid status adapter identity or watermark");
        auto inverse = state.effectByLegacyId.find(legacy->second);
        if (inverse == state.effectByLegacyId.end() || inverse->second != effect.id)
            invalid("Status inverse index mismatch");
        const EffectDefinition* definition = nullptr;
        if (content) {
            auto found = content->effect(effect.type);
            if (!found) throw Invalid{found.status()};
            definition = &found.value().get();
        } else
            definition = EffectRegistry::find(effect.type);
        if (!definition) invalid("Active RPG effect definition is missing");
        if (definition->id != effect.type) invalid("Active RPG effect definition identity differs from its key");
        auto valid = validateEffectDefinition(*definition);
        if (!valid) throw Invalid{valid.status()};
        if (!std::isfinite(metadata->second.periodAccum) || metadata->second.periodAccum < 0)
            invalid("Invalid status executor period accumulator");
        for (const auto& [attribute, id] : metadata->second.appliedModifiers) {
            auto found = modifiers.find(id);
            if (found == modifiers.end() || found->second->attribute != attribute ||
                found->second->source != "status:" + effect.type + ":" + effect.id || !bound.insert(id).second)
                invalid("Status modifier relationship is missing, foreign or duplicated");
        }
    }
    for (const auto& [id, modifier] : modifiers)
        if (modifier->source.starts_with("status:") && !bound.contains(id)) invalid("Orphaned status-owned modifier");
}
}  // namespace
Result<Value> encodeStatusState(const RPGActor::Statuses& state, const attributes::AttributeSet& attributes) {
    try {
        validate(state, attributes, nullptr);
        auto                     container = take(state.container.encodeState());
        Value::Array             rows;
        std::vector<std::string> ids;
        for (const auto& [id, metadata] : state.metadata) ids.push_back(id);
        std::sort(ids.begin(), ids.end());
        for (const auto& id : ids) {
            const auto&  metadata = state.metadata.at(id);
            Value::Array bindings;
            for (const auto& [attribute, modifier] : metadata.appliedModifiers)
                bindings.push_back(Value::Array{attribute, modifier});
            Value::Object props;
            for (const auto& [key, value] : metadata.props) props.emplace(key, value);
            rows.push_back(Value::Array{id, state.legacyIdByEffect.at(id), metadata.periodAccum,
                                        Value(std::move(bindings)), Value(std::move(props))});
        }
        return Result<Value>::success(Value::object({{"schema", "rpg.statuses"},
                                                     {"version", 1},
                                                     {"container", std::move(container)},
                                                     {"next", state.nextInstanceId},
                                                     {"members", Value(std::move(rows))}}));
    } catch (const Invalid& error) {
        return Result<Value>::failure(error.status);
    }
}
Result<RPGActor::Statuses> prepareStatusState(const Value& value, const attributes::AttributeSet& attributes,
                                              const RPGActor::Statuses& destination, const SkillContent* content) {
    using R = Result<RPGActor::Statuses>;
    try {
        fields(value, {"schema", "version", "container", "next", "members"});
        if (text(*value.find("schema")) != "rpg.statuses" || !value.find("version")->isInt64() ||
            value.find("version")->asInt() != 1)
            invalid("Unknown RPG status schema or version");
        const auto& next = *value.find("next");
        if (!next.isInt64() || next.asInt() < 0 || next.asInt() > INT_MAX) invalid("Invalid RPG status next identity");
        auto previous = destination.container.encodeState();
        if (!previous) return R::failure(previous.status());
        auto               container = take(effects::EffectContainer::prepareState(*value.find("container")));
        RPGActor::Statuses result;
        result.container = destination.container;
        auto restored    = result.container.restore(container);
        if (!restored) return R::failure(restored.status());
        result.nextInstanceId = static_cast<int>(next.asInt());
        for (const auto& member : array(*value.find("members"))) {
            const auto& row = array(member);
            if (row.size() != 5 || !row[1].isInt64() || row[1].asInt() <= 0 || row[1].asInt() > INT_MAX ||
                !row[2].isNumeric())
                invalid("Malformed RPG status adapter member");
            const auto             id     = text(row[0]);
            const int              legacy = static_cast<int>(row[1].asInt());
            StatusExecutorMetadata metadata;
            metadata.periodAccum = row[2].isInt64() ? double(row[2].asInt()) : row[2].asDouble();
            for (const auto& entry : array(row[3])) {
                const auto& pair = array(entry);
                if (pair.size() != 2) invalid("Malformed status modifier binding");
                metadata.appliedModifiers.emplace_back(text(pair[0]), text(pair[1]));
            }
            const auto* props = row[4].getIf<Value::Object>();
            if (!props) invalid("Malformed status properties");
            for (const auto& [key, value] : *props) metadata.props.emplace(key, text(value));
            if (!result.metadata.emplace(id, std::move(metadata)).second ||
                !result.legacyIdByEffect.emplace(id, legacy).second ||
                !result.effectByLegacyId.emplace(legacy, id).second)
                invalid("Duplicate RPG status adapter identity");
        }
        validate(result, attributes, content);
        return R::success(std::move(result));
    } catch (const Invalid& error) {
        return R::failure(error.status);
    }
}
}  // namespace eve::rpg
