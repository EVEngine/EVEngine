#include "rpg/spawn/RPGUnitCatalogue.h"
#include "definitions/Definitions.h"
#include "rpg/ActorInitialization.h"
#include "rpg/EncounterCatalogue.h"

#include <algorithm>
#include <cmath>

namespace eve::rpg_spawn {
namespace {
Result<rpg::EncounterMemberDefinition> decode(const Value& value, const rpg::SkillContent* skills) {
    using R = Result<rpg::EncounterMemberDefinition>;
    if (!value.isObject())
        return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected unit object", "$"));
    const std::vector<std::string> fields{"schema", "version", "id",     "display_name", "skill",
                                          "attack", "defense", "max_hp", "speed"};
    for (const auto& name : value.keys())
        if (std::find(fields.begin(), fields.end(), name) == fields.end())
            return R::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Unknown unit field", "$." + name));
    for (const auto& name : fields)
        if (value.find(name) == nullptr)
            return R::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Required unit field missing", "$." + name));
    if (!value.find("schema")->isString() || value.find("schema")->asString() != "eve.rpg.unit")
        return R::failure(Diagnostic::error(DiagnosticCode::Unsupported, "Expected eve.rpg.unit schema", "$.schema"));
    if (!value.find("version")->isInt64() || value.find("version")->asInt() != 1)
        return R::failure(Diagnostic::error(DiagnosticCode::UnknownVersion, "Unsupported unit version", "$.version"));
    for (const auto& name : {"id", "display_name", "skill"})
        if (!value.find(name)->isString() || value.find(name)->asString().empty())
            return R::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected nonempty text", "$." + std::string(name)));
    for (const auto& name : {"attack", "defense", "max_hp", "speed"})
        if (!value.find(name)->isNumeric())
            return R::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "Expected numeric stat", "$." + std::string(name)));
    auto number = [&](const std::string& name) {
        const auto& v = *value.find(name);
        return v.isInt64() ? static_cast<double>(v.asInt()) : v.asDouble();
    };
    rpg::EncounterMemberDefinition member;
    member.id          = value.find("id")->asString();
    member.displayName = value.find("display_name")->asString();
    member.skillId     = value.find("skill")->asString();
    auto id            = DefinitionRef::parse("rpg.unit:" + member.id);
    if (!id) return R::failure(id.status());
    member.attack  = number("attack");
    member.defense = number("defense");
    member.maxHp   = number("max_hp");
    member.speed   = number("speed");
    auto valid =
        skills ? rpg::validateCapturedMemberDefinition(member, *skills) : rpg::validateMemberDefinition(member);
    if (!valid) return R::failure(valid.status());
    return R::success(std::move(member));
}
}  // namespace
RPGUnitCatalogue::RPGUnitCatalogue(std::shared_ptr<definitions::DefinitionRegistry> registry)
    : registry_(std::move(registry)) {}
Result<void> RPGUnitCatalogue::publish(const Value& source) {
    if (!registry_) return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Unit registry absent"));
    auto recipe = decode(source, nullptr);
    if (!recipe) return Result<void>::failure(recipe.status());
    auto json = source.toJson();
    if (!json) return Result<void>::failure(json.status());
    auto existing = registry_->resolve("rpg.unit", recipe.value().id);
    if (!existing) {
        const auto* diagnostic = existing.status().primaryDiagnostic();
        if (!diagnostic || diagnostic->code() != DiagnosticCode::NotFound)
            return Result<void>::failure(existing.status());
        auto inserted = registry_->insert("rpg.unit", recipe.value().id, 1, json.value());
        if (!inserted) return Result<void>::failure(inserted.status());
    } else {
        auto replaced = registry_->replace("rpg.unit", recipe.value().id, 1, json.value());
        if (!replaced) return Result<void>::failure(replaced.status());
    }
    return Result<void>::success();
}
Result<UnitRecipe> RPGUnitCatalogue::resolve(const DefinitionRef& reference) const {
    if (!registry_)
        return Result<UnitRecipe>::failure(Diagnostic::error(DiagnosticCode::NotFound, "Unit registry absent"));
    if (!reference.id().isValid() || reference.id().namespaceName() != "rpg.unit")
        return Result<UnitRecipe>::failure(
            Diagnostic::error(DiagnosticCode::TypeMismatch, "Expected rpg.unit reference"));
    const std::string id(reference.id().name());
    auto              source = registry_->resolve("rpg.unit", id);
    if (!source) return Result<UnitRecipe>::failure(source.status());
    return prepareCaptured(source.value().get());
}
Result<UnitRecipe> RPGUnitCatalogue::prepareCaptured(const definitions::Definition& source) {
    return prepareWithContent(source, nullptr);
}
Result<UnitRecipe> RPGUnitCatalogue::prepareCaptured(const definitions::Definition& source,
                                                     const rpg::SkillContent&       skills) {
    return prepareWithContent(source, &skills);
}
Result<UnitRecipe> RPGUnitCatalogue::prepareWithContent(const definitions::Definition& source,
                                                        const rpg::SkillContent*       skills) {
    if (source.type != "rpg.unit")
        return Result<UnitRecipe>::failure(Diagnostic::error(DiagnosticCode::TypeMismatch, "Expected rpg.unit record"));
    if (source.version.value() != 1)
        return Result<UnitRecipe>::failure(
            Diagnostic::error(DiagnosticCode::UnknownVersion, "Unsupported unit record version"));
    if (source.generation.isZero())
        return Result<UnitRecipe>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Archived unit generation is zero"));
    auto reference = DefinitionRef::parse(source.type + ":" + source.id);
    if (!reference) return Result<UnitRecipe>::failure(reference.status());
    auto parsed = Value::fromJson(source.json);
    if (!parsed) return Result<UnitRecipe>::failure(parsed.status());
    auto member = decode(parsed.value(), skills);
    if (!member) return Result<UnitRecipe>::failure(member.status());
    if (member.value().id != source.id)
        return Result<UnitRecipe>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Unit identity mismatches archive key"));
    auto canonical = parsed.value().toJson();
    if (!canonical) return Result<UnitRecipe>::failure(canonical.status());
    return Result<UnitRecipe>::success(
        {std::make_shared<const rpg::EncounterMemberDefinition>(std::move(member).takeValue()),
         {std::move(reference).takeValue(), source.generation},
         std::move(canonical).takeValue()});
}
Result<void> RPGUnitCatalogue::validate(const DefinitionRef& reference) const {
    auto resolved = resolve(reference);
    if (!resolved) return Result<void>::failure(resolved.status());
    return Result<void>::success();
}
}  // namespace eve::rpg_spawn
