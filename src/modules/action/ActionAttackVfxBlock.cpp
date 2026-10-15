#include "action/ActionAttackVfxBlock.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace eve::action {
namespace {

Result<double> numeric(const Value::Object& payload, std::string_view key, double fallback) {
    const auto found = payload.find(std::string(key));
    if (found == payload.end()) return Result<double>::success(fallback);
    double value = 0.0;
    if (const auto* integer = found->second.getIf<std::int64_t>())
        value = static_cast<double>(*integer);
    else if (const auto* decimal = found->second.getIf<double>())
        value = *decimal;
    else
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "AttackVfx field must be numeric", std::string(key)));
    if (!std::isfinite(value))
        return Result<double>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                         "AttackVfx field must be finite", std::string(key)));
    return Result<double>::success(value);
}

}  // namespace

Result<ActionAttackVfxBinding> ActionAttackVfxBinding::fromPayload(const Value::Object& payload,
                                                                   ActionAttackVfxShape shape) {
    ActionAttackVfxBinding candidate;

    const auto recipeId = payload.find("recipeId");
    const auto uri      = payload.find("uri");
    const bool hasRecipe =
        recipeId != payload.end() && recipeId->second.getIf<std::string>() &&
        !recipeId->second.getIf<std::string>()->empty();
    const bool hasUri =
        uri != payload.end() && uri->second.getIf<std::string>() && !uri->second.getIf<std::string>()->empty();
    if (hasRecipe == hasUri)
        return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "AttackVfx requires exactly one of recipeId or uri", "recipeId"));
    if (hasRecipe) {
        auto parsed = LogicalId::parse(*recipeId->second.getIf<std::string>());
        if (!parsed)
            return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "AttackVfx recipeId must be namespace:name", "recipeId"));
        candidate.recipeId = std::move(*parsed);
    } else {
        candidate.uri = *uri->second.getIf<std::string>();
    }

    if (const auto skin = payload.find("skinId"); skin != payload.end()) {
        const auto* text = skin->second.getIf<std::string>();
        if (!text || text->empty())
            return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "AttackVfx skinId must be non-empty text", "skinId"));
        auto parsed = LogicalId::parse(*text);
        if (!parsed)
            return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "AttackVfx skinId must be namespace:name", "skinId"));
        candidate.skinId = std::move(*parsed);
    }

    auto spatial = ActionSpatialBinding::fromPayload(payload);
    if (!spatial) return Result<ActionAttackVfxBinding>::failure(spatial.status());
    candidate.spatial = std::move(spatial).takeValue();

    auto lifetime = numeric(payload, "lifetimeSeconds", 0.0);
    if (!lifetime) return Result<ActionAttackVfxBinding>::failure(lifetime.status());
    if (shape == ActionAttackVfxShape::Instant && lifetime.value() <= 0.0)
        return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "instant AttackVfx lifetimeSeconds must be positive",
            "lifetimeSeconds"));
    if (lifetime.value() < 0.0)
        return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "AttackVfx lifetimeSeconds must be non-negative", "lifetimeSeconds"));
    candidate.lifetimeSeconds = lifetime.value();

    if (const auto cues = payload.find("cues"); cues != payload.end()) {
        const auto* array = cues->second.getIf<Value::Array>();
        if (!array)
            return Result<ActionAttackVfxBinding>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "AttackVfx cues must be an array", "cues"));
        candidate.cues.reserve(array->size());
        for (std::size_t i = 0; i < array->size(); ++i) {
            const auto* object = (*array)[i].getIf<Value::Object>();
            const std::string path = "cues[" + std::to_string(i) + "]";
            if (!object)
                return Result<ActionAttackVfxBinding>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "AttackVfx cue must be an object", path));
            ActionAttackVfxCue cue;
            const auto offset = object->find("offsetSeconds");
            if (offset == object->end())
                return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "AttackVfx cue offsetSeconds is required",
                    path + ".offsetSeconds"));
            auto parsedOffset = numeric(*object, "offsetSeconds", 0.0);
            if (!parsedOffset) return Result<ActionAttackVfxBinding>::failure(parsedOffset.status());
            if (parsedOffset.value() < 0.0)
                return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "AttackVfx cue offsetSeconds must be >= 0",
                    path + ".offsetSeconds"));
            cue.offsetSeconds = parsedOffset.value();
            const auto name = object->find("cue");
            if (name == object->end() || !name->second.getIf<std::string>() ||
                name->second.getIf<std::string>()->empty())
                return Result<ActionAttackVfxBinding>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "AttackVfx cue name must be non-empty text", path + ".cue"));
            cue.cue = *name->second.getIf<std::string>();
            candidate.cues.push_back(std::move(cue));
        }
        std::stable_sort(candidate.cues.begin(), candidate.cues.end(),
                         [](const ActionAttackVfxCue& a, const ActionAttackVfxCue& b) {
                             return a.offsetSeconds < b.offsetSeconds;
                         });
    }

    return Result<ActionAttackVfxBinding>::success(std::move(candidate));
}

}  // namespace eve::action
