/** @file RTSEffectState.cpp
 * @brief rts module-interface: rts.effects v1 canonical lifecycle/settlement data face; no new runtime owner.
 */
#include <charconv>
#include <limits>
#include "rts/RTSEffects.h"
namespace eve::rts {
Result<Value> RTSEffectSnapshot::encodeState() const {
    auto valid = RTSEffectExecutor{}.validate(target);
    if (!valid) return Result<Value>::failure(valid.status());
    auto common = effects.encodeState();
    if (!common) return Result<Value>::failure(common.status());
    return Result<Value>::success(
        Value::object({{"schema", "rts.effects"},
                       {"version", 1},
                       {"container", std::move(common).takeValue()},
                       {"target", Value::Array{target.morale, std::to_string(target.suppression),
                                               target.productionLocked, std::to_string(target.commandInterrupts)}}}));
}
Result<RTSEffectSnapshot> RTSEffectSnapshot::prepareState(const Value& value) {
    const auto failure = [] {
        return Result<RTSEffectSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "Unknown or malformed RTS effect state", "rts.effects"));
    };
    const auto* root = value.getIf<Value::Object>();
    if (!root || root->size() != 4) return failure();
    for (const auto* key : {"schema", "version", "container", "target"})
        if (!root->contains(key)) return failure();
    const auto* schema = root->at("schema").getIf<std::string>();
    if (!schema || *schema != "rts.effects" || !root->at("version").isInt64() || root->at("version").asInt() != 1)
        return failure();
    const auto* row = root->at("target").getIf<Value::Array>();
    if (!row || row->size() != 4 || (!(*row)[0].isDouble() && !(*row)[0].isInt64()) || !(*row)[2].isBool())
        return failure();
    RTSEffectSnapshot result;
    result.target.morale           = (*row)[0].isDouble() ? (*row)[0].asDouble() : double((*row)[0].asInt());
    result.target.productionLocked = (*row)[2].asBool();
    for (auto [index, out] :
         {std::pair{1, &result.target.suppression}, std::pair{3, &result.target.commandInterrupts}}) {
        const auto* s = (*row)[index].getIf<std::string>();
        if (!s) return failure();
        std::uint64_t n = 0;
        const auto    p = std::from_chars(s->data(), s->data() + s->size(), n);
        if (p.ec != std::errc{} || p.ptr != s->data() + s->size() || n > std::numeric_limits<std::uint32_t>::max() ||
            std::to_string(n) != *s)
            return failure();
        *out = std::uint32_t(n);
    }
    auto valid = RTSEffectExecutor{}.validate(result.target);
    if (!valid) return Result<RTSEffectSnapshot>::failure(valid.status());
    auto common = effects::EffectContainer::prepareState(root->at("container"));
    if (!common) return Result<RTSEffectSnapshot>::failure(common.status());
    result.effects = std::move(common).takeValue();
    return Result<RTSEffectSnapshot>::success(std::move(result));
}
}  // namespace eve::rts
