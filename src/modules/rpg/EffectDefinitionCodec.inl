#pragma once
#include <cmath>
#include <limits>
#include "rpg/EffectDefinitionCodec.h"
namespace eve::rpg {
namespace {
struct EffectCodecError {
    Diagnostic diagnostic;
};
[[noreturn]] void effectCodecReject(std::string message, std::string path) {
    throw EffectCodecError{Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path))};
}
std::string effectText(const Value& object, const std::string& field, std::string fallback) {
    const auto* item = object.find(field);
    if (!item) return fallback;
    if (!item->isString()) effectCodecReject("Effect field must be a string", field);
    return item->asString();
}
double effectNumber(const Value& object, const std::string& field, double fallback) {
    const auto* item = object.find(field);
    if (!item) return fallback;
    if (!item->isNumeric()) effectCodecReject("Effect field must be a number", field);
    const auto number = item->isInt64() ? double(item->asInt()) : item->asDouble();
    if (!std::isfinite(number)) effectCodecReject("Effect field must be finite", field);
    return number;
}
int effectInteger(const Value& object, const std::string& field, int fallback) {
    const auto* item = object.find(field);
    if (!item) return fallback;
    if (!item->isInt64() || item->asInt() < std::numeric_limits<int>::min() ||
        item->asInt() > std::numeric_limits<int>::max())
        effectCodecReject("Effect field must be a bounded integer", field);
    return static_cast<int>(item->asInt());
}
float effectFloat(const Value& object, const std::string& field) {
    auto number = effectNumber(object, field, 0);
    if (number < 0 || number > std::numeric_limits<float>::max())
        effectCodecReject("Effect duration/period is out of range", field);
    return static_cast<float>(number);
}
}  // namespace
bool isBuiltinEffectStackPolicy(std::string_view name) noexcept {
    return name == "none" || name == "refresh" || name == "extend" || name == "stack";
}
Result<void> validateEffectDefinition(const EffectDefinition& definition) {
    auto fail = [](std::string message) {
        return Result<void>::failure(
            Status::failure(StatusCode::Rejected,
                            Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "rpg.effect")));
    };
    if (definition.id.empty()) return fail("effect definition id must not be empty");
    if (definition.durationPolicy != "instant" && definition.durationPolicy != "duration" &&
        definition.durationPolicy != "infinite")
        return fail("unknown effect duration policy: " + definition.durationPolicy);
    if (!std::isfinite(definition.duration) || definition.duration < 0)
        return fail("effect definition duration must be finite and non-negative");
    if (!std::isfinite(definition.period) || definition.period < 0)
        return fail("effect definition period must be finite and non-negative");
    if (definition.maxStacks < 0) return fail("effect definition maxStacks must be non-negative");
    if (definition.stackPolicy.empty()) return fail("effect stack policy must not be empty");
    for (const auto& modifier : definition.modifiers) {
        if (modifier.attribute.empty()) return fail("effect modifier attribute must not be empty");
        if (modifier.op.empty()) return fail("effect modifier operation must not be empty");
        if (!std::isfinite(modifier.value)) return fail("effect modifier value must be finite");
    }
    return Result<void>::success();
}
Result<Value> encodeEffectDefinition(const EffectDefinition& definition) {
    auto valid = validateEffectDefinition(definition);
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Array modifiers, tags;
    for (const auto& modifier : definition.modifiers)
        modifiers.push_back(Value::object({{"attribute", modifier.attribute},
                                           {"op", modifier.op},
                                           {"value", modifier.value},
                                           {"priority", modifier.priority}}));
    for (const auto& tag : definition.tags) tags.emplace_back(tag);
    Value::Object extra;
    for (const auto& [key, value] : definition.extra) extra.emplace(key, Value(value));
    return Result<Value>::success(Value::object({{"id", definition.id},
                                                 {"durationPolicy", definition.durationPolicy},
                                                 {"duration", double(definition.duration)},
                                                 {"period", double(definition.period)},
                                                 {"stackPolicy", definition.stackPolicy},
                                                 {"maxStacks", definition.maxStacks},
                                                 {"modifiers", Value(std::move(modifiers))},
                                                 {"tags", Value(std::move(tags))},
                                                 {"extra", Value(std::move(extra))}}));
}
Result<EffectDefinition> decodeEffectDefinition(const Value& value) {
    using R = Result<EffectDefinition>;
    try {
        if (!value.isObject()) effectCodecReject("Effect definition must be an object", "$effect");
        EffectDefinition result;
        result.id             = effectText(value, "id", {});
        result.durationPolicy = effectText(value, "durationPolicy", "instant");
        result.stackPolicy    = effectText(value, "stackPolicy", "none");
        result.duration       = effectFloat(value, "duration");
        result.period         = effectFloat(value, "period");
        result.maxStacks      = effectInteger(value, "maxStacks", 1);
        if (const auto* modifiers = value.find("modifiers")) {
            if (!modifiers->isArray()) effectCodecReject("Effect modifiers must be an array", "modifiers");
            for (const auto& item : *modifiers->getIf<Value::Array>()) {
                if (!item.isObject()) effectCodecReject("Effect modifier must be an object", "modifiers");
                result.modifiers.push_back({effectText(item, "attribute", {}), effectText(item, "op", "add"),
                                            effectNumber(item, "value", 0), effectInteger(item, "priority", 0)});
            }
        }
        if (const auto* tags = value.find("tags")) {
            if (!tags->isArray()) effectCodecReject("Effect tags must be an array", "tags");
            for (const auto& item : *tags->getIf<Value::Array>()) {
                if (!item.isString()) effectCodecReject("Effect tag must be a string", "tags");
                result.tags.push_back(item.asString());
            }
        }
        if (const auto* extra = value.find("extra")) {
            if (!extra->isObject()) effectCodecReject("Effect extra must be an object", "extra");
            for (const auto& [key, item] : *extra->getIf<Value::Object>()) {
                if (!item.isString()) effectCodecReject("Effect extra value must be a string", "extra." + key);
                result.extra.emplace(key, item.asString());
            }
        }
        auto valid = validateEffectDefinition(result);
        if (!valid) return R::failure(valid.status());
        return R::success(std::move(result));
    } catch (const EffectCodecError& error) {
        return R::failure(error.diagnostic);
    }
}
}  // namespace eve::rpg
