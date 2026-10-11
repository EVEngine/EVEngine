#include "action/ActionDamageBlock.h"

#include "tags/GameplayTag.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<double> number(const Value& value, std::string path) {
    double result = 0.0;
    if (const auto* integer = value.getIf<std::int64_t>())
        result = static_cast<double>(*integer);
    else if (const auto* decimal = value.getIf<double>())
        result = *decimal;
    else
        return invalid<double>("damage field must be numeric", std::move(path));
    if (!std::isfinite(result)) return invalid<double>("damage field must be finite", std::move(path));
    return Result<double>::success(result);
}

Result<double> optionalNumber(const Value::Object& payload, const char* field, double fallback) {
    const auto found = payload.find(field);
    return found == payload.end() ? Result<double>::success(fallback) : number(found->second, field);
}

}  // namespace

Result<ActionDamageBinding> ActionDamageBinding::fromPayload(const Value::Object& payload) {
    ActionDamageBinding candidate;
    const auto foundType = payload.find("damageType");
    const auto* damageType = foundType == payload.end() ? nullptr : foundType->second.getIf<std::string>();
    if (!damageType || !tags::isValidGameplayTagName(*damageType))
        return invalid<ActionDamageBinding>("damage type must be a gameplay tag", "damageType");
    candidate.damageType = *damageType;

    const auto foundAmount = payload.find("amount");
    if (foundAmount == payload.end()) return invalid<ActionDamageBinding>("damage amount is required", "amount");
    auto amount = number(foundAmount->second, "amount");
    if (!amount) return Result<ActionDamageBinding>::failure(amount.status());
    auto poise = optionalNumber(payload, "poiseAmount", 0.0);
    if (!poise) return Result<ActionDamageBinding>::failure(poise.status());
    if (amount.value() < 0.0) return invalid<ActionDamageBinding>("damage amount must be non-negative", "amount");
    if (poise.value() < 0.0)
        return invalid<ActionDamageBinding>("poise damage must be non-negative", "poiseAmount");
    candidate.amount = amount.value();
    candidate.poiseAmount = poise.value();

    if (const auto foundTarget = payload.find("targetIndex"); foundTarget != payload.end()) {
        const auto* target = foundTarget->second.getIf<std::int64_t>();
        if (!target || *target < 0 || static_cast<std::uint64_t>(*target) > std::numeric_limits<std::uint32_t>::max())
            return invalid<ActionDamageBinding>("damage target index must be an unsigned 32-bit integer", "targetIndex");
        candidate.targetIndex = static_cast<std::size_t>(*target);
    }

    if (const auto foundKnockback = payload.find("knockback"); foundKnockback != payload.end()) {
        const auto* values = foundKnockback->second.getIf<Value::Array>();
        if (!values || values->size() != 3)
            return invalid<ActionDamageBinding>("damage knockback must contain three numbers", "knockback");
        auto x = number((*values)[0], "knockback[0]");
        if (!x) return Result<ActionDamageBinding>::failure(x.status());
        auto y = number((*values)[1], "knockback[1]");
        if (!y) return Result<ActionDamageBinding>::failure(y.status());
        auto z = number((*values)[2], "knockback[2]");
        if (!z) return Result<ActionDamageBinding>::failure(z.status());
        candidate.knockback = {x.value(), y.value(), z.value()};
    }
    return Result<ActionDamageBinding>::success(std::move(candidate));
}

}  // namespace eve::action
